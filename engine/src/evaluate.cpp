#include "evaluate.h"
#include <algorithm>

// ============================================================
// Piece-square tables (from white's perspective, rank 1 at bottom)
// Values are [mg, eg] — will be mirrored for black
// Layout: a1=0 ... h8=63
// ============================================================

// Helper macro for defining PST in human-readable rank order (rank 8 first)
#define S(mg, eg) TaperedScore{mg, eg}

static const TaperedScore PawnPST[64] = {
    S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0),
    S( 98,178), S(134,173), S( 61,158), S( 95,134), S( 68,147), S(126,132), S( 34,165), S(-11,187),
    S( -6, 94), S(  7, 100), S( 26, 85), S( 31, 67), S( 65, 56), S( 56, 53), S( 25, 82), S(-20, 84),
    S(-14, 32), S( 13, 24), S(  6, 13), S( 21,  5), S( 23, -2), S( 12,  4), S( 17, 17), S(-23, 17),
    S(-27, 13), S( -2,  9), S( -5, -3), S( 12, -7), S( 17, -7), S(  6, -8), S( 10,  3), S(-25, -1),
    S(-26,  4), S( -4,  7), S( -4, -6), S(-10,  1), S(  3,  0), S(  3, -5), S( 33, -1), S(-12, -8),
    S(-35,  13), S( -1, 8), S(-20,  8), S(-23,  10), S(-15,  13), S( 24,  0), S( 38,  2), S(-22,  -7),
    S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0), S(  0,  0),
};

static const TaperedScore KnightPST[64] = {
    S(-167,-58), S(-89,-38), S(-34,-13), S(-49,-28), S( 61,-31), S(-97,-27), S(-15,-63), S(-107,-99),
    S( -73,-25), S(-41, -8), S( 72,-25), S( 36,  6), S( 23,  5), S( 62,-16), S(  7,-16), S( -17,-58),
    S( -47,-24), S( 60,-20), S( 37, 10), S( 65,  9), S( 84, -1), S(129, -9), S( 73,-19), S(  44,-41),
    S(  -9,-17), S( 17,  3), S( 19, 22), S( 53, 22), S( 37, 22), S( 69, 11), S( 18,  8), S(  22,-18),
    S( -13,-18), S(  4, -6), S( 16, 16), S( 13, 25), S( 28, 16), S( 19, 17), S( 21,  4), S(  -8,-18),
    S( -23,-23), S( -9, -3), S( 12, -1), S( 10, 15), S( 19, 10), S( 17, -3), S( 25,-20), S( -16,-22),
    S( -29,-42), S(-53,-20), S(-12,-10), S( -3, -5), S( -1, -2), S( 18,-20), S(-14,-23), S( -19,-44),
    S(-105,-29), S(-21,-51), S(-58,-23), S(-33,-15), S(-17,-22), S(-28,-18), S(-19,-50), S( -23,-64),
};

static const TaperedScore BishopPST[64] = {
    S(-29,-14), S(  4,-21), S(-82,-11), S(-37, -8), S(-25, -7), S(-42, -9), S(  7,-17), S( -8,-24),
    S(-26, -8), S( 16, -4), S(-18,  7), S(-13, -12), S( 30,  0), S( 59, -3), S( 18, -9), S(-47,-21),
    S(-16,  2), S( 37, -8), S( 43,  0), S( 40,  1), S( 35,  2), S( 50, 6), S( 37,  0), S( -2,  4),
    S( -4, -3), S(  5, 9), S( 19, 12), S( 50, 9), S( 37,  14), S( 37,  10), S(  7,  3), S( -2,  2),
    S( -6, -6), S( 13,  3), S( 13, 13), S( 26, 19), S( 34, 7), S( 12, 10), S( 10,  -3), S(  4, -9),
    S(  0, -12), S( 15,  -3), S( 15,  8), S( 15, 10), S( 14, 13), S( 27,  3), S( 18,-7), S( 10,-15),
    S(  4,-14), S( 15,-18), S( 16, -7), S(  0, -1), S(  7,  4), S( 21, -9), S( 33,-15), S(  1,-27),
    S(-33,-23), S( -3, -9), S(-14,-23), S(-21,-5), S(-13, -9), S(-12,-16), S(-39,-5), S( -21,-17),
};

