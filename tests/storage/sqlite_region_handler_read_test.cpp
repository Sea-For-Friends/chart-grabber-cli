// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018, FEAT-STO-032. Ports the read half of
// Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas: GetTileInfo/CallbackSelectTileInfo
// (:949-1039, :526-671), GetListOfTileVersions/GetListOfVersions/CallbackSelectVersions
// (:773-816, :817-853, :715-772); see docs/specs/storage.md §2.15.1, §2.16.3. Kept in its own file
// (not sqlite_region_handler_test.cpp) per the ticket's parallel-development note: T-0022B4 writes
// the write side in sqlite_region_handler_write.cpp / its own test file at the same time.
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_region_handler.h"
#include "cg/storage/sqlite/sqlite_region_schema.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_info.h"

#include <QByteArray>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

#include "fixture_path.h"

using cg::core::kNoHash;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionInfo;
using cg::storage::MapVersionList;
using cg::storage::TileDate;
using cg::storage::TileInfoType;
using cg::storage::sqlite::SqliteRegionHandler;
using cg::storage::sqlite::SqliteRegionSchema;
using cg::storage::sqlite::VersionColMode;

namespace {

std::shared_ptr<SqliteRegionSchema> makeSchema(const QString& storagePath) {
    return std::make_shared<SqliteRegionSchema>(storagePath, QByteArray("image/jpg"),
                                                std::make_shared<MapVersionFactory>(), /*logging=*/false);
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    ASSERT_EQ(f.write(content), content.size());
}

// A region file whose `t.v` is declared TEXT (so LOGICALSORTING applies), 8 columns like the
// built-in default otherwise, with two rows on the same tile (5,5): v='9' (s=100) and v='10'
// (s=200) -- the ticket's "TEXT-v file holding '9' and '10'" fixture, built with SqliteDb::exec
// per the ticket's instruction (no legacy DUnit exists to port).
std::unique_ptr<SqliteRegionHandler> openTextVersionHandlerWithTwoRows(QTemporaryDir& overridesDir,
                                                                       QTemporaryDir& dbDir) {
    writeFile(overridesDir.path() + QStringLiteral("/t.sql"),
              "CREATE TABLE IF NOT EXISTS t (x INTEGER NOT NULL,y INTEGER NOT NULL,"
              "v TEXT DEFAULT '' NOT NULL,c TEXT,s INTEGER DEFAULT 0 NOT NULL,"
              "h INTEGER DEFAULT 0 NOT NULL,d INTEGER NOT NULL,b BLOB,"
              "constraint PK_TB primary key (x,y,v))\n");

    auto schema = makeSchema(overridesDir.path() + QStringLiteral("/"));
    const QString dbPath = QDir(dbDir.path()).filePath(QStringLiteral("region.sqlitedb"));
    auto handler = std::make_unique<SqliteRegionHandler>(schema, dbPath, /*useVersionFieldInDb=*/true,
                                                         /*readOnly=*/false);
    if (!handler->isOpened()) {
        return handler;
    }
    if (!handler->db()->exec(
            QByteArrayView("INSERT INTO t (x,y,v,s,d,b) VALUES (5,5,'9',100,1000,x'0102')"))) {
        return nullptr;
    }
    if (!handler->db()->exec(
            QByteArrayView("INSERT INTO t (x,y,v,s,d,b) VALUES (5,5,'10',200,2000,x'030405')"))) {
        return nullptr;
    }
    return handler;
}

} // namespace

TEST(SqliteRegionHandlerRead, RepoFixtureTileReadsBackWithSizeDateAndBody) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString src = cgStorageFixture(QStringLiteral("cache/cache_sqlite/map/z1/0/0/0.0.sqlitedb"));
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("0.0.sqlitedb"));
    ASSERT_TRUE(QFile::copy(src, dbPath));

    auto schema = makeSchema(dir.path() + QStringLiteral("/"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());

    auto info =
        handler.tileInfo(cg::geo::PointI(0, 0), nullptr, /*useOtherVersions=*/true, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Exists);
    EXPECT_EQ(info->size, 1435u);
    EXPECT_EQ(info->data.size(), 1435);
    EXPECT_EQ(info->contentType, QByteArray("image/jpg")); // c is NULL -> the storage's main type
    // Measured (docs/specs/storage.md §2.4.3): d=1789643597 -> 2026-09-17 11:13:17 (naive local
    // re-read of the UTC instant), NOT the source file's real mtime.
    EXPECT_EQ(info->loadDate, TileDate(QDate(2026, 9, 17), QTime(11, 13, 17)));
}

