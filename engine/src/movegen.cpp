#include "movegen.h"
#include "history.h"
#include <algorithm>

// ============================================================
// Internal generation helpers
// ============================================================

static void addPawnMoves(const Position& pos, MoveList& list, bool capturesOnly) {
    Color us   = pos.sideToMove();
    Color them = ~us;

    Bitboard pawns = pos.pieces(us, PAWN);
    Bitboard occ   = pos.pieces();
    Bitboard enemy = pos.pieces(them);

    int  push_dir    = (us == WHITE) ? NORTH  : SOUTH;

    // ---- Promotions (must generate even in captures-only) ----
    Bitboard promo_pawns = pawns & RankBB[us == WHITE ? RANK_7 : RANK_2];
    Bitboard other_pawns = pawns & ~promo_pawns;

    // Promotion pushes
    if (!capturesOnly) {
        Bitboard push1 = (us == WHITE ? shift<NORTH>(promo_pawns) : shift<SOUTH>(promo_pawns)) & ~occ;
        while (push1) {
            Square to = popLSB(push1);
            Square from = Square(to - push_dir);
            for (PieceType pt : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                list.add(makeMove(from, to, makePiece(us, PAWN), NO_PIECE, pt,
                                  MF_PROMOTION));
            }
        }
    }

    // Promotion captures (left and right)
    {
        Bitboard att_l = (us == WHITE ? shift<NORTH_WEST>(promo_pawns) : shift<SOUTH_WEST>(promo_pawns)) & enemy;
        Bitboard att_r = (us == WHITE ? shift<NORTH_EAST>(promo_pawns) : shift<SOUTH_EAST>(promo_pawns)) & enemy;
        int dir_l = (us == WHITE) ? NORTH_WEST : SOUTH_WEST;
        int dir_r = (us == WHITE) ? NORTH_EAST : SOUTH_EAST;
        while (att_l) {
            Square to = popLSB(att_l);
            Square from = Square(int(to) - dir_l);
            Piece cap = pos.pieceOn(to);
            for (PieceType pt : {QUEEN, ROOK, BISHOP, KNIGHT})
                list.add(makeMove(from, to, makePiece(us, PAWN), cap, pt, MF_PROMOTION));
        }
        while (att_r) {
            Square to = popLSB(att_r);
            Square from = Square(int(to) - dir_r);
            Piece cap = pos.pieceOn(to);
            for (PieceType pt : {QUEEN, ROOK, BISHOP, KNIGHT})
                list.add(makeMove(from, to, makePiece(us, PAWN), cap, pt, MF_PROMOTION));
        }
    }

    // ---- Non-promotion captures ----
    {
        int dir_l = (us == WHITE) ? NORTH_WEST : SOUTH_WEST;
        int dir_r = (us == WHITE) ? NORTH_EAST : SOUTH_EAST;
        Bitboard att_l = (us == WHITE ? shift<NORTH_WEST>(other_pawns) : shift<SOUTH_WEST>(other_pawns)) & enemy;
        Bitboard att_r = (us == WHITE ? shift<NORTH_EAST>(other_pawns) : shift<SOUTH_EAST>(other_pawns)) & enemy;
        while (att_l) {
            Square to = popLSB(att_l);
            Square from = Square(int(to) - dir_l);
            list.add(makeMove(from, to, makePiece(us, PAWN), pos.pieceOn(to)));
        }
        while (att_r) {
            Square to = popLSB(att_r);
            Square from = Square(int(to) - dir_r);
            list.add(makeMove(from, to, makePiece(us, PAWN), pos.pieceOn(to)));
        }
    }

    // ---- En passant ----
    if (pos.epSquare() != SQ_NONE) {
        Square ep = pos.epSquare();
        Bitboard ep_pawns = other_pawns & BB::PawnAttacks[them][ep];
        while (ep_pawns) {
            Square from = popLSB(ep_pawns);
            list.add(makeMove(from, ep, makePiece(us, PAWN),
                              makePiece(them, PAWN), NO_PIECE_TYPE, MF_EN_PASSANT));
        }
    }

    if (capturesOnly) return;

    // ---- Quiet pawn moves ----
    Bitboard push1 = (us == WHITE ? shift<NORTH>(other_pawns) : shift<SOUTH>(other_pawns)) & ~occ;
    Bitboard push2 = (us == WHITE ? shift<NORTH>(push1 & Rank3BB) : shift<SOUTH>(push1 & Rank6BB)) & ~occ;

    while (push1) {
        Square to = popLSB(push1);
        Square from = Square(to - push_dir);
        list.add(makeMove(from, to, makePiece(us, PAWN)));
    }
    while (push2) {
        Square to = popLSB(push2);
        Square from = Square(to - push_dir * 2);
        list.add(makeMove(from, to, makePiece(us, PAWN), NO_PIECE, NO_PIECE_TYPE, MF_DOUBLE_PUSH));
    }
}