static const TaperedScore RookPST[64] = {
    S( 32, 13), S( 42, 10), S( 32, 18), S( 51, 15), S( 63, 12), S(  9, 12), S( 31,  8), S( 43,  5),
    S( 27, 11), S( 32, 13), S( 58, 13), S( 62, 11), S( 80,  -3), S( 67,  3), S( 26,  8), S( 44,  3),
    S( -5,  7), S( 19,  7), S( 26,  7), S( 36,  5), S( 17,  4), S( 45, -3), S( 61, -5), S( 16, -3),
    S(-24,  4), S(-11,  3), S(  7, 13), S( 26, 1), S( 24,  2), S( 35,  1), S( -8, -1), S(-20,  2),
    S(-36,  3), S(-26,  5), S(-12,  8), S( -1,  4), S(  9, -5), S( -7,  -6), S(  6, -8), S(-23,-11),
    S(-45, -4), S(-25,  0), S(-16, -5), S(-17, -1), S(  3, -7), S(  0, -12), S( -5, -8), S(-33,-16),
    S(-44, -6), S(-16, -6), S(-20,  0), S( -9,  2), S( -1,  -9), S( 11,-9), S( -6,-11), S(-71,-3),
    S(-19, -9), S(-13,  2), S(  1, 3), S( 17, -1), S( 16, -5), S(  7,-13), S(-37,  4), S(-26,-20),
};

static const TaperedScore QueenPST[64] = {
    S(-28, -9), S(  0, 22), S( 29, 22), S( 12, 27), S( 59, 27), S( 44, 19), S( 43, 10), S( 45, 20),
    S(-24,-17), S(-39, 20), S( -5, 32), S(  1, 41), S(-16, 58), S( 57, 25), S( 28, 30), S( 54, 0),
    S(-13,-20), S(-17,  6), S(  7,  9), S(  8, 49), S( 29, 47), S( 56, 35), S( 47, 19), S( 57,  9),
    S(-27,  3), S(-27, 22), S(-16, 24), S(-16, 45), S( -1, 57), S( 17, 40), S( -2, 57), S(  1, 36),
    S( -9, -18), S(-26, 28), S( -9, 19), S(-10, 47), S( -2, 31), S( -4, 34), S(  3, 39), S( -3, 23),
    S(-14,-16), S(  2,-27), S(-11, 15), S( -2,  6), S( -5,  9), S(  2, 17), S( 14,  10), S(  5,  5),
    S(-35,-22), S( -8,-23), S( 11,-30), S(  2,-16), S(  8,-16), S( 15,-23), S( -3,-36), S(  1,-32),
    S( -1,-33), S(-18,-28), S( -9,-22), S( 10,-43), S(-15,-5), S(-25,-32), S(-31,-20), S(-50,-41),
};

static const TaperedScore KingPST[64] = {
    S(-65,-74), S( 23,-35), S( 16,-18), S(-15,-18), S(-56,-11), S(-34, 15), S(  2,  4), S( 13,-17),
    S( 29,-12), S( -1, 17), S(-20, 14), S( -7, 17), S( -8, 17), S( -4, 38), S(-38, 23), S(-29, 11),
    S( -9, 10), S( 24, 17), S(  2, 23), S(-16, 15), S(-20, 20), S(  6, 45), S( 22, 44), S(-22, 13),
    S(-17, -8), S(-20, 22), S(-12, 24), S(-27, 27), S(-30, 23), S(-25, 33), S(-14, 26), S(-36,  3),
    S(-49,-18), S( -1, -4), S(-27, 21), S(-39, 24), S(-46, 27), S(-44, 23), S(-33,  9), S(-51,-11),
    S(-14,-19), S(-14, -3), S(-22, 11), S(-46, 21), S(-44, 23), S(-30, 16), S(-15,  7), S(-27, -9),
    S(  1,-27), S(  7,-11), S( -8,  4), S(-64, 13), S(-43, 14), S(-16,  4), S(  9, -5), S(  8,-17),
    S(-15,-53), S( 36,-34), S( 12,-21), S(-54, -11), S(-28,-28), S(-14,-14), S( 24,-24), S( 14,-43),
};

// Piece material values (tapered)
static const TaperedScore MaterialValue[PIECE_TYPE_NB] = {
    S(0, 0),      // NO_PIECE
    S(82, 94),    // PAWN
    S(337, 281),  // KNIGHT
    S(365, 297),  // BISHOP
    S(477, 512),  // ROOK
    S(1025, 936), // QUEEN
    S(0, 0),      // KING
};

// ============================================================
// PST access (auto-mirrors for black)
// White: use table as-is (a1=rank1 file a)
// Black: flip rank (XOR 56), negate
// ============================================================

