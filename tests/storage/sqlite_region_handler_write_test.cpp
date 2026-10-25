// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018. Ports the write half of
// Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas: SaveTile (:1075-1229, TNE at
// :1196-1204), DeleteTile (:401-425) / GetSQL_DeleteTile (:854-881), GetTileRectInfo (:444-469) /
// GetSQL_TileRectInfo (:882-943); see docs/specs/storage.md §2.16.1, §2.16.3.
//
// Kept in its own file (not sqlite_region_handler_test.cpp) per the Tech Lead's T-0022B4
// dispatch, to avoid a merge conflict with the T-0022B3 developer's read-side tests landing in
// that file concurrently.
#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"
#include "cg/storage/sqlite/sqlite_region_handler.h"
#include "cg/storage/sqlite/sqlite_region_schema.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_rect_info.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <vector>

#include "fixture_path.h"

using cg::geo::PointI;
using cg::geo::RectI;
using cg::storage::fromLegacyUnixSeconds;
using cg::storage::MapVersionFactory;
using cg::storage::TileDate;
using cg::storage::TileInfoType;
using cg::storage::TileRectInfoEntry;
using cg::storage::sqlite::SaveFlag;
using cg::storage::sqlite::SaveFlags;
using cg::storage::sqlite::SqliteRegionHandler;
using cg::storage::sqlite::SqliteRegionSchema;
using cg::storage::sqlite::SqliteStmt;

namespace {

std::shared_ptr<SqliteRegionSchema> makeSchema(const QString& storagePath) {
    return std::make_shared<SqliteRegionSchema>(storagePath, QByteArray("image/jpg"),
                                                std::make_shared<MapVersionFactory>(), false);
}

// Opens a fresh 8-column (x,y,v,c,s,h,d,b) region file in a scratch temp dir. `storagePath` has no
// s.sql/t.sql/e.sql, so the built-in default schema (docs/specs/storage.md §2.16.1) applies.
std::unique_ptr<SqliteRegionHandler> openFreshHandler(QTemporaryDir& dir) {
    auto schema = makeSchema(dir.path() + QStringLiteral("/"));
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb"));
    return std::make_unique<SqliteRegionHandler>(schema, dbPath, /*useVersionFieldInDb=*/true,
                                                 /*readOnly=*/false);
}

// Opens a region file built from the §7.4 fixture's 5-column t.sql (x,y,d,s,b -- no v, c or h).
std::unique_ptr<SqliteRegionHandler> openFiveColumnHandler(QTemporaryDir& dir) {
    const QString overridesDir =
        QDir(cgStorageFixture(QStringLiteral("cache/sqlite_overrides"))).absolutePath() + QStringLiteral("/");
    auto schema = makeSchema(overridesDir);
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb"));
    return std::make_unique<SqliteRegionHandler>(schema, dbPath, /*useVersionFieldInDb=*/true,
                                                 /*readOnly=*/false);
}

// Reads back s and b (NULL-ness) for one row through the handler's raw db() accessor.
struct RawRow {
    bool found = false;
    int s = -1;
    bool bIsNull = true;
    QByteArray b;
};

RawRow readRow(SqliteRegionHandler& handler, PointI xy) {
    auto stmt = handler.db()->prepare("SELECT s,b FROM t WHERE x=" + QByteArray::number(xy.x()) +
                                      " AND y=" + QByteArray::number(xy.y()));
    if (!stmt) {
        return {};
    }
    auto step = stmt->step();
    if (!step || *step != SqliteStmt::StepResult::Row) {
        return {};
    }
    RawRow row;
    row.found = true;
    row.s = stmt->columnInt(0);
    row.bIsNull = stmt->isNull(1);
    row.b = stmt->columnBlob(1);
    return row;
}

} // namespace

TEST(SqliteRegionHandlerWrite, SaveTileTneStoresZeroSizeAndNullBody) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto handler = openFreshHandler(dir);
    ASSERT_TRUE(handler->isOpened());

    auto result = handler->saveTile(PointI(1, 1), nullptr, fromLegacyUnixSeconds(1700000000),
                                    QByteArrayView(), QByteArray(), SaveFlags());
    ASSERT_TRUE(result);
    EXPECT_TRUE(*result);

    const RawRow row = readRow(*handler, PointI(1, 1));
    ASSERT_TRUE(row.found);
    EXPECT_EQ(row.s, 0);
    EXPECT_TRUE(row.bIsNull);
}

