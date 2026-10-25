// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018 (open region-file map, per-file mutex, idle sweep), FEAT-STO-035 (scan).
// Ports u_TileStorageSQLiteList.pas / u_TileStorageSQLiteHelper.pas (file map) and
// u_EnumTileInfoBySQLite.pas / u_TileStorageSQLiteFetcher.pas (scan); no legacy DUnit exists.
#include "cg/storage/codecs/region_tile_name_codec.h"
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_region_pool.h"
#include "cg/storage/sqlite/sqlite_region_scan.h"
#include "cg/storage/tile_info.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <set>
#include <stop_token>
#include <thread>

#include "fixture_path.h"

using cg::storage::MapVersionFactory;
using cg::storage::RegionTileNameCodec;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::sqlite::SqliteRegionHandler;
using cg::storage::sqlite::SqliteRegionPool;
using cg::storage::sqlite::SqliteRegionSchema;

namespace {

std::shared_ptr<SqliteRegionPool> makePool(const QString& root, bool useVersionFieldInDb = true) {
    auto schema = std::make_shared<SqliteRegionSchema>(root, QByteArray("image/jpg"),
                                                       std::make_shared<MapVersionFactory>(),
                                                       /*logging=*/false);
    return std::make_shared<SqliteRegionPool>(std::move(schema), useVersionFieldInDb,
                                              /*readOnly=*/false);
}

} // namespace

TEST(SqliteRegionPool, TwoThreadsShareOneHandlerAndNeverRunOnItConcurrently) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    const QString path = root + QStringLiteral("z1/0/0/0.0.sqlitedb");

    std::atomic<int> inside{0};
    std::atomic<int> maxInside{0};
    std::mutex seenMutex;
    std::set<const SqliteRegionHandler*> seen;
    const auto worker = [&] {
        for (int i = 0; i < 50; ++i) {
            EXPECT_TRUE(pool->run(path, SqliteRegionPool::EntryKind::Dynamic, true, std::stop_token{},
                                  [&](SqliteRegionHandler& handler) {
                                      const int now = ++inside;
                                      int prev = maxInside.load();
                                      while (prev < now && !maxInside.compare_exchange_weak(prev, now)) {
                                      }
                                      {
                                          const std::lock_guard lock(seenMutex);
                                          seen.insert(&handler);
                                      }
                                      std::this_thread::yield();
                                      --inside;
                                  }));
        }
    };
    std::thread a(worker);
    std::thread b(worker);
    a.join();
    b.join();

    EXPECT_EQ(seen.size(), 1u);
    EXPECT_EQ(maxInside.load(), 1);
}

TEST(SqliteRegionPool, SweepIdleClosesIdleFileButNotOneWhoseMutexIsHeld) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    const QString idlePath = root + QStringLiteral("idle.sqlitedb");
    const QString busyPath = root + QStringLiteral("busy.sqlitedb");
    ASSERT_TRUE(pool->run(idlePath, SqliteRegionPool::EntryKind::Dynamic, true, std::stop_token{},
                          [](SqliteRegionHandler&) {}));
    ASSERT_EQ(pool->entryCountForTest(), 1u);

    std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> releaseFuture = release.get_future().share();
    std::thread holder([&] {
        EXPECT_TRUE(pool->run(busyPath, SqliteRegionPool::EntryKind::Dynamic, true, std::stop_token{},
                              [&](SqliteRegionHandler&) {
                                  entered.set_value();
                                  releaseFuture.wait();
                              }));
    });
    entered.get_future().wait();
    ASSERT_EQ(pool->entryCountForTest(), 2u);

    const quint32 later = SqliteRegionPool::currentTickMs() + SqliteRegionPool::kIdleCloseMs + 1000;
    pool->sweepIdle(later);
    EXPECT_EQ(pool->entryCountForTest(), 1u); // idle one closed, the held one kept

    release.set_value();
    holder.join();
    pool->sweepIdle(later);
    EXPECT_EQ(pool->entryCountForTest(), 0u);
}