static const TaperedScore* PiecePST[PIECE_TYPE_NB] = {
    nullptr, PawnPST, KnightPST, BishopPST, RookPST, QueenPST, KingPST
};

// Tables are written rank-8-first (human-readable), so white needs sq^56 to
// map LERF rank-1-first squares to the table. Black uses sq directly because
// black's rank-8 (high LERF index) maps naturally to the table's low indices.
inline TaperedScore pst(Color c, PieceType pt, Square s) {
    Square sq = (c == WHITE) ? Square(int(s) ^ 56) : s;
    return PiecePST[pt][sq];
}

// ============================================================
// Pawn structure evaluation
// ============================================================

static TaperedScore evalPawns(const Position& pos) {
    TaperedScore score;

    for (Color c : {WHITE, BLACK}) {
        Color them = ~c;
        Bitboard pawns     = pos.pieces(c, PAWN);
        Bitboard theirPawns = pos.pieces(them, PAWN);
        int sign = (c == WHITE) ? 1 : -1;

        Bitboard pp = pawns;
        while (pp) {
            Square s = popLSB(pp);
            File f = fileOf(s);
            Rank r = rankOf(s);

            // Passed pawn: no enemy pawns ahead on same or adjacent files
            Bitboard ahead = (c == WHITE)
                ? (FileBB[f] | (f > 0 ? FileBB[f-1] : 0) | (f < 7 ? FileBB[f+1] : 0))
                  & ~((1ULL << (s+1)) - 1) & ~squareBB(s)
                : (FileBB[f] | (f > 0 ? FileBB[f-1] : 0) | (f < 7 ? FileBB[f+1] : 0))
                  & ((1ULL << s) - 1);
            bool passed = !(theirPawns & ahead);
            if (passed) {
                int rank = (c == WHITE) ? r : 7 - r;
                int bonus = rank * rank * 5; // scales with rank
                score += TaperedScore{sign * bonus / 2, sign * bonus};
            }

            // Doubled pawn: another friendly pawn on same file
            bool doubled = popcount(pawns & FileBB[f]) > 1;
            if (doubled) score += TaperedScore{sign * -11, sign * -56};

            // Isolated pawn: no friendly pawns on adjacent files
            bool isolated = !((f > 0 ? pos.pieces(c, PAWN) & FileBB[f-1] : 0)
                           |   (f < 7 ? pos.pieces(c, PAWN) & FileBB[f+1] : 0));
            if (isolated) score += TaperedScore{sign * -5, sign * -15};
        }
    }
    return score;
}

// ============================================================
// Mobility tables — scaled to ~50% of Stockfish values because our
// mobility counts are higher (raw slider attacks, no pawn-attack exclusion)
// ============================================================

// Mobility tables scaled to ~25% of Stockfish values.
// Our raw slider mobility (no pawn-attack exclusion) produces 2-3x higher counts
// than Stockfish's restricted computation, so the same tables inflate scores.
static const TaperedScore KnightMobBonus[9] = {
    S(-15,-20),S(-13,-14),S(-3,-7),S(-1,-3),S(0,2),S(3,3),S(5,5),S(7,6),S(8,8)
};
static const TaperedScore BishopMobBonus[14] = {
    S(-12,-14),S(-5,-5),S(4,0),S(6,3),S(9,6),S(12,10),S(13,13),S(15,14),
    S(15,16),S(17,18),S(20,19),S(20,21),S(22,22),S(24,24)
};
static const TaperedScore RookMobBonus[15] = {
    S(-15,-19),S(-5,-4),S(0,5),S(0,9),S(0,17),S(2,24),S(5,25),S(7,30),
    S(10,33),S(10,34),S(10,39),S(12,41),S(14,42),S(14,42),S(15,43)
};
static const TaperedScore QueenMobBonus[28] = {
    S(-7,-12),S(-3,-7),S(-2,-1),S(-2,4),S(5,10),S(5,13),S(5,14),S(8,18),
    S(9,19),S(13,24),S(16,24),S(16,25),S(16,30),S(16,31),S(16,32),S(16,33),
    S(18,34),S(18,35),S(19,36),S(19,37),S(23,37),S(27,42),S(28,42),S(29,42),
    S(45,42),S(46,44),S(48,45),S(51,47)
};

// ============================================================
// Mobility evaluation
// ============================================================

