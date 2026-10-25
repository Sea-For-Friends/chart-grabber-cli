// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-026 (the application GC tick on a non-GUI thread). Ports
// Src/System/u_GarbageCollectorThread.pas; no legacy DUnit exists.
#include "cg/core/garbage_collector_thread.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

using cg::core::GarbageCollectorThread;
using namespace std::chrono_literals;

namespace {

struct TickCounter {
    std::mutex mutex;
    std::condition_variable cv;
    int ticks = 0;
    std::thread::id lastThread;

    bool waitForTicks(int n) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, 30s, [&] { return ticks >= n; });
    }
};

} // namespace

TEST(GarbageCollectorThread, TicksRepeatedlyOnAThreadOfItsOwn) {
    cg::core::Notifier notifier;
    TickCounter counter;
    const cg::core::Subscription sub = notifier.subscribe([&] {
        {
            const std::lock_guard lock(counter.mutex);
            ++counter.ticks;
            counter.lastThread = std::this_thread::get_id();
        }
        counter.cv.notify_all();
    });
    {
        GarbageCollectorThread thread(notifier, 1ms);
        ASSERT_TRUE(counter.waitForTicks(3));
    }
    const std::lock_guard lock(counter.mutex);
    EXPECT_NE(counter.lastThread, std::this_thread::get_id());
}

TEST(GarbageCollectorThread, DestructionWakesTheSleepAndWaitsForNothingMore) {
    cg::core::Notifier notifier;
    TickCounter counter;
    const cg::core::Subscription sub = notifier.subscribe([&] {
        {
            const std::lock_guard lock(counter.mutex);
            ++counter.ticks;
        }
        counter.cv.notify_all();
    });
    {
        GarbageCollectorThread thread(notifier, std::chrono::hours(1));
        ASSERT_TRUE(counter.waitForTicks(1)); // the first tick comes before the first sleep
    } // would hang for an hour if the sleep were not cancelable
    const std::lock_guard lock(counter.mutex);
    EXPECT_EQ(counter.ticks, 1);
}

// L-132: an exception escaping a tick ends the thread for good (legacy's try/except wraps the whole
// loop); the process survives, whatever the exception type (legacy's `except` catches everything).
TEST(GarbageCollectorThread, AnExceptionInATickEndsTheThreadForGoodWithoutTerminate) {
    for (const bool stdException : {true, false}) {
        cg::core::Notifier notifier;
        TickCounter counter;
        const cg::core::Subscription sub = notifier.subscribe([&] {
            {
                const std::lock_guard lock(counter.mutex);
                ++counter.ticks;
            }
            counter.cv.notify_all();
            if (stdException) {
                throw std::runtime_error("tick failed");
            }
            throw 42;
        });
        {
            GarbageCollectorThread thread(notifier, 1ms);
            ASSERT_TRUE(counter.waitForTicks(1));
            std::this_thread::sleep_for(50ms); // a regression ticks every 1 ms; this only widens that window
        } // joins: the thread function returned normally
        const std::lock_guard lock(counter.mutex);
        EXPECT_EQ(counter.ticks, 1);
    }
}
