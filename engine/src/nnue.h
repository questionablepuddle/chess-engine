#pragma once
#include "types.h"
#include <cstdint>
#include <string>
#include <array>

class Position;  // forward declaration

// ============================================================
// NNUE — HalfKP-style network
// Architecture: 768 inputs -> 256 hidden -> 1 output
//
// For now: simplified HalfKP with 768 binary features
// (12 piece types × 64 squares = 768), one accumulator per perspective.
//
// The full HalfKP (king-relative) accumulator is stubbed and can be
// activated once training data + weights are generated.
// ============================================================

namespace NNUE {

constexpr int INPUT_SIZE   = 768;   // 12 * 64
constexpr int HIDDEN_SIZE  = 256;   // accumulator width
constexpr int OUTPUT_SIZE  = 1;
constexpr int SCALE        = 400;   // output scale to centipawns

// Accumulator (two perspectives: ours, theirs)
struct Accumulator {
    alignas(64) int16_t white[HIDDEN_SIZE];
    alignas(64) int16_t black[HIDDEN_SIZE];
    bool computed[2] = {false, false};
};

// Network weights (quantized int16 / int8)
struct Network {
    alignas(64) int16_t ft_weights[INPUT_SIZE * HIDDEN_SIZE];
    alignas(64) int16_t ft_biases[HIDDEN_SIZE];
    alignas(64) int8_t  l1_weights[HIDDEN_SIZE * 2];  // 2 perspectives concatenated
    alignas(64) int32_t l1_biases[1];
};

// Global network (loaded at startup)
extern Network net;
extern bool    loaded;

// Load weights from binary file
bool load(const std::string& path);

// Compute feature index for a piece on a square from a given perspective
inline int featureIndex(Color perspective, Piece piece, Square sq) {
    // Simple 768-feature encoding: (piece - 1) * 64 + sq
    // piece: W_PAWN=1..W_KING=6, B_PAWN=9..B_KING=14
    int pt = typeOf(piece) - 1;       // 0..5
    int c  = (colorOf(piece) != perspective) ? 6 : 0;  // enemy pieces offset by 6
    return (pt + c) * 64 + sq;
}

// Reset and fully recompute accumulator from scratch
void refreshAccumulator(const Position& pos, Accumulator& acc, Color perspective);

// Incremental update: add/remove a feature
void addFeature(Accumulator& acc, Color perspective, int featureIdx);
void removeFeature(Accumulator& acc, Color perspective, int featureIdx);

// Forward pass: returns eval in centipawns from side-to-move perspective
int  evaluate(const Accumulator& acc, Color stm);

// Initialize with zeroed weights (HCE fallback when no weights file)
void init();

} // namespace NNUE