static void addPieceMoves(const Position& pos, MoveList& list, PieceType pt, bool capturesOnly) {
    Color us    = pos.sideToMove();
    Bitboard bb = pos.pieces(us, pt);
    Bitboard occ = pos.pieces();
    Bitboard own = pos.pieces(us);

    while (bb) {
        Square from = popLSB(bb);
        Bitboard atk = BB::attacks(pt, from, occ) & ~own;
        if (capturesOnly) atk &= pos.pieces(~us);

        while (atk) {
            Square to = popLSB(atk);
            Piece cap = pos.pieceOn(to);
            list.add(makeMove(from, to, makePiece(us, pt), cap));
        }
    }
}

static void addKingMoves(const Position& pos, MoveList& list, bool capturesOnly) {
    Color  us   = pos.sideToMove();
    Square from = pos.kingSquare(us);
    Bitboard occ = pos.pieces();
    Bitboard atk = BB::KingAttacks[from] & ~pos.pieces(us);
    if (capturesOnly) atk &= pos.pieces(~us);

    while (atk) {
        Square to = popLSB(atk);
        Piece cap = pos.pieceOn(to);
        list.add(makeMove(from, to, makePiece(us, KING), cap));
    }

    if (!capturesOnly && !pos.isInCheck()) {
        // Castling
        const CastlingRights cr_oo  = (us == WHITE) ? WHITE_OO  : BLACK_OO;
        const CastlingRights cr_ooo = (us == WHITE) ? WHITE_OOO : BLACK_OOO;
        int cr_idx_oo  = us * 2 + 0;
        int cr_idx_ooo = us * 2 + 1;

        if (pos.canCastle(cr_oo) && !(CastlingPath[cr_idx_oo] & occ)) {
            // Check king doesn't pass through/land on attacked square
            bool attacked = false;
            Bitboard path = CastlingKingPath[cr_idx_oo];
            while (path) {
                Square s = popLSB(path);
                if (pos.isSquareAttacked(s, ~us)) { attacked = true; break; }
            }
            if (!attacked) {
                Square to = CastlingKingTo[cr_idx_oo];
                list.add(makeMove(from, to, makePiece(us, KING), NO_PIECE, NO_PIECE_TYPE, MF_CASTLING));
            }
        }
        if (pos.canCastle(cr_ooo) && !(CastlingPath[cr_idx_ooo] & occ)) {
            bool attacked = false;
            Bitboard path = CastlingKingPath[cr_idx_ooo];
            while (path) {
                Square s = popLSB(path);
                if (pos.isSquareAttacked(s, ~us)) { attacked = true; break; }
            }
            if (!attacked) {
                Square to = CastlingKingTo[cr_idx_ooo];
                list.add(makeMove(from, to, makePiece(us, KING), NO_PIECE, NO_PIECE_TYPE, MF_CASTLING));
            }
        }
    }
}

// ============================================================
// Public generation functions
// ============================================================

void generateAllMoves(const Position& pos, MoveList& list) {
    addPawnMoves(pos, list, false);
    addPieceMoves(pos, list, KNIGHT, false);
    addPieceMoves(pos, list, BISHOP, false);
    addPieceMoves(pos, list, ROOK,   false);
    addPieceMoves(pos, list, QUEEN,  false);
    addKingMoves(pos, list, false);
}

