#include "syzygy.h"
#include "movegen.h"
#include "bitboard.h"
#include <iostream>

extern "C" {
#include "fathom/src/tbprobe.h"
}

namespace Syzygy {

int MaxPieces = 0;

bool init(const std::string& path) {
    bool ok = tb_init(path.c_str());
    MaxPieces = ok ? static_cast<int>(TB_LARGEST) : 0;
    std::cerr << "[Syzygy] tb_init=" << (ok ? "true" : "false")
              << " TB_LARGEST=" << TB_LARGEST
              << " path=" << path << "\n";
    if (TB_LARGEST == 0)
        std::cerr << "[Syzygy] TB_LARGEST=0: tablebase files not found\n";
    return ok;
}

// ---------------------------------------------------------------------------
// WDL probe — called during search for positions with few enough pieces
// ---------------------------------------------------------------------------

int probeWDL(const Position& pos) {
    if (MaxPieces == 0) return VALUE_NONE;
    if (popcount(pos.pieces()) > MaxPieces) return VALUE_NONE;
    // Castling rights make TB results incorrect — skip
    if (pos.castlingRights() != 0) return VALUE_NONE;

    unsigned ep = (pos.epSquare() == SQ_NONE) ? 0u : static_cast<unsigned>(pos.epSquare());

    // Pass rule50=0 to get the "pure" WDL without 50-move-rule filtering.
    // Most practical endgames are far from the 50-move limit; this maximises probe coverage.
    unsigned wdl = tb_probe_wdl(
        static_cast<uint64_t>(pos.pieces(WHITE)),
        static_cast<uint64_t>(pos.pieces(BLACK)),
        static_cast<uint64_t>(pos.pieces(KING)),
        static_cast<uint64_t>(pos.pieces(QUEEN)),
        static_cast<uint64_t>(pos.pieces(ROOK)),
        static_cast<uint64_t>(pos.pieces(BISHOP)),
        static_cast<uint64_t>(pos.pieces(KNIGHT)),
        static_cast<uint64_t>(pos.pieces(PAWN)),
        /*rule50=*/0,
        /*castling=*/0,
        ep,
        pos.sideToMove() == WHITE
    );

    if (wdl == TB_RESULT_FAILED) return VALUE_NONE;

    switch (wdl) {
        case TB_WIN:          return  VALUE_MATE - 200;
        case TB_CURSED_WIN:   return  1;
        case TB_DRAW:         return  VALUE_DRAW;
        case TB_BLESSED_LOSS: return -1;
        case TB_LOSS:         return -(VALUE_MATE - 200);
        default:              return  VALUE_NONE;
    }
}

// ---------------------------------------------------------------------------
// Root DTZ probe — called once at the root before iterative deepening
// ---------------------------------------------------------------------------

Move probeRoot(const Position& pos, int& score) {
    score = VALUE_NONE;
    if (MaxPieces == 0) return Move::none();
    if (popcount(pos.pieces()) > MaxPieces) return Move::none();
    if (pos.castlingRights() != 0) return Move::none();

    unsigned ep = (pos.epSquare() == SQ_NONE) ? 0u : static_cast<unsigned>(pos.epSquare());

    unsigned results[TB_MAX_MOVES];
    unsigned result = tb_probe_root(
        static_cast<uint64_t>(pos.pieces(WHITE)),
        static_cast<uint64_t>(pos.pieces(BLACK)),
        static_cast<uint64_t>(pos.pieces(KING)),
        static_cast<uint64_t>(pos.pieces(QUEEN)),
        static_cast<uint64_t>(pos.pieces(ROOK)),
        static_cast<uint64_t>(pos.pieces(BISHOP)),
        static_cast<uint64_t>(pos.pieces(KNIGHT)),
        static_cast<uint64_t>(pos.pieces(PAWN)),
        static_cast<unsigned>(pos.halfMoveClock()),
        /*castling=*/0,
        ep,
        pos.sideToMove() == WHITE,
        results
    );

    if (result == TB_RESULT_FAILED) return Move::none();

    unsigned wdl  = TB_GET_WDL(result);
    unsigned dtz  = TB_GET_DTZ(result);
    unsigned from = TB_GET_FROM(result);
    unsigned to   = TB_GET_TO(result);
    unsigned promo = TB_GET_PROMOTES(result);

    // Map WDL + DTZ to a search score from the side-to-move perspective
    if (wdl == TB_WIN)
        score = VALUE_MATE - 200 - static_cast<int>(dtz);
    else if (wdl == TB_CURSED_WIN)
        score = 1;
    else if (wdl == TB_DRAW)
        score = VALUE_DRAW;
    else if (wdl == TB_BLESSED_LOSS)
        score = -1;
    else // TB_LOSS
        score = -(VALUE_MATE - 200 - static_cast<int>(dtz));

    // Find the legal move that matches the from/to/promotion returned by Fathom
    MoveList list;
    generateAllMoves(pos, list);
    for (int i = 0; i < list.size; i++) {
        Move m = list.moves[i];
        if (!pos.isLegal(m)) continue;
        if (static_cast<unsigned>(m.from()) != from) continue;
        if (static_cast<unsigned>(m.to())   != to)   continue;

        if (!m.isPromotion() && promo == TB_PROMOTES_NONE) return m;

        if (m.isPromotion()) {
            PieceType pt = m.promotion();
            if ((promo == TB_PROMOTES_QUEEN  && pt == QUEEN)  ||
                (promo == TB_PROMOTES_ROOK   && pt == ROOK)   ||
                (promo == TB_PROMOTES_BISHOP && pt == BISHOP) ||
                (promo == TB_PROMOTES_KNIGHT && pt == KNIGHT))
                return m;
        }
    }

    return Move::none();
}

} // namespace Syzygy
