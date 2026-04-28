#include "position.h"
#include <sstream>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <random>
#include <algorithm>

// ============================================================
// Zobrist
// ============================================================

namespace Zobrist {
    Key pieceSquare[PIECE_NB][64];
    Key castling[16];
    Key enPassant[8];
    Key sideToMove;

    void init() {
        std::mt19937_64 rng(1070372ull);
        auto rand64 = [&]() { return rng(); };

        for (int p = 0; p < PIECE_NB; p++)
            for (int s = 0; s < 64; s++)
                pieceSquare[p][s] = rand64();

        for (int cr = 0; cr < 16; cr++)
            castling[cr] = rand64();

        for (int f = 0; f < 8; f++)
            enPassant[f] = rand64();

        sideToMove = rand64();
    }
}

// ============================================================
// Castling rights mask
// ============================================================

int CastlingRightsMask[64];

void initCastlingMask() {
    for (int i = 0; i < 64; i++) CastlingRightsMask[i] = ANY_CASTLING;
    CastlingRightsMask[SQ_A1] &= ~WHITE_OOO;
    CastlingRightsMask[SQ_E1] &= ~(WHITE_OO | WHITE_OOO);
    CastlingRightsMask[SQ_H1] &= ~WHITE_OO;
    CastlingRightsMask[SQ_A8] &= ~BLACK_OOO;
    CastlingRightsMask[SQ_E8] &= ~(BLACK_OO | BLACK_OOO);
    CastlingRightsMask[SQ_H8] &= ~BLACK_OO;
}

// ============================================================
// Internal piece management
// ============================================================

void Position::putPiece(Piece p, Square s) {
    board_[s] = p;
    byType_[typeOf(p)]     |= squareBB(s);
    occupancy_[colorOf(p)] |= squareBB(s);
    if (typeOf(p) == KING) kingSquare_[colorOf(p)] = s;
}

void Position::removePiece(Square s) {
    Piece p = board_[s];
    byType_[typeOf(p)]     &= ~squareBB(s);
    occupancy_[colorOf(p)] &= ~squareBB(s);
    board_[s] = NO_PIECE;
}

void Position::movePiece(Square from, Square to) {
    Piece p = board_[from];
    Bitboard mask = squareBB(from) | squareBB(to);
    byType_[typeOf(p)]     ^= mask;
    occupancy_[colorOf(p)] ^= mask;
    board_[from] = NO_PIECE;
    board_[to]   = p;
    if (typeOf(p) == KING) kingSquare_[colorOf(p)] = to;
}

// ============================================================
// FEN parsing
// ============================================================

void Position::setFen(const std::string& fenStr, StateInfo& si) {
    std::memset(board_,     NO_PIECE, sizeof(board_));
    std::memset(byType_,    0, sizeof(byType_));
    std::memset(occupancy_, 0, sizeof(occupancy_));
    si = StateInfo{};
    si_ = &si;

    std::istringstream ss(fenStr);
    std::string token;

    // Piece placement — FEN starts at rank 8 (a8), moves right then down
    ss >> token;
    Square sq = SQ_A8;
    for (char c : token) {
        if (c == '/') { sq = Square(int(sq) - 16); continue; }
        if (c >= '1' && c <= '8') { sq = Square(int(sq) + (c - '0')); continue; }
        Piece p = NO_PIECE;
        switch (c) {
            case 'P': p = W_PAWN;   break; case 'p': p = B_PAWN;   break;
            case 'N': p = W_KNIGHT; break; case 'n': p = B_KNIGHT; break;
            case 'B': p = W_BISHOP; break; case 'b': p = B_BISHOP; break;
            case 'R': p = W_ROOK;   break; case 'r': p = B_ROOK;   break;
            case 'Q': p = W_QUEEN;  break; case 'q': p = B_QUEEN;  break;
            case 'K': p = W_KING;   break; case 'k': p = B_KING;   break;
        }
        if (p != NO_PIECE) putPiece(p, sq);
        sq = Square(int(sq) + 1);
    }

    // Side to move
    ss >> token;
    stm_ = (token == "w") ? WHITE : BLACK;

    // Castling rights
    ss >> token;
    si_->castlingRights = 0;
    for (char c : token) {
        if (c == 'K') si_->castlingRights |= WHITE_OO;
        if (c == 'Q') si_->castlingRights |= WHITE_OOO;
        if (c == 'k') si_->castlingRights |= BLACK_OO;
        if (c == 'q') si_->castlingRights |= BLACK_OOO;
    }

    // En passant
    ss >> token;
    si_->epSquare = SQ_NONE;
    if (token != "-" && token.size() >= 2) {
        File f = File(token[0] - 'a');
        Rank r = Rank(token[1] - '1');
        si_->epSquare = makeSquare(f, r);
    }

    // Clocks
    ss >> si_->halfMoveClock;
    ss >> fullMove_;

    gamePly_ = (fullMove_ - 1) * 2 + (stm_ == BLACK ? 1 : 0);

    // Compute Zobrist key
    si_->key = 0;
    for (int s = 0; s < 64; s++)
        if (board_[s] != NO_PIECE)
            si_->key ^= Zobrist::pieceSquare[board_[s]][s];

    if (stm_ == BLACK)
        si_->key ^= Zobrist::sideToMove;

    si_->key ^= Zobrist::castling[si_->castlingRights];

    if (si_->epSquare != SQ_NONE)
        si_->key ^= Zobrist::enPassant[fileOf(si_->epSquare)];

    si_->pliesFromNull = 0;
    si_->previous = nullptr;
}

