#pragma once
#include "search.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

// ============================================================
// Thread pool for Lazy SMP
// ============================================================

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool() { resize(0); }

    void resize(int n);
    int  size()  const { return threads_.size(); }

    // Start all threads searching the given position
    SearchResult search(Position& pos, const SearchLimits& limits);
    void stop();

private:
    struct WorkerThread {
        SearchThread  searcher;
        std::thread   thread;
        std::atomic<bool> idle{true};

        // Synchronization
        std::mutex              mutex;
        std::condition_variable cv;
        bool                    working = false;
    };

    std::vector<std::unique_ptr<WorkerThread>> workers_;
    std::vector<std::thread> threads_;
    std::atomic<bool> stopFlag_{false};
};

extern ThreadPool Threads;
