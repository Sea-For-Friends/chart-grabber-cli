// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018 (SqliteRegionTileStorage + SqliteRegionTileStorageType: read, save, delete,
// rect across region files, scan, read-only StorageConfig.ini), FEAT-STO-035 (scan, rect).
// Ports u_TileStorageSQLite.pas / u_TileStorageSQLiteHelper.pas / u_TileStorageTypeSQLite.pas; no
// legacy DUnit exists. Real fixture: cache/cache_sqlite/map/z1/0/0/0.0.sqlitedb (copied to a temp dir).
// Region files at zero-based zoom <= 10 are legacy's "fixed" entries (L-94); the tests that
// write use zoom 11 and the fixture (zoom 0) is only read.
#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/sqlite/sqlite_region_tile_storage_type.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "fixture_path.h"
#include "storage_state_changeable_check.h"

using cg::core::ContentTypeRegistry;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::RectI;
using cg::storage::fromPosixSeconds;
using cg::storage::ITileStoragePtr;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionRequest;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileStorageAbilities;
using cg::storage::sqlite::SqliteRegionTileStorageType;

namespace {

cg::geo::ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

cg::core::Notifier& noTick() {
    static cg::core::Notifier tick;
    return tick;
}

ITileStoragePtr build(const QString& root, cg::core::Notifier& gcTick = noTick(),
                      TileStorageAbilities abilities = TileStorageAbilities::readWrite()) {
    SqliteRegionTileStorageType type(std::make_shared<MapVersionFactory>(), nullptr, gcTick);
    const auto* jpeg = ContentTypeRegistry::standard().infoByType(QByteArrayView("image/jpg"));
    auto built = type.build(abilities, makeProjectionSet(), *jpeg, std::make_shared<TilePyramidNotifier>(),
                            root, nullptr);
    EXPECT_TRUE(built.has_value());
    return built.has_value() ? *built : nullptr;
}

void copyFixture(const QString& root) {
    ASSERT_TRUE(QDir().mkpath(root + QStringLiteral("z1/0/0")));
    ASSERT_TRUE(QFile::copy(cgStorageFixture(QStringLiteral("cache/cache_sqlite/map/z1/0/0/0.0.sqlitedb")),
                            root + QStringLiteral("z1/0/0/0.0.sqlitedb")));
    QFile::setPermissions(root + QStringLiteral("z1/0/0/0.0.sqlitedb"), QFile::ReadOwner | QFile::WriteOwner);
}

bool saveJpeg(ITileStoragePtr& storage, PointI xy, quint8 zoom, const QByteArray& data) {
    const auto saved = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                         QByteArrayView("image/jpg"), data, true);
    return saved.has_value() && *saved;
}

} // namespace

TEST(SqliteRegionTileStorage, ReadsTheFixtureTile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    copyFixture(root);
    ITileStoragePtr storage = build(root);
    ASSERT_NE(storage, nullptr);

    const auto info = storage->tileInfo(PointI{0, 0}, 0, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Exists);
    EXPECT_EQ(info->size, 1435u);
    EXPECT_EQ(info->data.size(), 1435);
    EXPECT_EQ(storage->tileFileName(PointI{0, 0}, 0, nullptr), root + QStringLiteral("z1/0/0/0.0.sqlitedb"));
}

// u_TileStorageSQLite.pas:271-278, :299-304: no read access is the not-exists object, not nil (D-106).
TEST(SqliteRegionTileStorage, NoReadAccessStillAnswersNotExists) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    copyFixture(root); // the tile exists on disk, but is not read
    ITileStoragePtr storage = build(root, noTick(), TileStorageAbilities::none());
    ASSERT_NE(storage, nullptr);
    const auto info = storage->tileInfo(PointI{0, 0}, 0, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::NotExists);
}

TEST(SqliteRegionTileStorage, SaveReadBackAndDeleteInANewRegionFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    ITileStoragePtr storage = build(root);
    ASSERT_NE(storage, nullptr);

    EXPECT_TRUE(saveJpeg(storage, PointI{5, 7}, 11, QByteArray("tile bytes")));
    EXPECT_TRUE(QFileInfo::exists(root + QStringLiteral("z12/0/0/0.0.sqlitedb")));

    auto info = storage->tileInfo(PointI{5, 7}, 11, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Exists);
    EXPECT_EQ(info->data, QByteArray("tile bytes"));

    const auto deleted = storage->deleteTile(PointI{5, 7}, 11, nullptr);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(*deleted);
    info = storage->tileInfo(PointI{5, 7}, 11, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::NotExists);
}