// FEAT-STO-018. The GC tick is gated as legacy's TListenerTTLCheck(Sync, 180 s): a tick after a use
// only stamps the time, the sweep runs once kIdleCloseMs have passed since that stamp with no use.
// The file is idle for longer than the TTL at every tick below, so only the gate can keep it open.
TEST(SqliteRegionPool, GcTickSweepsOnlyAfterTheTtlSinceTheLastObservedUse) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    ASSERT_TRUE(pool->run(root + QStringLiteral("a.sqlitedb"), SqliteRegionPool::EntryKind::Dynamic, true,
                          std::stop_token{}, [](SqliteRegionHandler&) {}));
    constexpr quint32 ttl = SqliteRegionPool::kIdleCloseMs;
    const quint32 stamp = SqliteRegionPool::currentTickMs() + 100000;

    pool->gcTick(stamp);           // use flag set: only stamps
    pool->gcTick(stamp + ttl - 1); // one millisecond short of the TTL: gate closed
    EXPECT_EQ(pool->entryCountForTest(), 1u);
    pool->gcTick(stamp + ttl); // exactly the TTL: Sync runs and closes the file
    EXPECT_EQ(pool->entryCountForTest(), 0u);
}

// The sweep that follows a use re-arms the gate (helper.Sync -> CheckUseTimeUpdated when
// FIsNeedCleanup was set): a file the first sweep had to skip is closed by a later tick without any
// further use of the pool.
TEST(SqliteRegionPool, GcTickGateRearmsAfterASweepThatFollowedAUse) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    const QString path = root + QStringLiteral("a.sqlitedb");
    ASSERT_TRUE(pool->run(path, SqliteRegionPool::EntryKind::Dynamic, true, std::stop_token{},
                          [](SqliteRegionHandler&) {}));
    auto lease = pool->lease(path); // pinned: the first sweep must skip the file
    ASSERT_NE(lease, nullptr);
    constexpr quint32 ttl = SqliteRegionPool::kIdleCloseMs;
    const quint32 stamp = SqliteRegionPool::currentTickMs() + 100000;

    pool->gcTick(stamp);
    pool->gcTick(stamp + ttl); // gate opens, the pinned file stays, the gate re-arms
    EXPECT_EQ(pool->entryCountForTest(), 1u);
    lease.reset(); // no pool call: nothing raises the use flag again
    const quint32 second = stamp + ttl + 1;
    pool->gcTick(second); // the re-armed use flag: stamps
    pool->gcTick(second + ttl - 1);
    EXPECT_EQ(pool->entryCountForTest(), 1u);
    pool->gcTick(second + ttl);
    EXPECT_EQ(pool->entryCountForTest(), 0u);
}

TEST(SqliteRegionPool, ReadOfMissingFileDoesNotCreateIt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    const QString path = root + QStringLiteral("z1/0/0/0.0.sqlitedb");

    bool called = false;
    EXPECT_FALSE(pool->run(path, SqliteRegionPool::EntryKind::Dynamic, false, std::stop_token{},
                           [&](SqliteRegionHandler&) { called = true; }));
    EXPECT_FALSE(called);
    EXPECT_FALSE(QFileInfo::exists(path));
    EXPECT_FALSE(QFileInfo::exists(root + QStringLiteral("z1")));
}

TEST(SqliteRegionPool, ScanYieldsRepoFixtureTileWithRegionCodecZoom) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    ASSERT_TRUE(QDir().mkpath(root + QStringLiteral("z1/0/0")));
    ASSERT_TRUE(QFile::copy(cgStorageFixture(QStringLiteral("cache/cache_sqlite/map/z1/0/0/0.0.sqlitedb")),
                            root + QStringLiteral("z1/0/0/0.0.sqlitedb")));

    auto pool = makePool(root);
    auto scan = cg::storage::sqlite::makeSqliteRegionScan(pool, root, std::make_shared<RegionTileNameCodec>(),
                                                          /*ignoreTne=*/false);
    TileInfo info;
    cg::geo::PointI tile(-1, -1);
    quint8 zoom = 99;
    ASSERT_TRUE(scan->next(info, tile, zoom));
    EXPECT_EQ(info.type, TileInfoType::Exists);
    EXPECT_EQ(info.size, 1435u);
    EXPECT_EQ(info.data.size(), 1435);
    EXPECT_EQ(tile, cg::geo::PointI(0, 0));
    EXPECT_EQ(zoom, 0); // "z1" -> zoom 0
    EXPECT_FALSE(scan->next(info, tile, zoom));
    EXPECT_FALSE(scan->next(info, tile, zoom));
}

