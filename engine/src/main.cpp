#include "bitboard.h"
#include "position.h"
#include "search.h"
#include "uci.h"
#include "nnue.h"
#include <iostream>
#include <string>
#include <csignal>
#include <unistd.h>
#include <sys/resource.h>

static void crashHandler(int sig) {
    const char prefix[] = "ENGINE CRASH: signal ";
    char buf[64];
    unsigned pos = 0;
    for (const char* p = prefix; *p; ++p) buf[pos++] = *p;
    if (sig >= 10) buf[pos++] = static_cast<char>('0' + sig / 10);
    buf[pos++] = static_cast<char>('0' + sig % 10);
    buf[pos++] = '\n';
    write(STDERR_FILENO, buf, pos);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

int main(int argc, char* argv[]) {
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE,  crashHandler);

    // Raise the stack limit to 64 MB to guard against deep-search stack overflow.
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0) {
        const rlim_t target = 64UL * 1024 * 1024;
        if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < target) {
            rl.rlim_cur = (rl.rlim_max == RLIM_INFINITY || rl.rlim_max >= target)
                        ? target : rl.rlim_max;
            setrlimit(RLIMIT_STACK, &rl);
        }
    }

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

    // Start UCI loop — catch any exception so it surfaces on stderr rather
    // than producing a silent crash.
    try {
        UCI::loop();
    } catch (const std::exception& e) {
        std::cerr << "ENGINE EXCEPTION: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "ENGINE EXCEPTION: unknown\n";
        return 1;
    }
    return 0;
}
