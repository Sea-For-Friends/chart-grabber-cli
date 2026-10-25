// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-019, FEAT-STO-020, FEAT-STO-021. Ports
// Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFile.pas (+ the builder's format dispatch, the scan
// enumerator and the storage type); see docs/specs/storage.md §2.17. No legacy DUnit exists
// (ticket's Tests section): one save / read / rect / scan / delete round-trip through ITileStorage
// on an MBTiles file and on an RMaps file, and an OruxMaps storage refusing a save. Each case has
// its own QTemporaryDir.
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/cache_converter.h"
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_file_tile_storage_type.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

#include "storage_state_changeable_check.h"

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::geo::RectI;
using cg::storage::ITileStoragePtr;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionRequest;
using cg::storage::TileDate;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileStorageAbilities;
using cg::storage::sqlite::SqliteFileFormat;
using cg::storage::sqlite::SqliteFileTileStorageType;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

ITileStoragePtr build(SqliteFileFormat format, const QString& path) {
    SqliteFileTileStorageType type(format, std::make_shared<MapVersionFactory>(), nullptr);
    const ContentTypeInfo png{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"), ContentKind::Bitmap};
    auto built = type.build(TileStorageAbilities::readWrite(), makeProjectionSet(), png,
                            std::make_shared<TilePyramidNotifier>(), path, nullptr);
    EXPECT_TRUE(built.has_value());
    return built.has_value() ? *built : nullptr;
}

void roundTrip(SqliteFileFormat format, const QString& fileName) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(fileName);
    const ITileStoragePtr storage = build(format, path);
    ASSERT_NE(storage, nullptr);
    EXPECT_EQ(storage->tileFileName({0, 0}, 0, nullptr), path);

    const PointI xy{5, 7};
    constexpr quint8 zoom = 10;
    const QByteArray data("sqlite tile bytes");
    const TileDate loadDate = cg::storage::fromPosixSeconds(1700000000);

    auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/png"), data, true);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(*saved);

    auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isExists());
    EXPECT_EQ(info->data, data);

    auto rect = storage->tileRectInfo(RectI{4, 6, 7, 9}, zoom, MapVersionRequest{}, std::stop_token{});
    ASSERT_TRUE(rect.has_value());
    ASSERT_NE(*rect, nullptr);
    const auto* entry = (*rect)->at(xy);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type, cg::storage::TileInfoType::Exists);
    EXPECT_EQ(entry->size, static_cast<quint32>(data.size()));

    auto scan = storage->scanTiles(false, false);
    ASSERT_TRUE(scan.has_value());
    ASSERT_NE(*scan, nullptr);
    cg::storage::TileInfo scanned;
    PointI scannedXy;
    quint8 scannedZoom = 0;
    ASSERT_TRUE((*scan)->next(scanned, scannedXy, scannedZoom));
    EXPECT_EQ(scannedXy, xy);
    EXPECT_EQ(scannedZoom, zoom);
    EXPECT_FALSE((*scan)->next(scanned, scannedXy, scannedZoom));

    auto deleted = storage->deleteTile(xy, zoom, nullptr);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(*deleted);
    auto after = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->type, cg::storage::TileInfoType::NotExists);
}

} // namespace

TEST(SqliteFileTileStorage, MbtilesRoundTrip) {
    roundTrip(SqliteFileFormat::MbTiles, QStringLiteral("test.mbtiles"));
}

TEST(SqliteFileTileStorage, RmapsRoundTrip) {
    roundTrip(SqliteFileFormat::RMaps, QStringLiteral("test.sqlitedb"));
}

TEST(SqliteFileTileStorage, OruxMapsRefusesSave) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const ITileStoragePtr storage = build(SqliteFileFormat::OruxMaps, QDir(dir.path()).filePath("test.db"));
    ASSERT_NE(storage, nullptr);
    EXPECT_FALSE(storage->state()->addAccess);
    auto saved =
        storage->saveTile({1, 1}, 5, nullptr, TileDate{}, QByteArrayView("image/png"), QByteArray("x"), true);
    ASSERT_TRUE(saved.has_value());
    EXPECT_FALSE(*saved);
}

