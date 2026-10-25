// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-034. Ports i_TileInfoBasicMemCache.pas, u_TileInfoBasicMemCache.pas
// (TTileInfoBasicMemCache); docs/specs/storage.md §2.9 (:550-623).
//
// Test-count ruling (docs/backlog/T-0021E-storage-mem-cache-and-simple-config.md's "22" is not a
// quota, tests/conventions/tests-port-dont-multiply): legacy has no DUnit suite for this class, so
// this is the ticket's own minimal ~8-10 case list: hit, miss, negative cache,
// WithData-miss-on-data-less, version key case sensitivity, TTL expiry + refresh-on-hit (injected
// clock, no sleeps), capacity eviction order, one table-driven case over the four ClearStrategy
// values, and enumerate with/without ignoreTne.
#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/tile_info_mem_cache.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using cg::geo::PointI;
using cg::storage::ClearStrategy;
using cg::storage::EnumTileInfoPtr;
using cg::storage::LoadMode;
using cg::storage::MapVersionInfo;
using cg::storage::MapVersionInfoPtr;
using cg::storage::TileInfo;
using cg::storage::TileInfoMemCache;
using cg::storage::TileInfoType;
using cg::storage::TileUpdate;

namespace {

MapVersionInfoPtr version(const QString& v) {
    return std::make_shared<const MapVersionInfo>(v, cg::core::kNoHash);
}

TileInfo makeExists(QByteArray data = QByteArray("payload")) {
    TileInfo info;
    info.type = TileInfoType::Exists;
    info.size = static_cast<quint32>(data.size());
    info.data = std::move(data);
    return info;
}

TileInfo makeNotExists() {
    TileInfo info;
    info.type = TileInfoType::NotExists;
    return info;
}

TileInfo makeTne() {
    TileInfo info;
    info.type = TileInfoType::TneExists;
    return info;
}

// A manual clock the test advances explicitly -- no sleeps, deterministic TTL math.
class FakeClock {
public:
    [[nodiscard]] TileInfoMemCache::Clock asClockFn() {
        return [this] { return now_; };
    }
    void advance(std::chrono::milliseconds by) { now_ += by; }

private:
    std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::duration::zero()};
};

std::vector<std::pair<PointI, quint8>> drain(const EnumTileInfoPtr& e) {
    std::vector<std::pair<PointI, quint8>> out;
    TileInfo info;
    PointI tile;
    quint8 zoom = 0;
    while (e->next(info, tile, zoom)) {
        out.emplace_back(tile, zoom);
    }
    return out;
}

TEST(TileInfoMemCache, HitReturnsWhatWasAdded) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    cache.add(PointI(1, 2), 3, version("v1"), makeExists());

    const auto hit = cache.get(PointI(1, 2), 3, version("v1"), LoadMode::WithData, false);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->type, TileInfoType::Exists);
    EXPECT_EQ(hit->data, QByteArray("payload"));
}

TEST(TileInfoMemCache, MissWhenNeverAdded) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    EXPECT_FALSE(cache.get(PointI(9, 9), 1, version("v1"), LoadMode::AsIs, false).has_value());
}

TEST(TileInfoMemCache, NegativeResultIsCachedAndIsAHit) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    cache.add(PointI(4, 4), 2, version("v1"), makeNotExists());

    const auto hit = cache.get(PointI(4, 4), 2, version("v1"), LoadMode::WithoutData, false);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->type, TileInfoType::NotExists);
}

TEST(TileInfoMemCache, WithDataMissOnADataLessEntryIsAMissNotAHit) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    // Cached without data (e.g. a WithoutData load); WithData must still miss, not fabricate a hit.
    TileInfo exists = makeExists(QByteArray());
    cache.add(PointI(0, 0), 0, version("v1"), exists);

    EXPECT_FALSE(cache.get(PointI(0, 0), 0, version("v1"), LoadMode::WithData, false).has_value());
    // WithoutData/AsIs still see the entry.
    EXPECT_TRUE(cache.get(PointI(0, 0), 0, version("v1"), LoadMode::WithoutData, false).has_value());
}

TEST(TileInfoMemCache, VersionKeyIsCaseSensitive) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    cache.add(PointI(5, 5), 1, version("Ver"), makeExists());

    EXPECT_FALSE(cache.get(PointI(5, 5), 1, version("VER"), LoadMode::AsIs, false).has_value());
    EXPECT_FALSE(cache.get(PointI(5, 5), 1, version("ver"), LoadMode::AsIs, false).has_value());
    EXPECT_TRUE(cache.get(PointI(5, 5), 1, version("Ver"), LoadMode::AsIs, false).has_value());
}

