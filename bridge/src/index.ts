import 'dotenv/config';
import * as path from 'path';
import { UCIEngine } from './engine';
import { ChessDotCom } from './browser';
import { getLegalUciMoves, sanMovesToUci, sleep, humanDelay, log } from './utils';

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

  // Move count when it's our turn:
  //   White moves at  0, 2, 4, … (even indices)
  //   Black moves at  1, 3, 5, … (odd indices)
  let expectedMoveCount = ourColor === 'white' ? 0 : 1;

  log('Main', `We are ${ourColor} — first move expected at index ${expectedMoveCount}`);

  while (true) {
    // -----------------------------------------------------------------------
    // Wait for our turn
    // -----------------------------------------------------------------------
    let actualCount: number;
    try {
      actualCount = await browser.waitForOurTurn(expectedMoveCount);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      if (msg === 'GAME_OVER') {
        log('Main', 'Game over detected while waiting for our turn');
        break;
      }
      throw err;
    }

    // -----------------------------------------------------------------------
    // Check game over (belt-and-suspenders)
    // -----------------------------------------------------------------------
    if (await browser.isGameOver()) {
      log('Main', 'Game over detected at top of loop');
      break;
    }

    // -----------------------------------------------------------------------
    // Read position — prefer FEN (authoritative), fall back to SAN→UCI
    // -----------------------------------------------------------------------
    const sanMoves = await browser.readMoves();
    let fen = await browser.readFen();

    if (fen) {
      log('Main', `Position from FEN (${sanMoves.length} moves on board)`);
    } else {
      // FEN unavailable — fall back to SAN→UCI conversion
      const uciMoves = sanMovesToUci(sanMoves);
      log('Main', `FEN unavailable — using SAN→UCI (${uciMoves.length}/${sanMoves.length} moves)`);
      // Synthesise a FEN from the move list so the engine interface stays uniform
      fen = uciMoves.length
        ? `startpos moves ${uciMoves.join(' ')}`  // engine will receive "position startpos moves ..."
        : 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
    }

    // -----------------------------------------------------------------------
    // Pre-move legality check (uses SAN→UCI path regardless)
    // -----------------------------------------------------------------------
    const legalMoves = getLegalUciMoves(sanMoves);

    // -----------------------------------------------------------------------
    // Ask engine for best move
    // -----------------------------------------------------------------------
    let bestMove: string;
    try {
      bestMove = await engine.bestMove(fen, MOVE_TIME_MS);
    } catch (err) {
      log('Main', `Engine error: ${err} — restarting engine`);
      await engine.restart();
      bestMove = await engine.bestMove(fen, MOVE_TIME_MS);
    }

    if (!legalMoves.includes(bestMove)) {
      log('Main', `WARNING: engine move ${bestMove} not in legal list: ${legalMoves.slice(0, 15).join(' ')}`);
    }

    // -----------------------------------------------------------------------
    // Human-like delay then execute
    // -----------------------------------------------------------------------
    await humanDelay();

    let registered = false;
    try {
      registered = await browser.executeMove(bestMove, sanMoves.length);
    } catch (err) {
      log('Main', `Move execution threw: ${err}`);
    }

    if (!registered) {
      log('Main', `${bestMove} failed to register — re-asking engine`);
      try {
        const freshFen = await browser.readFen() ?? fen;
        const freshSan = await browser.readMoves();
        bestMove = await engine.bestMove(freshFen, MOVE_TIME_MS);
        log('Main', `Retry move: ${bestMove}`);
        await humanDelay();
        await browser.executeMove(bestMove, freshSan.length);
      } catch (err) {
        log('Main', `Retry failed: ${err} — continuing`);
      }
    }

    // -----------------------------------------------------------------------
    // Advance expected move count: our move (+1) + opponent's reply (+1)
    // -----------------------------------------------------------------------
    expectedMoveCount = sanMoves.length + 2;

    // Small safety pause after our move
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

  // Graceful shutdown
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

      const delay = 5_000 + Math.random() * (GAME_DELAY_MAX - GAME_DELAY_MIN);
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