void generateCaptures(const Position& pos, MoveList& list) {
    addPawnMoves(pos, list, true);
    addPieceMoves(pos, list, KNIGHT, true);
    addPieceMoves(pos, list, BISHOP, true);
    addPieceMoves(pos, list, ROOK,   true);
    addPieceMoves(pos, list, QUEEN,  true);
    addKingMoves(pos, list, true);
}

void generateQuiets(const Position& pos, MoveList& list) {
    addPawnMoves(pos, list, false);
    // Then keep only quiet moves by not generating captures...
    // Simpler: just generate all and filter. For staged gen we use MovePicker.
    addPieceMoves(pos, list, KNIGHT, false);
    addPieceMoves(pos, list, BISHOP, false);
    addPieceMoves(pos, list, ROOK,   false);
    addPieceMoves(pos, list, QUEEN,  false);
    addKingMoves(pos, list, false);
}

void generateEvasions(const Position& pos, MoveList& list) {
    generateAllMoves(pos, list);
}

int countLegalMoves(Position& pos) {
    MoveList list;
    generateAllMoves(pos, list);
    int legal = 0;
    StateInfo si;
    for (int i = 0; i < list.size; i++) {
        if (pos.isLegal(list.moves[i])) {
            pos.makeMove(list.moves[i], si);
            legal++;
            pos.unmakeMove(list.moves[i]);
        }
    }
    return legal;
}

// ============================================================
// MovePicker implementation
// ============================================================

#include "history.h"

MovePicker::MovePicker(const Position& pos, Move ttMove, Move killer1, Move killer2,
                       Move counter, const History& history, int depth)
    : pos_(pos), history_(&history), ttMove_(ttMove),
      killer1_(killer1), killer2_(killer2), counter_(counter), depth_(depth)
{
    inCheck_ = pos.isInCheck();
    if (inCheck_) {
        stage_ = STAGE_EVASION_TT;
    } else {
        stage_ = STAGE_TT;
    }
}

MovePicker::MovePicker(const Position& pos, Move ttMove, int qs_depth)
    : pos_(pos), history_(nullptr), ttMove_(ttMove),
      killer1_(Move::none()), killer2_(Move::none()), counter_(Move::none()),
      depth_(qs_depth)
{
    inCheck_ = pos.isInCheck();
    stage_ = inCheck_ ? STAGE_EVASION_TT : STAGE_QS_TT;
}

int MovePicker::mvvLva(Move m) const {
    PieceType atk = typeOf(m.piece());
    PieceType vic = typeOf(m.captured());
    if (vic == NO_PIECE_TYPE) return 0;
    return MVV_LVA[vic][atk - 1];
}

void MovePicker::scoreCaptures() {
    for (int i = 0; i < captures_.size; i++) {
        Move m = captures_.moves[i];
        int score = mvvLva(m);
        if (pos_.seeGe(m, 0))
            score += 10000;
        // Add capture history bonus for fine-grained ordering within SEE tiers
        if (history_) {
            Piece piece = pos_.pieceOn(m.from());
            PieceType captured = typeOf(m.captured());
            score += history_->capture.get(piece, m.to(), captured) / 16;
        }
        captures_.scores[i] = score;
    }
}

void MovePicker::scoreQuiets() {
    if (!history_) return;
    Color us = pos_.sideToMove();
    for (int i = 0; i < quiets_.size; i++) {
        Move m = quiets_.moves[i];
        int score = history_->getHistory(us, m.from(), m.to());
        // Add continuation history
        score += history_->cont1.get(m.piece(), m.to());
        score += history_->cont2.get(m.piece(), m.to());
        quiets_.scores[i] = score;
    }
}