TEST(TileInfoMemCache, TtlExpiryAndRefreshOnHit) {
    FakeClock clock;
    TileInfoMemCache cache(10, std::chrono::milliseconds(1'000), ClearStrategy::Never, clock.asClockFn());
    cache.add(PointI(1, 1), 1, nullptr, makeExists());

    clock.advance(std::chrono::milliseconds(900));
    // updateTtl = true refreshes the deadline to now (900) + 1000 = 1900.
    ASSERT_TRUE(cache.get(PointI(1, 1), 1, nullptr, LoadMode::AsIs, true).has_value());

    clock.advance(std::chrono::milliseconds(900)); // now = 1800, refreshed deadline 1900 still live
    EXPECT_TRUE(cache.get(PointI(1, 1), 1, nullptr, LoadMode::AsIs, false).has_value());

    clock.advance(std::chrono::milliseconds(200)); // now = 2000, past the 1900 deadline
    EXPECT_FALSE(cache.get(PointI(1, 1), 1, nullptr, LoadMode::AsIs, false).has_value());
}

TEST(TileInfoMemCache, CapacityEvictsTheOldestDeadlineFirst) {
    FakeClock clock;
    TileInfoMemCache cache(2, std::chrono::milliseconds(1'000), ClearStrategy::Never, clock.asClockFn());
    cache.add(PointI(0, 0), 0, nullptr, makeExists()); // deadline 1000
    clock.advance(std::chrono::milliseconds(100));
    cache.add(PointI(1, 1), 0, nullptr, makeExists()); // deadline 1100, cache now full (capacity 2)

    clock.advance(std::chrono::milliseconds(100));
    // Adding a third key must evict (0,0) (the smaller deadline), keeping (1,1).
    cache.add(PointI(2, 2), 0, nullptr, makeExists());

    EXPECT_FALSE(cache.get(PointI(0, 0), 0, nullptr, LoadMode::AsIs, false).has_value());
    EXPECT_TRUE(cache.get(PointI(1, 1), 0, nullptr, LoadMode::AsIs, false).has_value());
    EXPECT_TRUE(cache.get(PointI(2, 2), 0, nullptr, LoadMode::AsIs, false).has_value());
}

struct ClearStrategyCase {
    ClearStrategy strategy;
    bool oldestSurvives;   // the entry whose deadline expires first
    bool youngestSurvives; // the entry whose deadline expires later
};

class ClearStrategyTest : public ::testing::TestWithParam<ClearStrategyCase> {};

TEST_P(ClearStrategyTest, ClearExpiredBehavesPerStrategy) {
    const ClearStrategyCase param = GetParam();
    FakeClock clock;
    TileInfoMemCache cache(10, std::chrono::milliseconds(1'000), param.strategy, clock.asClockFn());
    cache.add(PointI(0, 0), 0, nullptr, makeExists()); // deadline 1000 (the older one)
    clock.advance(std::chrono::milliseconds(500));
    cache.add(PointI(1, 1), 0, nullptr, makeExists()); // deadline 1500 (the younger one)

    // Observe eviction directly through the update callback -- get()'s own per-entry deadline
    // check is independent of ClearStrategy (an expired entry is always a miss through get(),
    // strategy or not; see the header's note on Get), so it cannot distinguish "clearExpired()
    // physically evicted this slot" from "this slot's own TTL happens to have passed".
    std::vector<PointI> evicted;
    cache.setOnTileInfoUpdate([&](const TileUpdate& u) { evicted.push_back(u.tile); });

    clock.advance(std::chrono::milliseconds(600)); // now = 1100: oldest expired, youngest not yet
    cache.clearExpired();

    const bool oldestEvicted = std::find(evicted.begin(), evicted.end(), PointI(0, 0)) != evicted.end();
    const bool youngestEvicted = std::find(evicted.begin(), evicted.end(), PointI(1, 1)) != evicted.end();
    EXPECT_EQ(!oldestEvicted, param.oldestSurvives);
    EXPECT_EQ(!youngestEvicted, param.youngestSurvives);
}

INSTANTIATE_TEST_SUITE_P(FourStrategies, ClearStrategyTest,
                         ::testing::Values(ClearStrategyCase{ClearStrategy::ByOldest, false,
                                                             false}, // min deadline expired -> wipe all
                                           ClearStrategyCase{ClearStrategy::ByYoungest, true,
                                                             true}, // max deadline not expired -> keep all
                                           ClearStrategyCase{ClearStrategy::OneByOne, false,
                                                             true}, // only the expired one drops
                                           ClearStrategyCase{ClearStrategy::Never, true, true})); // no-op

TEST(TileInfoMemCache, ClearExpiredAlwaysNotifiesEvenWithNothingToEvict) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::ByYoungest);
    int fired = 0;
    const auto sub = cache.clearedByTtl().subscribe([&] { ++fired; });
    cache.clearExpired(); // empty cache, nothing expired -- still fires (legacy's unconditional Notify)
    EXPECT_EQ(fired, 1);
}

TEST(TileInfoMemCache, EnumerateWithAndWithoutIgnoreTne) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    cache.add(PointI(0, 0), 0, nullptr, makeExists());
    cache.add(PointI(1, 0), 0, nullptr, makeTne());
    cache.add(PointI(2, 0), 0, nullptr, makeNotExists()); // never enumerated, TNE or not

    const auto all = drain(cache.enumerate(false, false));
    EXPECT_EQ(all.size(), 2u); // Exists + TneExists; plain NotExists excluded

    const auto noTne = drain(cache.enumerate(true, false));
    EXPECT_EQ(noTne.size(), 1u);
    EXPECT_EQ(noTne.front().first, PointI(0, 0));
}

// Regression for the T-0021E1 rework findings (last-empty-slot reuse, data-less entries skipped,
// key version returned), all in one case as the Tech Lead ruling asked.
TEST(TileInfoMemCache, EnumerateAfterRemoveAndReAddUsesLastSlotSkipsDatalessReturnsKeyVersion) {
    TileInfoMemCache cache(4, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    cache.add(PointI(0, 0), 0, version("vA"), makeExists());
    cache.add(PointI(1, 0), 0, version("vB"), makeExists());
    cache.add(PointI(2, 0), 0, version("vC"), makeExists());

    ASSERT_TRUE(cache.remove(PointI(0, 0), 0, version("vA"))); // hole at slot 0
    ASSERT_TRUE(cache.remove(PointI(1, 0), 0, version("vB"))); // hole at slot 1

    // info.version deliberately wrong -- enumerate must emit the key passed to add(), not this.
    TileInfo dInfo = makeExists();
    dInfo.version = version("wrong-info-version");
    cache.add(PointI(3, 0), 0, version("vD"), dInfo);        // must reuse the LAST hole (slot 1), not slot 0
    cache.add(PointI(4, 0), 0, version("vE"), makeExists()); // must reuse the remaining hole (slot 0)
    cache.add(PointI(5, 0), 0, version("vF"), makeExists(QByteArray())); // data-less -- must not enumerate

    const auto e = cache.enumerate(false, false);
    std::vector<PointI> order;
    TileInfo info;
    PointI tile;
    quint8 zoom = 0;
    while (e->next(info, tile, zoom)) {
        order.push_back(tile);
        if (tile == PointI(3, 0)) {
            ASSERT_TRUE(info.version != nullptr);
            EXPECT_EQ(info.version->storeString(), QString("vD")); // key version, not info.version
        }
    }

    // Slot reuse order (0 refilled by E, 1 refilled by D, 2 untouched C) drives enumerate's order;
    // slot 3 (F) is skipped entirely for having no data.
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], PointI(4, 0)); // E, refilled slot 0 (the LAST hole seen on its own add)
    EXPECT_EQ(order[1], PointI(3, 0)); // D, refilled slot 1
    EXPECT_EQ(order[2], PointI(2, 0)); // C, never removed
}

TEST(TileInfoMemCache, RemoveFiresUpdateAndFutureGetsMiss) {
    TileInfoMemCache cache(10, std::chrono::milliseconds(60'000), ClearStrategy::Never);
    cache.add(PointI(3, 3), 0, version("v1"), makeExists());

    std::optional<TileUpdate> seen;
    cache.setOnTileInfoUpdate([&](const TileUpdate& u) { seen = u; });

    ASSERT_TRUE(cache.remove(PointI(3, 3), 0, version("v1")));
    ASSERT_TRUE(seen.has_value());
    EXPECT_EQ(seen->tile, PointI(3, 3));
    EXPECT_FALSE(cache.get(PointI(3, 3), 0, version("v1"), LoadMode::AsIs, false).has_value());
    EXPECT_FALSE(cache.remove(PointI(3, 3), 0, version("v1"))); // already gone
}

} // namespace
