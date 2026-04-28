import 'dotenv/config';
import * as path from 'path';
import { Chess } from 'chess.js';
import { UCIEngine } from './engine';
import { ChessDotCom } from './browser';
import { applySanToBoard, sleep, humanDelay, log } from './utils';

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const ENGINE_PATH = process.env.ENGINE_PATH
  ? path.resolve(__dirname, '..', process.env.ENGINE_PATH)
  : path.resolve(__dirname, '../../engine/build/chess_engine');

const MOVE_TIME_MS = 1_000; // engine think time per move
const GAME_DELAY_MIN = 5_000; // pause between games (ms)
const GAME_DELAY_MAX = 10_000;

// ---------------------------------------------------------------------------
// Main game loop
// ---------------------------------------------------------------------------

async function playGame(engine: UCIEngine, browser: ChessDotCom): Promise<void> {
  log('Main', '--- Starting new game ---');

  await engine.newGame();
  await browser.startGameVsBot();

  const ourColor = browser.ourColor;

  // masterMoveList is the authoritative UCI history for this game.
  // Our moves are pushed directly (we know the exact UCI we played).
  // Opponent moves are converted from the DOM SAN string using the known
  // board position, so "f3" is correctly resolved as Nf3 vs pawn f3.
  const masterMoveList: string[] = [];
  const trackerChess = new Chess(); // mirrors masterMoveList at all times

  // White moves at 0, 2, 4 … (even); black at 1, 3, 5 … (odd).
  let expectedMoveCount = ourColor === 'white' ? 0 : 1;
  log('Main', `We are ${ourColor} — first turn at move count ${expectedMoveCount}`);

  while (true) {
    // -----------------------------------------------------------------------
    // Wait for our turn
    // -----------------------------------------------------------------------
    try {
      await browser.waitForOurTurn(expectedMoveCount);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      if (msg === 'GAME_OVER') { log('Main', 'Game over while waiting'); break; }
      throw err;
    }

    if (await browser.isGameOver()) { log('Main', 'Game over at top of loop'); break; }

    // -----------------------------------------------------------------------
    // Sync opponent moves from DOM into masterMoveList
    // -----------------------------------------------------------------------
    // syncMoves() is used only for its COUNT (turn detection, executeMove check)
    // and to extract the raw SAN text of new opponent moves.
    const domSanMoves = await browser.syncMoves();

    // Log FEN probes so we can see which chess.com API surface is live.
    // We don't use the FEN for the engine — masterMoveList is authoritative.
    await browser.readFen();

    while (masterMoveList.length < domSanMoves.length) {
      const san = domSanMoves[masterMoveList.length];
      const uci = applySanToBoard(trackerChess, san);
      if (uci) {
        masterMoveList.push(uci);
        log('Main', `Opponent: "${san}" → ${uci}  [ply ${masterMoveList.length}]`);
      } else {
        log('Main', `WARN: cannot parse opponent SAN "${san}" at ply ${masterMoveList.length} — position may drift`);
        break;
      }
    }

    const positionStr = masterMoveList.length > 0
      ? `startpos moves ${masterMoveList.join(' ')}`
      : 'startpos';

    const legalMoves = trackerChess.moves({ verbose: true })
      .map(m => `${m.from}${m.to}${m.promotion ?? ''}`);

    // -----------------------------------------------------------------------
    // Ask engine for best move
    // -----------------------------------------------------------------------
    let bestMove: string;
    try {
      bestMove = await engine.bestMove(positionStr, MOVE_TIME_MS);
    } catch (err) {
      log('Main', `Engine error: ${err} — restarting`);
      await engine.restart();
      bestMove = await engine.bestMove(positionStr, MOVE_TIME_MS);
    }

    if (!legalMoves.includes(bestMove)) {
      log('Main', `WARNING: engine move ${bestMove} not in legal list [${legalMoves.slice(0, 10).join(' ')}]`);
    }

    await humanDelay();

    // -----------------------------------------------------------------------
    // Execute — retry up to 3 times with different moves if needed
    // -----------------------------------------------------------------------
    const prevCount = domSanMoves.length;
    const failedMoves = new Set<string>();
    let registered = false;
    let finalMove = bestMove;

    for (let attempt = 0; attempt < 3; attempt++) {
      registered = await browser.executeMove(finalMove, prevCount).catch(() => false);
      if (registered) { bestMove = finalMove; break; }

      log('Main', `Move ${finalMove} did not register (attempt ${attempt + 1}/3)`);
      failedMoves.add(finalMove);

      if (attempt < 2) {
        // Re-ask engine; if it repeats a failed move, fall back to a random legal move
        let nextMove = '';
        try { nextMove = await engine.bestMove(positionStr, MOVE_TIME_MS); } catch {}

        if (!nextMove || failedMoves.has(nextMove)) {
          const alternatives = legalMoves.filter(m => !failedMoves.has(m));
          if (alternatives.length === 0) { log('Main', 'No alternative moves left'); break; }
          nextMove = alternatives[Math.floor(Math.random() * alternatives.length)];
          log('Main', `Engine repeated a failed move — random fallback: ${nextMove}`);
        }
        finalMove = nextMove;
      }
    }

    if (!registered) {
      log('Main', 'All 3 move attempts failed — ending game');
      break;
    }

    // -----------------------------------------------------------------------
    // Move registered — update tracker and master list
    // -----------------------------------------------------------------------
    const from = finalMove.slice(0, 2);
    const to   = finalMove.slice(2, 4);
    const promo = finalMove.length === 5 ? finalMove[4] : undefined;
    try {
      trackerChess.move({ from, to, ...(promo ? { promotion: promo } : {}) });
    } catch (err) {
      log('Main', `WARN: could not apply our move ${finalMove} to trackerChess: ${err}`);
    }
    masterMoveList.push(finalMove);
    log('Main', `Our move: ${finalMove}  [ply ${masterMoveList.length}]`);

    // Our move (+1) + opponent's reply (+1)
    expectedMoveCount = domSanMoves.length + 2;
    await sleep(200);
  }

  // -----------------------------------------------------------------------
  // Game ended — log result
  // -----------------------------------------------------------------------
  const result = await browser.page.evaluate(() => {
    const el = document.querySelector(
      '[class*="game-over"] [class*="result"], ' +
      '[class*="result-modal"] [class*="result"], ' +
      '[class*="GameOver"] [class*="title"], ' +
      '.game-over-modal-title',
    );
    return el?.textContent?.trim() ?? 'unknown';
  });
  log('Main', `Game result: ${result}`);
}