// u_TileStorageSQLiteFile.pas:205-209: no read access answers nil BEFORE the mem-cache, also when the
// mem-cache holds the tile (a save fills it without read access, :330-336 of the port).
TEST(SqliteFileTileStorage, NoReadAccessAnswersUnknownEvenWithTheTileInTheMemCache) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SqliteFileTileStorageType type(SqliteFileFormat::MbTiles, std::make_shared<MapVersionFactory>(), nullptr);
    const ContentTypeInfo png{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"), ContentKind::Bitmap};
    auto memCache = std::make_shared<cg::storage::TileInfoMemCache>(16, std::chrono::minutes(5),
                                                                    cg::storage::ClearStrategy::ByOldest);
    auto built =
        type.build(TileStorageAbilities{false, false, true, true, true}, makeProjectionSet(), png,
                   std::make_shared<TilePyramidNotifier>(), QDir(dir.path()).filePath("w.mbtiles"), memCache);
    ASSERT_TRUE(built.has_value());
    const PointI xy{5, 7};
    auto saved = (*built)->saveTile(xy, 10, nullptr, cg::storage::fromPosixSeconds(1700000000),
                                    QByteArrayView("image/png"), QByteArray("bytes"), true);
    ASSERT_TRUE(saved.has_value());
    ASSERT_TRUE(*saved);
    ASSERT_TRUE(memCache->get(xy, 10, nullptr, LoadMode::AsIs, false).has_value()); // the cache holds it

    auto info = (*built)->tileInfo(xy, 10, nullptr, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, cg::storage::TileInfoType::Unknown);
    auto infoEx = (*built)->tileInfoEx(xy, 10, MapVersionRequest{}, LoadMode::WithData);
    ASSERT_TRUE(infoEx.has_value());
    EXPECT_EQ(infoEx->type, cg::storage::TileInfoType::Unknown);
}

// FEAT-STO-019, FEAT-STO-029 (D-45): a read-only storage over a missing file fails to open, as
// legacy's `File not found` raise (u_TileStorageSQLiteFileConnection.pas:207); the scan reports it
// through status() and convertCache returns it instead of finishing with no tiles.
TEST(SqliteFileTileStorage, ScanOfMissingReadOnlyFileReportsError) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString missing = QDir(dir.path()).filePath(QStringLiteral("missing.sqlitedb"));
    SqliteFileTileStorageType type(SqliteFileFormat::RMaps, std::make_shared<MapVersionFactory>(), nullptr);
    const ContentTypeInfo png{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"), ContentKind::Bitmap};
    const auto buildSource = [&] {
        auto built = type.build(TileStorageAbilities::readOnly(), makeProjectionSet(), png,
                                std::make_shared<TilePyramidNotifier>(), missing, nullptr);
        return built.has_value() ? *built : nullptr;
    };
    const ITileStoragePtr source = buildSource();
    ASSERT_NE(source, nullptr);

    auto scan = source->scanTiles(false, false);
    ASSERT_TRUE(scan.has_value());
    ASSERT_NE(*scan, nullptr);
    EXPECT_TRUE((*scan)->status().has_value()); // not failed before the first next()
    cg::storage::TileInfo info;
    PointI xy;
    quint8 zoom = 0;
    EXPECT_FALSE((*scan)->next(info, xy, zoom));
    auto st = (*scan)->status();
    ASSERT_FALSE(st.has_value());
    EXPECT_TRUE(st.error().what().contains(QStringLiteral("File not found")));
    EXPECT_FALSE((*scan)->next(info, xy, zoom)); // sticky
    EXPECT_FALSE((*scan)->status().has_value());

    const ITileStoragePtr dest =
        build(SqliteFileFormat::RMaps, QDir(dir.path()).filePath(QStringLiteral("d.sqlitedb")));
    ASSERT_NE(dest, nullptr);
    // A fresh source: the test spent the first open above, and the builder latches a failed open (L-98).
    const ITileStoragePtr fresh = buildSource();
    ASSERT_NE(fresh, nullptr);
    auto converted = cg::storage::convertCache(*fresh, *dest, {}, std::stop_token{});
    ASSERT_FALSE(converted.has_value());
    EXPECT_TRUE(converted.error().what().contains(QStringLiteral("File not found")));
    // 2nd attempt on the same storage: legacy's latch text (L-98), through the same channel.
    converted = cg::storage::convertCache(*fresh, *dest, {}, std::stop_token{});
    ASSERT_FALSE(converted.has_value());
    EXPECT_TRUE(converted.error().what().contains(QStringLiteral("Can't open file")));
    EXPECT_TRUE(converted.error().what().contains(QStringLiteral("as RMaps (SQLite3) database!")));
    // 3rd and later: legacy dereferences a nil enumerator (L-114, D-47); the port returns an error.
    converted = cg::storage::convertCache(*fresh, *dest, {}, std::stop_token{});
    ASSERT_FALSE(converted.has_value());
    EXPECT_TRUE(
        converted.error().what().contains(QStringLiteral("source storage does not support scanning")));
}

