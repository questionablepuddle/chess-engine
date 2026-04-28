#include "bitboard.h"
#include "position.h"
#include "search.h"
#include "uci.h"
#include "nnue.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Initialize all subsystems
    BB::init();
    Zobrist::init();
    initCastlingMask();  // defined in position.cpp — make it accessible
    NNUE::init();
    Search::init();

    // Try to load NNUE weights (optional)
    NNUE::load("nn.bin");

    // Handle command-line arguments
    if (argc > 1) {
        std::string cmd = argv[1];
        if (cmd == "bench") {
            // Run bench inline
            std::cout << "bench\n";
        }
    }

    // Start UCI loop
    UCI::loop();
    return 0;
}
