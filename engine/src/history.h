#pragma once
#include "types.h"
#include <array>
#include <algorithm>
#include <cstring>

// ============================================================
// History heuristics — multiple tables for move ordering
// ============================================================

// Clamp a history value to [-MAX_HISTORY, MAX_HISTORY]
constexpr int MAX_HISTORY = 16384;

inline void historyUpdate(int& entry, int bonus) {
    entry += bonus - entry * std::abs(bonus) / MAX_HISTORY;
    entry = std::clamp(entry, -MAX_HISTORY, MAX_HISTORY);
}

// ============================================================
// Butterfly history: [color][from][to]
// ============================================================

struct ButterflyHistory {
    int table[COLOR_NB][64][64] = {};

    void update(Color c, Square from, Square to, int bonus) {
        historyUpdate(table[c][from][to], bonus);
    }

    int get(Color c, Square from, Square to) const {
        return table[c][from][to];
    }

    void clear() { std::memset(table, 0, sizeof(table)); }
};

// ============================================================
// Continuation history: [piece][to_square]
// (used for 1-ply and 2-ply continuations)
// ============================================================

struct ContinuationHistory {
    int table[PIECE_NB][64] = {};

    void update(Piece piece, Square to, int bonus) {
        historyUpdate(table[piece][to], bonus);
    }

    int get(Piece piece, Square to) const {
        return table[piece][to];
    }

    void clear() { std::memset(table, 0, sizeof(table)); }
};

// ============================================================
// Capture history: [piece][to][captured_piece_type]
// ============================================================

struct CaptureHistory {
    int table[PIECE_NB][64][PIECE_TYPE_NB] = {};

    void update(Piece piece, Square to, PieceType captured, int bonus) {
        historyUpdate(table[piece][to][captured], bonus);
    }

    int get(Piece piece, Square to, PieceType captured) const {
        return table[piece][to][captured];
    }

    void clear() { std::memset(table, 0, sizeof(table)); }
};

// ============================================================
// Counter move table: [piece][to] -> best response move
// ============================================================

struct CounterMoveTable {
    Move table[PIECE_NB][64];

    CounterMoveTable() {
        for (auto& row : table)
            for (auto& m : row)
                m = Move::none();
    }

    void update(Piece piece, Square to, Move counter) {
        table[piece][to] = counter;
    }

    Move get(Piece piece, Square to) const {
        return table[piece][to];
    }

    void clear() {
        for (auto& row : table)
            for (auto& m : row)
                m = Move::none();
    }
};

// ============================================================
// Killer move table: 2 killers per ply
// ============================================================

struct KillerTable {
    static constexpr int MAX_PLY = 256;
    Move killers[MAX_PLY][2];

    KillerTable() {
        for (auto& row : killers)
            row[0] = row[1] = Move::none();
    }

    void update(int ply, Move m) {
        if (killers[ply][0] != m) {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = m;
        }
    }

    Move get(int ply, int slot) const {
        return (ply >= 0 && ply < MAX_PLY) ? killers[ply][slot] : Move::none();
    }

    void clear() {
        for (auto& row : killers)
            row[0] = row[1] = Move::none();
    }
};

// ============================================================
// Combined History object (held by SearchThread)
// ============================================================

struct History {
    ButterflyHistory    butterfly;
    ContinuationHistory cont1;    // 1-ply continuation
    ContinuationHistory cont2;    // 2-ply continuation
    CaptureHistory      capture;
    CounterMoveTable    counter;
    KillerTable         killers;

    int getHistory(Color c, Square from, Square to) const {
        return butterfly.get(c, from, to);
    }

    void updateQuiet(Color c, Square from, Square to,
                     Piece piece, Move prevMove1, Move prevMove2,
                     int bonus) {
        butterfly.update(c, from, to, bonus);
        if (!prevMove1.isNone() && !prevMove1.isNull())
            cont1.update(piece, to, bonus);
        if (!prevMove2.isNone() && !prevMove2.isNull())
            cont2.update(piece, to, bonus);
    }

    void updateCapture(Piece piece, Square to, PieceType captured, int bonus) {
        capture.update(piece, to, captured, bonus);
    }

    void updateKiller(int ply, Move m) {
        killers.update(ply, m);
    }

    void updateCounter(Piece piece, Square to, Move counter) {
        this->counter.update(piece, to, counter);
    }

    Move getCounter(Piece piece, Square to) const {
        return counter.get(piece, to);
    }

    void clear() {
        butterfly.clear();
        cont1.clear();
        cont2.clear();
        capture.clear();
        counter.clear();
        killers.clear();
    }
};
