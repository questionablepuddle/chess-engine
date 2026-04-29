#pragma once
#include "position.h"
#include "history.h"
#include "timeman.h"
#include "tt.h"
#include "nnue.h"
#include <atomic>
#include <vector>

// ============================================================
// Search stack — one entry per ply
// ============================================================

struct SearchStack {
    static constexpr int MAX_PLY = 246;

    Move     currentMove     = Move::none();
    Move     excludedMove    = Move::none();  // for singular extension
    Move     killer[2]       = {Move::none(), Move::none()};
    int      staticEval      = VALUE_NONE;
    int      ply             = 0;
    bool     inCheck         = false;
    bool     ttPv            = false;         // on a PV path from TT
    NNUE::Accumulator* accumulator = nullptr;

    // Access pliesFromNull from the position state (search sets this via pos)
    int pliesFromNull() const { return ply; } // placeholder — actual value from StateInfo
};

// ============================================================
// Search limits (set by UCI go command)
// ============================================================

struct SearchLimits {
    int  wtime = 0, btime = 0;
    int  winc  = 0, binc  = 0;
    int  movestogo = 0;
    int  depth     = 0;    // 0 = unlimited
    int  movetime  = 0;    // ms, 0 = not set
    bool infinite  = false;
    bool ponder    = false;
    std::vector<std::string> searchMoves; // UCI move strings; empty = all moves
};

// ============================================================
// Search result
// ============================================================

struct SearchResult {
    Move  bestMove  = Move::none();
    Move  ponderMove = Move::none();
    int   score     = 0;
    int   depth     = 0;
};

// ============================================================
// Per-thread search state
// ============================================================

class SearchThread {
public:
    SearchThread() = default;

    void init(int id) { id_ = id; }

    // Run iterative deepening search
    SearchResult search(Position& pos, const SearchLimits& limits,
                        const TimeManager& tm, std::atomic<bool>& stop);

    // Run quiescence search (public for perft/testing)
    int  qsearch(Position& pos, int alpha, int beta, int ply, SearchStack* ss);

    int64_t nodesSearched() const { return nodes_; }
    int  id() const { return id_; }

    History history;

private:
    int     id_    = 0;
    int64_t nodes_ = 0;
    int     tbHits_ = 0;

    // Shared stop flag (points to the one in UCI/main)
    std::atomic<bool>* stop_ = nullptr;
    const TimeManager* tm_   = nullptr;

    // PV table
    Move pvTable_[SearchStack::MAX_PLY][SearchStack::MAX_PLY];
    int  pvLength_[SearchStack::MAX_PLY];

    void updatePV(Move m, int ply) {
        pvTable_[ply][ply] = m;
        for (int i = ply + 1; i < pvLength_[ply + 1]; i++)
            pvTable_[ply][i] = pvTable_[ply + 1][i];
        pvLength_[ply] = pvLength_[ply + 1];
    }

    void clearPV() {
        for (int i = 0; i < SearchStack::MAX_PLY; i++) pvLength_[i] = 0;
    }

    // The core negamax function
    int negamax(Position& pos, int alpha, int beta, int depth, int ply,
                SearchStack* ss, bool cutNode);

    // Static eval with NNUE / HCE
    int staticEval(const Position& pos, SearchStack* ss);

    // Update history on a beta cutoff
    void updateHistories(Position& pos, Move bestMove, int depth, int ply,
                         SearchStack* ss, Move* quiets, int quietCount,
                         Move* captures, int captureCount);

    const SearchLimits* limits_ = nullptr;

    // Check if we should stop searching
    bool shouldStop() const {
        if (!stop_) return false;
        if (stop_->load(std::memory_order_relaxed)) return true;
        if (id_ == 0 && tm_) return tm_->shouldStop();
        return false;
    }
};

// ============================================================
// Global search control
// ============================================================

namespace Search {
    void init();
    SearchResult go(Position& pos, const SearchLimits& limits);
    void stop();
    void clearHistory();
    void resizeTT(size_t mb);
    void printInfo(int depth, int score, int64_t nodes, int64_t ms, int hashfull,
                   const Move* pv, int pvLen);
}
