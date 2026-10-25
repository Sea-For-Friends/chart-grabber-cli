// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// PascalScriptGlobal (u_PascalScriptGlobal.pas)
#include "cg/maptype/pascal_script_global.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using cg::maptype::PascalScriptGlobal;
using cg::script::Value;

TEST(PascalScriptGlobal, RawAndLockedMethodsShareTheStoreAndAbsentIdIsEmpty) {
    PascalScriptGlobal g;
    EXPECT_FALSE(g.exists(7));
    EXPECT_FALSE(g.existsTs(7));
    EXPECT_EQ(g.getVar(7).kind(), cg::script::ValueKind::Empty);
    EXPECT_EQ(g.getVarTs(7).kind(), cg::script::ValueKind::Empty);

    g.setVar(7, Value::fromInt(5));
    EXPECT_TRUE(g.existsTs(7));
    EXPECT_EQ(g.getVarTs(7).toInt().value(), 5);
    g.setVarTs(7, Value::fromStr("x")); // an existing id is overwritten
    EXPECT_EQ(g.getVar(7).toStr(), QByteArray("x"));
    EXPECT_FALSE(g.exists(8));
}

TEST(PascalScriptGlobal, LockedMethodsWaitForTheWriteLockAndShareTheReadLock) {
    PascalScriptGlobal g;
    g.setVar(1, Value::fromInt(1));

    g.lock();
    auto blocked = std::async(std::launch::async, [&g] { return g.getVarTs(1).toInt().value(); });
    EXPECT_EQ(blocked.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    g.unlock();
    EXPECT_EQ(blocked.get(), 1);

    g.lockRead();
    auto shared = std::async(std::launch::async, [&g] { return g.existsTs(1); });
    EXPECT_EQ(shared.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_TRUE(shared.get());
    auto writer = std::async(std::launch::async, [&g] { g.setVarTs(1, Value::fromInt(2)); });
    EXPECT_EQ(writer.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    g.unlockRead();
    writer.get();
    EXPECT_EQ(g.getVar(1).toInt().value(), 2);
}

// Two threads do a read-modify-write of one variable inside lock()/unlock(); each runs until it
// has seen the other's increments, so the overlap is asserted, not hoped for.
TEST(PascalScriptGlobal, RawReadModifyWriteUnderTheLockIsExactWhileTheThreadsOverlap) {
    constexpr int kRepeats = 1000;
    constexpr int kMinSteps = 20;
    for (int rep = 0; rep < kRepeats; ++rep) {
        PascalScriptGlobal g;
        g.setVarTs(1, Value::fromInt(0));
        std::atomic<int> ready{0};
        std::atomic<int> steps[2] = {0, 0};
        std::atomic<bool> sawOther[2] = {false, false};

        auto body = [&](int self) {
            ready.fetch_add(1);
            while (ready.load() < 2) {
            }
            qint64 last = -1;
            int mine = 0;
            for (int i = 0; i < 10'000'000 && (mine < kMinSteps || !sawOther[self].load()); ++i) {
                g.lock();
                const qint64 v = g.getVar(1).toInt().value();
                if (last >= 0 && v != last + 1 && steps[1 - self].load() > 0) {
                    sawOther[self] = true; // the other thread wrote between my two critical sections
                }
                g.setVar(1, Value::fromInt(v + 1));
                last = v + 1;
                g.unlock();
                ++mine;
                steps[self] = mine;
            }
        };
        std::thread a(body, 0);
        std::thread b(body, 1);
        a.join();
        b.join();
        ASSERT_TRUE(sawOther[0] && sawOther[1]) << "no overlap in repeat " << rep;
        ASSERT_EQ(g.getVarTs(1).toInt().value(), steps[0].load() + steps[1].load()) << "repeat " << rep;
    }
}

// The raw methods take no script lock, but the container is guarded (deviation): two threads
// calling them concurrently stay memory-safe and no key is lost. Only that is asserted.
TEST(PascalScriptGlobal, RawMethodsWithoutLockAreMemorySafeWhileTheThreadsOverlap) {
    constexpr int kRepeats = 1000;
    constexpr int kKeys = 50;
    for (int rep = 0; rep < kRepeats; ++rep) {
        PascalScriptGlobal g;
        std::atomic<int> ready{0};
        std::atomic<int> progress[2] = {0, 0};
        std::atomic<bool> overlapped[2] = {false, false};
        // A thread leaves only after both have seen the other still working, so the overlap is
        // asserted, not hoped for.
        auto body = [&](int self) {
            const int other = 1 - self;
            ready.fetch_add(1);
            while (ready.load() < 2) {
            }
            for (int i = 0; i < 10'000'000 && (i < kKeys || !(overlapped[0].load() && overlapped[1].load()));
                 ++i) {
                const int key = i % kKeys;
                g.setVar(self * 1000 + key, Value::fromInt(i));
                (void)g.exists(key);
                (void)g.getVar(other * 1000 + key);
                progress[self] = 1;
                if (progress[other].load() > 0) {
                    overlapped[self] = true; // the other thread is running at the same time
                }
            }
        };
        std::thread a(body, 0);
        std::thread b(body, 1);
        a.join();
        b.join();
        ASSERT_TRUE(overlapped[0] && overlapped[1]) << "no overlap in repeat " << rep;
        for (int self = 0; self < 2; ++self) {
            for (int i = 0; i < kKeys; ++i) {
                ASSERT_TRUE(g.exists(self * 1000 + i));
            }
        }
    }
}
