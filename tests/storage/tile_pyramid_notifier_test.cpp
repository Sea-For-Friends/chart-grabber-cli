// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-035. Ports i_NotifierTilePyramidUpdate.pas, u_NotifierTilePyramidUpdate.pas
// (the collapsed subscribe-all API); docs/specs/storage.md §2.8 (:531-549)
#include "cg/geo/geo_types.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <latch>
#include <thread>
#include <vector>

using cg::geo::PointI;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileUpdate;

namespace {

TEST(TilePyramidNotifier, SubscriberReceivesThePayloadPassedToNotify) {
    TilePyramidNotifier notifier;
    TileUpdate seen;
    bool called = false;
    const auto sub = notifier.subscribe([&](const TileUpdate* u) {
        seen = *u;
        called = true;
    });
    notifier.notify(TileUpdate{PointI(3, 4), 5, nullptr});
    ASSERT_TRUE(called);
    EXPECT_EQ(seen.tile, PointI(3, 4));
    EXPECT_EQ(seen.zoom, 5);
    EXPECT_EQ(seen.version, nullptr);
}

TEST(TilePyramidNotifier, NotifyFullCallsEverySubscriberOnceWithNull) {
    TilePyramidNotifier notifier;
    std::vector<const TileUpdate*> a;
    std::vector<const TileUpdate*> b;
    const auto subA = notifier.subscribe([&](const TileUpdate* u) { a.push_back(u); });
    const auto subB = notifier.subscribe([&](const TileUpdate* u) { b.push_back(u); });
    notifier.notifyFull();
    ASSERT_EQ(a.size(), 1u);
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(a[0], nullptr);
    EXPECT_EQ(b[0], nullptr);
    // a one-tile notification afterwards is not null again
    notifier.notify(TileUpdate{PointI(7, 8), 9, nullptr});
    ASSERT_EQ(a.size(), 2u);
    EXPECT_NE(a[1], nullptr);
}

TEST(TilePyramidNotifier, NestedNotifyInsideFullUpdateGetsItsPayloadAndOuterStaysFull) {
    TilePyramidNotifier notifier;
    // events: -1 = full (null), otherwise the zoom received
    std::vector<int> first;
    std::vector<int> second;
    bool nested = false;
    const auto subA = notifier.subscribe([&](const TileUpdate* u) {
        first.push_back(u == nullptr ? -1 : u->zoom);
        if (u == nullptr && !nested) {
            nested = true;
            notifier.notify(TileUpdate{PointI(1, 2), 6, nullptr});
        }
    });
    const auto subB =
        notifier.subscribe([&](const TileUpdate* u) { second.push_back(u == nullptr ? -1 : u->zoom); });
    notifier.notifyFull();
    // subA: full, then (own nested call) 6; subB: the nested 6 first, then the outer full
    EXPECT_EQ(first, (std::vector<int>{-1, 6}));
    EXPECT_EQ(second, (std::vector<int>{6, -1}));
}

TEST(TilePyramidNotifier, NestedFullInsideTileUpdateLeavesOuterDispatchOnItsPayload) {
    TilePyramidNotifier notifier;
    std::vector<int> first;
    std::vector<int> second; // -1 = full (null), otherwise the zoom received
    bool nested = false;
    const auto subA = notifier.subscribe([&](const TileUpdate* u) {
        first.push_back(u == nullptr ? -1 : u->zoom);
        if (u != nullptr && !nested) {
            nested = true;
            notifier.notifyFull();
        }
    });
    const auto subB =
        notifier.subscribe([&](const TileUpdate* u) { second.push_back(u == nullptr ? -1 : u->zoom); });
    notifier.notify(TileUpdate{PointI(1, 2), 6, nullptr});
    EXPECT_EQ(first, (std::vector<int>{6, -1}));
    EXPECT_EQ(second, (std::vector<int>{-1, 6})); // nested full first, then the outer tile payload
}

TEST(TilePyramidNotifier, DroppedSubscriptionReceivesNeitherKind) {
    TilePyramidNotifier notifier;
    int count = 0;
    {
        const auto sub = notifier.subscribe([&](const TileUpdate*) { ++count; });
    }
    notifier.notifyFull();
    notifier.notify(TileUpdate{PointI(0, 0), 0, nullptr});
    EXPECT_EQ(count, 0);
}

TEST(TilePyramidNotifier, FullAndTileNotifyOnTwoThreadsEachSeeTheirOwnPayload) {
    // Both threads must be inside the slot at the same moment (barrier of two inside the slot);
    // otherwise the test fails instead of passing vacuously. Run under --tsan too.
    TilePyramidNotifier notifier;
    std::barrier<> inside(2);
    std::latch go(1);
    std::atomic<int> fullThreadSawNull{0};
    std::atomic<int> tileThreadSawZoom{-1};
    const auto sub = notifier.subscribe([&](const TileUpdate* u) {
        inside.arrive_and_wait(); // blocks until the other thread is in its slot call too
        if (u == nullptr) {
            ++fullThreadSawNull;
        } else {
            tileThreadSawZoom = u->zoom;
        }
    });
    std::thread tFull([&] {
        go.wait();
        notifier.notifyFull();
    });
    std::thread tTile([&] {
        go.wait();
        notifier.notify(TileUpdate{PointI(4, 5), 11, nullptr});
    });
    go.count_down();
    tFull.join();
    tTile.join();
    EXPECT_EQ(fullThreadSawNull.load(), 1);
    EXPECT_EQ(tileThreadSawZoom.load(), 11);
}

TEST(TilePyramidNotifier, EverySubscriberSeesEveryUpdate) {
    TilePyramidNotifier notifier;
    int a = 0;
    int b = 0;
    const auto subA = notifier.subscribe([&](const TileUpdate*) { ++a; });
    const auto subB = notifier.subscribe([&](const TileUpdate*) { ++b; });
    notifier.notify(TileUpdate{PointI(0, 0), 0, nullptr});
    notifier.notify(TileUpdate{PointI(1, 1), 1, nullptr});
    EXPECT_EQ(a, 2);
    EXPECT_EQ(b, 2);
}

TEST(TilePyramidNotifier, SubscriptionDestructionStopsFurtherNotifications) {
    TilePyramidNotifier notifier;
    int count = 0;
    {
        const auto sub = notifier.subscribe([&](const TileUpdate*) { ++count; });
        notifier.notify(TileUpdate{PointI(0, 0), 0, nullptr});
    }
    EXPECT_EQ(count, 1);
    notifier.notify(TileUpdate{PointI(0, 0), 0, nullptr}); // subscription gone
    EXPECT_EQ(count, 1);
}

TEST(TilePyramidNotifier, ReentrantNotifyFromWithinASlotSeesItsOwnPayload) {
    // A render-thread slot that re-enters the storage (invalidate + re-read) can trigger a nested
    // save/delete that itself calls notify() -- the reentrant call must not clobber the outer
    // call's payload once control returns to it (spec §3.1 rule 3 concern).
    TilePyramidNotifier notifier;
    std::vector<int> seenZooms;
    const auto sub = notifier.subscribe([&](const TileUpdate* u) {
        seenZooms.push_back(u->zoom);
        if (u->zoom == 1) {
            notifier.notify(TileUpdate{PointI(0, 0), 2, nullptr});
        }
    });
    notifier.notify(TileUpdate{PointI(0, 0), 1, nullptr});
    ASSERT_EQ(seenZooms.size(), 2u);
    EXPECT_EQ(seenZooms[0], 1); // outer call dispatched first
    EXPECT_EQ(seenZooms[1], 2); // nested call, then control returns (no zoom-1 payload corruption)
}

TEST(TilePyramidNotifier, ConcurrentNotifyFromDifferentThreadsNeverMixPayloads) {
    // Each thread's payload must stay isolated (thread_local dispatch pointer, tile_pyramid_
    // notifier.cpp) -- run under --tsan too (validate.sh gate).
    TilePyramidNotifier notifier;
    std::atomic<int> mismatches{0};
    std::atomic<int> calls{0};
    const auto sub = notifier.subscribe([&](const TileUpdate* u) {
        ++calls;
        if (u->tile.x() != u->zoom) { // each thread notifies with tile.x() == zoom
            ++mismatches;
        }
    });

    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&notifier, t] {
            for (int i = 0; i < kPerThread; ++i) {
                notifier.notify(TileUpdate{PointI(t, 0), static_cast<quint8>(t), nullptr});
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(calls.load(), kThreads * kPerThread);
    EXPECT_EQ(mismatches.load(), 0);
}

} // namespace
