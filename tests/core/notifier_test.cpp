// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT (core idiom); docs/specs/core.md 2.4, test plan 7.3
#include "cg/core/notifier.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using cg::core::Notifier;
using cg::core::Subscription;

TEST(Notifier, NoSubscribersNotifyIsNoop) {
    const Notifier notifier;
    EXPECT_EQ(notifier.subscriberCount(), 0U);
    EXPECT_NO_THROW(notifier.notify());
}

TEST(Notifier, SingleSubscriberFires) {
    Notifier notifier;
    int calls = 0;
    const Subscription subscription = notifier.subscribe([&calls] { ++calls; });
    notifier.notify();
    notifier.notify();
    EXPECT_EQ(calls, 2);
    EXPECT_TRUE(subscription.isActive());
}

TEST(Notifier, MultipleSubscribersFireInSubscriptionOrder) {
    Notifier notifier;
    std::vector<int> order;
    const Subscription first = notifier.subscribe([&order] { order.push_back(1); });
    const Subscription second = notifier.subscribe([&order] { order.push_back(2); });
    const Subscription third = notifier.subscribe([&order] { order.push_back(3); });
    notifier.notify();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(Notifier, SubscriptionDestructorUnsubscribes) {
    Notifier notifier;
    int calls = 0;
    {
        const Subscription subscription = notifier.subscribe([&calls] { ++calls; });
        EXPECT_EQ(notifier.subscriberCount(), 1U);
    }
    EXPECT_EQ(notifier.subscriberCount(), 0U);
    notifier.notify();
    EXPECT_EQ(calls, 0);
}

TEST(Notifier, ResetUnsubscribes) {
    Notifier notifier;
    int calls = 0;
    Subscription subscription = notifier.subscribe([&calls] { ++calls; });
    subscription.reset();
    EXPECT_FALSE(subscription.isActive());
    notifier.notify();
    EXPECT_EQ(calls, 0);
}

TEST(Notifier, MoveTransfersSubscription) {
    Notifier notifier;
    int calls = 0;
    Subscription original = notifier.subscribe([&calls] { ++calls; });
    Subscription moved = std::move(original);
    EXPECT_FALSE(original.isActive()); // NOLINT(bugprone-use-after-move)
    EXPECT_TRUE(moved.isActive());
    notifier.notify();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(notifier.subscriberCount(), 1U);
}

TEST(Notifier, SlotMaySubscribeDuringNotify) {
    Notifier notifier;
    int lateCalls = 0;
    Subscription late;
    const Subscription first =
        notifier.subscribe([&] { late = notifier.subscribe([&lateCalls] { ++lateCalls; }); });
    notifier.notify();
    EXPECT_EQ(lateCalls, 0); // the slot added during a notify is not called by that notify
    EXPECT_TRUE(late.isActive());
}

TEST(Notifier, SlotMayUnsubscribeDuringNotify) {
    Notifier notifier;
    auto other = std::make_unique<Subscription>();
    const Subscription first = notifier.subscribe([&other] { other->reset(); });
    *other = notifier.subscribe([] {});
    EXPECT_NO_THROW(notifier.notify()); // legacy copies the list first: no crash is the contract
    EXPECT_EQ(notifier.subscriberCount(), 1U);
}

TEST(Notifier, NotifierDestroyedBeforeSubscriptionIsSafe) {
    Subscription subscription;
    {
        Notifier notifier;
        subscription = notifier.subscribe([] {});
        EXPECT_TRUE(subscription.isActive());
    }
    EXPECT_FALSE(subscription.isActive());
    EXPECT_NO_THROW(subscription.reset());
}

TEST(Notifier, NotifyDoesNotHoldLock) {
    Notifier notifier;
    std::size_t observed = 0;
    const Subscription subscription = notifier.subscribe([&] {
        const Subscription inner = notifier.subscribe([] {});
        observed = notifier.subscriberCount(); // deadlocks if notify() held the lock
    });
    notifier.notify();
    EXPECT_EQ(observed, 2U);
}

TEST(Notifier, ConcurrentSubscribeAndNotify) {
    Notifier notifier;
    std::atomic<bool> stop{false};
    std::atomic<bool> notifyThreadRunning{false};
    std::atomic<int> calls{0};
    const Subscription permanent = notifier.subscribe([&calls] { ++calls; });

    std::thread notifyThread([&notifier, &stop, &notifyThreadRunning] {
        while (!stop.load()) {
            notifier.notify();
            notifyThreadRunning.store(true, std::memory_order_release);
        }
    });
    // Test-side race (T-0026): under heavy CPU contention (limited cores, background load),
    // OS scheduling of a freshly-created thread can be delayed long enough that the main thread
    // finishes all 2000 subscribe/destroy iterations and sets `stop` before notifyThread ever
    // gets to run notify() once, leaving `calls` at 0 -- reproduced at ~49% (244/500) under ASan
    // with `taskset -c 0,1` plus 18 busy-loop processes; never under plain runs or under TSan
    // (TSan's slowdown gives the new thread enough relative time to start). Waiting here for
    // proof that notify() has actually fired at least once removes the assumption deterministically,
    // with no change to Notifier itself.
    while (!notifyThreadRunning.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (int loop = 0; loop < 2000; ++loop) {
        const Subscription temporary = notifier.subscribe([] {});
    }
    stop.store(true);
    notifyThread.join();

    EXPECT_EQ(notifier.subscriberCount(), 1U);
    EXPECT_GT(calls.load(), 0);
}
