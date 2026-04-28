#pragma once
#include "types.h"
#include <array>
#include <immintrin.h>

// ============================================================
// Bitboard primitives
// ============================================================

inline int  popcount(Bitboard b)  { return __builtin_popcountll(b); }
inline int  lsb(Bitboard b)       { return __builtin_ctzll(b); }
inline int  msb(Bitboard b)       { return 63 ^ __builtin_clzll(b); }

inline Square popLSB(Bitboard& b) {
    Square s = Square(__builtin_ctzll(b));
    b &= b - 1;
    return s;
}

inline Bitboard squareBB(Square s)       { return Bitboard(1) << s; }
inline Bitboard squareBB(int s)          { return Bitboard(1) << s; }

// File / rank masks
constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileCBB = FileABB << 2;
constexpr Bitboard FileDBB = FileABB << 3;
constexpr Bitboard FileEBB = FileABB << 4;
constexpr Bitboard FileFBB = FileABB << 5;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << 8;
constexpr Bitboard Rank3BB = Rank1BB << 16;
constexpr Bitboard Rank4BB = Rank1BB << 24;
constexpr Bitboard Rank5BB = Rank1BB << 32;
constexpr Bitboard Rank6BB = Rank1BB << 40;
constexpr Bitboard Rank7BB = Rank1BB << 48;
constexpr Bitboard Rank8BB = Rank1BB << 56;

constexpr Bitboard FileBB[8] = {
    FileABB, FileBBB, FileCBB, FileDBB, FileEBB, FileFBB, FileGBB, FileHBB
};
constexpr Bitboard RankBB[8] = {
    Rank1BB, Rank2BB, Rank3BB, Rank4BB, Rank5BB, Rank6BB, Rank7BB, Rank8BB
};

// ============================================================
// Shift helpers
// ============================================================

template<Direction D>
constexpr Bitboard shift(Bitboard b) {
    if constexpr (D == NORTH)       return b << 8;
    if constexpr (D == SOUTH)       return b >> 8;
    if constexpr (D == EAST)        return (b & ~FileHBB) << 1;
    if constexpr (D == WEST)        return (b & ~FileABB) >> 1;
    if constexpr (D == NORTH_EAST)  return (b & ~FileHBB) << 9;
    if constexpr (D == NORTH_WEST)  return (b & ~FileABB) << 7;
    if constexpr (D == SOUTH_EAST)  return (b & ~FileHBB) >> 7;
    if constexpr (D == SOUTH_WEST)  return (b & ~FileABB) >> 9;
    return 0;
}

// ============================================================
// Attack tables — precomputed
// ============================================================

namespace BB {

// Knight and king attacks (64 entries each)
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];

// Pawn attacks [color][square]
extern Bitboard PawnAttacks[2][64];

// Between two squares (exclusive of endpoints), 0 if not on same ray
extern Bitboard Between[64][64];

// Line through two squares (entire ray), 0 if not on same ray
extern Bitboard Line[64][64];

// Magic bitboard structures for sliders
struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    int shift;

    inline unsigned index(Bitboard occ) const {
        return unsigned(((occ & mask) * magic) >> shift);
    }
};

extern Magic RookMagics[64];
extern Magic BishopMagics[64];

// Slider attack functions (use magics)
inline Bitboard rookAttacks(Square s, Bitboard occ) {
    return RookMagics[s].attacks[RookMagics[s].index(occ)];
}
inline Bitboard bishopAttacks(Square s, Bitboard occ) {
    return BishopMagics[s].attacks[BishopMagics[s].index(occ)];
}
inline Bitboard queenAttacks(Square s, Bitboard occ) {
    return rookAttacks(s, occ) | bishopAttacks(s, occ);
}

// Attack from square for a piece type
Bitboard attacks(PieceType pt, Square s, Bitboard occ);

// Initialize all tables — must be called once at startup
void init();

// Pretty-print a bitboard for debugging
void print(Bitboard b);

} // namespace BB
