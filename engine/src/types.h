#pragma once
#include <cstdint>
#include <cassert>
#include <string>

// ============================================================
// Fundamental types and constants
// ============================================================

using Bitboard = uint64_t;
using Key      = uint64_t;
using Score    = int32_t;

// Squares: a1=0, b1=1, ..., h8=63 (LERF mapping)
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64,
    SQ_NB   = 64
};

enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NB };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NB };

enum Color : int { WHITE, BLACK, COLOR_NB };

enum PieceType : int {
    NO_PIECE_TYPE,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    PIECE_TYPE_NB = 7
};

// Pieces encode color + type: lower 3 bits = type, bit 3 = color
enum Piece : int {
    NO_PIECE,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

// Castling rights bitmask
enum CastlingRights : int {
    NO_CASTLING  = 0,
    WHITE_OO     = 1,
    WHITE_OOO    = 2,
    BLACK_OO     = 4,
    BLACK_OOO    = 8,
    ANY_CASTLING = 15
};

enum Direction : int {
    NORTH =  8, SOUTH = -8,
    EAST  =  1, WEST  = -1,
    NORTH_EAST =  9, NORTH_WEST =  7,
    SOUTH_EAST = -7, SOUTH_WEST = -9
};

// Score constants
constexpr int VALUE_ZERO    =    0;
constexpr int VALUE_DRAW    =    0;
constexpr int VALUE_MATE    = 32000;
constexpr int VALUE_INFINITE = 32001;
constexpr int VALUE_NONE    = 32002;
constexpr int MATE_IN_MAX_PLY = VALUE_MATE - 256;

// Material values (centipawns)
constexpr int PieceValue[PIECE_TYPE_NB] = {
    0,   // NO_PIECE_TYPE
    100, // PAWN
    320, // KNIGHT
    330, // BISHOP
    500, // ROOK
    900, // QUEEN
    0    // KING (not used in material)
};

// ============================================================
// Square utilities
// ============================================================

constexpr Square makeSquare(File f, Rank r) { return Square(r * 8 + f); }
constexpr File   fileOf(Square s)           { return File(s & 7); }
constexpr Rank   rankOf(Square s)           { return Rank(s >> 3); }
constexpr Square flip(Square s)             { return Square(s ^ 56); }
constexpr Square mirrorFile(Square s)       { return Square(s ^ 7); }

constexpr bool isValid(Square s) { return s >= SQ_A1 && s <= SQ_H8; }

constexpr int chebyshev(Square a, Square b) {
    int fd = fileOf(a) - fileOf(b);
    int rd = rankOf(a) - rankOf(b);
    return (fd < 0 ? -fd : fd) > (rd < 0 ? -rd : rd)
         ? (fd < 0 ? -fd : fd)
         : (rd < 0 ? -rd : rd);
}

// ============================================================
// Piece utilities
// ============================================================

constexpr Piece     makePiece(Color c, PieceType pt) { return Piece((c << 3) | pt); }
constexpr PieceType typeOf(Piece p)   { return PieceType(p & 7); }
constexpr Color     colorOf(Piece p)  { return Color(p >> 3); }

constexpr Color operator~(Color c) { return Color(c ^ BLACK); }

// ============================================================
// Move encoding — packed into 32 bits
// ============================================================
//
//  bits  0- 5 : from square
//  bits  6-11 : to square
//  bits 12-15 : moved piece (Piece enum)
//  bits 16-19 : captured piece (Piece enum)
//  bits 20-22 : promotion piece type (PieceType)
//  bits 23-26 : flags
//
//  Flag bits (bit 23 = bit 0 of flags):
//    bit 0: promotion
//    bit 1: en-passant capture
//    bit 2: castling
//    bit 3: double pawn push

enum MoveFlags : uint32_t {
    MF_NONE        = 0,
    MF_PROMOTION   = 1 << 23,
    MF_EN_PASSANT  = 1 << 24,
    MF_CASTLING    = 1 << 25,
    MF_DOUBLE_PUSH = 1 << 26,
    MF_CAPTURE     = 1 << 27,
};

struct Move {
    uint32_t data = 0;

    Move() = default;
    explicit Move(uint32_t d) : data(d) {}

    static Move none()  { return Move(0); }
    static Move null()  { return Move(65); } // special null move

    bool isNone() const { return data == 0; }
    bool isNull() const { return data == 65; }

    Square    from()      const { return Square(data & 0x3F); }
    Square    to()        const { return Square((data >> 6) & 0x3F); }
    Piece     piece()     const { return Piece((data >> 12) & 0xF); }
    Piece     captured()  const { return Piece((data >> 16) & 0xF); }
    PieceType promotion() const { return PieceType((data >> 20) & 0x7); }
    uint32_t  flags()     const { return data >> 23; }

    bool isCapture()    const { return (data & MF_CAPTURE) != 0; }
    bool isPromotion()  const { return (data & MF_PROMOTION) != 0; }
    bool isEnPassant()  const { return (data & MF_EN_PASSANT) != 0; }
    bool isCastling()   const { return (data & MF_CASTLING) != 0; }
    bool isDoublePush() const { return (data & MF_DOUBLE_PUSH) != 0; }

    bool operator==(Move o) const { return data == o.data; }
    bool operator!=(Move o) const { return data != o.data; }
};

inline Move makeMove(Square from, Square to, Piece piece,
                     Piece captured = NO_PIECE,
                     PieceType promo = NO_PIECE_TYPE,
                     uint32_t flags = MF_NONE) {
    uint32_t d = uint32_t(from)
               | (uint32_t(to)       << 6)
               | (uint32_t(piece)    << 12)
               | (uint32_t(captured) << 16)
               | (uint32_t(promo)    << 20)
               | flags;
    if (captured != NO_PIECE) d |= MF_CAPTURE;
    return Move(d);
}

// Algebraic helpers
inline std::string squareName(Square s) {
    std::string n;
    n += char('a' + fileOf(s));
    n += char('1' + rankOf(s));
    return n;
}

inline std::string moveName(Move m) {
    std::string n = squareName(m.from()) + squareName(m.to());
    if (m.isPromotion()) {
        const char promo_chars[] = "?pnbrqk";
        n += promo_chars[m.promotion()];
    }
    return n;
}