// T-0211 (UAT D-1): a small MemCacheCapacity must not lose a tile. The write that creates a z1-z11 region
// file initialises its entry (deviation of T-0211; legacy L-94(a) left it unreadable,
// u_TileStorageSQLiteList.pas:489-566), so a tile evicted from the info cache is read back from the file.
// Capacity 1: the next save evicts the first tile, so the read cannot be answered by the info cache.
TEST(SqliteRegionTileStorage, SingleTileFixedRegionFileIsReadableOnceEvictedFromTheInfoCache) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    SqliteRegionTileStorageType type(std::make_shared<MapVersionFactory>(), nullptr, noTick());
    const auto* jpeg = ContentTypeRegistry::standard().infoByType(QByteArrayView("image/jpg"));
    auto built = type.build(TileStorageAbilities::readWrite(), makeProjectionSet(), *jpeg,
                            std::make_shared<TilePyramidNotifier>(), root,
                            std::make_shared<cg::storage::TileInfoMemCache>(
                                1, std::chrono::milliseconds(60'000), cg::storage::ClearStrategy::Never));
    ASSERT_TRUE(built.has_value());
    ITileStoragePtr storage = *built;

    const auto typeOf = [&](PointI xy, quint8 zoom) {
        const auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
        EXPECT_TRUE(info.has_value());
        return info.has_value() ? info->type : TileInfoType::Unknown;
    };

    // zero-based zoom 5 is a fixed entry (<= 10): file z6/0/0/0.0.sqlitedb, one tile
    ASSERT_TRUE(saveJpeg(storage, PointI{5, 7}, 5, QByteArray("first")));
    ASSERT_TRUE(saveJpeg(storage, PointI{1, 1}, 6, QByteArray("other"))); // evicts (5,7)
    EXPECT_TRUE(QFileInfo::exists(root + QStringLiteral("z6/0/0/0.0.sqlitedb")));
    EXPECT_EQ(typeOf(PointI{5, 7}, 5), TileInfoType::Exists);

    // the dynamic entry (zero-based zoom 11): one tile, evicted, readable as well
    ASSERT_TRUE(saveJpeg(storage, PointI{5, 7}, 11, QByteArray("dynamic")));
    ASSERT_TRUE(saveJpeg(storage, PointI{3, 1}, 6, QByteArray("other")));
    EXPECT_EQ(typeOf(PointI{5, 7}, 11), TileInfoType::Exists);
}

TEST(SqliteRegionTileStorage, RectCrossingTwoRegionFiles) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    ITileStoragePtr storage = build(root);
    ASSERT_NE(storage, nullptr);

    ASSERT_TRUE(saveJpeg(storage, PointI{255, 1}, 11, QByteArray("left")));
    ASSERT_TRUE(saveJpeg(storage, PointI{256, 1}, 11, QByteArray("right")));
    EXPECT_TRUE(QFileInfo::exists(root + QStringLiteral("z12/0/0/0.0.sqlitedb")));
    EXPECT_TRUE(QFileInfo::exists(root + QStringLiteral("z12/0/0/1.0.sqlitedb")));

    const auto rectInfo =
        storage->tileRectInfo(RectI{254, 0, 258, 2}, 11, MapVersionRequest{}, std::stop_token{});
    ASSERT_TRUE(rectInfo.has_value());
    ASSERT_NE(*rectInfo, nullptr);
    EXPECT_EQ((*rectInfo)->at(PointI{255, 1})->type, TileInfoType::Exists);
    EXPECT_EQ((*rectInfo)->at(PointI{255, 1})->size, 4u);
    EXPECT_EQ((*rectInfo)->at(PointI{256, 1})->type, TileInfoType::Exists);
    EXPECT_EQ((*rectInfo)->at(PointI{256, 1})->size, 5u);
    EXPECT_NE((*rectInfo)->at(PointI{257, 1})->type, TileInfoType::Exists);

    // Over the 2048 cap legacy returns nil (a successful null result), not an error.
    const auto huge = storage->tileRectInfo(RectI{0, 0, 2049, 1}, 13, MapVersionRequest{}, std::stop_token{});
    ASSERT_TRUE(huge.has_value());
    EXPECT_EQ(*huge, nullptr);
}

// The idle close (L-9x, review finding 1) is driven by the GC tick; the pool's clock cannot be
// advanced here, so this checks the wiring: a built storage listens to the tick, a destroyed one
// does not, and a tick on a live storage is harmless.
TEST(SqliteRegionTileStorage, BuiltStorageListensToTheGcTickUntilDestroyed) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    cg::core::Notifier gc;
    {
        ITileStoragePtr storage = build(dir.path() + QStringLiteral("/"), gc);
        ASSERT_NE(storage, nullptr);
        EXPECT_EQ(gc.subscriberCount(), 1u);
        gc.notify();
    }
    EXPECT_EQ(gc.subscriberCount(), 0u);
    gc.notify();
}

TEST(SqliteRegionTileStorage, ScanYieldsTheFixtureTile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    copyFixture(root);
    ITileStoragePtr storage = build(root);
    ASSERT_NE(storage, nullptr);

    auto scan = storage->scanTiles(false, false);
    ASSERT_TRUE(scan.has_value());
    ASSERT_NE(*scan, nullptr);
    TileInfo info;
    PointI tile;
    quint8 zoom = 99;
    ASSERT_TRUE((*scan)->next(info, tile, zoom));
    EXPECT_EQ(info.type, TileInfoType::Exists);
    EXPECT_EQ(tile, PointI(0, 0));
    EXPECT_EQ(zoom, 0);
    EXPECT_FALSE((*scan)->next(info, tile, zoom));
}

