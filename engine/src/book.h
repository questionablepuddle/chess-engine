#pragma once
#include "types.h"
#include <string>
#include <vector>

class Position;  // forward declaration

// ============================================================
// Polyglot opening book (.bin format)
// Entry: key(8) + move(2) + weight(2) + learn(4) = 16 bytes
// ============================================================

struct PolyEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};

class OpeningBook {
public:
    bool load(const std::string& path);
    void clear();

    // Returns a weighted-random book move for the given position key
    // Returns Move::none() if position not found or book not loaded
    Move probe(uint64_t key) const;

    bool loaded() const { return !entries_.empty(); }

private:
    std::vector<PolyEntry> entries_;

    // Convert Polyglot encoded move to our Move (needs position for piece info)
    Move decodePoly(uint16_t polyMove, uint64_t key) const;
};

extern OpeningBook Book;

// Compute Polyglot hash for a position (compatible with .bin books)
uint64_t polyglotKey(const Position& pos);
