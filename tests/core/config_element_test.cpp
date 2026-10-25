// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-199; docs/specs/core.md 2.16, test plan 7.13
#include "cg/core/config_element.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <latch>
#include <thread>
#include <utility>
#include <vector>

using cg::core::ConfigElement;
using cg::core::IConfigProviderPtr;
using cg::core::IConfigWriterPtr;

namespace {

// T-0093 part 2: rendezvous helpers. Spin-wait with a generous deadline (a failing run, not a
// passing one, pays the deadline) instead of sleeping a guessed number of milliseconds.
constexpr auto kRendezvousDeadline = std::chrono::seconds(10);

template <typename Pred> bool waitUntil(Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + kRendezvousDeadline;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

class TestElement : public ConfigElement {
public:
    void publicSetChanged() { setChanged(); }

    std::atomic<int> readConfigCalls{0};
    std::atomic<bool> lastProviderWasNull{false};
    std::function<void()> onBeforeChangeNotify;
    std::function<void()> onWriteConfig; // runs inside doWriteConfig, i.e. under the read lock

protected:
    void doReadConfig(const IConfigProviderPtr& src) override {
        readConfigCalls.fetch_add(1);
        lastProviderWasNull.store(src == nullptr);
        setChanged();
    }
    void doWriteConfig(const IConfigWriterPtr&) const override {
        if (onWriteConfig) {
            onWriteConfig();
        }
    }
    void doBeforeChangeNotify() override {
        if (onBeforeChangeNotify) {
            onBeforeChangeNotify();
        }
    }
};

} // namespace

TEST(ConfigElement, ReadGuardAllowsConcurrentReaders) {
    TestElement elem;
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};
    const auto worker = [&] {
        const auto guard = elem.readLock();
        const int now = concurrent.fetch_add(1) + 1;
        int prevMax = maxConcurrent.load();
        while (now > prevMax && !maxConcurrent.compare_exchange_weak(prevMax, now)) {
        }
        // Rendezvous: stay inside the guard until the other reader is inside too. Under a
        // (wrongly) exclusive lock the second reader can never enter, the wait times out and
        // maxConcurrent stays 1.
        waitUntil([&] { return maxConcurrent.load() >= 2; }); // monotonic: the peer may already have left
        concurrent.fetch_sub(1);
    };
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    EXPECT_EQ(maxConcurrent.load(), 2);
}