TEST(SqliteRegionHandlerWrite, SaveTileSkipIfSameAsPrevRefusesIdenticalCrcWhenHColumnExists) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto handler = openFreshHandler(dir);
    ASSERT_TRUE(handler->isOpened());
    ASSERT_TRUE(handler->columnInfo().hasCrc);

    const QByteArray original("AAAA");
    auto first = handler->saveTile(PointI(2, 2), nullptr, fromLegacyUnixSeconds(1700000000), QByteArrayView(),
                                   original, SaveFlags(SaveFlag::SkipIfSameAsPrev));
    ASSERT_TRUE(first);
    EXPECT_TRUE(*first);

    // Same bytes -> same CRC-32 -> the WHERE guard's COALESCE matches and the INSERT selects 0
    // rows (spec §2.16.3 "save tile").
    auto second = handler->saveTile(PointI(2, 2), nullptr, fromLegacyUnixSeconds(1800000000),
                                    QByteArrayView(), original, SaveFlags(SaveFlag::SkipIfSameAsPrev));
    ASSERT_TRUE(second);
    EXPECT_FALSE(*second);

    const RawRow row = readRow(*handler, PointI(2, 2));
    ASSERT_TRUE(row.found);
    EXPECT_EQ(row.b, original); // untouched by the refused second save
}

// u_TileStorageSQLiteHandler.pas:1136-1153: the whole SkipIfSameAsPrev WHERE-clause block sits
// INSIDE `if FTBColInfo.ModeV <> vcm_None` -- nested under the version column, not the CRC column.
// On the §7.4 5-column fixture (no v, c OR h) the flag is therefore a complete no-op: legacy never
// even reaches the `if FTBColInfo.HasH ... else GetCheckOtherVersionsBySizeSQLText` choice the
// ticket's Tests section describes for this fixture. Flagged as a ticket/legacy disagreement in
// the T-0022B4 handoff -- legacy wins, so this test pins the REAL behaviour instead of the
// ticket's assumed one: a same-size, different-byte second save still overwrites.
TEST(SqliteRegionHandlerWrite, SaveTileSkipIfSameAsPrevIsANoOpWithoutAVersionColumn) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto handler = openFiveColumnHandler(dir);
    ASSERT_TRUE(handler->isOpened());
    ASSERT_FALSE(handler->columnInfo().hasCrc);
    ASSERT_FALSE(handler->columnInfo().hasVersion);

    const QByteArray original("AAAA"); // size 4
    auto first = handler->saveTile(PointI(3, 3), nullptr, fromLegacyUnixSeconds(1700000000), QByteArrayView(),
                                   original, SaveFlags(SaveFlag::SkipIfSameAsPrev));
    ASSERT_TRUE(first);
    EXPECT_TRUE(*first);

    const QByteArray sameSize("BBBB"); // same size, different bytes
    auto second = handler->saveTile(PointI(3, 3), nullptr, fromLegacyUnixSeconds(1800000000),
                                    QByteArrayView(), sameSize, SaveFlags(SaveFlag::SkipIfSameAsPrev));
    ASSERT_TRUE(second);
    EXPECT_TRUE(*second); // no `v` column -> no WHERE guard was ever built -> plain REPLACE

    const RawRow row = readRow(*handler, PointI(3, 3));
    ASSERT_TRUE(row.found);
    EXPECT_EQ(row.b, sameSize);
}

// The size-only branch of SkipIfSameAsPrev DOES fire when a version column exists but `h` does
// not (a real, if unusual, schema): built by hand here since no such fixture is checked in.
TEST(SqliteRegionHandlerWrite, SaveTileSkipIfSameAsPrevRefusesBySizeWithVersionButNoCrcColumn) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = dir.path() + QStringLiteral("/");
    QFile tSql(storagePath + QStringLiteral("t.sql"));
    ASSERT_TRUE(tSql.open(QIODevice::WriteOnly));
    tSql.write("CREATE TABLE IF NOT EXISTS t (x INTEGER NOT NULL,y INTEGER NOT NULL,"
               "v INTEGER DEFAULT 0 NOT NULL,d INTEGER NOT NULL,s INTEGER DEFAULT 0 NOT NULL,"
               "b BLOB,constraint PK_TB primary key (x,y,v))\n");
    tSql.close();

    auto schema = makeSchema(storagePath);
    const QString dbPath = QDir(dir.path()).filePath(QStringLiteral("region.sqlitedb"));
    SqliteRegionHandler handler(schema, dbPath, /*useVersionFieldInDb=*/true, /*readOnly=*/false);
    ASSERT_TRUE(handler.isOpened());
    ASSERT_TRUE(handler.columnInfo().hasVersion);
    ASSERT_FALSE(handler.columnInfo().hasCrc);

    const QByteArray original("AAAA"); // size 4
    auto first = handler.saveTile(PointI(3, 3), nullptr, fromLegacyUnixSeconds(1700000000), QByteArrayView(),
                                  original, SaveFlags(SaveFlag::SkipIfSameAsPrev));
    ASSERT_TRUE(first);
    EXPECT_TRUE(*first);

    const QByteArray sameSize("BBBB"); // same size, different bytes -- refused, size-only check
    auto second = handler.saveTile(PointI(3, 3), nullptr, fromLegacyUnixSeconds(1800000000), QByteArrayView(),
                                   sameSize, SaveFlags(SaveFlag::SkipIfSameAsPrev));
    ASSERT_TRUE(second);
    EXPECT_FALSE(*second);

    const RawRow row = readRow(handler, PointI(3, 3));
    ASSERT_TRUE(row.found);
    EXPECT_EQ(row.b, original);
}

