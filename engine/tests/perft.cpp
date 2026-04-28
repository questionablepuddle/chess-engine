#include "../src/bitboard.h"
#include "../src/position.h"
#include "../src/movegen.h"
#include <iostream>
#include <chrono>
#include <string>
#include <cassert>

// ============================================================
// Perft — counts leaf nodes to validate move generation
// ============================================================

static uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveList list;
    generateAllMoves(pos, list);

    uint64_t nodes = 0;
    StateInfo si;

    for (int i = 0; i < list.size; i++) {
        Move m = list.moves[i];
        if (!pos.isLegal(m)) continue;

        pos.makeMove(m, si);
        nodes += perft(pos, depth - 1);
        pos.unmakeMove(m);
    }
    return nodes;
}

static void perftDivide(Position& pos, int depth) {
    MoveList list;
    generateAllMoves(pos, list);
    uint64_t total = 0;
    StateInfo si;

    for (int i = 0; i < list.size; i++) {
        Move m = list.moves[i];
        if (!pos.isLegal(m)) continue;

        pos.makeMove(m, si);
        uint64_t count = perft(pos, depth - 1);
        pos.unmakeMove(m);

        std::cout << moveName(m) << ": " << count << "\n";
        total += count;
    }
    std::cout << "\nTotal: " << total << "\n";
}

struct PerftSuite {
    const char* fen;
    uint64_t    expected[7]; // depth 1..6
};

static const PerftSuite suites[] = {
    // Standard starting position
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      { 20, 400, 8902, 197281, 4865609, 119060324, 0 } },

    // Kiwipete position (catches many bugs)
    { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      { 48, 2039, 97862, 4085603, 193690690, 0, 0 } },

    // Endgame position
    { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
      { 14, 191, 2812, 43238, 674624, 11030083, 0 } },

    // Tricky position (promotions, en passant)
    { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
      { 6, 264, 9467, 422333, 15833292, 0, 0 } },

    { nullptr, { 0 } }
};

int main(int argc, char* argv[]) {
    BB::init();
    Zobrist::init();

    initCastlingMask();

    if (argc >= 3 && std::string(argv[1]) == "divide") {
        int depth = std::stoi(argv[2]);
        std::string fen = (argc >= 4)
            ? argv[3]
            : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        Position pos;
        StateInfo si;
        pos.setFen(fen, si);
        perftDivide(pos, depth);
        return 0;
    }

    int maxDepth = (argc >= 2) ? std::stoi(argv[1]) : 5;
    bool allPassed = true;

    for (int s = 0; suites[s].fen; s++) {
        Position pos;
        StateInfo si;
        pos.setFen(suites[s].fen, si);

        std::cout << "\nFEN: " << suites[s].fen << "\n";

        for (int d = 1; d <= maxDepth && suites[s].expected[d-1] != 0; d++) {
            auto t0 = std::chrono::steady_clock::now();
            uint64_t nodes = perft(pos, d);
            auto t1 = std::chrono::steady_clock::now();
            int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            uint64_t expected = suites[s].expected[d-1];
            bool pass = (nodes == expected);
            allPassed &= pass;

            std::cout << "  depth " << d
                      << ": " << nodes
                      << " (" << (pass ? "PASS" : "FAIL expected " + std::to_string(expected) + ")")
                      << "  " << ms << "ms\n";
        }
    }

    std::cout << "\n" << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return allPassed ? 0 : 1;
}
