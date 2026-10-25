// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034. Ports u_BackgroundTask.pas (+ the Start/Terminate half of
// u_InterfacedThread.pas); no DUnit suite exists. Synchronisation is by latches and semaphores
// only: no sleeps, no timing assumptions.
#include "cg/core/background_task.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <vector>

using cg::core::BackgroundTask;
using cg::core::ThreadConfig;
using cg::core::ThreadPriority;

namespace {

using Gate = std::binary_semaphore;

std::shared_ptr<ThreadConfig> config() {
    return std::make_shared<ThreadConfig>(ThreadPriority::Normal);
}

std::unique_ptr<BackgroundTask> makeTask(cg::core::BackgroundTaskExecuteEvent fn,
                                         std::stop_token closing = {}) {
    return std::make_unique<BackgroundTask>(std::move(closing), std::move(fn), config(),
                                            QStringLiteral("test"));
}

} // namespace

TEST(BackgroundTask, StartExecuteRunsTheProcedureOncePerRequest) {
    std::atomic<int> calls{0};
    Gate done(0);
    auto task = makeTask([&](std::stop_token) {
        ++calls;
        done.release();
    });
    task->start();
    task->startExecute();
    done.acquire();
    task->startExecute();
    done.acquire();
    task.reset();
    EXPECT_EQ(calls, 2);
}

TEST(BackgroundTask, RequestBeforeStartRunsOnceAfterStart) {
    std::atomic<int> calls{0};
    Gate done(0);
    auto task = makeTask([&](std::stop_token) {
        ++calls;
        done.release();
    });
    task->startExecute();
    task->start();
    done.acquire();
    task.reset();
    EXPECT_EQ(calls, 1);
}

TEST(BackgroundTask, RequestsDuringARunCollapseIntoOneRerunAndDoNotCancelIt) {
    std::atomic<int> calls{0};
    std::vector<bool> cancelledAtEnd;
    Gate entered(0);
    Gate release(0);
    Gate secondDone(0);
    auto task = makeTask([&](std::stop_token token) {
        const int n = ++calls;
        if (n == 1) {
            entered.release();
            release.acquire();
        }
        cancelledAtEnd.push_back(token.stop_requested());
        if (n == 2) {
            secondDone.release();
        }
    });
    task->start();
    task->startExecute();
    entered.acquire();
    task->startExecute();
    task->startExecute();
    task->startExecute();
    release.release();
    secondDone.acquire();
    task.reset(); // joins: no third call can be pending
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(cancelledAtEnd, (std::vector<bool>{false, false}));
}

// "No re-run" is asserted after the destructor joined; a wrongly pending re-run could lose that race.
TEST(BackgroundTask, StopExecuteCancelsTheRunningCallWithoutRerun) {
    std::atomic<int> calls{0};
    std::atomic<bool> sawCancel{false};
    Gate entered(0);
    Gate finished(0);
    auto task = makeTask([&](std::stop_token token) {
        ++calls;
        Gate cancelled(0);
        const std::stop_callback cb(token, [&cancelled] { cancelled.release(); });
        entered.release();
        cancelled.acquire();
        sawCancel = token.stop_requested();
        finished.release();
    });
    task->start();
    task->startExecute();
    entered.acquire();
    task->stopExecute();
    finished.acquire();
    task.reset();
    EXPECT_TRUE(sawCancel);
    EXPECT_EQ(calls, 1);
}

TEST(BackgroundTask, StopThenStartDuringARunRestartsWithAFreshToken) {
    std::atomic<int> calls{0};
    std::vector<bool> cancelledOnEntry;
    std::atomic<bool> firstCancelled{false};
    Gate entered(0);
    Gate secondDone(0);
    auto task = makeTask([&](std::stop_token token) {
        const int n = ++calls;
        if (n == 1) {
            Gate cancelled(0);
            const std::stop_callback cb(token, [&cancelled] { cancelled.release(); });
            entered.release();
            cancelled.acquire();
            firstCancelled = token.stop_requested();
            return;
        }
        cancelledOnEntry.push_back(token.stop_requested());
        secondDone.release();
    });
    task->start();
    task->startExecute();
    entered.acquire();
    task->stopExecute();
    task->startExecute(); // the usual "restart" of TMarkSystemImplChangeable
    secondDone.acquire();
    task.reset();
    EXPECT_TRUE(firstCancelled);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(cancelledOnEntry, (std::vector<bool>{false}));
}