TEST(SqliteRegionHandlerRead, MissingTileReportsNotExists) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto schema = makeSchema(dir.path() + QStringLiteral("/"));
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());

    auto info =
        handler.tileInfo(cg::geo::PointI(9, 9), nullptr, /*useOtherVersions=*/false, LoadMode::WithoutData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::NotExists);
}

TEST(SqliteRegionHandlerRead, ZeroSizeNullBodyReadsAsTne) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto schema = makeSchema(dir.path() + QStringLiteral("/"));
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());

    ASSERT_TRUE(handler.db()->exec(QByteArrayView("INSERT INTO t (x,y,v,s,d,b) VALUES (1,1,0,0,1000,NULL)")));

    auto info =
        handler.tileInfo(cg::geo::PointI(1, 1), nullptr, /*useOtherVersions=*/false, LoadMode::WithData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::TneExists);
    EXPECT_EQ(info->size, 0u);
    EXPECT_TRUE(info->data.isEmpty());
}

TEST(SqliteRegionHandlerRead, TextVersionOrdersNewestFirstByLogicalSorting) {
    QTemporaryDir overridesDir;
    QTemporaryDir dbDir;
    ASSERT_TRUE(overridesDir.isValid());
    ASSERT_TRUE(dbDir.isValid());
    auto handler = openTextVersionHandlerWithTwoRows(overridesDir, dbDir);
    ASSERT_TRUE(handler != nullptr && handler->isOpened());
    ASSERT_EQ(handler->columnInfo().versionMode, VersionColMode::Text);

    // no requested version, useOtherVersions -> the latest by LOGICALSORTING ("10" > "9", not
    // lexicographic).
    auto info =
        handler->tileInfo(cg::geo::PointI(5, 5), nullptr, /*useOtherVersions=*/true, LoadMode::WithoutData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Exists);
    EXPECT_EQ(info->size, 200u);
    ASSERT_TRUE(info->version != nullptr);
    EXPECT_EQ(info->version->storeString(), QStringLiteral("10"));

    auto versionsResult = handler->listTileVersions(cg::geo::PointI(5, 5), std::stop_token{});
    ASSERT_TRUE(versionsResult.has_value());
    const MapVersionList& versions = *versionsResult;
    ASSERT_TRUE(versions != nullptr);
    ASSERT_EQ(versions->size(), 2u);
    EXPECT_EQ((*versions)[0]->storeString(), QStringLiteral("10")); // newest first
    EXPECT_EQ((*versions)[1]->storeString(), QStringLiteral("9"));
}

TEST(SqliteRegionHandlerRead, RequestedVersionWinsOverLatest) {
    QTemporaryDir overridesDir;
    QTemporaryDir dbDir;
    ASSERT_TRUE(overridesDir.isValid());
    ASSERT_TRUE(dbDir.isValid());
    auto handler = openTextVersionHandlerWithTwoRows(overridesDir, dbDir);
    ASSERT_TRUE(handler != nullptr && handler->isOpened());

    auto requested = std::make_shared<const MapVersionInfo>(QStringLiteral("9"), kNoHash);
    auto info =
        handler->tileInfo(cg::geo::PointI(5, 5), requested, /*useOtherVersions=*/true, LoadMode::WithoutData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Exists);
    EXPECT_EQ(info->size, 100u); // the v='9' row, not the newer v='10' one (size 200)
    ASSERT_TRUE(info->version != nullptr);
    EXPECT_EQ(info->version->storeString(), QStringLiteral("9"));
}

TEST(SqliteRegionHandlerRead, ListVersionsReturnsEveryDistinctVersionInTheFile) {
    QTemporaryDir overridesDir;
    QTemporaryDir dbDir;
    ASSERT_TRUE(overridesDir.isValid());
    ASSERT_TRUE(dbDir.isValid());
    auto handler = openTextVersionHandlerWithTwoRows(overridesDir, dbDir);
    ASSERT_TRUE(handler != nullptr && handler->isOpened());
    // a different tile, one duplicate version ('9', collapsed by DISTINCT) and one new ('3').
    ASSERT_TRUE(
        handler->db()->exec(QByteArrayView("INSERT INTO t (x,y,v,s,d,b) VALUES (6,6,'9',10,1,x'01')")));
    ASSERT_TRUE(
        handler->db()->exec(QByteArrayView("INSERT INTO t (x,y,v,s,d,b) VALUES (6,6,'3',10,1,x'01')")));

    auto versionsResult = handler->listVersions();
    ASSERT_TRUE(versionsResult.has_value());
    const MapVersionList& versions = *versionsResult;
    ASSERT_TRUE(versions != nullptr);
    ASSERT_EQ(versions->size(), 3u); // {"9", "10", "3"} -- GetListOfVersions has no ORDER BY, so
                                     // only membership is pinned, not the row order.
    QStringList found;
    for (const auto& v : *versions) {
        found << v->storeString();
    }
    found.sort();
    EXPECT_EQ(found, (QStringList{QStringLiteral("10"), QStringLiteral("3"), QStringLiteral("9")}));
}

