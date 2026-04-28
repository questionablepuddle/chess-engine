#include "bitboard.h"
#include <cstring>
#include <iostream>
#include <iomanip>

namespace BB {

Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard PawnAttacks[2][64];
Bitboard Between[64][64];
Bitboard Line[64][64];

Magic RookMagics[64];
Magic BishopMagics[64];

// Full rook/bishop attack storage
static Bitboard RookTable[0x19000];
static Bitboard BishopTable[0x1480];

// ============================================================
// Known-good magic numbers (pre-found, hardcoded for fast startup)
// ============================================================

static const Bitboard RookMagicNumbers[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL,  0x2801880a0017001ULL,
    0x100081001000420ULL,  0x200020010080420ULL,  0x3001c0002010008ULL,
    0x8480008002000100ULL, 0x2080088004402900ULL, 0x800098204000ULL,
    0x2024401000200040ULL, 0x100802000801000ULL,  0x120800800801000ULL,
    0x208808088000400ULL,  0x2802200800400ULL,    0x2200800100020080ULL,
    0x801000060821100ULL,  0x80044006422000ULL,   0x100808020004000ULL,
    0x12108a0010204200ULL, 0x140848010000802ULL,  0x481828014002800ULL,
    0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,
    0x20100080080080ULL,   0x2000a00200410ULL,    0x20080800400ULL,
    0x80088400100102ULL,   0x80004600042881ULL,   0x4040008040800020ULL,
    0x440003000200801ULL,  0x4200011004500ULL,    0x188020010100100ULL,
    0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL,
    0x200046502000484ULL,  0x480400080088020ULL,  0x1000422010034000ULL,
    0x30200100110040ULL,   0x100021010009ULL,     0x2002080100110004ULL,
    0x202008004008002ULL,  0x20020004010100ULL,   0x2048440040820001ULL,
    0x101002200408200ULL,  0x40802000401080ULL,   0x4008142004410100ULL,
    0x2060820c0120200ULL,  0x1001004080100ULL,    0x20c020080040080ULL,
    0x2935610830022400ULL, 0x44440041009200ULL,   0x280001040802101ULL,
    0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL,    0x12001008414402ULL,   0x2006104900a0804ULL,
    0x1004081002402ULL
};

static const Bitboard BishopMagicNumbers[64] = {
    0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,
    0x108060845042010ULL,  0x581104180800210ULL,  0x2112080446200010ULL,
    0x1080820820060210ULL, 0x3c0808410220200ULL,  0x4050404440404ULL,
    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL,      0x4011002100800ULL,    0x401484104104005ULL,
    0x801010402020200ULL,  0x400210c3880100ULL,   0x404022024108200ULL,
    0x810018200204102ULL,  0x4002801a02003ULL,    0x85040820080400ULL,
    0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
    0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,
    0x20080002081110ULL,   0x4001001021004000ULL, 0x800040400a011002ULL,
    0xe4004081011002ULL,   0x1c004001012080ULL,   0x8004200962a00220ULL,
    0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL,
    0x42008c0340209202ULL, 0x209188240001000ULL,  0x400408a884001800ULL,
    0x110400a6080400ULL,   0x1840060a44020800ULL, 0x90080104000041ULL,
    0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL,
    0x300000261044000aULL, 0x802241102020002ULL,  0x20906061210001ULL,
    0x5a84841004010310ULL, 0x4010801011c04ULL,    0xa010109502200ULL,
    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL,
    0x4010011029020020ULL
};

// ============================================================
// Slider attack generation (used to build the magic tables)
// ============================================================

static Bitboard slidingAttacks(Square sq, Bitboard occ, const int deltas[4][2]) {
    Bitboard attacks = 0;
    int r = rankOf(sq), f = fileOf(sq);
    for (int d = 0; d < 4; d++) {
        int dr = deltas[d][0], df = deltas[d][1];
        for (int nr = r + dr, nf = f + df;
             nr >= 0 && nr < 8 && nf >= 0 && nf < 8;
             nr += dr, nf += df) {
            Square ns = makeSquare(File(nf), Rank(nr));
            attacks |= squareBB(ns);
            if (occ & squareBB(ns)) break;
        }
    }
    return attacks;
}

static Bitboard rookAttacksCalc(Square sq, Bitboard occ) {
    static const int deltas[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    return slidingAttacks(sq, occ, deltas);
}

static Bitboard bishopAttacksCalc(Square sq, Bitboard occ) {
    static const int deltas[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    return slidingAttacks(sq, occ, deltas);
}

// ============================================================
// Magic table initialization
// ============================================================

static Bitboard rookMask(Square sq) {
    Bitboard mask = 0;
    int r = rankOf(sq), f = fileOf(sq);
    for (int rr = r+1; rr < 7; rr++) mask |= squareBB(makeSquare(File(f), Rank(rr)));
    for (int rr = r-1; rr > 0; rr--) mask |= squareBB(makeSquare(File(f), Rank(rr)));
    for (int ff = f+1; ff < 7; ff++) mask |= squareBB(makeSquare(File(ff), Rank(r)));
    for (int ff = f-1; ff > 0; ff--) mask |= squareBB(makeSquare(File(ff), Rank(r)));
    return mask;
}

static Bitboard bishopMask(Square sq) {
    Bitboard mask = 0;
    int r = rankOf(sq), f = fileOf(sq);
    for (int rr = r+1, ff = f+1; rr < 7 && ff < 7; rr++, ff++) mask |= squareBB(makeSquare(File(ff), Rank(rr)));
    for (int rr = r+1, ff = f-1; rr < 7 && ff > 0; rr++, ff--) mask |= squareBB(makeSquare(File(ff), Rank(rr)));
    for (int rr = r-1, ff = f+1; rr > 0 && ff < 7; rr--, ff++) mask |= squareBB(makeSquare(File(ff), Rank(rr)));
    for (int rr = r-1, ff = f-1; rr > 0 && ff > 0; rr--, ff--) mask |= squareBB(makeSquare(File(ff), Rank(rr)));
    return mask;
}

// Enumerate all subsets of mask using Carry-Rippler trick
static void initMagicTable(Magic* magics, Bitboard* table, const Bitboard* magicNumbers,
                            bool isRook, int* tableOffset) {
    for (int sq = 0; sq < 64; sq++) {
        Magic& m = magics[sq];
        m.mask   = isRook ? rookMask(Square(sq)) : bishopMask(Square(sq));
        m.magic  = magicNumbers[sq];
        int bits = popcount(m.mask);
        m.shift  = 64 - bits;
        m.attacks = table + *tableOffset;
        *tableOffset += (1 << bits);

        // Fill attack table for all blocker subsets
        Bitboard occ = 0;
        do {
            unsigned idx = m.index(occ);
            m.attacks[idx] = isRook ? rookAttacksCalc(Square(sq), occ)
                                    : bishopAttacksCalc(Square(sq), occ);
            occ = (occ - m.mask) & m.mask; // Carry-Rippler
        } while (occ);
    }
}

// ============================================================
// Non-sliding attack generation
// ============================================================

static Bitboard knightAttacksCalc(Square sq) {
    Bitboard b = squareBB(sq);
    return ((b << 17) & ~FileABB)
         | ((b << 15) & ~FileHBB)
         | ((b << 10) & ~(FileABB | FileBBB))
         | ((b <<  6) & ~(FileGBB | FileHBB))
         | ((b >> 17) & ~FileHBB)
         | ((b >> 15) & ~FileABB)
         | ((b >> 10) & ~(FileGBB | FileHBB))
         | ((b >>  6) & ~(FileABB | FileBBB));
}

static Bitboard kingAttacksCalc(Square sq) {
    Bitboard b = squareBB(sq);
    Bitboard atk = ((b << 1) & ~FileABB) | ((b >> 1) & ~FileHBB) | (b << 8) | (b >> 8);
    atk |= ((b << 9) & ~FileABB) | ((b << 7) & ~FileHBB)
         | ((b >> 7) & ~FileABB) | ((b >> 9) & ~FileHBB);
    return atk;
}

// ============================================================
// Between / Line tables
// ============================================================

static void initRayTables() {
    for (int s1 = 0; s1 < 64; s1++) {
        for (int s2 = 0; s2 < 64; s2++) {
            Between[s1][s2] = 0;
            Line[s1][s2]    = 0;
            if (s1 == s2) continue;

            Bitboard b1 = squareBB(s1), b2 = squareBB(s2);

            // Check all four slider directions
            static const int rookDeltas[4][2]   = {{1,0},{-1,0},{0,1},{0,-1}};
            static const int bishopDeltas[4][2]  = {{1,1},{1,-1},{-1,1},{-1,-1}};

            auto onRay = [&](const int deltas[4][2]) -> bool {
                for (int d = 0; d < 4; d++) {
                    Bitboard between = 0;
                    int r = rankOf(Square(s1)), f = fileOf(Square(s1));
                    int dr = deltas[d][0], df = deltas[d][1];
                    bool hit = false;
                    for (int nr = r+dr, nf = f+df;
                         nr >= 0 && nr < 8 && nf >= 0 && nf < 8;
                         nr += dr, nf += df) {
                        Square ns = makeSquare(File(nf), Rank(nr));
                        if (ns == Square(s2)) { hit = true; break; }
                        between |= squareBB(ns);
                    }
                    if (hit) {
                        Between[s1][s2] = between;
                        // Build full line
                        Bitboard line = b1 | b2 | between;
                        for (int nr = r-dr, nf = f-df;
                             nr >= 0 && nr < 8 && nf >= 0 && nf < 8;
                             nr -= dr, nf -= df)
                            line |= squareBB(makeSquare(File(nf), Rank(nr)));
                        for (int nr = rankOf(Square(s2))+dr,
                                 nf = fileOf(Square(s2))+df;
                             nr >= 0 && nr < 8 && nf >= 0 && nf < 8;
                             nr += dr, nf += df)
                            line |= squareBB(makeSquare(File(nf), Rank(nr)));
                        Line[s1][s2] = line;
                        return true;
                    }
                }
                return false;
            };

            if (!onRay(rookDeltas)) onRay(bishopDeltas);
        }
    }
}

// ============================================================
// Public init
// ============================================================

void init() {
    // Knight and king tables
    for (int sq = 0; sq < 64; sq++) {
        KnightAttacks[sq] = knightAttacksCalc(Square(sq));
        KingAttacks[sq]   = kingAttacksCalc(Square(sq));

        // Pawn attacks
        Bitboard b = squareBB(sq);
        PawnAttacks[WHITE][sq] = shift<NORTH_EAST>(b) | shift<NORTH_WEST>(b);
        PawnAttacks[BLACK][sq] = shift<SOUTH_EAST>(b) | shift<SOUTH_WEST>(b);
    }

    // Magic tables
    int rookOffset = 0, bishopOffset = 0;
    initMagicTable(RookMagics,   RookTable,   RookMagicNumbers,   true,  &rookOffset);
    initMagicTable(BishopMagics, BishopTable, BishopMagicNumbers, false, &bishopOffset);

    // Ray tables
    initRayTables();
}

Bitboard attacks(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
        case KNIGHT: return KnightAttacks[s];
        case BISHOP: return bishopAttacks(s, occ);
        case ROOK:   return rookAttacks(s, occ);
        case QUEEN:  return queenAttacks(s, occ);
        case KING:   return KingAttacks[s];
        default:     return 0;
    }
}

void print(Bitboard b) {
    for (int r = 7; r >= 0; r--) {
        std::cout << (r + 1) << " ";
        for (int f = 0; f < 8; f++) {
            std::cout << ((b >> makeSquare(File(f), Rank(r))) & 1 ? "1 " : ". ");
        }
        std::cout << "\n";
    }
    std::cout << "  a b c d e f g h\n\n";
}

} // namespace BB
