#pragma once
#include "position.h"

// ============================================================
// Move list — fixed-size stack array for performance
// ============================================================

struct MoveList {
    static constexpr int MAX_MOVES = 256;

    Move  moves[MAX_MOVES];
    int   scores[MAX_MOVES];
    int   size = 0;

    void add(Move m, int score = 0) {
        moves[size]  = m;
        scores[size] = score;
        size++;
    }

    bool contains(Move m) const {
        for (int i = 0; i < size; i++)
            if (moves[i] == m) return true;
        return false;
    }

    // Selection sort: pick highest scored move, swap to position i
    Move pick(int i) {
        int best = i;
        for (int j = i + 1; j < size; j++)
            if (scores[j] > scores[best]) best = j;
        std::swap(moves[i],  moves[best]);
        std::swap(scores[i], scores[best]);
        return moves[i];
    }
};

// ============================================================
// Full move generation (generates all pseudo-legal moves)
// ============================================================

void generateAllMoves(const Position& pos, MoveList& list);
void generateCaptures(const Position& pos, MoveList& list);
void generateQuiets(const Position& pos, MoveList& list);
void generateEvasions(const Position& pos, MoveList& list);

// Count legal moves (for checkmate/stalemate detection)
int  countLegalMoves(Position& pos);

// ============================================================
// Staged move generator — feeds moves one at a time for search
// ============================================================

enum GenStage {
    STAGE_TT,
    STAGE_INIT_CAPTURES,
    STAGE_GOOD_CAPTURES,
    STAGE_KILLER1,
    STAGE_KILLER2,
    STAGE_COUNTER,
    STAGE_INIT_QUIETS,
    STAGE_QUIETS,
    STAGE_BAD_CAPTURES,
    STAGE_DONE,

    // Quiescence search stages
    STAGE_QS_TT,
    STAGE_QS_INIT_CAPTURES,
    STAGE_QS_CAPTURES,
    STAGE_QS_DONE,

    // Evasion stages (in check)
    STAGE_EVASION_TT,
    STAGE_EVASION_INIT,
    STAGE_EVASIONS,
    STAGE_EVASION_DONE,
};

struct History;  // forward decl

class MovePicker {
public:
    // For normal search
    MovePicker(const Position& pos, Move ttMove, Move killer1, Move killer2,
               Move counter, const History& history, int depth);

    // For quiescence search
    MovePicker(const Position& pos, Move ttMove, int qs_depth);

    Move next(bool skipQuiets = false);

    bool  inCheck() const { return inCheck_; }

private:
    const Position& pos_;
    const History*  history_;
    MoveList        captures_;
    MoveList        quiets_;
    MoveList        badCaptures_;
    int             captureIdx_ = 0;
    int             quietIdx_   = 0;
    int             badCapIdx_  = 0;
    GenStage        stage_;
    Move            ttMove_;
    Move            killer1_, killer2_, counter_;
    int             depth_;
    bool            inCheck_;

    void scoreCaptures();
    void scoreQuiets();
    int  mvvLva(Move m) const;
};

// ============================================================
// MVV-LVA table: Most Valuable Victim – Least Valuable Attacker
// ============================================================

// victim_value - attacker_value/10  (higher = better capture)
constexpr int MVV_LVA[PIECE_TYPE_NB][PIECE_TYPE_NB] = {
    // attacker:  P     N     B     R     Q     K
    {0},                                              // NO_PIECE_TYPE
    { 1050, 1040, 1030, 1020, 1010, 1000 }, // P captures
    { 2050, 2040, 2030, 2020, 2010, 2000 }, // N captures
    { 3050, 3040, 3030, 3020, 3010, 3000 }, // B captures
    { 4050, 4040, 4030, 4020, 4010, 4000 }, // R captures
    { 5050, 5040, 5030, 5020, 5010, 5000 }, // Q captures
    { 6050, 6040, 6030, 6020, 6010, 6000 }, // K captures
};