TEST(SqliteRegionTileStorage, ReadOnlyStorageConfigIniRefusesASave) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    QFile ini(root + QStringLiteral("StorageConfig.ini"));
    ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
    ini.write("[Common]\r\nIsReadOnly=1\r\n");
    ini.close();
    ITileStoragePtr storage = build(root);
    ASSERT_NE(storage, nullptr);

    EXPECT_TRUE(storage->state()->readAccess);
    EXPECT_FALSE(storage->state()->addAccess);
    EXPECT_FALSE(saveJpeg(storage, PointI{5, 7}, 11, QByteArray("tile bytes")));
    EXPECT_FALSE(QFileInfo::exists(root + QStringLiteral("z12")));
}

// T-0117: stateChangeable() is the region storage's own state holder.
TEST(SqliteRegionTileStorage, StateChangeableIsOwnHolder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const ITileStoragePtr storage = build(dir.path() + QStringLiteral("/"));
    ASSERT_NE(storage, nullptr);
    cg::storage::test::expectStateChangeableIsOwn(*storage, true);
}

// DEVIATION D-160 (T-0276, L-75): a failed read is an error and is not put into the info cache; once the
// file is healthy again the same call reads the tile (legacy cached the NotExists of the failure).
//
TEST(SqliteRegionTileStorage, ReadErrorIsNotCachedAndTheNextCallReadsTheFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    SqliteRegionTileStorageType type(std::make_shared<MapVersionFactory>(), nullptr, noTick());
    const auto* jpeg = ContentTypeRegistry::standard().infoByType(QByteArrayView("image/jpg"));
    auto built = type.build(TileStorageAbilities::readWrite(), makeProjectionSet(), *jpeg,
                            std::make_shared<TilePyramidNotifier>(), root,
                            std::make_shared<cg::storage::TileInfoMemCache>(
                                10, std::chrono::milliseconds(60'000), cg::storage::ClearStrategy::Never));
    ASSERT_TRUE(built.has_value());
    ITileStoragePtr storage = *built;

    ASSERT_TRUE(saveJpeg(storage, PointI{5, 7}, 11, QByteArray("tile bytes")));
    const QString file = storage->tileFileName(PointI{5, 7}, 11, nullptr);
    ASSERT_TRUE(QFileInfo::exists(file));
    // The tile is read from another position of the same region file, so the info cache has no entry.
    auto db = cg::storage::sqlite::SqliteDb::open(file, cg::storage::sqlite::SqliteDb::OpenMode::ReadWrite);
    ASSERT_TRUE(db.has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("ALTER TABLE t RENAME TO t_away")));

    const PointI other{5, 8};
    const auto failed = storage->tileInfo(other, 11, nullptr, LoadMode::WithoutData);
    ASSERT_FALSE(failed.has_value());

    ASSERT_TRUE(db->exec(QByteArrayView("ALTER TABLE t_away RENAME TO t")));
    const auto healthy = storage->tileInfo(other, 11, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(healthy.has_value());
    EXPECT_EQ(healthy->type, TileInfoType::NotExists);
    const auto stored = storage->tileInfo(PointI{5, 7}, 11, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->type, TileInfoType::Exists);
}

// DEVIATION D-160 (T-0276, L-75): a region file that exists but cannot be opened is an error of the read, for
// a Dynamic and a Fixed entry (the failed open of a Fixed entry is cached by the pool, the error must stay);
// a region file that does not exist stays NotExists.
TEST(SqliteRegionTileStorage, UnopenableRegionFileIsAReadErrorAndAMissingOneIsNotExists) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/");
    ITileStoragePtr storage = build(root);
    ASSERT_NE(storage, nullptr);

    // another region file of the same storage, never written: unchanged NotExists
    const auto missing = storage->tileInfo(PointI{5000, 7}, 11, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(missing.has_value());
    EXPECT_EQ(missing->type, TileInfoType::NotExists);

    for (const quint8 zoom : {quint8{5}, quint8{11}}) { // Fixed, Dynamic
        const QString file = storage->tileFileName(PointI{5, 7}, zoom, nullptr);
        ASSERT_TRUE(QDir().mkpath(QFileInfo(file).absolutePath()));
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.close();

        for (int attempt = 0; attempt < 2; ++attempt) {
            const auto info = storage->tileInfo(PointI{5, 7}, zoom, nullptr, LoadMode::WithoutData);
            ASSERT_FALSE(info.has_value()) << "zoom " << int(zoom) << " attempt " << attempt;
            EXPECT_FALSE(info.error().what().isEmpty());
        }
    }
}