// T-0117: stateChangeable() is the SQLite-file storage's own state holder.
TEST(SqliteFileTileStorage, StateChangeableIsOwnHolder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const ITileStoragePtr storage =
        build(SqliteFileFormat::MbTiles, QDir(dir.path()).filePath(QStringLiteral("t.mbtiles")));
    ASSERT_NE(storage, nullptr);
    cg::storage::test::expectStateChangeableIsOwn(*storage, true);
}

// T-0117: the legacy route that changes this storage's state -- a latched failed open disables all
// access (u_TileStorageSQLiteFileConnectionBuilder.pas:118-122, see make() in sqlite_file_tile_storage.cpp)
// -- reaches a subscriber of stateChangeable() once per real transition, with the new snapshot. A read-only
// storage starts with read + scan, so there are two transitions (add/delete/replace are no-ops).
TEST(SqliteFileTileStorage, FailedOpenNotifiesStateSubscriberPerTransition) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SqliteFileTileStorageType type(SqliteFileFormat::RMaps, std::make_shared<MapVersionFactory>(), nullptr);
    const ContentTypeInfo png{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"), ContentKind::Bitmap};
    auto built = type.build(TileStorageAbilities::readOnly(), makeProjectionSet(), png,
                            std::make_shared<TilePyramidNotifier>(),
                            QDir(dir.path()).filePath(QStringLiteral("missing.sqlitedb")), nullptr);
    ASSERT_TRUE(built.has_value());
    const ITileStoragePtr storage = *built;
    ASSERT_NE(storage, nullptr);

    int fired = 0;
    const auto sub = storage->stateChangeable().changed().subscribe([&fired] { ++fired; });
    ASSERT_TRUE(storage->state()->readAccess);

    cg::storage::TileInfo info;
    PointI xy;
    quint8 zoom = 0;
    auto first = storage->scanTiles(false, false); // 1st open fails with "File not found" and latches
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE((*first)->next(info, xy, zoom));
    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(storage->state()->readAccess);

    auto second = storage->scanTiles(false, false); // the latch disables the storage
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE((*second)->next(info, xy, zoom));
    EXPECT_EQ(fired, 2);
    EXPECT_FALSE(storage->state()->readAccess);
    EXPECT_FALSE(storage->state()->scanAccess);
    EXPECT_FALSE(storage->state()->addAccess);
    EXPECT_FALSE(storage->state()->deleteAccess);
    EXPECT_FALSE(storage->state()->replaceAccess);
}