TEST(SqliteRegionHandlerWrite, SaveTileKeepExistingDoesNotOverwrite) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto handler = openFreshHandler(dir);
    ASSERT_TRUE(handler->isOpened());

    const QByteArray original("ORIG");
    auto first = handler->saveTile(PointI(4, 4), nullptr, fromLegacyUnixSeconds(1700000000), QByteArrayView(),
                                   original, SaveFlags());
    ASSERT_TRUE(first);
    EXPECT_TRUE(*first);

    const QByteArray replacement("NEW!DATA");
    auto second = handler->saveTile(PointI(4, 4), nullptr, fromLegacyUnixSeconds(1800000000),
                                    QByteArrayView(), replacement, SaveFlags(SaveFlag::KeepExisting));
    ASSERT_TRUE(second);
    EXPECT_FALSE(*second); // INSERT OR IGNORE: the (x,y,v) row already exists, nothing changes

    const RawRow row = readRow(*handler, PointI(4, 4));
    ASSERT_TRUE(row.found);
    EXPECT_EQ(row.b, original);
}

TEST(SqliteRegionHandlerWrite, DeleteTileWithNoVersionColumnRemovesTheOnlyRow) {
    // L-49: "no v column" is the ONLY form of DeleteTile that is really "all versions" (the other
    // two filter by the default/requested version). The 5-column fixture has no v column.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto handler = openFiveColumnHandler(dir);
    ASSERT_TRUE(handler->isOpened());
    ASSERT_FALSE(handler->columnInfo().hasVersion);

    auto saved = handler->saveTile(PointI(5, 5), nullptr, fromLegacyUnixSeconds(1700000000), QByteArrayView(),
                                   QByteArray("DATA"), SaveFlags());
    ASSERT_TRUE(saved);
    ASSERT_TRUE(*saved);

    auto deleted = handler->deleteTile(PointI(5, 5), nullptr);
    ASSERT_TRUE(deleted);
    EXPECT_TRUE(*deleted);

    EXPECT_FALSE(readRow(*handler, PointI(5, 5)).found);
}

TEST(SqliteRegionHandlerWrite, TileRectInfoReturnsSizesAndDatesForSeveralTiles) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto handler = openFreshHandler(dir);
    ASSERT_TRUE(handler->isOpened());

    const TileDate dateA = fromLegacyUnixSeconds(1700000000);
    const TileDate dateB = fromLegacyUnixSeconds(1700000100);

    ASSERT_TRUE(*handler->saveTile(PointI(10, 10), nullptr, dateA, QByteArrayView(), QByteArray("12345"),
                                   SaveFlags())); // exists, size 5
    ASSERT_TRUE(*handler->saveTile(PointI(11, 10), nullptr, dateB, QByteArrayView(), QByteArray("1234567890"),
                                   SaveFlags())); // exists, size 10
    ASSERT_TRUE(*handler->saveTile(PointI(10, 11), nullptr, dateA, QByteArrayView(), QByteArray(),
                                   SaveFlags())); // TNE
    // (11,11) left unsaved.

    const RectI destRect{.left = 10, .top = 10, .right = 12, .bottom = 12};
    std::vector<TileRectInfoEntry> items(4); // 2x2, default-constructed (type Unknown)

    std::stop_source neverStops;
    const bool notCancelled =
        handler->tileRectInfo(destRect, nullptr, /*useOtherVersions=*/false, items, neverStops.get_token());
    EXPECT_TRUE(notCancelled);

    // Row-major index = (x-left) + (y-top)*width (tile_rect_info.h).
    const TileRectInfoEntry& a = items[0]; // (10,10)
    EXPECT_EQ(a.type, TileInfoType::Exists);
    EXPECT_EQ(a.size, 5u);
    EXPECT_EQ(a.loadDate, dateA);

    const TileRectInfoEntry& b = items[1]; // (11,10)
    EXPECT_EQ(b.type, TileInfoType::Exists);
    EXPECT_EQ(b.size, 10u);
    EXPECT_EQ(b.loadDate, dateB);

    const TileRectInfoEntry& tne = items[2]; // (10,11)
    EXPECT_EQ(tne.type, TileInfoType::TneExists);
    EXPECT_EQ(tne.size, 0u);

    const TileRectInfoEntry& untouched = items[3]; // (11,11), never saved
    EXPECT_EQ(untouched.type, TileInfoType::Unknown);
}
