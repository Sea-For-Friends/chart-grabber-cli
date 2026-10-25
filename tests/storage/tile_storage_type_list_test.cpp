// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-001 (cache-type id -> storage type),
// FEAT-STO-018 (SQLite region), FEAT-STO-019/020/021 (single-file SQLite), FEAT-STO-026 (GC tick).
// Ports u_TileStorageTypeListSimple.pas / u_TileStorageTypeList.pas / u_TileStorageTypeListItem.pas;
// no legacy DUnit exists. Each case has its own temporary cache and its own list.
#include "cg/core/content_type.h"
#include "cg/core/garbage_collector_thread.h"
#include "cg/core/path_config.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_storage_type_list_simple.h"

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace cg::storage;
using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::geo::PointI;

cg::geo::ProjectionSetPtr projectionSet() {
    static const auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
    static const cg::geo::ProjectionSetFactory factory{cg::geo::HashSeed{}, datums};
    return factory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
}

struct Rig {
    QTemporaryDir dir;
    GlobalCacheConfig config{cg::core::PathConfig::create(QString(), dir.path(), nullptr)};
    cg::core::Notifier gc; // declared before the list: outlives every type
    MapVersionFactoryPtr factory = std::make_shared<MapVersionFactory>();
    TileStorageTypeListSimple list{factory, config, gc};

    // Builds the storage of cache type `id`; folder types get a folder with a trailing '/', the
    // single-file ones a file name.
    ITileStoragePtr build(CacheTypeId id) {
        const ITileStorageTypeListItemPtr item = list.itemByCode(static_cast<int>(id));
        EXPECT_NE(item, nullptr);
        if (!item) {
            return nullptr;
        }
        const QString sub = QDir(dir.path()).filePath(QStringLiteral("c%1").arg(static_cast<int>(id)));
        const bool oneFile = item->storageType()->abilities().storageClass == StorageClass::OneFile;
        const QString path = oneFile ? sub + QStringLiteral(".db") : sub + u'/';
        if (!oneFile) {
            EXPECT_TRUE(QDir().mkpath(path));
        }
        // legacy: only the RAM storage requires the mem cache (it IS its storage)
        const auto memCache = id == CacheTypeId::Ram
                                  ? std::make_shared<TileInfoMemCache>(100, std::chrono::milliseconds(60'000),
                                                                       ClearStrategy::Never)
                                  : nullptr;
        auto built = item->storageType()->build(TileStorageAbilities::readWrite(), projectionSet(),
                                                ContentTypeInfo{"image/png", ".png", ContentKind::Bitmap},
                                                std::make_shared<TilePyramidNotifier>(), path, memCache);
        EXPECT_TRUE(built.has_value());
        return built.has_value() ? *built : nullptr;
    }
};

} // namespace