// Legacy defect L-92 is NOT reproduced (deviation D-44): a non-versioned map issues its SELECT once.
TEST(SqliteRegionPool, ScanOfNonVersionedFileEnds) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    ASSERT_TRUE(QDir().mkpath(root + QStringLiteral("z1/0/0")));
    ASSERT_TRUE(QFile::copy(cgStorageFixture(QStringLiteral("cache/cache_sqlite/map/z1/0/0/0.0.sqlitedb")),
                            root + QStringLiteral("z1/0/0/0.0.sqlitedb")));

    auto pool = makePool(root, /*useVersionFieldInDb=*/false);
    auto scan = cg::storage::sqlite::makeSqliteRegionScan(pool, root, std::make_shared<RegionTileNameCodec>(),
                                                          /*ignoreTne=*/false);
    TileInfo info;
    cg::geo::PointI tile(-1, -1);
    quint8 zoom = 99;
    ASSERT_TRUE(scan->next(info, tile, zoom));
    EXPECT_EQ(tile, cg::geo::PointI(0, 0));
    EXPECT_FALSE(scan->next(info, tile, zoom));
}

// Legacy defect L-94(a) is NOT reproduced (deviation of T-0211 (docs/parity/deviations.md)): for a z1-z11
// file the write that creates it initialises the entry, so a read straight after it finds the tile.
TEST(SqliteRegionPool, FixedEntryReadAfterCreatingWriteIsReadable) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    const QString path = root + QStringLiteral("z1/0/0/0.0.sqlitedb");
    const auto fixed = SqliteRegionPool::EntryKind::Fixed;
    const auto noop = [](SqliteRegionHandler&) {};

    EXPECT_TRUE(pool->run(path, fixed, true, std::stop_token{}, noop)); // creates the file
    EXPECT_TRUE(QFileInfo::exists(path));
    EXPECT_NE(pool->lease(path), nullptr); // a scan in between changes nothing
    EXPECT_TRUE(pool->run(path, fixed, false, std::stop_token{}, noop));
}

// The deviation of T-0211 initialises the entry only when the creating write produced a handler: a create
// that fails (non-writable parent folder) leaves it as in legacy, and the next write still creates the file.
TEST(SqliteRegionPool, FixedEntryFailedCreateIsRetriedAndThenReadable) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    auto pool = makePool(root);
    const QString folder = root + QStringLiteral("z1/0/0");
    const QString path = folder + QStringLiteral("/0.0.sqlitedb");
    const auto fixed = SqliteRegionPool::EntryKind::Fixed;
    const auto noop = [](SqliteRegionHandler&) {};
    ASSERT_TRUE(QDir().mkpath(folder));
    const auto original = QFile::permissions(folder);
    ASSERT_TRUE(QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QFile probe(folder + QStringLiteral("/probe"));
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        probe.remove();
        QFile::setPermissions(folder, original);
        GTEST_SKIP() << "the folder stays writable (running as root)";
    }

    EXPECT_FALSE(pool->run(path, fixed, false, std::stop_token{}, noop)); // remembers the absence
    EXPECT_FALSE(pool->run(path, fixed, true, std::stop_token{}, noop));  // the create fails
    EXPECT_FALSE(QFileInfo::exists(path));

    ASSERT_TRUE(QFile::setPermissions(folder, original));
    EXPECT_TRUE(pool->run(path, fixed, true, std::stop_token{}, noop)); // creates the file
    EXPECT_TRUE(QFileInfo::exists(path));
    EXPECT_TRUE(pool->run(path, fixed, false, std::stop_token{}, noop)); // readable at once
}
