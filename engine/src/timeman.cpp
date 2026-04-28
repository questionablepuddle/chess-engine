#include "timeman.h"
#include <algorithm>

void TimeManager::init(int wtime, int btime, int winc, int binc,
                       int movestogo, bool us_white, int movetime) {
    startTime = Clock::now();
    infinite  = false;

    if (movetime > 0) {
        movetimeMs = movetime - 50; // small overhead buffer
        optimalMs  = movetimeMs;
        maximumMs  = movetimeMs;
        return;
    }
    movetimeMs = 0;

    int myTime = us_white ? wtime : btime;
    int myInc  = us_white ? winc  : binc;

    if (myTime == 0 && myInc == 0) {
        // No time info — search briefly
        optimalMs = maximumMs = 5000;
        return;
    }

    int moves = (movestogo > 0) ? movestogo : 30;

    // Base time: split remaining time over expected moves
    int64_t base = myTime / moves + myInc * 3 / 4;

    // Soft limit: use up to this much normally
    optimalMs = std::clamp<int64_t>(base, 50, myTime / 2);

    // Hard limit: absolute maximum before stopping
    maximumMs = std::clamp<int64_t>(base * 4, optimalMs, myTime * 8 / 10);
}