TEST(TileStorageTypeListSimple, RegistersEveryLandedBackendInLegacyOrder) {
    Rig rig;
    // Literals from legacy u_TileStorageTypeListSimple.pas (registration order :191-557, GUIDs :104-142,
    // captions :146-171, flags and path sources per registration). Not derived from cache_type.cpp.
    struct Expected {
        int code;
        const char* guid;
        const char* caption;
        bool canUseAsDefault;
        bool isChangeable;
        CacheTypeId pathOf; // whose GlobalCacheConfig path the type gets; Invalid = none (nil config)
    };
    const Expected expected[] = {
        {2, "{BE87ACAB-7031-4F57-9C1D-FA62C709818F}", "SAS.Planet", true, true, CacheTypeId::Sas},
        {1, "{CB20D66C-FC79-4D1C-93A9-1C41A8D6B002}", "GoogleMV", true, true, CacheTypeId::Gmv},
        {3, "{F6056405-C25C-4573-AFAC-BC4F8DF52283}", "EarthSlicer 1.95", true, true, CacheTypeId::Es},
        {4, "{E6F98BC5-8684-42C9-92DE-3D994DA8C925}", "GlobalMapper Tiles", true, true, CacheTypeId::Gm},
        {41, "{4EF99AD6-D05E-4175-805C-DBBE08AC43B3}", "GlobalMapper Aux", true, true, CacheTypeId::Gm},
        {42, "{A65E31AC-7561-47FA-87B6-CE7F5603D5D1}", "GlobalMapper Bing", true, true, CacheTypeId::Gm},
        {44, "{A12465C1-2DB9-4309-8B48-A01E3BBDDE44}", "OsmAnd+ Tiles", true, true, CacheTypeId::Gm},
        {43, "{033B64B5-008B-4BAF-9EA9-B8176EA35433}", "Mobile Atlas Creator (MOBAC)", true, true,
         CacheTypeId::Mobac},
        {45, "{55510DF4-7FAD-4476-93AD-454F8689A109}", "Tile Map Service (TMS)", true, true,
         CacheTypeId::Tms},
        {71, "{5E8ABF86-92B4-48FA-8F71-E94E22DD7831}", "SQLite3", true, true, CacheTypeId::Sqlite},
        {9, "{717034B7-B49E-4C89-BC75-002D0523E548}", "RAM", true, true, CacheTypeId::Invalid},
        {72, "{8C217E83-8AA8-41A4-A01A-AB80D130C748}", "MBTiles (SQLite3)", true, false,
         CacheTypeId::SqliteMbTiles},
        {73, "{110AE260-747D-423B-90C6-D83F1CE40D3F}", "OsmAnd (SQLite3)", true, false,
         CacheTypeId::SqliteOsmAnd},
        {74, "{11EB8F68-5E2A-4CCA-BDA1-34E7B9FB0278}", "Locus (SQLite3)", true, false,
         CacheTypeId::SqliteLocus},
        {75, "{57788698-C306-416D-B374-B7B8120A0F0E}", "RMaps (SQLite3)", true, false,
         CacheTypeId::SqliteRMaps},
        {76, "{C50F37F3-8B2B-4F58-840A-772D004D4775}", "OruxMaps (SQLite3)", false, false,
         CacheTypeId::Invalid},
    };
    ASSERT_EQ(rig.list.count(), static_cast<int>(std::size(expected)));
    for (int i = 0; i < rig.list.count(); ++i) {
        const Expected& e = expected[i];
        SCOPED_TRACE(e.code);
        const ITileStorageTypeListItemPtr item = rig.list.item(i);
        EXPECT_EQ(item->intCode(), e.code);
        EXPECT_EQ(item->guid(), QUuid(QString::fromLatin1(e.guid)));
        EXPECT_EQ(item->caption(), QString::fromLatin1(e.caption));
        EXPECT_EQ(item->canUseAsDefault(), e.canUseAsDefault);
        EXPECT_EQ(item->isChangeable(), e.isChangeable);
        ASSERT_NE(item->storageType(), nullptr);
        if (e.pathOf == CacheTypeId::Invalid) {
            EXPECT_EQ(item->storageType()->basePath(), nullptr);
        } else {
            EXPECT_EQ(item->storageType()->basePath(), rig.config.pathFor(e.pathOf));
            EXPECT_NE(item->storageType()->basePath(), nullptr);
        }
    }
    // OsmAnd+ really defaults into the GlobalMapper folder (legacy GMTilesPath).
    EXPECT_EQ(rig.list.itemByCode(44)->storageType()->basePath(),
              rig.list.itemByCode(4)->storageType()->basePath());
    // T-0333 (owner 2026-10-21): BerkeleyDB (6) and BerkeleyDB Versioned (61) are not registered any more.
    for (const int missing : {5, 6, 7, 8, 51, 61, 99}) {
        EXPECT_EQ(rig.list.itemByCode(missing), nullptr) << missing;
    }
}

// i_TileStorageType.pas:45-46: the type hands back the factory it was constructed with.
TEST(TileStorageTypeListSimple, EveryTypeReturnsTheFactoryOfTheList) {
    Rig rig;
    ASSERT_GT(rig.list.count(), 0);
    for (int i = 0; i < rig.list.count(); ++i) {
        SCOPED_TRACE(i);
        const auto type = rig.list.item(i)->storageType();
        ASSERT_NE(type->mapVersionFactory(), nullptr);
        EXPECT_EQ(type->mapVersionFactory(), rig.factory);
    }
}