// ---------------------------------------------------------------------------
// Entry point — loops forever until CTRL+C
// ---------------------------------------------------------------------------

async function main(): Promise<void> {
  const engine = new UCIEngine(ENGINE_PATH);
  const browser = new ChessDotCom();

  let stopping = false;
  const shutdown = async () => {
    if (stopping) return;
    stopping = true;
    log('Main', 'Shutting down…');
    engine.quit();
    await browser.close();
    process.exit(0);
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);

  try {
    log('Main', `Engine path: ${ENGINE_PATH}`);
    await engine.init();
    await browser.init();

    let gameNumber = 0;
    while (!stopping) {
      gameNumber++;
      log('Main', `=== Game #${gameNumber} ===`);

      try {
        await playGame(engine, browser);
      } catch (err) {
        log('Main', `Game loop error: ${err}`);
        await browser.page.screenshot({ path: `/tmp/debug-game-${gameNumber}.png` });
        log('Main', `Screenshot saved to /tmp/debug-game-${gameNumber}.png`);
      }

      if (stopping) break;

      const delay = GAME_DELAY_MIN + Math.random() * (GAME_DELAY_MAX - GAME_DELAY_MIN);
      log('Main', `Waiting ${(delay / 1000).toFixed(1)}s before next game…`);
      await sleep(delay);
    }
  } finally {
    await shutdown();
  }
}

main().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});
