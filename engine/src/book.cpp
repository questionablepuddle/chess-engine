#include "book.h"
#include "position.h"
#include <fstream>
#include <algorithm>
#include <random>
#include <cstring>

OpeningBook Book;

// ============================================================
// Polyglot key computation (different from Zobrist — book-specific)
// ============================================================

// Polyglot random numbers for piece-square
static const uint64_t PolyPieceKeys[12][64] = {
    // Black pawn, black rook, black knight, black bishop, black queen, black king,
    // white pawn, white rook, white knight, white bishop, white queen, white king
    // (768 keys total — standard Polyglot values)
    // Using a simplified deterministic table (would normally be the official 781-key table)
    // For full compatibility, these should match the official Polyglot tables exactly.
    { 0x9D39247E33776D41ULL, 0x2AF7398005AAA5C7ULL, 0x44DB015024623547ULL, 0x9C15F73E62A76AE2ULL,
      0x75834465489C0C89ULL, 0x3290AC3A203001BFULL, 0x0FBBAD1F61042279ULL, 0xE83A908FF2FB60CAULL,
      0x0D7E765D58755C10ULL, 0x1A083822CEAFE02DULL, 0x9605D5F0E25EC3B0ULL, 0xD021FF5CD13A2ED5ULL,
      0x40BDF15D4A672D37ULL, 0x011355146FD56395ULL, 0x5DB4832046F3D9E5ULL, 0x239F8B2D7FF719CCULL,
      0x05D1A1AE85B49AA1ULL, 0x679F848F6E8FC971ULL, 0x7449BBFF801FED0BULL, 0x7D11CDB1C3B7ADF0ULL,
      0x82C7709E781EB7CCULL, 0xF3218F1C9510786CULL, 0x331478F3AF51BBE6ULL, 0x4BB38DE5E7219443ULL,
      0xAA649C6EBCFD50FCULL, 0x8DBD98A352AFD40BULL, 0x87D2074B81D79217ULL, 0x19F3C751D3E92AE1ULL,
      0xB4AB30F062B19ABFULL, 0x7B0500AC42047AC4ULL, 0xC9452CA81A09D85DULL, 0x24AA6C514DA27500ULL,
      0x4C9F34427501B447ULL, 0x14A280461FA196E6ULL, 0x707B643EC2ABE0F6ULL, 0xD7B5571FAD72E3E5ULL,
      0x0D8D58E4E8FC4AB4ULL, 0x5F3B05B773645ABBULL, 0xBB134B26BD76EE5BULL, 0xB2B5A3F080D49B7AULL,
      0xE9A82A40A4680B4BULL, 0x61CE0A0E4A6B9B2DULL, 0x82C8B3E6D12B0891ULL, 0x0B54F03DDA60B7ECULL,
      0x2E2BEACF1DE2E2E8ULL, 0xFE1E5A0F24E1D819ULL, 0x6B7E7257D48AE6ACULL, 0xD8EB92EB9A0CF4B1ULL,
      0x9E26B9BE5C0A22CBULL, 0x65E3EFCA25ABDE62ULL, 0xDBC7D3A43A7ADFF8ULL, 0x68F73DDF8D15C5F7ULL,
      0x8AFC6C9FA985AA85ULL, 0x5A8F4D01E0E84702ULL, 0x27EBB9C3219AA649ULL, 0x44C0FDE6E6F7E97DULL,
      0x7BBF37E51EB5A02BULL, 0x3C47E2BC60F6AF6EULL, 0x12640C1B5BF99DAEULL, 0x7DEC8BF81EAF6B9BULL,
      0x30D7B36A7B7D7E15ULL, 0x698FAB6CC40FE975ULL, 0x1E4AB87A478C86E2ULL, 0x53E1BA8CD27395D9ULL },
    // (10 more rows — abbreviated, use same pattern)
    { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }
};

uint64_t polyglotKey(const Position& pos) {
    // Simplified: reuse Zobrist key (books will still work if we generate our own book)
    // For real Polyglot compatibility, use official tables
    return pos.key();
}

// ============================================================
// Book loading
// ============================================================

bool OpeningBook::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    entries_.clear();
    PolyEntry e;
    while (f.read(reinterpret_cast<char*>(&e), sizeof(PolyEntry))) {
        // Polyglot stores values in big-endian; swap if needed
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        e.key    = __builtin_bswap64(e.key);
        e.move   = __builtin_bswap16(e.move);
        e.weight = __builtin_bswap16(e.weight);
#endif
        entries_.push_back(e);
    }

    // Sort by key for binary search
    std::sort(entries_.begin(), entries_.end(),
              [](const PolyEntry& a, const PolyEntry& b) { return a.key < b.key; });

    return !entries_.empty();
}

void OpeningBook::clear() {
    entries_.clear();
}

// ============================================================
// Book probe
// ============================================================

Move OpeningBook::probe(uint64_t key) const {
    if (entries_.empty()) return Move::none();

    // Binary search for first entry with this key
    auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
        [](const PolyEntry& e, uint64_t k) { return e.key < k; });

    if (it == entries_.end() || it->key != key) return Move::none();

    // Collect all moves for this position
    std::vector<std::pair<uint16_t, int>> candidates;
    int totalWeight = 0;
    for (auto jt = it; jt != entries_.end() && jt->key == key; ++jt) {
        candidates.push_back({jt->move, jt->weight});
        totalWeight += jt->weight;
    }

    if (candidates.empty() || totalWeight == 0) return Move::none();

    // Weighted random selection
    static std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, totalWeight - 1);
    int r = dist(rng);
    int cumulative = 0;
    uint16_t selected = candidates[0].first;
    for (auto& [mv, w] : candidates) {
        cumulative += w;
        if (r < cumulative) { selected = mv; break; }
    }

    return decodePoly(selected, key);
}

// ============================================================
// Polyglot move decoding
// Polyglot encodes: to_file(3) | to_rank(3) | from_file(3) | from_rank(3) | promo(3)
// ============================================================

Move OpeningBook::decodePoly(uint16_t polyMove, uint64_t key) const {
    if (polyMove == 0) return Move::none();

    int to_file   = (polyMove >> 0) & 7;
    int to_rank   = (polyMove >> 3) & 7;
    int from_file = (polyMove >> 6) & 7;
    int from_rank = (polyMove >> 9) & 7;
    int promo_idx = (polyMove >> 12) & 7;

    Square from = makeSquare(File(from_file), Rank(from_rank));
    Square to   = makeSquare(File(to_file),   Rank(to_rank));

    PieceType promo = NO_PIECE_TYPE;
    if (promo_idx > 0) {
        // Polyglot: 1=knight, 2=bishop, 3=rook, 4=queen
        static const PieceType promoMap[] = { NO_PIECE_TYPE, KNIGHT, BISHOP, ROOK, QUEEN };
        promo = (promo_idx <= 4) ? promoMap[promo_idx] : NO_PIECE_TYPE;
    }

    // Build a partial move (piece/captured info filled by make)
    uint32_t d = uint32_t(from) | (uint32_t(to) << 6);
    if (promo != NO_PIECE_TYPE) {
        d |= uint32_t(promo) << 20;
        d |= MF_PROMOTION;
    }
    return Move(d);
}
