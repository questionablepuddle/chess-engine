import { Chess } from 'chess.js';

// ---------------------------------------------------------------------------
// Square helpers
// ---------------------------------------------------------------------------

// "e4" → "square-54"  (chess.com square class: file a=1..h=8, rank 1-8)
export function squareToClass(sq: string): string {
  const file = sq.charCodeAt(0) - 'a'.charCodeAt(0) + 1;
  const rank = sq[1];
  return `square-${file}${rank}`;
}

// "e4" → { file: 5, rank: 4 }
export function parseSquare(sq: string): { file: number; rank: number } {
  return {
    file: sq.charCodeAt(0) - 'a'.charCodeAt(0) + 1,
    rank: parseInt(sq[1], 10),
  };
}

// ---------------------------------------------------------------------------
// SAN → UCI conversion via chess.js
// ---------------------------------------------------------------------------

// Convert an ordered array of SAN moves (as returned by chess.com's move list)
// into UCI strings by replaying them on a chess.js board.
export function sanMovesToUci(sanMoves: string[]): string[] {
  const chess = new Chess();
  const uci: string[] = [];

  for (const san of sanMoves) {
    // Attempt 1: pass the SAN to chess.js as-is (handles exd5, Nxd5, etc.)
    let m = tryMove(chess, san);

    // Attempt 2: "xd5" → missing from-file; try every legal move that lands
    // on the target square and involves a capture, pick the first that parses.
    if (!m && /^x[a-h][1-8]/.test(san)) {
      const target = san.slice(1, 3); // e.g. "d5"
      const legal = chess.moves({ verbose: true });
      const candidate = legal.find(mv => mv.to === target && mv.captured);
      if (candidate) m = tryMove(chess, candidate.san);
    }

    if (m) {
      uci.push(m.from + m.to + (m.promotion ?? ''));
    } else {
      console.warn(`[utils] Cannot parse SAN "${san}" — skipping (position may drift)`);
    }
  }

  return uci;
}

function tryMove(chess: Chess, san: string) {
  try {
    return chess.move(san);
  } catch {
    return null;
  }
}

// Returns all legal moves from the position after replaying sanMoves, as UCI strings.
export function getLegalUciMoves(sanMoves: string[]): string[] {
  const chess = new Chess();
  for (const san of sanMoves) {
    try { chess.move(san); } catch { break; }
  }
  return chess.moves({ verbose: true }).map(m => m.from + m.to + (m.promotion ?? ''));
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

export const sleep = (ms: number): Promise<void> =>
  new Promise(r => setTimeout(r, ms));

// Simulate human think-and-move delay: 500 – 2000 ms
export const humanDelay = (): Promise<void> =>
  sleep(500 + Math.random() * 1500);

export const randomBetween = (lo: number, hi: number): number =>
  lo + Math.random() * (hi - lo);

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

export function log(tag: string, msg: string): void {
  const t = new Date().toISOString().slice(11, 23);
  console.log(`[${t}] [${tag}] ${msg}`);
}
