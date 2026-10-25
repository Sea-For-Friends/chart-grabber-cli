// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: T-0225 (NEW code, no FEAT id): ADR-0014 point 5, the script-time limit of the JavaScript builders.

#include "cg/maptype/js_script_watchdog.h"

#include <QJSEngine>
#include <QJSValue>

#include <gtest/gtest.h>

#include <chrono>
#include <latch>
#include <memory>
#include <thread>

#include "maptype_test_util.h"

using cg::maptype::JsScriptWatchdog;
using namespace std::chrono_literals;

namespace {

constexpr auto kLimit = 200ms;

class JsScriptWatchdogTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cg::maptype::test::ensureGuiApp(); }

    // Runs an endless loop on a worker thread (the engine belongs to it) until the watchdog stops it; returns
    // the time it ran and says whether the engine reported an interruption.
    struct LoopRun {
        std::chrono::milliseconds elapsed{0};
        bool interrupted = false;
        bool nextRunWorks = false;
    };
    LoopRun runLoop(JsScriptWatchdog::Entry& entry) {
        LoopRun out;
        std::thread worker([&] {
            QJSEngine engine;
            entry.arm(&engine, kLimit);
            const auto start = JsScriptWatchdog::Clock::now();
            const QJSValue r = engine.evaluate(QStringLiteral("for (;;) {}"));
            out.elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(JsScriptWatchdog::Clock::now() - start);
            out.interrupted = r.isError() && entry.disarm();
            engine.setInterrupted(false);
            out.nextRunWorks = engine.evaluate(QStringLiteral("1 + 1")).toInt() == 2;
        });
        worker.join();
        return out;
    }
};

TEST_F(JsScriptWatchdogTest, EndlessLoopIsInterruptedNearTheLimitAndTheEngineRunsAgain) {
    JsScriptWatchdog watchdog;
    const auto entry = watchdog.add();
    const LoopRun run = runLoop(*entry);
    EXPECT_TRUE(run.interrupted);
    EXPECT_GE(run.elapsed, 190ms);
    EXPECT_LE(run.elapsed, 700ms);
    EXPECT_TRUE(run.nextRunWorks);
}

// The deadline moves by the paused time: a pause longer than the limit does not fire, and what is left of the
// limit after resume() still applies (the loop below is stopped about kLimit after the resume).
TEST_F(JsScriptWatchdogTest, PausedTimeIsNotCountedButTheRestOfTheLimitIs) {
    JsScriptWatchdog watchdog;
    const auto entry = watchdog.add();
    JsScriptWatchdog::Clock::time_point resumedAt;
    std::chrono::milliseconds firedAfterResume{0};
    std::thread worker([&] {
        QJSEngine engine;
        entry->arm(&engine, kLimit);
        entry->pause();
        std::this_thread::sleep_for(3 * kLimit); // a slow http reply
        const bool interruptedWhilePaused = engine.isInterrupted();
        entry->resume();
        resumedAt = JsScriptWatchdog::Clock::now();
        const QJSValue r = engine.evaluate(QStringLiteral("for (;;) {}"));
        firedAfterResume =
            std::chrono::duration_cast<std::chrono::milliseconds>(JsScriptWatchdog::Clock::now() - resumedAt);
        EXPECT_TRUE(r.isError());
        EXPECT_FALSE(interruptedWhilePaused);
        EXPECT_TRUE(entry->disarm());
    });
    worker.join();
    EXPECT_GE(firedAfterResume, 150ms);
    EXPECT_LE(firedAfterResume, 700ms);
}

TEST_F(JsScriptWatchdogTest, NestedPausesResumeOnlyWhenTheOutermostEnds) {
    JsScriptWatchdog watchdog;
    const auto entry = watchdog.add();
    std::thread worker([&] {
        QJSEngine engine;
        entry->arm(&engine, kLimit);
        entry->pause();
        entry->pause();
        entry->resume(); // one pause is still open
        std::this_thread::sleep_for(3 * kLimit);
        EXPECT_FALSE(engine.isInterrupted());
        entry->resume();
        EXPECT_FALSE(entry->disarm());
    });
    worker.join();
}

TEST_F(JsScriptWatchdogTest, DisarmedEngineIsNeverTouched) {
    JsScriptWatchdog watchdog;
    const auto entry = watchdog.add();
    std::thread worker([&] {
        QJSEngine engine;
        entry->arm(&engine, 50ms);
        EXPECT_FALSE(entry->disarm());
        std::this_thread::sleep_for(4 * kLimit); // well past the old deadline and several ticks
        EXPECT_FALSE(engine.isInterrupted());
    });
    worker.join();
}

TEST_F(JsScriptWatchdogTest, OnlyTheEngineOverItsLimitIsInterrupted) {
    JsScriptWatchdog watchdog;
    const auto loopEntry = watchdog.add();
    const auto idleEntry = watchdog.add();
    std::latch idleArmed(1);
    std::latch loopDone(1);
    bool idleInterrupted = true;
    bool idleFired = true;
    std::thread idle([&] {
        QJSEngine engine;
        idleEntry->arm(&engine, 60s); // armed the whole time the other engine loops
        idleArmed.count_down();
        loopDone.wait();
        idleInterrupted = engine.isInterrupted();
        idleFired = idleEntry->disarm();
    });
    idleArmed.wait();
    const LoopRun run = runLoop(*loopEntry);
    loopDone.count_down();
    idle.join();
    EXPECT_TRUE(run.interrupted);
    EXPECT_FALSE(idleInterrupted);
    EXPECT_FALSE(idleFired);
}

} // namespace
