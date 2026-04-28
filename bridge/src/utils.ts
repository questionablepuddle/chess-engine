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
    const m = tryParseSAN(chess, san);
    if (m) {
      uci.push(m.from + m.to + (m.promotion ?? ''));
    } else {
      console.warn(`[utils] Cannot parse SAN "${san}" — skipping (position may drift)`);
    }
  }

  return uci;
}

// Try to parse a SAN move, handling abbreviated / annotated forms from chess.com:
//   - Strip check ("+"), checkmate ("#"), and annotation ("!", "?") suffixes
//   - Use sloppy:true so chess.js accepts ambiguous notation
//   - If the bare token still fails, try each piece prefix (N/B/R/Q/K) in case
//     chess.com omitted it (e.g. "c3" for "Nc3")
//   - Handle "xd5" (missing from-file) by scanning legal captures
function tryParseSAN(chess: Chess, raw: string) {
  // Normalise: strip trailing +, #, !, ?
  const san = raw.replace(/[+#!?]/g, '').trim();

  // Attempt 1: as-is with sloppy mode
  let m = tryMove(chess, san);
  if (m) return m;

  // Attempt 2: try each piece prefix
  for (const prefix of ['N', 'B', 'R', 'Q', 'K']) {
    m = tryMove(chess, prefix + san);
    if (m) return m;
  }

  // Attempt 3: "xd5" → no from-file capture; find legal capture landing on target
  if (/^x[a-h][1-8]/.test(san)) {
    const target = san.slice(1, 3);
    const legal = chess.moves({ verbose: true });
    const candidate = legal.find(mv => mv.to === target && mv.captured);
    if (candidate) { m = tryMove(chess, candidate.san); if (m) return m; }
  }

  return null;
}

function tryMove(chess: Chess, san: string) {
  try {
    return chess.move(san, { sloppy: true } as any);
  } catch {
    return null;
  }
}

// Apply a single SAN move (as displayed by chess.com) to a Chess instance.
// Handles missing piece prefixes ("f3" for "Nf3") and annotation suffixes.
// Returns the UCI string (e.g. "g1f3") on success, null if unparseable.
// Mutates `chess` by applying the move.
export function applySanToBoard(chess: Chess, san: string): string | null {
  const m = tryParseSAN(chess, san);
  if (!m) return null;
  return m.from + m.to + (m.promotion ?? '');
}

// Returns all legal moves from the position after replaying sanMoves, as UCI strings.
export function getLegalUciMoves(sanMoves: string[]): string[] {
  const chess = new Chess();
  for (const san of sanMoves) {
    if (!tryParseSAN(chess, san)) break;
  }
  return chess.moves({ verbose: true }).map(m => m.from + m.to + (m.promotion ?? ''));
}

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
