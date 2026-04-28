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
    try {
      const m = chess.move(san);
      // promotion is already lowercase ('q','r','b','n') which is what UCI wants
      uci.push(m.from + m.to + (m.promotion ?? ''));
    } catch {
      console.error(`[utils] Cannot parse SAN "${san}" — stopping conversion`);
      break;
    }
  }

  return uci;
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
