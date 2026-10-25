// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for core::Mutex (src/cg/core/sync.h, platform/shared_mutex.h; new code, T-0335). On Windows the MinGW
// std::shared_mutex let two writers in at once: these tests fail with it.
#include "cg/core/sync.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

using cg::core::Mutex;
using cg::core::ReadLock;
using cg::core::WriteLock;

TEST(CoreMutex, TwoWritersNeverOverlap) {
    constexpr int kRounds = 4000;
    constexpr int kThreads = 8;
    int violations = 0;
    for (int round = 0; round < kRounds; ++round) {
        Mutex mutex;
        int inside = 0; // guarded by `mutex`
        int maxInside = 0;
        std::barrier start(kThreads);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                start.arrive_and_wait();
                const WriteLock guard(mutex);
                maxInside = std::max(maxInside, ++inside);
                std::this_thread::yield();
                --inside;
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        violations += maxInside == 1 ? 0 : 1;
    }
    EXPECT_EQ(violations, 0);
}

TEST(CoreMutex, ReadersShareAndAWriterExcludesEveryone) {
    constexpr int kRounds = 1500;
    std::atomic<int> writerSawReader{0};
    std::atomic<int> readerSawWriter{0};
    int readersTogether = 0;
    for (int round = 0; round < kRounds; ++round) {
        Mutex mutex;
        std::atomic<int> readers{0};
        std::atomic<int> writers{0};
        std::atomic<int> maxReaders{0};
        std::barrier start(5);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&] {
                start.arrive_and_wait();
                const ReadLock guard(mutex);
                const int now = ++readers;
                int seen = maxReaders.load();
                while (seen < now && !maxReaders.compare_exchange_weak(seen, now)) {
                }
                if (writers.load() != 0) {
                    ++readerSawWriter;
                }
                std::this_thread::yield();
                --readers;
            });
        }
        threads.emplace_back([&] {
            start.arrive_and_wait();
            const WriteLock guard(mutex);
            ++writers;
            if (readers.load() != 0) {
                ++writerSawReader;
            }
            std::this_thread::yield();
            --writers;
        });
        for (auto& thread : threads) {
            thread.join();
        }
        readersTogether = std::max(readersTogether, maxReaders.load());
    }
    EXPECT_EQ(writerSawReader.load(), 0);
    EXPECT_EQ(readerSawWriter.load(), 0);
    EXPECT_GE(readersTogether, 2); // shared locks really are shared
}

TEST(CoreMutex, TryLockFailsWhileHeldAndSucceedsAfterwards) {
    Mutex mutex;
    {
        const WriteLock guard(mutex);
        bool otherGotShared = true;
        bool otherGotExclusive = true;
        std::thread([&] {
            otherGotShared = mutex.try_lock_shared();
            otherGotExclusive = mutex.try_lock();
        }).join();
        EXPECT_FALSE(otherGotShared);
        EXPECT_FALSE(otherGotExclusive);
    }
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
    EXPECT_TRUE(mutex.try_lock_shared());
    EXPECT_TRUE(mutex.try_lock_shared());
    mutex.unlock_shared();
    mutex.unlock_shared();
}