std::string Position::fen() const {
    std::ostringstream ss;

    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            Square s = makeSquare(File(f), Rank(r));
            if (board_[s] == NO_PIECE) { empty++; continue; }
            if (empty) { ss << empty; empty = 0; }
            Piece p = board_[s];
            int idx = (colorOf(p) == WHITE) ? typeOf(p) : typeOf(p) + 6;
            ss << "?PNBRQKpnbrqk"[idx];
        }
        if (empty) ss << empty;
        if (r > 0) ss << '/';
    }

    ss << (stm_ == WHITE ? " w " : " b ");

    std::string cr;
    if (si_->castlingRights & WHITE_OO)  cr += 'K';
    if (si_->castlingRights & WHITE_OOO) cr += 'Q';
    if (si_->castlingRights & BLACK_OO)  cr += 'k';
    if (si_->castlingRights & BLACK_OOO) cr += 'q';
    ss << (cr.empty() ? "-" : cr);

    if (si_->epSquare == SQ_NONE) ss << " -";
    else ss << " " << squareName(si_->epSquare);

    ss << " " << si_->halfMoveClock << " " << fullMove_;
    return ss.str();
}

// ============================================================
// Attack / check queries
// ============================================================

Bitboard Position::attackersTo(Square s, Bitboard occ) const {
    return (BB::PawnAttacks[BLACK][s]  & pieces(WHITE, PAWN))
         | (BB::PawnAttacks[WHITE][s]  & pieces(BLACK, PAWN))
         | (BB::KnightAttacks[s]       & byType_[KNIGHT])
         | (BB::bishopAttacks(s, occ)  & (byType_[BISHOP] | byType_[QUEEN]))
         | (BB::rookAttacks(s, occ)    & (byType_[ROOK]   | byType_[QUEEN]))
         | (BB::KingAttacks[s]         & byType_[KING]);
}

bool Position::isSquareAttacked(Square s, Color byColor) const {
    Bitboard occ  = pieces();
    Bitboard them = pieces(byColor);
    if (BB::KnightAttacks[s] & them & byType_[KNIGHT]) return true;
    if (BB::KingAttacks[s]   & them & byType_[KING])   return true;
    if (BB::PawnAttacks[~byColor][s] & them & byType_[PAWN]) return true;
    if (BB::bishopAttacks(s, occ) & them & (byType_[BISHOP] | byType_[QUEEN])) return true;
    if (BB::rookAttacks(s, occ)   & them & (byType_[ROOK]   | byType_[QUEEN])) return true;
    return false;
}

bool Position::isInCheck() const {
    return isSquareAttacked(kingSquare_[stm_], ~stm_);
}