TEST(SqliteRegionHandlerRead, PreCancelledTokenStoppedBeforeFirstStepYieldsEmptyList) {
    // A token already stopped before the first step() yields an empty (nullptr) list -- same
    // outcome collectVersions() gives for a genuinely empty result (review finding 4, T-0022B3
    // rework: renamed from CancellationDiscardsWhateverWasCollected, which claimed to pin
    // GetMapVersionInfoStatic's "discard whatever was collected" behavior but stops the token
    // before any row is ever collected, so it cannot distinguish that from the old "keep the
    // partial list" semantics).
    QTemporaryDir overridesDir;
    QTemporaryDir dbDir;
    ASSERT_TRUE(overridesDir.isValid());
    ASSERT_TRUE(dbDir.isValid());
    auto handler = openTextVersionHandlerWithTwoRows(overridesDir, dbDir);
    ASSERT_TRUE(handler != nullptr && handler->isOpened());

    std::stop_source alreadyStopped;
    alreadyStopped.request_stop();
    auto versionsResult = handler->listTileVersions(cg::geo::PointI(5, 5), alreadyStopped.get_token());
    ASSERT_TRUE(versionsResult.has_value());
    EXPECT_EQ(*versionsResult, nullptr);
}

TEST(SqliteRegionHandlerRead, ListsDisabledWhenTheTableHasNoVersionColumn) {
    // The §7.4 fixture's t.sql has no v/c/h columns at all.
    const QString overridesDir =
        QDir(cgStorageFixture(QStringLiteral("cache/sqlite_overrides"))).absolutePath() + QStringLiteral("/");
    auto schema = makeSchema(overridesDir);
    QTemporaryDir dbDir;
    ASSERT_TRUE(dbDir.isValid());
    const QString dbPath = QDir(dbDir.path()).filePath(QStringLiteral("region.sqlitedb"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());
    ASSERT_FALSE(handler.columnInfo().hasVersion);

    auto listTile = handler.listTileVersions(cg::geo::PointI(0, 0), std::stop_token{});
    ASSERT_TRUE(listTile.has_value());
    EXPECT_EQ(*listTile, nullptr);

    auto listAll = handler.listVersions();
    ASSERT_TRUE(listAll.has_value());
    EXPECT_EQ(*listAll, nullptr);
}

// DEVIATION D-160 (T-0276, L-75): an SQL failure of the tile read is an error and is logged; legacy
// (u_TileStorageSQLiteHandler.pas:1027-1037) answered NotExists.
TEST(SqliteRegionHandlerRead, SqlFailureOfTheReadIsAnErrorWithTheSqlTextAndStillLogged) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = dir.path() + QStringLiteral("/");
    auto schema = std::make_shared<SqliteRegionSchema>(
        storagePath, QByteArray("image/jpg"), std::make_shared<MapVersionFactory>(), /*logging=*/true);
    SqliteRegionHandler handler(schema, QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb")),
                                /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());
    ASSERT_TRUE(handler.db()->exec(QByteArrayView("DROP TABLE t")));

    const auto info =
        handler.tileInfo(cg::geo::PointI(9, 9), nullptr, /*useOtherVersions=*/false, LoadMode::WithoutData);
    ASSERT_FALSE(info.has_value());
    EXPECT_TRUE(info.error().what().contains(QStringLiteral("no such table")))
        << qPrintable(info.error().what());

    QFile log(storagePath + QStringLiteral("sqlite.log"));
    ASSERT_TRUE(log.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(log.readAll());
    EXPECT_TRUE(content.contains(QStringLiteral("(select)\t"))) << qPrintable(content);
    EXPECT_TRUE(content.contains(QStringLiteral("no such table"))) << qPrintable(content);
}

// A garbage file opened the way the read-only storage opens it (immutable URI, nothing is run on
// open): the first read fails and says so.
TEST(SqliteRegionHandlerRead, GarbageFileOpenedReadOnlyIsAnErrorOnTheRead) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb"));
    writeFile(dbPath, QByteArray(4096, 'x'));
    auto schema = makeSchema(dir.path() + QStringLiteral("/"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/true);
    ASSERT_TRUE(handler.isOpened());

    const auto info =
        handler.tileInfo(cg::geo::PointI(0, 0), nullptr, /*useOtherVersions=*/false, LoadMode::WithoutData);
    ASSERT_FALSE(info.has_value());
    EXPECT_FALSE(info.error().what().isEmpty());
}