static TaperedScore evalMobility(const Position& pos) {
    TaperedScore score;
    Bitboard occ = pos.pieces();

    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Bitboard own = pos.pieces(c);

        Bitboard knights = pos.pieces(c, KNIGHT);
        while (knights) {
            Square s = popLSB(knights);
            int mob = popcount(BB::KnightAttacks[s] & ~own);
            score += TaperedScore{sign * KnightMobBonus[mob].mg, sign * KnightMobBonus[mob].eg};
        }
        Bitboard bishops = pos.pieces(c, BISHOP);
        while (bishops) {
            Square s = popLSB(bishops);
            int mob = std::min(popcount(BB::bishopAttacks(s, occ) & ~own), 13);
            score += TaperedScore{sign * BishopMobBonus[mob].mg, sign * BishopMobBonus[mob].eg};
        }
        Bitboard rooks = pos.pieces(c, ROOK);
        while (rooks) {
            Square s = popLSB(rooks);
            int mob = std::min(popcount(BB::rookAttacks(s, occ) & ~own), 14);
            score += TaperedScore{sign * RookMobBonus[mob].mg, sign * RookMobBonus[mob].eg};
        }
        Bitboard queens = pos.pieces(c, QUEEN);
        while (queens) {
            Square s = popLSB(queens);
            int mob = std::min(popcount(BB::queenAttacks(s, occ) & ~own), 27);
            score += TaperedScore{sign * QueenMobBonus[mob].mg, sign * QueenMobBonus[mob].eg};
        }
    }

    return score;
}

// ============================================================
// Rook evaluation
// ============================================================

static TaperedScore evalRooks(const Position& pos) {
    TaperedScore score;
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Bitboard rooks = pos.pieces(c, ROOK);
        while (rooks) {
            Square s = popLSB(rooks);
            File f = fileOf(s);
            // Open file
            bool no_own_pawn  = !(pos.pieces(c, PAWN) & FileBB[f]);
            bool no_their_pawn = !(pos.pieces(~c, PAWN) & FileBB[f]);
            if (no_own_pawn && no_their_pawn)
                score += TaperedScore{sign * 36, sign * 16}; // open file
            else if (no_own_pawn)
                score += TaperedScore{sign * 20, sign * 7};  // semi-open file
        }
    }
    return score;
}

// ============================================================
// Bishop pair
// ============================================================

static TaperedScore evalBishopPair(const Position& pos) {
    TaperedScore score;
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        if (pos.count(c, BISHOP) >= 2)
            score += TaperedScore{sign * 22, sign * 30};
    }
    return score;
}

// ============================================================
// King safety (simplified)
// ============================================================

static TaperedScore evalKingSafety(const Position& pos) {
    TaperedScore score;
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Square ksq = pos.kingSquare(c);
        File kf = fileOf(ksq);

        // Pawn shield — count friendly pawns near king
        Bitboard shield = 0;
        for (int df = -1; df <= 1; df++) {
            int nf = kf + df;
            if (nf < 0 || nf > 7) continue;
            shield |= FileBB[nf];
        }
        // Only pawns 1-2 ranks in front of king
        Rank kr = rankOf(ksq);
        if (c == WHITE) {
            shield &= (RankBB[std::min(7, kr+1)] | RankBB[std::min(7, kr+2)]);
        } else {
            shield &= (RankBB[std::max(0, kr-1)] | RankBB[std::max(0, kr-2)]);
        }
        int shield_count = popcount(pos.pieces(c, PAWN) & shield);
        score += TaperedScore{sign * (shield_count * 13 - 40), 0};

        // Open file near king penalty (only in midgame)
        for (int df = -1; df <= 1; df++) {
            int nf = kf + df;
            if (nf < 0 || nf > 7) continue;
            bool open = !(pos.pieces(c, PAWN) & FileBB[nf]);
            if (open) score += TaperedScore{sign * -10, 0};
        }
    }
    return score;
}

// ============================================================
// Main evaluation
// ============================================================