TEST(ConfigElement, WriteGuardExcludesReaders) {
    TestElement elem;
    std::atomic<bool> writerShouldRelease{false};
    std::atomic<bool> readerAcquired{false};
    std::atomic<bool> writerHolds{false};
    std::atomic<bool> readerStarted{false};
    std::thread writer([&] {
        const auto guard = elem.writeLock();
        writerHolds.store(true);
        while (!writerShouldRelease.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    ASSERT_TRUE(waitUntil([&] { return writerHolds.load(); })) << "writer never took the lock";
    std::thread reader([&] {
        readerStarted.store(true);
        const auto guard = elem.readLock();
        readerAcquired.store(true);
    });
    ASSERT_TRUE(waitUntil([&] { return readerStarted.load(); }));
    // Negative check: a window in which the reader would have got in if it could. Timing can
    // only make this pass wrongly (never fail), so it is flake-free.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(readerAcquired.load()); // still excluded
    writerShouldRelease.store(true);
    writer.join();
    reader.join();
    EXPECT_TRUE(readerAcquired.load());
}

TEST(ConfigElement, SetChangedWithoutGuardDoesNotNotify) {
    TestElement elem;
    int notifications = 0;
    const auto sub = elem.changed().subscribe([&] { ++notifications; });
    elem.publicSetChanged();
    EXPECT_EQ(notifications, 0);
}

TEST(ConfigElement, SingleWriteNotifiesOnce) {
    TestElement elem;
    int notifications = 0;
    const auto sub = elem.changed().subscribe([&] { ++notifications; });
    {
        const auto guard = elem.writeLock();
        elem.publicSetChanged();
    }
    EXPECT_EQ(notifications, 1);
}

TEST(ConfigElement, NestedWritesNotifyOnce) {
    TestElement elem;
    int notifications = 0;
    const auto sub = elem.changed().subscribe([&] { ++notifications; });
    std::atomic<bool> firstShouldRelease{false};
    std::atomic<bool> firstHolds{false};
    std::atomic<bool> secondStarted{false};
    std::thread first([&] {
        const auto guard = elem.writeLock();
        elem.publicSetChanged();
        firstHolds.store(true);
        while (!firstShouldRelease.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    ASSERT_TRUE(waitUntil([&] { return firstHolds.load(); })) << "`first` never took the lock";
    std::thread second([&] {
        secondStarted.store(true);
        const auto guard = elem.writeLock(); // blocks; bumps the counter before it does
        elem.publicSetChanged();
    });
    ASSERT_TRUE(waitUntil([&] { return secondStarted.load(); }));
    // `second` bumps the (private) batch counter inside writeLock() just before it blocks; that is
    // not observable from a test without a product seam, so this short settle window stays
    // (T-0093 part 2: left on purpose; it is now only the gap from "thread running" to "counter
    // bumped", a handful of instructions, not thread start-up).
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    firstShouldRelease.store(true);
    first.join();
    second.join();
    EXPECT_EQ(notifications, 1); // coalesced into exactly one notification, not two
}

TEST(ConfigElement, NoChangeNoNotification) {
    TestElement elem;
    int notifications = 0;
    const auto sub = elem.changed().subscribe([&] { ++notifications; });
    {
        const auto guard = elem.writeLock(); // no setChanged() call
    }
    EXPECT_EQ(notifications, 0);
}

TEST(ConfigElement, NotificationFiresAfterUnlock) {
    TestElement elem;
    const auto sub = elem.changed().subscribe([&] {
        const auto guard = elem.readLock(); // deadlocks (and the test hangs) if fired under the write lock
    });
    {
        const auto guard = elem.writeLock();
        elem.publicSetChanged();
    }
    SUCCEED();
}

TEST(ConfigElement, FiringOrderIsBeforeChangedAfter) {
    TestElement elem;
    std::vector<int> order;
    const auto s1 = elem.beforeChanged().subscribe([&] { order.push_back(0); });
    const auto s2 = elem.changed().subscribe([&] { order.push_back(1); });
    const auto s3 = elem.afterChanged().subscribe([&] { order.push_back(2); });
    {
        const auto guard = elem.writeLock();
        elem.publicSetChanged();
    }
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

TEST(ConfigElement, DoBeforeChangeNotifyRunsUnderTheWriteLock) {
    TestElement elem;
    std::atomic<bool> readerAcquired{false};
    std::atomic<bool> readerStarted{false};
    std::thread reader; // started from the hook below, joined before this test returns
    elem.onBeforeChangeNotify = [&] {
        reader = std::thread([&] {
            readerStarted.store(true);
            const auto guard = elem.readLock();
            readerAcquired.store(true);
        });
        EXPECT_TRUE(waitUntil([&] { return readerStarted.load(); }));
        // Negative check, window only makes it pass wrongly, never fail.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        EXPECT_FALSE(readerAcquired.load()); // proves the write lock is still held here
    };
    {
        const auto guard = elem.writeLock();
        elem.publicSetChanged();
    }
    reader.join(); // the lock is free now; the reader completes promptly
    EXPECT_TRUE(readerAcquired.load());
}

TEST(ConfigElement, ReadConfigTakesWriteLockAndBatches) {
    TestElement elem;
    int notifications = 0;
    const auto sub = elem.changed().subscribe([&] { ++notifications; });
    elem.readConfig(nullptr);
    EXPECT_EQ(elem.readConfigCalls.load(), 1);
    EXPECT_EQ(notifications, 1);
}

TEST(ConfigElement, WriteConfigTakesReadLock) {
    TestElement elem;
    std::atomic<int> inside{0};
    std::atomic<bool> bothInside{false};
    // Rendezvous inside doWriteConfig: each call waits for the other. Under an exclusive lock the
    // second can never enter, the wait times out and bothInside stays false.
    elem.onWriteConfig = [&] {
        inside.fetch_add(1);
        if (waitUntil([&] { return inside.load() == 2; })) {
            bothInside.store(true);
        }
    };
    std::thread t1([&] { elem.writeConfig(nullptr); });
    std::thread t2([&] { elem.writeConfig(nullptr); });
    t1.join();
    t2.join();
    // Both ran concurrently under the shared (read) lock.
    EXPECT_TRUE(bothInside.load());
}

TEST(ConfigElement, ReadConfigWithNullProviderIsSafe) {
    TestElement elem;
    elem.readConfig(nullptr);
    EXPECT_TRUE(elem.lastProviderWasNull.load());
}

// T-0094: legacy fires DoChangeNotify -> DoBeforeChangeNotify at the moment the stop-notify counter
// reaches 0 (u_ConfigDataElementBase.pas:163-173, u_ChangeableBase.pas:87-95), and a child change
// (OnItemChange, u_ConfigDataElementComplexBase.pas:352-361) that sets the flag after that gets its
// own full notification including the hook. So a child change arriving between a writer's pre-pass
// and its finishWrite must never be notified with a derived snapshot that predates it.
TEST(ConfigElement, ChildChangeBetweenPrePassAndFinishIsNotifiedWithFreshSnapshot) {
    TestElement elem;
    std::atomic<int> value{0};
    std::atomic<int> snapshot{-1};
    std::atomic<int> hookCalls{0};
    std::latch hookDone(1);
    std::latch childDone(1);
    elem.onBeforeChangeNotify = [&] {
        snapshot.store(value.load()); // the "derived state" rebuilt from the children
        if (hookCalls.fetch_add(1) == 0) {
            hookDone.count_down();
            childDone.wait(); // hold the writer between its pre-pass and its finishWrite
        }
    };
    int snapshotAtChanged = -1;
    const auto sub = elem.changed().subscribe([&] { snapshotAtChanged = snapshot.load(); });
    std::thread writer([&] {
        const auto guard = elem.writeLock();
        value.store(1);
        elem.publicSetChanged();
    });
    hookDone.wait();
    {
        // what a listened child's changed() slot does (config_element_complex.cpp addChild)
        ConfigElement::BatchToken token(elem);
        value.store(2);
        elem.publicSetChanged();
    }
    childDone.count_down();
    writer.join();
    EXPECT_EQ(snapshotAtChanged, 2);
}

// T-0094 part 2: legacy rebuilds under LockWrite (u_ConfigDataElementBase.pas:221-238), so a hook run
// from finishWrite is serialised with writers. Order reproduced: W1 pre-passes, a child token clears
// the record, W1's finishWrite starts hook U which reads the OLD value, W2 writes 2 and pre-passes
// fresh, U stores last. Without the lock U's stale store wins permanently. The 300 ms wait only
// decides whether the buggy order is exercised; the assertion holds on any timing once fixed.
TEST(ConfigElement, UnlockedHookRunCannotPublishStateOlderThanALaterWriter) {
    TestElement elem;
    std::atomic<int> value{0};
    std::atomic<int> snapshot{-1};
    std::atomic<int> hookCalls{0};
    std::promise<void> uStarted;
    std::promise<void> w2Finished;
    std::shared_future<void> w2FinishedF = w2Finished.get_future().share();
    elem.onBeforeChangeNotify = [&] {
        const int call = hookCalls.fetch_add(1) + 1;
        const int seen = value.load();
        if (call == 1) {                           // W1's pre-pass, under its lock
            ConfigElement::BatchToken token(elem); // a child's change clears the hook record
            elem.publicSetChanged();
        } else if (call == 2) { // U: the hook run from W1's finishWrite
            uStarted.set_value();
            w2FinishedF.wait_for(std::chrono::milliseconds(300));
        }
        snapshot.store(seen);
    };
    std::thread w1([&] {
        const auto guard = elem.writeLock();
        value.store(1);
        elem.publicSetChanged();
    });
    uStarted.get_future().wait();
    std::thread w2([&] {
        {
            const auto guard = elem.writeLock();
            value.store(2);
            elem.publicSetChanged();
        }
        w2Finished.set_value();
    });
    w1.join();
    w2.join();
    EXPECT_EQ(snapshot.load(), 2);
}
