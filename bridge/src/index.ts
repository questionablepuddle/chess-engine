import 'dotenv/config';
import * as path from 'path';
import { UCIEngine } from './engine';
import { ChessDotCom } from './browser';
import { getLegalUciMovesFromFen, sleep, humanDelay, log } from './utils';

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const ENGINE_PATH = process.env.ENGINE_PATH
  ? path.resolve(__dirname, '..', process.env.ENGINE_PATH)
  : path.resolve(__dirname, '../../engine/build/chess_engine');

const MOVE_TIME_MS = 1_000; // engine think time per move
const GAME_DELAY_MIN = 5_000;
const GAME_DELAY_MAX = 10_000;

// ---------------------------------------------------------------------------
// Main game loop
// ---------------------------------------------------------------------------

async function playGame(engine: UCIEngine, browser: ChessDotCom): Promise<void> {
  log('Main', '--- Starting new game ---');

  await engine.newGame();
  await browser.startGameVsBot();

  const ourColor = browser.ourColor;

  // White moves at 0, 2, 4 … (even); black at 1, 3, 5 … (odd).
  let expectedMoveCount = ourColor === 'white' ? 0 : 1;
  log('Main', `We are ${ourColor}`);

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
    // Read board state
    // -----------------------------------------------------------------------
    // syncMoves() gives us the move COUNT (for turn parity and executeMove
    // registration detection). The SAN content is never parsed for position.
    const domSanMoves = await browser.syncMoves();
    const moveCount = domSanMoves.length;

    // Read actual piece positions from the rendered DOM — no SAN parsing.
    // Active color is inferred from moveCount (even = white, odd = black).
    const fen = await browser.readBoardFen(moveCount);
    if (!fen) {
      log('Main', 'Cannot read board FEN — skipping turn, retrying in 500 ms');
      await sleep(500);
      continue;
    }

    const legalMoves = getLegalUciMovesFromFen(fen);

    // -----------------------------------------------------------------------
    // Ask engine for best move
    // -----------------------------------------------------------------------
    let bestMove: string;
    try {
      bestMove = await engine.bestMove(fen, MOVE_TIME_MS);
    } catch (err) {
      log('Main', `Engine error: ${err} — restarting`);
      await engine.restart();
      bestMove = await engine.bestMove(fen, MOVE_TIME_MS);
    }

    if (!legalMoves.includes(bestMove)) {
      log('Main', `WARNING: engine move ${bestMove} not in legal list [${legalMoves.slice(0, 10).join(' ')}]`);
    }

    await humanDelay();

    // -----------------------------------------------------------------------
    // Execute — retry up to 3 times with different moves if needed
    // -----------------------------------------------------------------------
    const failedMoves = new Set<string>();
    let registered = false;
    let finalMove = bestMove;

    for (let attempt = 0; attempt < 3; attempt++) {
      registered = await browser.executeMove(finalMove, moveCount).catch(() => false);
      if (registered) { bestMove = finalMove; break; }

      log('Main', `Move ${finalMove} did not register (attempt ${attempt + 1}/3)`);
      failedMoves.add(finalMove);

      if (attempt < 2) {
        // Re-ask engine; if it returns a previously-failed move, pick a random legal one
        let nextMove = '';
        try { nextMove = await engine.bestMove(fen, MOVE_TIME_MS); } catch {}

        if (!nextMove || failedMoves.has(nextMove)) {
          const alternatives = legalMoves.filter(m => !failedMoves.has(m));
          if (!alternatives.length) { log('Main', 'No alternative moves left'); break; }
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

    log('Main', `Played: ${finalMove}  [after move ${moveCount}]`);

    // Our move (+1) + opponent's reply (+1)
    expectedMoveCount = moveCount + 2;
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
