#pragma once
#include "bitboard.h"
#include <string>
#include <array>

// ============================================================
// Zobrist hashing keys
// ============================================================

namespace Zobrist {
    extern Key pieceSquare[PIECE_NB][64];
    extern Key castling[16];
    extern Key enPassant[8];    // file only
    extern Key sideToMove;      // XOR when black to move
    void init();
}

// ============================================================
// State info — stored on the search stack for unmake
// ============================================================

struct StateInfo {
    // Preserved across make/unmake
    Key      key          = 0;
    Piece    capturedPiece = NO_PIECE;
    Square   epSquare     = SQ_NONE;
    int      castlingRights = 0;
    int      halfMoveClock  = 0;
    int      pliesFromNull  = 0;
    bool     epCapture     = false;  // was the move an en-passant capture?

    // Pointer to previous state (for unmake chain)
    StateInfo* previous = nullptr;
};

// ============================================================
// Position class — full board state
// ============================================================

class Position {
public:
    Position() = default;

    // Set board from FEN string (standard chess FEN)
    void setFen(const std::string& fen, StateInfo& si);

    // Returns current FEN
    std::string fen() const;

    // ---- Board queries ----
    Bitboard pieces() const                  { return occupancy_[0] | occupancy_[1]; }
    Bitboard pieces(Color c) const           { return occupancy_[c]; }
    Bitboard pieces(PieceType pt) const      { return byType_[pt]; }
    Bitboard pieces(Color c, PieceType pt) const { return occupancy_[c] & byType_[pt]; }
    Bitboard pieces(PieceType p1, PieceType p2) const { return byType_[p1] | byType_[p2]; }

    Piece    pieceOn(Square s) const         { return board_[s]; }
    PieceType typeOn(Square s) const         { return typeOf(board_[s]); }
    Color    colorOn(Square s) const         { return colorOf(board_[s]); }

    Square   kingSquare(Color c) const       { return kingSquare_[c]; }
    Color    sideToMove() const              { return stm_; }
    int      castlingRights() const          { return si_->castlingRights; }
    Square   epSquare() const                { return si_->epSquare; }
    int      halfMoveClock() const           { return si_->halfMoveClock; }
    int      fullMoveNumber() const          { return fullMove_; }
    Key      key() const                     { return si_->key; }
    int      pliesFromNull() const           { return si_->pliesFromNull; }
    int      ply() const                     { return gamePly_; }

    bool     isEmpty(Square s) const         { return board_[s] == NO_PIECE; }
    bool     isCapture(Move m) const         { return m.captured() != NO_PIECE; }

    // Piece count
    int      count(Color c, PieceType pt) const { return popcount(pieces(c, pt)); }
    int      count(PieceType pt) const          { return popcount(byType_[pt]); }

    // ---- Attack queries ----
    bool     isInCheck() const;
    bool     isSquareAttacked(Square s, Color byColor) const;
    Bitboard attackersTo(Square s, Bitboard occ) const;
    Bitboard attackersTo(Square s) const { return attackersTo(s, pieces()); }
    Bitboard checkersTo(Color c) const;  // pieces giving check to king of color c

    // Sliding piece attack that handles blockers for discovered check detection
    Bitboard slidingAttackersTo(Square s, Bitboard occ) const;

    // ---- Make / Unmake ----
    void makeMove(Move m, StateInfo& newSi);
    void unmakeMove(Move m);
    void makeNullMove(StateInfo& newSi);
    void unmakeNullMove();

    // ---- Static Exchange Evaluation ----
    int  see(Move m) const;
    bool seeGe(Move m, int threshold) const;

    // ---- Move legality ----
    bool isLegal(Move m) const;
    bool isPseudoLegal(Move m) const;

    // ---- Game state ----
    bool isDraw() const;           // 50-move or repetition
    bool isRepetition(int count = 2) const;

    // ---- Debug: recompute key from board state ----
    Key computeKey() const;

    // ---- Non-pawn material ----
    int  nonPawnMaterial(Color c) const;
    int  nonPawnMaterial() const { return nonPawnMaterial(WHITE) + nonPawnMaterial(BLACK); }

    // Piece on square helpers
    bool canCastle(CastlingRights cr) const { return si_->castlingRights & cr; }

    void print() const;

private:
    // Board arrays
    Piece     board_[64];
    Bitboard  byType_[PIECE_TYPE_NB];   // indexed by PieceType
    Bitboard  occupancy_[2];            // by color
    Square    kingSquare_[2];

    Color     stm_;
    int       fullMove_;
    int       gamePly_;

    StateInfo* si_;  // points to current state in search stack

    // Internal helpers
    void putPiece(Piece p, Square s);
    void removePiece(Square s);
    void movePiece(Square from, Square to);
};

// ============================================================
// Castling right tables (indexed by from/to square)
// ============================================================

// Which castling rights are lost if a piece moves from/to a square
extern int CastlingRightsMask[64];

// Must be called once at startup (after BB::init)
void initCastlingMask();

// Rook source/dest squares for each castling type
constexpr Square CastlingRookFrom[4] = { SQ_H1, SQ_A1, SQ_H8, SQ_A8 };
constexpr Square CastlingRookTo[4]   = { SQ_F1, SQ_D1, SQ_F8, SQ_D8 };
constexpr Square CastlingKingFrom[4] = { SQ_E1, SQ_E1, SQ_E8, SQ_E8 };
constexpr Square CastlingKingTo[4]   = { SQ_G1, SQ_C1, SQ_G8, SQ_C8 };

// Squares between king and rook that must be empty + not attacked
constexpr Bitboard CastlingPath[4] = {
    (1ULL << SQ_F1) | (1ULL << SQ_G1),                   // White OO
    (1ULL << SQ_B1) | (1ULL << SQ_C1) | (1ULL << SQ_D1), // White OOO
    (1ULL << SQ_F8) | (1ULL << SQ_G8),                   // Black OO
    (1ULL << SQ_B8) | (1ULL << SQ_C8) | (1ULL << SQ_D8)  // Black OOO
};
// Squares king passes through (must not be attacked)
constexpr Bitboard CastlingKingPath[4] = {
    (1ULL << SQ_E1) | (1ULL << SQ_F1) | (1ULL << SQ_G1),
    (1ULL << SQ_C1) | (1ULL << SQ_D1) | (1ULL << SQ_E1),
    (1ULL << SQ_E8) | (1ULL << SQ_F8) | (1ULL << SQ_G8),
    (1ULL << SQ_C8) | (1ULL << SQ_D8) | (1ULL << SQ_E8),
};