int evaluate(const Position& pos) {
    Color us = pos.sideToMove();

    // Compute game phase
    int phase = 0;
    for (int pt = KNIGHT; pt <= QUEEN; pt++)
        phase += PhaseWeights[pt] * pos.count(PieceType(pt));
    phase = std::min(phase, MAX_PHASE);

    TaperedScore score;

    // Material + PST for all pieces
    for (Color c : {WHITE, BLACK}) {
        for (int pt = PAWN; pt <= KING; pt++) {
            PieceType ptype = PieceType(pt);
            Bitboard bb = pos.pieces(c, ptype);
            while (bb) {
                Square s = popLSB(bb);
                score += MaterialValue[ptype];
                if (c == BLACK) score += TaperedScore{-MaterialValue[ptype].mg * 2,
                                                       -MaterialValue[ptype].eg * 2};
                score += pst(c, ptype, s);
            }
        }
    }

    // Recompute material cleanly
    score = TaperedScore{};
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        for (int pt = PAWN; pt <= QUEEN; pt++) {
            PieceType ptype = PieceType(pt);
            int cnt = pos.count(c, ptype);
            score += TaperedScore{sign * cnt * MaterialValue[ptype].mg,
                                  sign * cnt * MaterialValue[ptype].eg};
        }
        for (int pt = PAWN; pt <= KING; pt++) {
            PieceType ptype = PieceType(pt);
            Bitboard bb = pos.pieces(c, ptype);
            while (bb) {
                Square s = popLSB(bb);
                TaperedScore p = pst(c, ptype, s);
                score.mg += sign * p.mg;
                score.eg += sign * p.eg;
            }
        }
    }

    // Structural terms
    score += evalPawns(pos);
    score += evalMobility(pos);
    score += evalRooks(pos);
    score += evalBishopPair(pos);
    score += evalKingSafety(pos);

    // Taper
    int mg_weight = phase;
    int eg_weight = MAX_PHASE - phase;
    int total = (score.mg * mg_weight + score.eg * eg_weight) / MAX_PHASE;

    // Tempo bonus
    total += (us == WHITE) ? TEMPO : -TEMPO;

    // Return from perspective of side to move
    return (us == WHITE) ? total : -total;
}

