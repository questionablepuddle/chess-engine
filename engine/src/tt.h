#pragma once
#include "types.h"
#include <vector>
#include <atomic>
#include <cstring>

// ============================================================
// Transposition Table
// ============================================================

enum TTBound : uint8_t {
    BOUND_NONE  = 0,
    BOUND_EXACT = 1,
    BOUND_LOWER = 2,  // fail-high (alpha cut-off): score >= beta
    BOUND_UPPER = 3,  // fail-low: score <= alpha
};

// Single TT entry — exactly 16 bytes (cache-line friendly, 4 entries per 64-byte line)
struct alignas(16) TTEntry {
    uint64_t key64;    // full 64-bit Zobrist key — eliminates hash collisions
    int16_t  score;    // stored score (mate scores adjusted for ply)
    int16_t  eval;     // static eval at this node
    uint16_t move16;   // best move (compact encoding)
    int8_t   depth;    // depth of search (capped at 127)
    uint8_t  genBound; // upper 6 bits = generation, lower 2 bits = TTBound

    TTBound  bound()      const { return TTBound(genBound & 3); }
    int      generation() const { return genBound >> 2; }
    int      depth8()     const { return depth; }
    int16_t  scoreRaw()   const { return score; }
    int16_t  evalRaw()    const { return eval; }

    // Decode the stored move back to a full Move
    Move     move()       const;
    void     encode(Move m);
};

// Cluster of 4 entries shares one 64-byte cache line (4*16 = 64 bytes exactly)
struct alignas(64) TTCluster {
    TTEntry entries[4];
};

class TranspositionTable {
public:
    TranspositionTable()  = default;
    ~TranspositionTable() = default;

    // Resize the table (megabytes). Clears all entries.
    void resize(size_t mb);
    void clear();
    void newSearch();     // Increment generation (aging)

    // Look up an entry. Returns true if hit. Fills score/move/depth/bound.
    bool probe(Key key, int ply, int& score, int& eval, Move& move, int& depth, TTBound& bound) const;

    // Store an entry.
    void store(Key key, int ply, int score, int eval, Move move, int depth, TTBound bound);

    // Approximate fill percentage (0–1000)
    int hashFull() const;

    // Convert a mate score to/from stored form (relative to ply)
    static int scoreToTT(int score, int ply);
    static int scoreFromTT(int score, int ply);

private:
    std::vector<TTCluster> clusters_;
    size_t                 clusterCount_ = 0;
    uint8_t                generation_   = 0;

    TTCluster* clusterOf(Key key) const {
        // Use upper bits to index into cluster table
        size_t idx = (size_t)(((unsigned __int128)key * (unsigned __int128)clusterCount_) >> 64);
        return const_cast<TTCluster*>(&clusters_[idx]);
    }
};

// Global transposition table — shared across all search threads
extern TranspositionTable TT;