Bitboard Position::checkersTo(Color c) const {
    Square ksq = kingSquare_[c];
    Color  them = ~c;
    Bitboard occ = pieces();
    return (BB::PawnAttacks[c][ksq]      & pieces(them, PAWN))
         | (BB::KnightAttacks[ksq]       & pieces(them, KNIGHT))
         | (BB::bishopAttacks(ksq, occ)  & (pieces(them, BISHOP) | pieces(them, QUEEN)))
         | (BB::rookAttacks(ksq, occ)    & (pieces(them, ROOK)   | pieces(them, QUEEN)));
}

// ============================================================
// Legal move check
// ============================================================

bool Position::isLegal(Move m) const {
    Color  us   = stm_;
    Color  them = ~us;
    Square from = m.from();
    Square to   = m.to();
    Square ksq  = kingSquare_[us];

    // ---- En passant: both pawns removed ----
    if (m.isEnPassant()) {
        Square cap   = makeSquare(fileOf(to), rankOf(from));
        Bitboard occ = (pieces() ^ squareBB(from) ^ squareBB(cap)) | squareBB(to);
        return !(BB::bishopAttacks(ksq, occ) & (pieces(them, BISHOP) | pieces(them, QUEEN)))
             & !(BB::rookAttacks(ksq, occ)   & (pieces(them, ROOK)   | pieces(them, QUEEN)));
    }

    // ---- King move: destination must not be attacked ----
    if (typeOf(board_[from]) == KING) {
        Bitboard occ = pieces() ^ squareBB(from); // remove king so sliders can't hide behind it
        return !(BB::KnightAttacks[to] & pieces(them, KNIGHT))
             & !(BB::PawnAttacks[us][to] & pieces(them, PAWN))
             & !(BB::KingAttacks[to] & pieces(them, KING))
             & !(BB::bishopAttacks(to, occ) & (pieces(them, BISHOP) | pieces(them, QUEEN)))
             & !(BB::rookAttacks(to, occ)   & (pieces(them, ROOK)   | pieces(them, QUEEN)));
    }

    // ---- Non-king moves ----

    // If in check, must either capture the checker or interpose on its ray
    Bitboard checkers = checkersTo(us);
    if (checkers) {
        // Double check: only king moves are legal (covered above already)
        if (checkers & (checkers - 1)) return false;
        // Single check: to-square must be checker or between king and checker
        Square checker_sq = Square(lsb(checkers));
        if (!(squareBB(to) & (squareBB(checker_sq) | BB::Between[ksq][checker_sq])))
            return false;
    }

    // Check for discovered checks / pins: would moving from→to expose king?
    // newOcc: remove 'from', add 'to'. Captured enemy piece stays in piece-type sets
    // but ~squareBB(to) excludes it from the attacker check.
    Bitboard newOcc = (pieces() ^ squareBB(from)) | squareBB(to);

    if (BB::bishopAttacks(ksq, newOcc) & (pieces(them, BISHOP) | pieces(them, QUEEN)) & ~squareBB(to))
        return false;
    if (BB::rookAttacks(ksq, newOcc)   & (pieces(them, ROOK)   | pieces(them, QUEEN)) & ~squareBB(to))
        return false;

    return true;
}

// ============================================================
// Make move
// ============================================================

