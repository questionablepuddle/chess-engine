#pragma once
#include "position.h"
#include "types.h"
#include <string>

// ---------------------------------------------------------------------------
// Syzygy endgame tablebase interface
// Wraps the Fathom library (src/fathom/src/tbprobe.h).
// ---------------------------------------------------------------------------

namespace Syzygy {

// Maximum piece count in the loaded tablebases (0 if not initialised).
// Mirrors TB_LARGEST from Fathom.
extern int MaxPieces;

// Initialise from a path (colon-separated on Unix, semicolon on Windows).
// Returns true when at least one tablebase file was found.
bool init(const std::string& path);

// Probe WDL during search.
// Returns a score from the side-to-move's perspective:
//   +VALUE_MATE-200  win     (TB_WIN)
//   +1               cursed win (TB_CURSED_WIN, 50-move draw possible)
//    0               draw    (TB_DRAW)
//   -1               blessed loss (TB_BLESSED_LOSS)
//   -VALUE_MATE+200  loss    (TB_LOSS)
// Returns VALUE_NONE if the probe failed or conditions are not met.
// Should only be called for positions with <= MaxPieces pieces, no castling.
int probeWDL(const Position& pos);

// Probe DTZ at the root and return the tablebase best move.
// score is set to the equivalent centipawn value (positive = winning side to move).
// Returns Move::none() if the probe failed.
// Should not be called during search (Fathom is not thread-safe at root).
Move probeRoot(const Position& pos, int& score);

} // namespace Syzygy
