#include "uci.h"
#include "bitboard.h"
#include "evaluate.h"
#include "book.h"
#include "tt.h"
#include "movegen.h"
#include "syzygy.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>

namespace UCI {

static constexpr int MAX_GAME_PLY = 1024;

// ============================================================
// Engine identification
// ============================================================

void printId() {
    std::cout << "id name QuantumChess 1.0\n";
    std::cout << "id author QuantumChess Team\n";
    std::cout << "option name Hash type spin default 64 min 1 max 32768\n";
    std::cout << "option name Threads type spin default 1 min 1 max 512\n";
    std::cout << "option name SyzygyPath type string default <empty>\n";
    std::cout << "option name MoveOverhead type spin default 10 min 0 max 5000\n";
    std::cout << "option name OwnBook type check default false\n";
    std::cout << "option name BookFile type string default <empty>\n";
    std::cout << "option name NNUEFile type string default nn.bin\n";
    std::cout << "option name Ponder type check default false\n";
    std::cout << "uciok\n";
}

// ============================================================
// Position parsing
// ============================================================

void parsePosition(const std::string& line, Position& pos, StateInfo* states, int& stateIdx) {
    std::istringstream ss(line);
    std::string token;
    ss >> token; // "position"

    std::string fen;
    ss >> token;
    if (token == "startpos") {
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        ss >> token; // consume optional "moves"
    } else if (token == "fen") {
        while (ss >> token && token != "moves")
            fen += (fen.empty() ? "" : " ") + token;
    }

    stateIdx = 0;
    pos.setFen(fen, states[stateIdx++]);

    // Apply moves
    while (ss >> token) {
        if (token == "moves") continue;

        // Find matching legal move
        MoveList list;
        generateAllMoves(pos, list);
        Move found = Move::none();

        for (int i = 0; i < list.size; i++) {
            Move m = list.moves[i];
            if (!pos.isLegal(m)) continue;
            if (moveName(m) == token) {
                found = m;
                break;
            }
        }

        if (found.isNone()) continue; // skip invalid move strings

        if (stateIdx >= MAX_GAME_PLY) {
            // Shift states
            states[0] = states[stateIdx - 1];
            stateIdx = 1;
        }
        pos.makeMove(found, states[stateIdx++]);
    }
}

// ============================================================
// Go command parsing
// ============================================================

SearchLimits parseGo(const std::string& line) {
    SearchLimits limits;
    std::istringstream ss(line);
    std::string token;
    ss >> token; // "go"

    while (ss >> token) {
        if      (token == "wtime")     ss >> limits.wtime;
        else if (token == "btime")     ss >> limits.btime;
        else if (token == "winc")      ss >> limits.winc;
        else if (token == "binc")      ss >> limits.binc;
        else if (token == "movestogo") ss >> limits.movestogo;
        else if (token == "depth")     ss >> limits.depth;
        else if (token == "movetime")  ss >> limits.movetime;
        else if (token == "infinite")  limits.infinite = true;
        else if (token == "ponder")    limits.ponder = true;
        else if (token == "searchmoves") {
            while (ss >> token) {
                limits.searchMoves.push_back(token);
            }
            break;
        }
    }
    return limits;
}

// ============================================================
// Bench command: fixed position set
// ============================================================

static const char* BenchFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/p1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "3rr1k1/pp3pp1/1qn2np1/8/3p4/PP1RPPB1/2P1Q1PP/R5K1 b - - 0 1",
    "8/7p/p5pb/4k3/P1pPn3/8/P5PP/1rB2RK1 b - d3 0 28",
    "5rk1/1pp1pn1p/p3Brp1/8/1n6/5N2/PP3PPP/2R1R1K1 w - - 2 20",
    "8/1p4p1/7p/5P1P/2k3P1/8/2K1p3/8 w - - 0 1",
    nullptr
};

static void bench() {
    int totalNodes = 0;
    auto start = Clock::now();
    Position pos;
    StateInfo states[MAX_GAME_PLY];
    for (int i = 0; BenchFens[i]; i++) {
        pos.setFen(BenchFens[i], states[0]);

        SearchLimits limits;
        limits.depth = 13;

        TT.clear();
        SearchResult result = Search::go(pos, limits);
        totalNodes += 1; // placeholder
        std::cout << "bestmove " << moveName(result.bestMove) << "\n";
    }

    int64_t ms = elapsed(start);
    std::cout << "\n=== Bench: " << totalNodes << " nodes in "
              << ms << " ms ===\n";
}

// ============================================================
// Main UCI loop
// ============================================================