Move MovePicker::next(bool skipQuiets) {
    while (true) {
        switch (stage_) {

        case STAGE_TT:
            stage_ = STAGE_INIT_CAPTURES;
            if (!ttMove_.isNone() && pos_.isPseudoLegal(ttMove_))
                return ttMove_;
            break;

        case STAGE_INIT_CAPTURES:
            generateCaptures(pos_, captures_);
            scoreCaptures();
            captureIdx_ = 0;
            stage_ = STAGE_GOOD_CAPTURES;
            break;

        case STAGE_GOOD_CAPTURES:
            while (captureIdx_ < captures_.size) {
                Move m = captures_.pick(captureIdx_++);
                if (m == ttMove_) continue;
                if (pos_.seeGe(m, 0)) return m;
                // Bad capture — defer
                badCaptures_.add(m, captures_.scores[captureIdx_-1]);
            }
            stage_ = STAGE_KILLER1;
            break;

        case STAGE_KILLER1:
            stage_ = STAGE_KILLER2;
            if (!killer1_.isNone() && killer1_ != ttMove_
                && pos_.isPseudoLegal(killer1_) && !pos_.isCapture(killer1_))
                return killer1_;
            break;

        case STAGE_KILLER2:
            stage_ = STAGE_COUNTER;
            if (!killer2_.isNone() && killer2_ != ttMove_ && killer2_ != killer1_
                && pos_.isPseudoLegal(killer2_) && !pos_.isCapture(killer2_))
                return killer2_;
            break;

        case STAGE_COUNTER:
            stage_ = STAGE_INIT_QUIETS;
            if (!counter_.isNone() && counter_ != ttMove_
                && counter_ != killer1_ && counter_ != killer2_
                && pos_.isPseudoLegal(counter_) && !pos_.isCapture(counter_))
                return counter_;
            break;

        case STAGE_INIT_QUIETS:
            if (!skipQuiets) {
                generateQuiets(pos_, quiets_);
                scoreQuiets();
            }
            quietIdx_ = 0;
            stage_ = STAGE_QUIETS;
            break;

        case STAGE_QUIETS:
            if (!skipQuiets) {
                while (quietIdx_ < quiets_.size) {
                    Move m = quiets_.pick(quietIdx_++);
                    if (m == ttMove_ || m == killer1_ || m == killer2_ || m == counter_)
                        continue;
                    if (!pos_.isCapture(m)) return m;
                }
            }
            stage_ = STAGE_BAD_CAPTURES;
            badCapIdx_ = 0;
            break;

        case STAGE_BAD_CAPTURES:
            while (badCapIdx_ < badCaptures_.size) {
                Move m = badCaptures_.pick(badCapIdx_++);
                if (m == ttMove_) continue;
                return m;
            }
            stage_ = STAGE_DONE;
            break;

        case STAGE_DONE:
            return Move::none();

        // ---- Quiescence ----
        case STAGE_QS_TT:
            stage_ = STAGE_QS_INIT_CAPTURES;
            if (!ttMove_.isNone() && pos_.isPseudoLegal(ttMove_))
                return ttMove_;
            break;

        case STAGE_QS_INIT_CAPTURES:
            generateCaptures(pos_, captures_);
            scoreCaptures();
            captureIdx_ = 0;
            stage_ = STAGE_QS_CAPTURES;
            break;

        case STAGE_QS_CAPTURES:
            while (captureIdx_ < captures_.size) {
                Move m = captures_.pick(captureIdx_++);
                if (m == ttMove_) continue;
                return m;
            }
            stage_ = STAGE_QS_DONE;
            break;

        case STAGE_QS_DONE:
            return Move::none();

        // ---- Evasions ----
        case STAGE_EVASION_TT:
            stage_ = STAGE_EVASION_INIT;
            if (!ttMove_.isNone() && pos_.isPseudoLegal(ttMove_))
                return ttMove_;
            break;

        case STAGE_EVASION_INIT:
            generateEvasions(pos_, captures_);
            // Score: captures by MVV-LVA, quiets by history
            for (int i = 0; i < captures_.size; i++) {
                Move m = captures_.moves[i];
                if (pos_.isCapture(m))
                    captures_.scores[i] = mvvLva(m) + 1000000;
                else if (history_)
                    captures_.scores[i] = history_->getHistory(pos_.sideToMove(), m.from(), m.to());
                else
                    captures_.scores[i] = 0;
            }
            captureIdx_ = 0;
            stage_ = STAGE_EVASIONS;
            break;

        case STAGE_EVASIONS:
            while (captureIdx_ < captures_.size) {
                Move m = captures_.pick(captureIdx_++);
                if (m == ttMove_) continue;
                return m;
            }
            stage_ = STAGE_EVASION_DONE;
            break;

        case STAGE_EVASION_DONE:
            return Move::none();

        default:
            return Move::none();
        }
    }
}