void Position::makeMove(Move m, StateInfo& newSi) {
    newSi = *si_;
    newSi.previous = si_;
    si_ = &newSi;

    gamePly_++;
    si_->pliesFromNull++;

    Square from  = m.from();
    Square to    = m.to();
    Piece  moved = board_[from];   // always from board, not move.piece()
    Color  us    = stm_;
    Color  them  = ~us;

    bool isEP         = m.isEnPassant();
    bool isCastling   = m.isCastling();
    bool isDoublePush = m.isDoublePush();
    bool isPromotion  = m.isPromotion();


    // Get captured piece from board (not from move.captured(), which is 0 for TT moves)
    Piece captured;
    if (isEP) {
        Square capSq = makeSquare(fileOf(to), rankOf(from));
        captured = board_[capSq];
    } else {
        captured = board_[to];
    }

    si_->capturedPiece = captured;
    si_->epCapture     = isEP;
    si_->halfMoveClock++;

    // Update castling rights in key (remove old contribution)
    si_->key ^= Zobrist::castling[si_->castlingRights];

    // Remove en passant key
    if (si_->epSquare != SQ_NONE) {
        si_->key ^= Zobrist::enPassant[fileOf(si_->epSquare)];
        si_->epSquare = SQ_NONE;
    }

    // Handle captures
    if (isEP) {
        Square capSq = makeSquare(fileOf(to), rankOf(from));
        si_->key ^= Zobrist::pieceSquare[board_[capSq]][capSq];
        removePiece(capSq);
        si_->halfMoveClock = 0;
    } else if (captured != NO_PIECE) {
        si_->castlingRights &= CastlingRightsMask[to];
        si_->key ^= Zobrist::pieceSquare[captured][to];
        removePiece(to);
        si_->halfMoveClock = 0;
    }

    // Handle castling (move the rook)
    if (isCastling) {
        bool kingside = fileOf(to) > fileOf(from);
        int cr_idx = int(us) * 2 + (kingside ? 0 : 1);
        Square rookFrom = CastlingRookFrom[cr_idx];
        Square rookTo   = CastlingRookTo[cr_idx];
        Piece rook = board_[rookFrom];
        si_->key ^= Zobrist::pieceSquare[rook][rookFrom];
        si_->key ^= Zobrist::pieceSquare[rook][rookTo];
        movePiece(rookFrom, rookTo);
    }

    // Move the piece (or promote)
    si_->key ^= Zobrist::pieceSquare[moved][from];
    if (isPromotion) {
        Piece promoPiece = makePiece(us, m.promotion());
        si_->key ^= Zobrist::pieceSquare[promoPiece][to];
        removePiece(from);
        putPiece(promoPiece, to);
    } else {
        si_->key ^= Zobrist::pieceSquare[moved][to];
        movePiece(from, to);
    }

    // Pawn-specific updates
    if (typeOf(moved) == PAWN) {
        si_->halfMoveClock = 0;
        if (isDoublePush) {
            Rank epRank = Rank(int(rankOf(from)) + (us == WHITE ? 1 : -1));
            si_->epSquare = makeSquare(fileOf(from), epRank);
            si_->key ^= Zobrist::enPassant[fileOf(si_->epSquare)];
        }
    }

    // Update castling rights for king/rook moves
    si_->castlingRights &= CastlingRightsMask[from];
    si_->key ^= Zobrist::castling[si_->castlingRights];

    // Flip side
    stm_ = them;
    si_->key ^= Zobrist::sideToMove;

    if (stm_ == WHITE) fullMove_++;
}

void Position::unmakeMove(Move m) {
    stm_ = ~stm_;
    if (stm_ == BLACK) fullMove_--;

    Square from     = m.from();
    Square to       = m.to();
    Piece  captured = si_->capturedPiece;
    bool   wasEP    = si_->epCapture;
    Color  us       = stm_;

    bool isCastling = m.isCastling();

    if (m.isPromotion()) {
        removePiece(to);
        putPiece(makePiece(us, PAWN), from);
    } else {
        movePiece(to, from);
    }

    if (wasEP) {
        Square capSq = makeSquare(fileOf(to), rankOf(from));
        putPiece(makePiece(~us, PAWN), capSq);
    } else if (captured != NO_PIECE) {
        putPiece(captured, to);
    }

    if (isCastling) {
        bool kingside = fileOf(to) > fileOf(from);
        int cr_idx = int(us) * 2 + (kingside ? 0 : 1);
        movePiece(CastlingRookTo[cr_idx], CastlingRookFrom[cr_idx]);
    }

    gamePly_--;
    si_ = si_->previous;
}

void Position::makeNullMove(StateInfo& newSi) {
    newSi = *si_;
    newSi.previous = si_;
    si_ = &newSi;
    gamePly_++;

    if (si_->epSquare != SQ_NONE) {
        si_->key ^= Zobrist::enPassant[fileOf(si_->epSquare)];
        si_->epSquare = SQ_NONE;
    }

    si_->key ^= Zobrist::sideToMove;
    si_->pliesFromNull = 0;
    si_->capturedPiece = NO_PIECE;
    si_->halfMoveClock++;

    stm_ = ~stm_;
}

