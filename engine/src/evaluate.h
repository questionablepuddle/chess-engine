#pragma once
#include "position.h"

// ============================================================
// Evaluation — returns score in centipawns from side-to-move's perspective
// Positive = side to move is winning
// ============================================================

// Game phase constants (for tapered evaluation)
constexpr int PhaseWeights[PIECE_TYPE_NB] = { 0, 0, 1, 1, 2, 4, 0 };
constexpr int MAX_PHASE = 24; // 4 knights + 4 bishops + 4 rooks + 2*2 queens

// Tapered eval: interpolate between midgame and endgame
struct TaperedScore {
    int mg = 0, eg = 0;
    TaperedScore() = default;
    TaperedScore(int mg, int eg) : mg(mg), eg(eg) {}
    TaperedScore& operator+=(const TaperedScore& o) { mg += o.mg; eg += o.eg; return *this; }
    TaperedScore& operator-=(const TaperedScore& o) { mg -= o.mg; eg -= o.eg; return *this; }
    TaperedScore  operator+(const TaperedScore& o) const { return {mg + o.mg, eg + o.eg}; }
    TaperedScore  operator-(const TaperedScore& o) const { return {mg - o.mg, eg - o.eg}; }
    TaperedScore  operator-() const { return {-mg, -eg}; }
};

// Main evaluation entry point
int evaluate(const Position& pos);

// Detailed evaluation (for debugging / tuning)
// Index by Color (WHITE=0, BLACK=1)
struct EvalInfo {
    TaperedScore material[2];
    TaperedScore psqt[2];
    TaperedScore mobility[2];
    TaperedScore pawns[2];
    TaperedScore kingSafety[2];
    TaperedScore rooks[2];
    TaperedScore bishops[2];
    int phase;
};

int evaluateDetailed(const Position& pos, EvalInfo& info);

// Tempo bonus
constexpr int TEMPO = 10;