int evaluateDetailed(const Position& pos, EvalInfo& info) {
    Color us = pos.sideToMove();

    int phase = 0;
    for (int pt = KNIGHT; pt <= QUEEN; pt++)
        phase += PhaseWeights[pt] * pos.count(PieceType(pt));
    info.phase = std::min(phase, MAX_PHASE);

    for (int ci = 0; ci < 2; ci++) {
        Color c = Color(ci);

        // Material
        info.material[ci] = {};
        for (int pt = PAWN; pt <= QUEEN; pt++) {
            PieceType ptype = PieceType(pt);
            int cnt = pos.count(c, ptype);
            info.material[ci].mg += cnt * MaterialValue[ptype].mg;
            info.material[ci].eg += cnt * MaterialValue[ptype].eg;
        }

        // PST
        info.psqt[ci] = {};
        for (int pt = PAWN; pt <= KING; pt++) {
            PieceType ptype = PieceType(pt);
            Bitboard bb = pos.pieces(c, ptype);
            while (bb) {
                Square s = popLSB(bb);
                TaperedScore p = pst(c, ptype, s);
                info.psqt[ci].mg += p.mg;
                info.psqt[ci].eg += p.eg;
            }
        }

        // Pawns
        info.pawns[ci] = {};
        {
            Color them = ~c;
            Bitboard pawns      = pos.pieces(c, PAWN);
            Bitboard theirPawns = pos.pieces(them, PAWN);
            Bitboard pp = pawns;
            while (pp) {
                Square s = popLSB(pp);
                File f = fileOf(s);
                Rank r = rankOf(s);
                Bitboard ahead = (c == WHITE)
                    ? (FileBB[f] | (f > 0 ? FileBB[f-1] : 0) | (f < 7 ? FileBB[f+1] : 0))
                      & ~((1ULL << (s+1)) - 1) & ~squareBB(s)
                    : (FileBB[f] | (f > 0 ? FileBB[f-1] : 0) | (f < 7 ? FileBB[f+1] : 0))
                      & ((1ULL << s) - 1);
                bool passed = !(theirPawns & ahead);
                if (passed) {
                    int rank = (c == WHITE) ? r : 7 - r;
                    int bonus = rank * rank * 5;
                    info.pawns[ci].mg += bonus / 2;
                    info.pawns[ci].eg += bonus;
                }
                bool doubled = popcount(pawns & FileBB[f]) > 1;
                if (doubled) { info.pawns[ci].mg += -11; info.pawns[ci].eg += -56; }
                bool isolated = !((f > 0 ? pos.pieces(c, PAWN) & FileBB[f-1] : 0)
                               |   (f < 7 ? pos.pieces(c, PAWN) & FileBB[f+1] : 0));
                if (isolated) { info.pawns[ci].mg += -5; info.pawns[ci].eg += -15; }
            }
        }

        // Mobility
        info.mobility[ci] = {};
        {
            Bitboard occ = pos.pieces();
            Bitboard own = pos.pieces(c);
            Bitboard knights = pos.pieces(c, KNIGHT);
            while (knights) {
                Square s = popLSB(knights);
                int mob = popcount(BB::KnightAttacks[s] & ~own);
                info.mobility[ci].mg += KnightMobBonus[mob].mg;
                info.mobility[ci].eg += KnightMobBonus[mob].eg;
            }
            Bitboard bishops = pos.pieces(c, BISHOP);
            while (bishops) {
                Square s = popLSB(bishops);
                int mob = std::min(popcount(BB::bishopAttacks(s, occ) & ~own), 13);
                info.mobility[ci].mg += BishopMobBonus[mob].mg;
                info.mobility[ci].eg += BishopMobBonus[mob].eg;
            }
            Bitboard rooks = pos.pieces(c, ROOK);
            while (rooks) {
                Square s = popLSB(rooks);
                int mob = std::min(popcount(BB::rookAttacks(s, occ) & ~own), 14);
                info.mobility[ci].mg += RookMobBonus[mob].mg;
                info.mobility[ci].eg += RookMobBonus[mob].eg;
            }
            Bitboard queens = pos.pieces(c, QUEEN);
            while (queens) {
                Square s = popLSB(queens);
                int mob = std::min(popcount(BB::queenAttacks(s, occ) & ~own), 27);
                info.mobility[ci].mg += QueenMobBonus[mob].mg;
                info.mobility[ci].eg += QueenMobBonus[mob].eg;
            }
        }

        // Rooks
        info.rooks[ci] = {};
        {
            Bitboard rooks = pos.pieces(c, ROOK);
            while (rooks) {
                Square s = popLSB(rooks);
                File f = fileOf(s);
                bool no_own_pawn   = !(pos.pieces(c, PAWN) & FileBB[f]);
                bool no_their_pawn = !(pos.pieces(~c, PAWN) & FileBB[f]);
                if (no_own_pawn && no_their_pawn) { info.rooks[ci].mg += 36; info.rooks[ci].eg += 16; }
                else if (no_own_pawn)              { info.rooks[ci].mg += 20; info.rooks[ci].eg += 7; }
            }
        }

        // Bishop pair
        info.bishops[ci] = {};
        if (pos.count(c, BISHOP) >= 2) {
            info.bishops[ci].mg += 22;
            info.bishops[ci].eg += 30;
        }

        // King safety
        info.kingSafety[ci] = {};
        {
            Square ksq = pos.kingSquare(c);
            File kf = fileOf(ksq);
            Bitboard shield = 0;
            for (int df = -1; df <= 1; df++) {
                int nf = kf + df;
                if (nf < 0 || nf > 7) continue;
                shield |= FileBB[nf];
            }
            Rank kr = rankOf(ksq);
            if (c == WHITE)
                shield &= (RankBB[std::min(7, kr+1)] | RankBB[std::min(7, kr+2)]);
            else
                shield &= (RankBB[std::max(0, kr-1)] | RankBB[std::max(0, kr-2)]);
            int shield_count = popcount(pos.pieces(c, PAWN) & shield);
            info.kingSafety[ci].mg += shield_count * 13 - 40;
            for (int df = -1; df <= 1; df++) {
                int nf = kf + df;
                if (nf < 0 || nf > 7) continue;
                if (!(pos.pieces(c, PAWN) & FileBB[nf]))
                    info.kingSafety[ci].mg += -10;
            }
        }
    }

    // Build total score (WHITE=positive, BLACK negated)
    TaperedScore total;
    for (int ci = 0; ci < 2; ci++) {
        int sign = (ci == 0) ? 1 : -1;
        total.mg += sign * (info.material[ci].mg + info.psqt[ci].mg + info.pawns[ci].mg
                          + info.mobility[ci].mg + info.rooks[ci].mg
                          + info.bishops[ci].mg + info.kingSafety[ci].mg);
        total.eg += sign * (info.material[ci].eg + info.psqt[ci].eg + info.pawns[ci].eg
                          + info.mobility[ci].eg + info.rooks[ci].eg
                          + info.bishops[ci].eg + info.kingSafety[ci].eg);
    }

    int mg_weight = info.phase;
    int eg_weight = MAX_PHASE - info.phase;
    int result = (total.mg * mg_weight + total.eg * eg_weight) / MAX_PHASE;
    result += (us == WHITE) ? TEMPO : -TEMPO;
    return (us == WHITE) ? result : -result;
}