void loop() {
    Position pos;
    static StateInfo states[MAX_GAME_PLY];
    int stateIdx = 0;

    pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", states[0]);
    stateIdx = 1;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        try {

        if (token == "uci") {
            printId();

        } else if (token == "isready") {
            std::cout << "readyok\n";

        } else if (token == "ucinewgame") {
            Search::clearHistory();
            TT.clear();
            pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                       states[0]);
            stateIdx = 1;

        } else if (token == "position") {
            parsePosition(line, pos, states, stateIdx);

        } else if (token == "go") {
            SearchLimits limits = parseGo(line);

            // Check opening book first
            if (Book.loaded()) {
                Move bookMove = Book.probe(pos.key());
                if (!bookMove.isNone()) {
                    std::cout << "bestmove " << moveName(bookMove) << "\n";
                    continue;
                }
            }

            SearchResult result = Search::go(pos, limits);
            std::cout << "bestmove " << moveName(result.bestMove);
            if (!result.ponderMove.isNone())
                std::cout << " ponder " << moveName(result.ponderMove);
            std::cout << "\n";
            std::cout.flush();

        } else if (token == "stop") {
            Search::stop();

        } else if (token == "ponderhit") {
            // For now: stop pondering (just continue searching)

        } else if (token == "setoption") {
            std::string name, value;
            ss >> token; // "name"
            while (ss >> token && token != "value")
                name += (name.empty() ? "" : " ") + token;
            while (ss >> token)
                value += (value.empty() ? "" : " ") + token;

            if (name == "Hash") {
                Search::resizeTT(std::stoul(value));
            } else if (name == "BookFile") {
                Book.load(value);
            } else if (name == "NNUEFile") {
                NNUE::load(value);
            } else if (name == "SyzygyPath") {
                if (!value.empty() && value != "<empty>") {
                    bool ok = Syzygy::init(value);
                    std::cout << "info string Syzygy "
                              << (ok ? "loaded" : "failed")
                              << " TB_LARGEST=" << Syzygy::MaxPieces << "\n";
                    std::cout.flush();
                }
            }

        } else if (token == "quit") {
            Search::stop();
            break;

        } else if (token == "bench") {
            bench();

        } else if (token == "d") {
            pos.print();

        } else if (token == "eval") {
            EvalInfo info;
            int score = evaluateDetailed(pos, info);
            int ph = info.phase;
            int mw = ph, ew = MAX_PHASE - ph;
            auto taper = [&](const TaperedScore& s) {
                return (s.mg * mw + s.eg * ew) / MAX_PHASE;
            };
            std::cout << "\n--- Eval breakdown (phase=" << ph << "/24) ---\n";
            std::cout << std::left;
            std::printf("%-16s %6s %6s %6s %6s %6s\n", "Term", "W.mg", "W.eg", "B.mg", "B.eg", "Net");
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "Material",
                info.material[0].mg, info.material[0].eg,
                info.material[1].mg, info.material[1].eg,
                taper({info.material[0].mg - info.material[1].mg, info.material[0].eg - info.material[1].eg}));
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "PST",
                info.psqt[0].mg, info.psqt[0].eg,
                info.psqt[1].mg, info.psqt[1].eg,
                taper({info.psqt[0].mg - info.psqt[1].mg, info.psqt[0].eg - info.psqt[1].eg}));
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "Mobility",
                info.mobility[0].mg, info.mobility[0].eg,
                info.mobility[1].mg, info.mobility[1].eg,
                taper({info.mobility[0].mg - info.mobility[1].mg, info.mobility[0].eg - info.mobility[1].eg}));
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "Pawns",
                info.pawns[0].mg, info.pawns[0].eg,
                info.pawns[1].mg, info.pawns[1].eg,
                taper({info.pawns[0].mg - info.pawns[1].mg, info.pawns[0].eg - info.pawns[1].eg}));
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "King safety",
                info.kingSafety[0].mg, info.kingSafety[0].eg,
                info.kingSafety[1].mg, info.kingSafety[1].eg,
                taper({info.kingSafety[0].mg - info.kingSafety[1].mg, info.kingSafety[0].eg - info.kingSafety[1].eg}));
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "Rooks",
                info.rooks[0].mg, info.rooks[0].eg,
                info.rooks[1].mg, info.rooks[1].eg,
                taper({info.rooks[0].mg - info.rooks[1].mg, info.rooks[0].eg - info.rooks[1].eg}));
            std::printf("%-16s %6d %6d %6d %6d %6d\n", "Bishops",
                info.bishops[0].mg, info.bishops[0].eg,
                info.bishops[1].mg, info.bishops[1].eg,
                taper({info.bishops[0].mg - info.bishops[1].mg, info.bishops[0].eg - info.bishops[1].eg}));
            std::printf("%-16s %6s %6s %6s %6s %6d\n", "Tempo", "", "", "", "", (pos.sideToMove() == WHITE) ? TEMPO : -TEMPO);
            std::printf("%-16s %6s %6s %6s %6s %6d\n", "TOTAL", "", "", "", "", score);
            std::cout << "(positive = good for white, result is from side-to-move perspective: " << score << " cp)\n";
        }

        } catch (const std::exception& e) {
            std::cerr << "UCI command error (" << token << "): " << e.what() << "\n";
            std::cout.flush();
        } catch (...) {
            std::cerr << "UCI command error (" << token << "): unknown exception\n";
            std::cout.flush();
        }
    }
}

} // namespace UCI
