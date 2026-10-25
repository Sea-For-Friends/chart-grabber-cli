// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT (core idiom); docs/specs/core.md 2.6, test plan 7.10
#include "cg/core/operation_notifier.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>

using cg::core::OperationNotifier;
using cg::core::Subscription;

namespace {

// Runs `body` (which triggers generationChanged_) on its own thread and waits with a timeout,
// so a regression that fires the notifier under its (non-recursive) mutex is reported as a test
// failure instead of hanging the whole test binary forever. On timeout the stuck thread is
// detached (leaked on purpose) rather than joined or left to call std::terminate.
template <class Body> bool runsToCompletionWithinTimeout(Body body) {
    std::promise<void> donePromise;
    std::future<void> doneFuture = donePromise.get_future();
    std::thread worker([&body, &donePromise] {
        body();
        donePromise.set_value();
    });
    const std::future_status status = doneFuture.wait_for(std::chrono::seconds(2));
    if (status == std::future_status::ready) {
        worker.join();
        return true;
    }
    worker.detach();
    return false;
}

} // namespace

TEST(OperationNotifier, BeginReturnsLiveToken) {
    OperationNotifier notifier;
    const std::stop_token token = notifier.begin();
    EXPECT_TRUE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(notifier.isCancelled());
}

TEST(OperationNotifier, BeginCancelsPreviousGeneration) {
    OperationNotifier notifier;
    const std::stop_token first = notifier.begin();
    const std::stop_token second = notifier.begin();
    EXPECT_TRUE(first.stop_requested());
    EXPECT_FALSE(second.stop_requested());
}

TEST(OperationNotifier, CancelStopsCurrent) {
    OperationNotifier notifier;
    const std::stop_token token = notifier.begin();
    notifier.cancel();
    EXPECT_TRUE(token.stop_requested());
}

TEST(OperationNotifier, IsCancelledReflectsCurrent) {
    OperationNotifier notifier;
    EXPECT_FALSE(notifier.isCancelled());
    notifier.cancel();
    EXPECT_TRUE(notifier.isCancelled());
    const std::stop_token fresh = notifier.begin();
    EXPECT_FALSE(notifier.isCancelled());
    EXPECT_FALSE(fresh.stop_requested());
}

TEST(OperationNotifier, StopCallbackFires) {
    OperationNotifier notifier;
    const std::stop_token token = notifier.begin();
    bool fired = false;
    const std::stop_callback callback(token, [&fired] { fired = true; });
    EXPECT_FALSE(fired);
    notifier.cancel();
    EXPECT_TRUE(fired);
}

TEST(OperationNotifier, GenerationChangedFiresOnBeginAndCancel) {
    OperationNotifier notifier;
    int calls = 0;
    const Subscription subscription = notifier.generationChanged().subscribe([&calls] { ++calls; });
    const std::stop_token token = notifier.begin();
    EXPECT_EQ(calls, 1);
    notifier.cancel();
    EXPECT_EQ(calls, 2);
}

// Finding 4 (T-0002A rework): spec §2.6/§3 requires generationChanged_ to fire *outside*
// mutex_. Mutex is a non-recursive std::shared_mutex (sync.h), so a listener that calls back
// into the notifier while the mutex is held deadlocks. These two tests exercise exactly the
// call-backs the finding names and would hang (reported as a timeout failure, not a crash) if
// the fire were ever moved back inside the lock.
TEST(OperationNotifier, GenerationChangedListenerMayCallIsCancelledWithoutDeadlock) {
    OperationNotifier notifier;
    std::atomic<bool> cancelledDuringBegin{true};
    const Subscription subscription =
        notifier.generationChanged().subscribe([&] { cancelledDuringBegin = notifier.isCancelled(); });

    const bool completed = runsToCompletionWithinTimeout([&] {
        const std::stop_token token = notifier.begin();
        static_cast<void>(token);
    });

    ASSERT_TRUE(completed) << "generationChanged() fired under the mutex: isCancelled() deadlocked";
    EXPECT_FALSE(cancelledDuringBegin); // a freshly begun generation is not cancelled
}

TEST(OperationNotifier, GenerationChangedListenerMayCallBeginReentrantlyWithoutDeadlock) {
    OperationNotifier notifier;
    std::atomic<int> reentrantCalls{0};
    const Subscription subscription = notifier.generationChanged().subscribe([&] {
        if (reentrantCalls.fetch_add(1) == 0) {
            const std::stop_token nested = notifier.begin();
            static_cast<void>(nested);
        }
    });

    const bool completed = runsToCompletionWithinTimeout([&] {
        const std::stop_token token = notifier.begin();
        static_cast<void>(token);
    });

    ASSERT_TRUE(completed) << "generationChanged() fired under the mutex: begin() deadlocked";
    EXPECT_GE(reentrantCalls.load(), 2); // outer begin() + the reentrant one it triggered
}

TEST(OperationNotifier, DestructorCancels) {
    std::stop_token token;
    {
        OperationNotifier notifier;
        token = notifier.begin();
        EXPECT_FALSE(token.stop_requested());
    }
    EXPECT_TRUE(token.stop_requested());
}