void Position::unmakeNullMove() {
    si_ = si_->previous;
    stm_ = ~stm_;
    gamePly_--;
}

// ============================================================
// SEE
// ============================================================

int Position::see(Move m) const {
    Square from = m.from(), to = m.to();
    Bitboard occ = pieces();
    int gain[32];
    int d = 0;
    Color stm = stm_;

    gain[d] = (m.captured() != NO_PIECE) ? PieceValue[typeOf(m.captured())] : 0;
    if (m.isPromotion())
        gain[d] += PieceValue[m.promotion()] - PieceValue[PAWN];

    occ ^= squareBB(from);
    Bitboard attackers = attackersTo(to, occ);

    stm = ~stm;
    PieceType lastPt = typeOf(m.piece());

    while (true) {
        Bitboard stmAttackers = attackers & pieces(stm);
        if (!stmAttackers) break;
        d++;
        gain[d] = PieceValue[lastPt] - gain[d-1];

        PieceType pt;
        Bitboard subset;
        for (pt = PAWN; pt <= KING; pt = PieceType(pt + 1)) {
            subset = stmAttackers & byType_[pt];
            if (subset) break;
        }
        occ ^= squareBB(lsb(subset));
        attackers |= (BB::bishopAttacks(to, occ) & (byType_[BISHOP] | byType_[QUEEN]))
                   | (BB::rookAttacks(to, occ)   & (byType_[ROOK]   | byType_[QUEEN]));
        attackers &= occ;

        lastPt = pt;
        stm = ~stm;

        if (gain[d] < 0 && gain[d-1] > 0) {
            if (-gain[d] > gain[d-1]) gain[d-1] = -gain[d];
            break;
        }
    }

    while (--d >= 0)
        gain[d] = -std::max(-gain[d], gain[d+1]);

    return gain[0];
}

bool Position::seeGe(Move m, int threshold) const {
    Square from = m.from(), to = m.to();

    int value = (m.captured() != NO_PIECE) ? PieceValue[typeOf(m.captured())] : 0;
    if (m.isPromotion()) value += PieceValue[m.promotion()] - PieceValue[PAWN];

    value -= threshold;
    if (value < 0) return false;

    value -= PieceValue[typeOf(m.piece())];
    if (value >= 0) return true;

    Bitboard occ = pieces() ^ squareBB(from);
    Bitboard attackers = attackersTo(to, occ);
    Color stm = ~stm_;

    while (true) {
        Bitboard stmAttackers = attackers & pieces(stm);
        if (!stmAttackers) break;

        PieceType pt;
        for (pt = PAWN; pt <= KING; pt = PieceType(pt+1))
            if (stmAttackers & byType_[pt]) break;

        occ ^= squareBB(lsb(stmAttackers & byType_[pt]));
        attackers |= (BB::bishopAttacks(to, occ) & (byType_[BISHOP] | byType_[QUEEN]))
                   | (BB::rookAttacks(to, occ)   & (byType_[ROOK]   | byType_[QUEEN]));
        attackers &= occ;

        stm = ~stm;
        value = -value - 1 - PieceValue[pt];
        if (value >= 0) {
            if (pt == KING && (attackers & pieces(stm))) stm = ~stm;
            break;
        }
    }

    return stm_ != stm;
}

// ============================================================
// Non-pawn material
// ============================================================

int Position::nonPawnMaterial(Color c) const {
    return count(c, KNIGHT) * PieceValue[KNIGHT]
         + count(c, BISHOP) * PieceValue[BISHOP]
         + count(c, ROOK)   * PieceValue[ROOK]
         + count(c, QUEEN)  * PieceValue[QUEEN];
}

// ============================================================
// Draw detection
// ============================================================

bool Position::isRepetition(int count) const {
    int reps = 0;
    const StateInfo* cur = si_->previous;
    int limit = si_->pliesFromNull;
    for (int i = 2; i <= limit && cur; i += 2) {
        cur = cur->previous;
        if (!cur) break;
        cur = cur->previous;
        if (!cur) break;
        if (cur->key == si_->key) {
            if (++reps >= count - 1) return true;
        }
    }
    return false;
}

