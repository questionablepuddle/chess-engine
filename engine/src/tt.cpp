#include "tt.h"
#include <cstring>
#include <algorithm>
#include <cassert>

TranspositionTable TT;

// ============================================================
// Move encoding into 16 bits for TT storage
// from(6) | to(6) | promo(3) | flags(1 = promotion)
// ============================================================

// 16-bit layout:
//   bits  0-5:  from square
//   bits  6-11: to square
//   bit   15:   isPromotion
//   When isPromo=1: bits 12-14 = promotion piece type
//   When isPromo=0: bit 12 = isEnPassant, bit 13 = isCastling, bit 14 = isDoublePush
void TTEntry::encode(Move m) {
    if (m.isNone()) { move16 = 0; return; }
    uint16_t d = uint16_t(m.from())
               | (uint16_t(m.to()) << 6);
    if (m.isPromotion()) {
        d |= (uint16_t(m.promotion()) << 12);
        d |= (1 << 15);
    } else {
        if (m.isEnPassant())  d |= (1 << 12);
        if (m.isCastling())   d |= (1 << 13);
        if (m.isDoublePush()) d |= (1 << 14);
    }
    move16 = d;
}

Move TTEntry::move() const {
    if (move16 == 0) return Move::none();
    Square from  = Square(move16 & 0x3F);
    Square to    = Square((move16 >> 6) & 0x3F);
    bool isPromo = (move16 >> 15) & 1;
    uint32_t d = uint32_t(from) | (uint32_t(to) << 6);
    if (isPromo) {
        PieceType pr = PieceType((move16 >> 12) & 0x7);
        d |= uint32_t(pr) << 20;
        d |= MF_PROMOTION;
    } else {
        if ((move16 >> 12) & 1) d |= MF_EN_PASSANT;
        if ((move16 >> 13) & 1) d |= MF_CASTLING;
        if ((move16 >> 14) & 1) d |= MF_DOUBLE_PUSH;
    }
    return Move(d);
}

// ============================================================
// Resize / clear
// ============================================================

void TranspositionTable::resize(size_t mb) {
    size_t bytes = mb * 1024 * 1024;
    clusterCount_ = bytes / sizeof(TTCluster);
    if (clusterCount_ == 0) clusterCount_ = 1;
    clusters_.resize(clusterCount_);
    std::memset(clusters_.data(), 0, clusterCount_ * sizeof(TTCluster));
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::memset(clusters_.data(), 0, clusterCount_ * sizeof(TTCluster));
    generation_ = 0;
}

void TranspositionTable::newSearch() {
    generation_ = (generation_ + 4) & 0xFC; // 6 bits for generation
}

// ============================================================
// Mate score adjustment
// TT stores mate scores relative to the root; we need ply-adjusted values
// ============================================================

int TranspositionTable::scoreToTT(int score, int ply) {
    if (score >= MATE_IN_MAX_PLY)  return score + ply;
    if (score <= -MATE_IN_MAX_PLY) return score - ply;
    return score;
}

int TranspositionTable::scoreFromTT(int score, int ply) {
    if (score >= MATE_IN_MAX_PLY)  return score - ply;
    if (score <= -MATE_IN_MAX_PLY) return score + ply;
    return score;
}

// ============================================================
// Probe
// ============================================================

bool TranspositionTable::probe(Key key, int ply, int& score, int& eval,
                                Move& move, int& depth, TTBound& bound) const {
    if (clusterCount_ == 0) return false;
    const TTCluster* cluster = clusterOf(key);

    for (int i = 0; i < 4; i++) {
        const TTEntry& e = cluster->entries[i];
        if (e.key64 != key || e.bound() == BOUND_NONE) continue;

        score = scoreFromTT(e.score, ply);
        eval  = e.eval;
        move  = e.move();
        depth = e.depth8();
        bound = e.bound();
        return true;
    }
    return false;
}

// ============================================================
// Store — two-tier replacement scheme:
//   - Prefer depth: replace if new depth >= old depth
//   - Always: use lowest-priority slot (oldest gen, shallowest depth)
// ============================================================

void TranspositionTable::store(Key key, int ply, int score, int eval,
                                Move move, int depth, TTBound bound) {
    if (clusterCount_ == 0) return;
    TTCluster* cluster = clusterOf(key);

    TTEntry* replace = nullptr;
    int      replace_priority = INT32_MAX;

    for (int i = 0; i < 4; i++) {
        TTEntry& e = cluster->entries[i];

        // Empty slot or same key — use it
        if (e.bound() == BOUND_NONE || e.key64 == key) {
            replace = &e;
            break;
        }

        // Priority = depth - 2 * age_penalty
        int age_diff = (generation_ - e.generation()) & 0x3F;
        int priority = e.depth8() - age_diff * 2;
        if (priority < replace_priority) {
            replace_priority = priority;
            replace = &e;
        }
    }

    assert(replace != nullptr);

    // Don't overwrite a deeper same-key entry with an UPPER bound
    // (it would lose exact/lower info)
    if (bound == BOUND_UPPER && replace->key64 == key
        && replace->depth8() > depth + 2
        && replace->generation() == generation_)
        return;

    replace->key64   = key;
    replace->score   = int16_t(scoreToTT(score, ply));
    replace->eval    = int16_t(eval);
    replace->depth   = int8_t(std::clamp(depth, -127, 127));
    replace->genBound = uint8_t(generation_ | uint8_t(bound));
    replace->encode(move);
}

// ============================================================
// Hash full
// ============================================================

int TranspositionTable::hashFull() const {
    if (clusterCount_ == 0) return 0;
    int filled = 0;
    int samples = std::min<int>(1000, clusterCount_ * 4);
    for (int i = 0; i < samples; i++) {
        const TTEntry& e = clusters_[i / 4].entries[i % 4];
        if (e.bound() != BOUND_NONE && e.generation() == generation_)
            filled++;
    }
    return filled * 1000 / samples;
}
