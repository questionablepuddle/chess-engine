import 'dotenv/config';
import * as path from 'path';
import { Chess } from 'chess.js';
import { UCIEngine } from './engine';
import { ChessDotCom } from './browser';
import { sleep, humanDelay, log } from './utils';

// Chess.com always promotes to queen via the handlePromotion dialog.
// Normalise any engine promotion suggestion to queen so trackerChess stays
// consistent with the actual board regardless of what the engine prefers.
const queenPromo = (mv: string): string =>
  mv.length === 5 ? mv.slice(0, 4) + 'q' : mv;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const ENGINE_PATH = process.env.ENGINE_PATH
  ? path.resolve(__dirname, '..', process.env.ENGINE_PATH)
  : path.resolve(__dirname, '../../engine/build/chess_engine');

const MOVE_TIME_MS = 3_000;
const GAME_DELAY_MIN = 5_000;
const GAME_DELAY_MAX = 10_000;

// ---------------------------------------------------------------------------
// trackerChess helpers
// ---------------------------------------------------------------------------

// Rebuild a Chess instance by replaying every UCI move from scratch.
// Used to recover from position drift without losing game history.
function rebuildTrackerFromList(moveList: string[]): Chess {
  const chess = new Chess();
  for (const mv of moveList) {
    try {
      chess.move({
        from: mv.slice(0, 2),
        to:   mv.slice(2, 4),
        ...(mv.length === 5 ? { promotion: mv[4] } : {}),
      });
    } catch (e) {
      log('Main', `WARN: rebuildTracker failed replaying ${mv}: ${e}`);
      break;
    }
  }
  return chess;
}

function countPieces(chess: Chess): number {
  return chess.board().flat().filter(Boolean).length;
}

// ---------------------------------------------------------------------------
// Main game loop
// ---------------------------------------------------------------------------

async function playGame(engine: UCIEngine, browser: ChessDotCom): Promise<void> {
  log('Main', '--- Starting new game ---');

  await engine.newGame();
  await browser.startGameVsBot();

  const ourColor = browser.ourColor;

  // trackerChess is the single source of truth for position.
  //   Our moves  → applied directly with exact UCI coordinates.
  //   Opponent moves → detected via last-move highlight squares (light DOM).
  let trackerChess = new Chess();
  let knownPlyCount = 0; // half-moves already applied to trackerChess

  // Full ordered list of every UCI move made so far (ours + opponent's).
  // Used to rebuild trackerChess from scratch when sync drift is detected.
  const masterMoveList: string[] = [];
  let consecutiveIllegalCount = 0;

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
    // Sync move count — used for turn parity and executeMove check.
    // The SAN content is never used.
    // -----------------------------------------------------------------------
    const domSanMoves = await browser.syncMoves();
    const moveCount = domSanMoves.length;

    // -----------------------------------------------------------------------
    // Sync opponent's move into trackerChess via highlight detection
    // -----------------------------------------------------------------------
    if (knownPlyCount < moveCount) {
      const highlighted = await browser.readHighlightedSquares();
      const legal = trackerChess.moves({ verbose: true });
      let applied = false;

      // Try every pair of highlighted squares — one of them should be the
      // opponent's from/to.  We test both orderings and all promotions.
      outer: for (let i = 0; i < highlighted.length && !applied; i++) {
        for (let j = i + 1; j < highlighted.length && !applied; j++) {
          const sq1 = highlighted[i];
          const sq2 = highlighted[j];
          const candidates = legal.filter(m =>
            (m.from === sq1 && m.to === sq2) || (m.from === sq2 && m.to === sq1),
          );
          if (candidates.length === 0) continue;

          // Non-promotion moves first; fall back to queen promotion.
          const move = candidates.find(m => !m.promotion)
                    ?? candidates.find(m => m.promotion === 'q')
                    ?? candidates[0];
          // Always record opponent promotion as 'q' to match the board UI
          const oppPromo = move.promotion ? 'q' : undefined;
          const oppUCI   = `${move.from}${move.to}${oppPromo ?? ''}`;
          trackerChess.move({ from: move.from, to: move.to, ...(oppPromo ? { promotion: oppPromo } : {}) });
          masterMoveList.push(oppUCI);
          knownPlyCount++;
          log('Main', `Opponent: ${oppUCI}  [ply ${knownPlyCount}]  pieces=${countPieces(trackerChess)}`);
          log('Main', `FEN after opponent move: ${trackerChess.fen()}`);
          applied = true;
        }
      }

      if (!applied) {
        log('Main', `WARN: could not determine opponent move from highlights [${highlighted.join(', ')}] — position may drift`);
      }
    }

    // -----------------------------------------------------------------------
    // Ask engine for best move
    // -----------------------------------------------------------------------
    const fen = trackerChess.fen();
    const legalMoves = trackerChess.moves({ verbose: true })
      .map(m => `${m.from}${m.to}${m.promotion ?? ''}`);

    log('Main', `Sending FEN to engine: ${fen}  pieces=${countPieces(trackerChess)}`);

    let bestMove: string;
    try {
      bestMove = queenPromo(await engine.bestMove(fen, MOVE_TIME_MS));
    } catch (err) {
      log('Main', `Engine error: ${err} — restarting`);
      await engine.restart();
      bestMove = queenPromo(await engine.bestMove(fen, MOVE_TIME_MS));
    }

    if (!legalMoves.includes(bestMove)) {
      consecutiveIllegalCount++;
      log('Main', `WARNING: engine move ${bestMove} not in legal list (illegal streak=${consecutiveIllegalCount})`);
      if (consecutiveIllegalCount >= 2) {
        log('Main', `DIAG — FEN: ${fen}`);
        log('Main', `DIAG — legal moves: [${legalMoves.join(' ')}]`);
        log('Main', `Rebuilding trackerChess from masterMoveList (${masterMoveList.length} moves): ${masterMoveList.join(' ')}`);
        trackerChess = rebuildTrackerFromList(masterMoveList);
        log('Main', `FEN after rebuild: ${trackerChess.fen()}  pieces=${countPieces(trackerChess)}`);
      }
    } else {
      consecutiveIllegalCount = 0;
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
        let nextMove = '';
        try { nextMove = queenPromo(await engine.bestMove(fen, MOVE_TIME_MS)); } catch {}

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

    // -----------------------------------------------------------------------
    // Move registered — apply to trackerChess
    // -----------------------------------------------------------------------
    const from  = finalMove.slice(0, 2);
    const to    = finalMove.slice(2, 4);
    const promo = finalMove.length === 5 ? finalMove[4] : undefined;
    try {
      trackerChess.move({ from, to, ...(promo ? { promotion: promo } : {}) });
      masterMoveList.push(finalMove);
      knownPlyCount++;
      log('Main', `Our move: ${finalMove}  [ply ${knownPlyCount}]  pieces=${countPieces(trackerChess)}`);
      log('Main', `FEN after our move: ${trackerChess.fen()}`);
    } catch (err) {
      log('Main', `WARN: could not apply our move to trackerChess: ${err}`);
    }

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
