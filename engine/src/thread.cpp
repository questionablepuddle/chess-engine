#include "thread.h"
#include <algorithm>

ThreadPool Threads;

void ThreadPool::resize(int n) {
    // For now: single-threaded search lives in Search::go()
    // Lazy SMP expansion goes here
    (void)n;
}

void ThreadPool::stop() {
    stopFlag_.store(true);
    Search::stop();
}

SearchResult ThreadPool::search(Position& pos, const SearchLimits& limits) {
    stopFlag_.store(false);
    return Search::go(pos, limits);
}