bool Position::isDraw() const {
    if (si_->halfMoveClock >= 100) return true;
    return isRepetition(2);
}

// ============================================================
// Print board
// ============================================================

void Position::print() const {
    const char* pieceChars = ".PNBRQKpnbrqk";
    std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; r--) {
        std::cout << (r + 1) << " |";
        for (int f = 0; f < 8; f++) {
            Piece p = board_[makeSquare(File(f), Rank(r))];
            int idx = (p == NO_PIECE) ? 0
                    : (colorOf(p) == WHITE ? typeOf(p) : typeOf(p) + 6);
            std::cout << " " << pieceChars[idx] << " |";
        }
        std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    }
    std::cout << "    a   b   c   d   e   f   g   h\n\n";
    std::cout << "FEN: " << fen() << "\n";
    std::cout << "Key: " << std::hex << si_->key << std::dec << "\n\n";
}

// ============================================================
// computeKey — recomputes Zobrist key from scratch for validation
// ============================================================

Key Position::computeKey() const {
    Key k = 0;
    for (int s = 0; s < 64; s++) {
        Piece p = board_[s];
        if (p != NO_PIECE) k ^= Zobrist::pieceSquare[p][s];
    }
    k ^= Zobrist::castling[si_->castlingRights];
    if (si_->epSquare != SQ_NONE) k ^= Zobrist::enPassant[fileOf(si_->epSquare)];
    if (stm_ == BLACK) k ^= Zobrist::sideToMove;
    return k;
}

// ============================================================
// isPseudoLegal
// ============================================================

bool Position::isPseudoLegal(Move m) const {
    if (m.isNone() || m.isNull()) return false;
    Square from = m.from(), to = m.to();
    Piece p = board_[from];
    if (p == NO_PIECE || colorOf(p) != stm_) return false;
    if (pieces(stm_) & squareBB(to)) return false;

    PieceType pt = typeOf(p);

    // Validate special flags against board state to catch TT hash collisions
    if (m.isEnPassant()) {
        return pt == PAWN
            && si_->epSquare != SQ_NONE
            && to == si_->epSquare;
    }
    if (m.isCastling()) {
        if (pt != KING) return false;
        bool kingside = fileOf(to) > fileOf(from);
        int cr_idx = int(stm_) * 2 + (kingside ? 0 : 1);
        CastlingRights cr = CastlingRights(1 << cr_idx);
        return canCastle(cr)
            && board_[CastlingRookFrom[cr_idx]] == makePiece(stm_, ROOK)
            && !(pieces() & CastlingPath[cr_idx]);
    }
    if (m.isDoublePush()) {
        if (pt != PAWN) return false;
        Rank startRank = (stm_ == WHITE) ? RANK_2 : RANK_7;
        if (rankOf(from) != startRank) return false;
        Square mid = (stm_ == WHITE) ? Square(from + 8) : Square(from - 8);
        return board_[mid] == NO_PIECE && board_[to] == NO_PIECE;
    }

    // Validate piece movement geometry — catches TT moves with wrong piece type
    int fd = int(fileOf(to)) - int(fileOf(from));
    int rd = int(rankOf(to)) - int(rankOf(from));
    int afd = fd < 0 ? -fd : fd;
    int ard = rd < 0 ? -rd : rd;

    switch (pt) {
    case PAWN: {
        int forward = (stm_ == WHITE) ? rd : -rd;
        if (forward <= 0 || forward > 2 || afd > 1) return false;
        if (afd == 1 && board_[to] == NO_PIECE) return false; // diagonal requires capture
        if (afd == 0 && board_[to] != NO_PIECE) return false; // push requires empty square
        break;
    }
    case KNIGHT:
        if (!((afd == 1 && ard == 2) || (afd == 2 && ard == 1))) return false;
        break;
    case BISHOP:
        if (afd != ard) return false;
        break;
    case ROOK:
        if (afd != 0 && ard != 0) return false;
        break;
    case QUEEN:
        if (afd != 0 && ard != 0 && afd != ard) return false;
        break;
    case KING:
        if (afd > 1 || ard > 1) return false;
        break;
    default: break;
    }

    return true;
}