TEST(TileStorageTypeListSimple, EveryBackendBuiltByIdReadsAndWritesATile) {
    Rig rig;
    for (const CacheTypeId id :
         {CacheTypeId::Sas, CacheTypeId::Gmv, CacheTypeId::Es, CacheTypeId::Gm, CacheTypeId::GmAux,
          CacheTypeId::GmBing, CacheTypeId::OsmAnd, CacheTypeId::Mobac, CacheTypeId::Tms, CacheTypeId::Sqlite,
          CacheTypeId::Ram, CacheTypeId::SqliteMbTiles, CacheTypeId::SqliteRMaps}) {
        SCOPED_TRACE(static_cast<int>(id));
        const ITileStoragePtr storage = rig.build(id);
        ASSERT_NE(storage, nullptr);
        // zoom 11 (zero-based): SQLite region files at <= 10 are legacy's "fixed" entries (L-94)
        const PointI xy{5, 7};
        const QByteArray data("tile bytes");
        const auto saved =
            storage->saveTile(xy, 11, nullptr, fromPosixSeconds(1700000000), "image/png", data, true);
        ASSERT_TRUE(saved.has_value());
        EXPECT_TRUE(*saved);
        const auto info = storage->tileInfo(xy, 11, nullptr, LoadMode::WithData);
        ASSERT_TRUE(info.has_value());
        EXPECT_TRUE(info->isExists());
        EXPECT_EQ(info->data, data);
    }
    // The three single-file formats whose write path is not exercised here still build by id.
    for (const CacheTypeId id :
         {CacheTypeId::SqliteOsmAnd, CacheTypeId::SqliteLocus, CacheTypeId::SqliteOruxMaps}) {
        EXPECT_NE(rig.build(id), nullptr) << static_cast<int>(id);
    }
}

TEST(TileStorageTypeListSimple, GcTickReachesTheSqliteRegionStorageOnly) {
    Rig rig;
    EXPECT_EQ(rig.gc.subscriberCount(), 0U);
    {
        const ITileStoragePtr region = rig.build(CacheTypeId::Sqlite);
        const ITileStoragePtr files = rig.build(CacheTypeId::Sas); // legacy has no GC consumer here
        EXPECT_EQ(rig.gc.subscriberCount(), 1U);
        rig.gc.notify();
    }
    EXPECT_EQ(rig.gc.subscriberCount(), 0U); // the subscriptions end with the storages
    rig.gc.notify();
}

// FEAT-STO-018, FEAT-STO-026. The ticks come from the GC thread while the test thread
// uses a real storage built through the list: the SQLite region gate runs against save/load, and the last
// reference to a storage is dropped while ticks continue. Deterministic: every operation succeeds, the
// process survives (TSan is the judge of the races), the subscriptions are gone at the end.
template <class Pred> bool waitFor(Pred done) { // bounded: a condition, never a fixed sleep
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

TEST(TileStorageTypeListSimple, StoragesUsedAndDroppedWhileTheGcThreadTicks) {
    Rig rig;
    std::atomic<int> ticks{0};
    const cg::core::Subscription counter = rig.gc.subscribe([&ticks] { ++ticks; });
    cg::core::GarbageCollectorThread gcThread(rig.gc, std::chrono::milliseconds(1));

    for (int round = 0; round < 5; ++round) {
        ITileStoragePtr region = rig.build(CacheTypeId::Sqlite);
        ASSERT_NE(region, nullptr);
        const int ticksBefore = ticks.load();
        for (int i = 0; i < 40; ++i) {
            const QByteArray data = QByteArray("tile ") + QByteArray::number(i);
            for (ITileStoragePtr* storage : {&region}) {
                const PointI xy{i, 7};
                const auto saved = (*storage)->saveTile(xy, 11, nullptr, fromPosixSeconds(1700000000),
                                                        "image/png", data, true);
                ASSERT_TRUE(saved.has_value())
                    << (saved.has_value() ? "" : saved.error().what().toStdString()) << " round " << round
                    << " i " << i;
                const auto info = (*storage)->tileInfo(xy, 11, nullptr, LoadMode::WithData);
                ASSERT_TRUE(info.has_value()) << (info.has_value() ? "" : info.error().what().toStdString());
                ASSERT_EQ(info->data, data);
            }
        }
        // A tick must have run while the storages were in use, or this test proves nothing.
        ASSERT_TRUE(waitFor([&] { return ticks.load() > ticksBefore; })) << "no tick overlapped the use";
        region.reset(); // the last reference goes while ticks continue
        // A tick in flight holds the storage until it returns, so its destructor (and the unsubscribe)
        // may still be running on the GC thread: wait for it.
        ASSERT_TRUE(waitFor([&] { return rig.gc.subscriberCount() == 1U; })); // only the counter is left
    }
    // at least one tick after the last drop: the thread kept running with nothing subscribed
    const int seen = ticks.load();
    EXPECT_TRUE(waitFor([&] { return ticks.load() > seen; }));
}
