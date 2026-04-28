#pragma once
#include <chrono>
#include <atomic>
#include <cstdint>

// ============================================================
// Time management
// ============================================================

using Clock    = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

inline int64_t elapsed(TimePoint start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - start).count();
}

struct TimeManager {
    TimePoint startTime;
    int64_t   optimalMs  = 0;   // normal time target
    int64_t   maximumMs  = 0;   // hard cutoff (panic mode)
    int64_t   movetimeMs = 0;   // fixed movetime (0 = not set)
    bool      infinite   = false;

    void init(int wtime, int btime, int winc, int binc,
              int movestogo, bool us_white, int movetime);

    bool shouldStop() const {
        if (infinite) return false;
        int64_t ms = ::elapsed(startTime);
        if (movetimeMs > 0) return ms >= movetimeMs;
        return ms >= maximumMs;
    }

    bool shouldStopOptimal() const {
        if (infinite || movetimeMs > 0) return false;
        return ::elapsed(startTime) >= optimalMs;
    }

    int64_t elapsed() const { return ::elapsed(startTime); }
};