TEST(BackgroundTask, DestructionWhileRunningCancelsAndWaitsForTheCall) {
    std::atomic<bool> returned{false};
    Gate entered(0);
    auto task = makeTask([&](std::stop_token token) {
        Gate cancelled(0);
        const std::stop_callback cb(token, [&cancelled] { cancelled.release(); });
        entered.release();
        cancelled.acquire();
        returned = true;
    });
    task->start();
    task->startExecute();
    entered.acquire();
    task.reset();
    EXPECT_TRUE(returned); // the destructor joined the worker
}

TEST(BackgroundTask, ExceptionInTheProcedureIsLoggedAndTheTaskKeepsWorking) {
    std::atomic<int> calls{0};
    Gate threw(0);
    Gate done(0);
    auto task = makeTask([&](std::stop_token) {
        if (++calls == 1) {
            threw.release();
            throw std::runtime_error("boom");
        }
        done.release();
    });
    task->start();
    task->startExecute();
    threw.acquire();
    task->startExecute(); // whether it lands before or after the catch, the worker is still alive
    done.acquire();
    task.reset();
    EXPECT_EQ(calls, 2);
}

TEST(BackgroundTask, RequestAfterTerminateOfARunningThreadStartsNothing) {
    std::atomic<int> calls{0};
    Gate done(0);
    auto task = makeTask([&](std::stop_token) {
        ++calls;
        done.release();
    });
    task->start();
    task->startExecute();
    done.acquire();
    task->terminate();
    task->startExecute();
    task.reset();
    EXPECT_EQ(calls, 1);
}

TEST(BackgroundTask, PendingRequestSurvivesStopAndRunsWithAFreshToken) {
    std::atomic<int> calls{0};
    std::atomic<bool> firstCancelled{false};
    std::vector<bool> secondCancelled;
    Gate entered(0);
    Gate secondDone(0);
    auto task = makeTask([&](std::stop_token token) {
        if (++calls == 1) {
            Gate cancelled(0);
            const std::stop_callback cb(token, [&cancelled] { cancelled.release(); });
            entered.release();
            cancelled.acquire();
            firstCancelled = true;
            return;
        }
        secondCancelled.push_back(token.stop_requested());
        secondDone.release();
    });
    task->start();
    task->startExecute();
    entered.acquire();
    task->startExecute(); // pending request ...
    task->stopExecute();  // ... survives the stop: run 1 is cancelled AND run 2 happens
    secondDone.acquire();
    task.reset();
    EXPECT_TRUE(firstCancelled);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(secondCancelled, (std::vector<bool>{false}));
}

TEST(BackgroundTask, TerminateBeforeStartNeverRuns) {
    std::atomic<int> calls{0};
    auto task = makeTask([&](std::stop_token) { ++calls; });
    task->terminate();
    task->start();
    task->startExecute();
    task.reset();
    EXPECT_EQ(calls, 0);
}

TEST(BackgroundTask, AppClosingCancelsTheRunAndEndsTheThread) {
    std::stop_source closing;
    std::atomic<bool> sawCancel{false};
    std::atomic<int> calls{0};
    Gate entered(0);
    Gate finished(0);
    auto task = makeTask(
        [&](std::stop_token token) {
            ++calls;
            Gate cancelled(0);
            const std::stop_callback cb(token, [&cancelled] { cancelled.release(); });
            entered.release();
            cancelled.acquire();
            sawCancel = true;
            finished.release();
        },
        closing.get_token());
    task->start();
    task->startExecute();
    entered.acquire();
    closing.request_stop();
    finished.acquire();
    task->startExecute(); // after closing nothing runs any more
    task.reset();
    EXPECT_TRUE(sawCancel);
    EXPECT_EQ(calls, 1); // D-58: the request after closing starts nothing
}

TEST(BackgroundTask, AlreadyClosingApplicationTerminatesAtConstruction) {
    std::stop_source closing;
    closing.request_stop();
    std::atomic<int> calls{0};
    auto task = makeTask([&](std::stop_token) { ++calls; }, closing.get_token());
    task->start();
    task->startExecute();
    task.reset();
    EXPECT_EQ(calls, 0);
}
