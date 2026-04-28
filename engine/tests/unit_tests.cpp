#include "../src/bitboard.h"
#include "../src/position.h"
#include "../src/movegen.h"
#include "../src/evaluate.h"
#include "../src/tt.h"
#include <iostream>
#include <cassert>
#include <string>

static int passed = 0, failed = 0;

#define TEST(name) \
    do { \
        bool _result = test_##name(); \
        if (_result) { std::cout << "PASS: " #name "\n"; passed++; } \
        else         { std::cout << "FAIL: " #name "\n"; failed++; } \
    } while(0)

#define ASSERT(cond) do { if (!(cond)) { std::cout << "  assertion failed: " #cond "\n"; return false; } } while(0)

// ============================================================
// Bitboard tests
// ============================================================

bool test_lsb() {
    Bitboard b = 0x10;
    ASSERT(lsb(b) == 4);
    b = 1ULL << 63;
    ASSERT(lsb(b) == 63);
    return true;
}

bool test_popcount() {
    ASSERT(popcount(0) == 0);
    ASSERT(popcount(0xFF) == 8);
    ASSERT(popcount(~0ULL) == 64);
    return true;
}

bool test_knight_attacks() {
    // Knight on e4 (sq 28)
    Square sq = SQ_E4;
    Bitboard atk = BB::KnightAttacks[sq];
    ASSERT(atk & squareBB(SQ_D2));
    ASSERT(atk & squareBB(SQ_F2));
    ASSERT(atk & squareBB(SQ_C3));
    ASSERT(atk & squareBB(SQ_G3));
    ASSERT(atk & squareBB(SQ_C5));
    ASSERT(atk & squareBB(SQ_G5));
    ASSERT(atk & squareBB(SQ_D6));
    ASSERT(atk & squareBB(SQ_F6));
    ASSERT(popcount(atk) == 8);
    return true;
}

bool test_knight_corner() {
    // Knight on a1 — only 2 moves
    Bitboard atk = BB::KnightAttacks[SQ_A1];
    ASSERT(popcount(atk) == 2);
    ASSERT(atk & squareBB(SQ_B3));
    ASSERT(atk & squareBB(SQ_C2));
    return true;
}

bool test_rook_attacks() {
    // Rook on e4, no blockers — should see 14 squares
    Bitboard atk = BB::rookAttacks(SQ_E4, 0);
    ASSERT(popcount(atk) == 14);
    ASSERT(atk & squareBB(SQ_E1));
    ASSERT(atk & squareBB(SQ_E8));
    ASSERT(atk & squareBB(SQ_A4));
    ASSERT(atk & squareBB(SQ_H4));
    return true;
}

bool test_rook_blocked() {
    // Rook on e4, blocker on e6 — should not see e7/e8
    Bitboard occ = squareBB(SQ_E6);
    Bitboard atk = BB::rookAttacks(SQ_E4, occ);
    ASSERT(atk & squareBB(SQ_E6)); // sees blocker
    ASSERT(!(atk & squareBB(SQ_E7)));
    ASSERT(!(atk & squareBB(SQ_E8)));
    return true;
}

bool test_bishop_attacks() {
    Bitboard atk = BB::bishopAttacks(SQ_E4, 0);
    ASSERT(atk & squareBB(SQ_D3));
    ASSERT(atk & squareBB(SQ_F5));
    ASSERT(atk & squareBB(SQ_H7));
    ASSERT(atk & squareBB(SQ_B1));
    return true;
}

bool test_pawn_attacks() {
    // White pawn on e2 attacks d3 and f3
    Bitboard atk = BB::PawnAttacks[WHITE][SQ_E2];
    ASSERT(atk & squareBB(SQ_D3));
    ASSERT(atk & squareBB(SQ_F3));
    ASSERT(popcount(atk) == 2);

    // Black pawn on e7 attacks d6 and f6
    atk = BB::PawnAttacks[BLACK][SQ_E7];
    ASSERT(atk & squareBB(SQ_D6));
    ASSERT(atk & squareBB(SQ_F6));
    return true;
}

bool test_between() {
    // Squares between e1 and e5 should be e2, e3, e4
    Bitboard b = BB::Between[SQ_E1][SQ_E5];
    ASSERT(b & squareBB(SQ_E2));
    ASSERT(b & squareBB(SQ_E3));
    ASSERT(b & squareBB(SQ_E4));
    ASSERT(!(b & squareBB(SQ_E5)));
    ASSERT(!(b & squareBB(SQ_E1)));
    ASSERT(popcount(b) == 3);

    // Diagonal: a1 to d4
    b = BB::Between[SQ_A1][SQ_D4];
    ASSERT(b & squareBB(SQ_B2));
    ASSERT(b & squareBB(SQ_C3));
    ASSERT(popcount(b) == 2);

    // Non-ray squares: no between
    b = BB::Between[SQ_A1][SQ_B3];
    ASSERT(b == 0);
    return true;
}

// ============================================================
// Position tests
// ============================================================

bool test_fen_startpos() {
    Position pos;
    StateInfo si;
    pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", si);

    ASSERT(pos.count(WHITE, PAWN)   == 8);
    ASSERT(pos.count(WHITE, KNIGHT) == 2);
    ASSERT(pos.count(WHITE, BISHOP) == 2);
    ASSERT(pos.count(WHITE, ROOK)   == 2);
    ASSERT(pos.count(WHITE, QUEEN)  == 1);
    ASSERT(pos.count(WHITE, KING)   == 1);
    ASSERT(pos.count(BLACK, PAWN)   == 8);
    ASSERT(pos.sideToMove() == WHITE);
    ASSERT(pos.canCastle(WHITE_OO));
    ASSERT(pos.canCastle(WHITE_OOO));
    ASSERT(pos.canCastle(BLACK_OO));
    ASSERT(pos.canCastle(BLACK_OOO));
    ASSERT(pos.epSquare() == SQ_NONE);
    ASSERT(pos.halfMoveClock() == 0);
    ASSERT(pos.fullMoveNumber() == 1);
    return true;
}

bool test_fen_roundtrip() {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    Position pos;
    StateInfo si;
    pos.setFen(fen, si);
    // FEN roundtrip
    std::string out = pos.fen();
    Position pos2;
    StateInfo si2;
    pos2.setFen(out, si2);
    ASSERT(pos2.key() == pos.key());
    return true;
}

bool test_make_unmake() {
    Position pos;
    StateInfo states[16];
    pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", states[0]);

    Key key0 = pos.key();

    // Make e2e4
    Move e2e4 = makeMove(SQ_E2, SQ_E4, W_PAWN, NO_PIECE, NO_PIECE_TYPE, MF_DOUBLE_PUSH);
    pos.makeMove(e2e4, states[1]);

    ASSERT(pos.sideToMove() == BLACK);
    ASSERT(pos.epSquare() == SQ_E3);
    ASSERT(pos.pieceOn(SQ_E4) == W_PAWN);
    ASSERT(pos.pieceOn(SQ_E2) == NO_PIECE);

    pos.unmakeMove(e2e4);

    ASSERT(pos.sideToMove() == WHITE);
    ASSERT(pos.epSquare() == SQ_NONE);
    ASSERT(pos.pieceOn(SQ_E2) == W_PAWN);
    ASSERT(pos.key() == key0);
    return true;
}

bool test_in_check() {
    Position pos;
    StateInfo si;

    // Starting position: not in check
    pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", si);
    ASSERT(!pos.isInCheck());

    // White king on e1, black rook on e8, all clear on e-file: check via rook
    pos.setFen("4r3/8/8/8/8/8/8/4K3 w - - 0 1", si);
    ASSERT(pos.isInCheck());

    // Black queen on e2 checks white king on e1 directly
    pos.setFen("4k3/8/8/8/8/8/4q3/4K3 w - - 0 1", si);
    ASSERT(pos.isInCheck());

    // Not in check: f2 pawn blocks queen h4→e1 diagonal
    pos.setFen("rnb1kbnr/pppp1ppp/8/4p3/2B1P2q/8/PPPP1PPP/RNBQK1NR w KQkq - 3 3", si);
    ASSERT(!pos.isInCheck());

    return true;
}

// ============================================================
// Move generation tests
// ============================================================

bool test_movegen_startpos() {
    Position pos;
    StateInfo si;
    pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", si);

    MoveList list;
    generateAllMoves(pos, list);

    int legal = 0;
    for (int i = 0; i < list.size; i++)
        if (pos.isLegal(list.moves[i])) legal++;

    ASSERT(legal == 20);
    return true;
}

bool test_movegen_kiwipete() {
    Position pos;
    StateInfo si;
    pos.setFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", si);

    MoveList list;
    generateAllMoves(pos, list);

    int legal = 0;
    for (int i = 0; i < list.size; i++)
        if (pos.isLegal(list.moves[i])) legal++;

    ASSERT(legal == 48);
    return true;
}

// ============================================================
// SEE tests
// ============================================================

bool test_see_basic() {
    Position pos;
    StateInfo si;
    // Rook captures pawn defended by rook — net loss
    pos.setFen("1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", si);
    // Re1 x e5 — pawn captured, then rook recaptures, then nothing
    Move m = makeMove(SQ_E1, SQ_E5, W_ROOK, B_PAWN);
    int see_val = pos.see(m);
    ASSERT(see_val > 0); // rook captures pawn: net positive after no recapture available
    return true;
}

// ============================================================
// Evaluation tests
// ============================================================

bool test_eval_symmetric() {
    // Evaluation of symmetric positions should be near zero
    Position pos;
    StateInfo si;
    pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", si);
    int eval = evaluate(pos);
    ASSERT(std::abs(eval) <= 50); // should be near zero, tempo bonus small
    return true;
}

// ============================================================
// TT tests
// ============================================================

bool test_tt_basic() {
    TT.resize(4);
    TT.clear();

    Key key = 0x12345678ABCDEF01ULL;
    Move m  = makeMove(SQ_E2, SQ_E4, W_PAWN);

    TT.store(key, 0, 100, 50, m, 5, BOUND_EXACT);

    int score, eval, depth;
    Move found;
    TTBound bound;
    bool hit = TT.probe(key, 0, score, eval, found, depth, bound);

    ASSERT(hit);
    ASSERT(score == 100);
    ASSERT(depth == 5);
    ASSERT(bound == BOUND_EXACT);
    return true;
}

// ============================================================
// Main
// ============================================================

int main() {
    BB::init();
    Zobrist::init();

    initCastlingMask();

    std::cout << "=== Unit Tests ===\n\n";

    TEST(lsb);
    TEST(popcount);
    TEST(knight_attacks);
    TEST(knight_corner);
    TEST(rook_attacks);
    TEST(rook_blocked);
    TEST(bishop_attacks);
    TEST(pawn_attacks);
    TEST(between);
    TEST(fen_startpos);
    TEST(fen_roundtrip);
    TEST(make_unmake);
    TEST(in_check);
    TEST(movegen_startpos);
    TEST(movegen_kiwipete);
    TEST(see_basic);
    TEST(eval_symmetric);
    TEST(tt_basic);

    std::cout << "\n=== " << passed << " passed, " << failed << " failed ===\n";
    return (failed == 0) ? 0 : 1;
}
