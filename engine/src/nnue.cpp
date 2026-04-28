#include "nnue.h"
#include "position.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iostream>

#if defined(__x86_64__) && defined(USE_AVX2)
#include <immintrin.h>
#endif

namespace NNUE {

Network net;
bool    loaded = false;

// ============================================================
// Init: zero all weights (engine uses HCE if not loaded)
// ============================================================

void init() {
    std::memset(&net, 0, sizeof(net));
    loaded = false;
}

// ============================================================
// Load binary weights file
// Format:
//   [ft_weights: INPUT_SIZE * HIDDEN_SIZE * int16]
//   [ft_biases:  HIDDEN_SIZE * int16]
//   [l1_weights: HIDDEN_SIZE * 2 * int8]
//   [l1_biases:  1 * int32]
// ============================================================

bool load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.read(reinterpret_cast<char*>(net.ft_weights), sizeof(net.ft_weights));
    f.read(reinterpret_cast<char*>(net.ft_biases),  sizeof(net.ft_biases));
    f.read(reinterpret_cast<char*>(net.l1_weights), sizeof(net.l1_weights));
    f.read(reinterpret_cast<char*>(net.l1_biases),  sizeof(net.l1_biases));

    if (!f) { init(); return false; }

    loaded = true;
    return true;
}

// ============================================================
// Accumulator refresh (full recompute from position)
// ============================================================

void refreshAccumulator(const Position& pos, Accumulator& acc, Color /*perspective*/) {
    // Initialize to biases
    std::memcpy(acc.white, net.ft_biases, sizeof(net.ft_biases));
    std::memcpy(acc.black, net.ft_biases, sizeof(net.ft_biases));

    for (Color c : {WHITE, BLACK}) {
        for (int pt = PAWN; pt <= KING; pt++) {
            Bitboard bb = pos.pieces(c, PieceType(pt));
            while (bb) {
                Square s = popLSB(bb);
                Piece  p = makePiece(c, PieceType(pt));

                int widx = featureIndex(WHITE, p, s);
                int bidx = featureIndex(BLACK, p, flip(s));

                // Add to white accumulator
                for (int i = 0; i < HIDDEN_SIZE; i++)
                    acc.white[i] += net.ft_weights[widx * HIDDEN_SIZE + i];

                // Add to black accumulator
                for (int i = 0; i < HIDDEN_SIZE; i++)
                    acc.black[i] += net.ft_weights[bidx * HIDDEN_SIZE + i];
            }
        }
    }

    acc.computed[WHITE] = true;
    acc.computed[BLACK] = true;
}

// ============================================================
// Incremental feature updates
// ============================================================

void addFeature(Accumulator& acc, Color perspective, int featureIdx) {
    int16_t* side = (perspective == WHITE) ? acc.white : acc.black;
    const int16_t* w = &net.ft_weights[featureIdx * HIDDEN_SIZE];

#if defined(__x86_64__) && defined(USE_AVX2)
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i a = _mm256_load_si256((__m256i*)(side + i));
        __m256i b = _mm256_load_si256((__m256i*)(w + i));
        _mm256_store_si256((__m256i*)(side + i), _mm256_add_epi16(a, b));
    }
#else
    for (int i = 0; i < HIDDEN_SIZE; i++)
        side[i] += w[i];
#endif
}

void removeFeature(Accumulator& acc, Color perspective, int featureIdx) {
    int16_t* side = (perspective == WHITE) ? acc.white : acc.black;
    const int16_t* w = &net.ft_weights[featureIdx * HIDDEN_SIZE];

#if defined(__x86_64__) && defined(USE_AVX2)
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i a = _mm256_load_si256((__m256i*)(side + i));
        __m256i b = _mm256_load_si256((__m256i*)(w + i));
        _mm256_store_si256((__m256i*)(side + i), _mm256_sub_epi16(a, b));
    }
#else
    for (int i = 0; i < HIDDEN_SIZE; i++)
        side[i] -= w[i];
#endif
}

// ============================================================
// Forward pass: ClippedReLU + dot product
// Returns centipawns from STM perspective
// ============================================================

int evaluate(const Accumulator& acc, Color stm) {
    if (!loaded) return 0;  // Fall back to HCE

    // Concatenate: [us_acc | them_acc], apply ClippedReLU(0..127)
    const int16_t* us   = (stm == WHITE) ? acc.white : acc.black;
    const int16_t* them = (stm == WHITE) ? acc.black : acc.white;

    int32_t output = net.l1_biases[0];

#if defined(__x86_64__) && defined(USE_AVX2)
    __m256i zero = _mm256_setzero_si256();
    __m256i max_val = _mm256_set1_epi16(127);
    __m256i sum = _mm256_setzero_si256();

    // Process 'us' half
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i x = _mm256_load_si256((__m256i*)(us + i));
        x = _mm256_max_epi16(x, zero);
        x = _mm256_min_epi16(x, max_val);
        // Multiply by weights (int8 -> int16 -> accumulate)
        __m128i w8 = _mm_loadu_si128((__m128i*)(net.l1_weights + i));
        __m256i w16 = _mm256_cvtepi8_epi16(w8);
        __m256i prod = _mm256_madd_epi16(x, w16);
        sum = _mm256_add_epi32(sum, prod);
    }
    // Process 'them' half
    for (int i = 0; i < HIDDEN_SIZE; i += 16) {
        __m256i x = _mm256_load_si256((__m256i*)(them + i));
        x = _mm256_max_epi16(x, zero);
        x = _mm256_min_epi16(x, max_val);
        __m128i w8 = _mm_loadu_si128((__m128i*)(net.l1_weights + HIDDEN_SIZE + i));
        __m256i w16 = _mm256_cvtepi8_epi16(w8);
        __m256i prod = _mm256_madd_epi16(x, w16);
        sum = _mm256_add_epi32(sum, prod);
    }
    // Horizontal sum
    __m128i s128 = _mm_add_epi32(_mm256_extracti128_si256(sum, 0),
                                   _mm256_extracti128_si256(sum, 1));
    s128 = _mm_add_epi32(s128, _mm_shuffle_epi32(s128, 0x4E));
    s128 = _mm_add_epi32(s128, _mm_shuffle_epi32(s128, 0xB1));
    output += _mm_cvtsi128_si32(s128);
#else
    for (int i = 0; i < HIDDEN_SIZE; i++) {
        int16_t a = std::clamp(us[i], int16_t(0), int16_t(127));
        output += a * net.l1_weights[i];
    }
    for (int i = 0; i < HIDDEN_SIZE; i++) {
        int16_t a = std::clamp(them[i], int16_t(0), int16_t(127));
        output += a * net.l1_weights[HIDDEN_SIZE + i];
    }
#endif

    // Scale to centipawns
    return output * SCALE / (127 * 64);
}

} // namespace NNUE
