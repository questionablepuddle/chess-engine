import { Chess } from 'chess.js';

// Returns all legal moves from a FEN string, as UCI strings.
export function getLegalUciMovesFromFen(fen: string): string[] {
  try {
    const chess = new Chess(fen);
    return chess.moves({ verbose: true }).map(m => m.from + m.to + (m.promotion ?? ''));
  } catch {
    return [];
  }
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
