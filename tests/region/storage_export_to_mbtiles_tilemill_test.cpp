// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of SqliteStorageMbtilesTileMill and the file-local MD5Buffer of
// src/cg/region/storage_export_to_mbtiles, ported from
// Src/RegionProcess/Export/MBTiles/u_StorageExportToMBTiles.pas :550-757 and Src/u_MD5Func.pas :36-42. T-0171
// (E3 of docs/specs/region-export.md); T-0172 tags the feature id.
//
// Rig as T-0170: write into a QTemporaryDir, read the finished file back with storage::sqlite::SqliteDb.
// Expected values are literals taken from the Pascal. Route of the expected sqlite_master texts: the
// fifteen Pascal statements (:551-615, cCRLF = #10) were extracted from the unit by a script and run once
// in SQLite (python sqlite3 module on an in-memory database); the stored text has no "IF NOT EXISTS" and
// no trailing ";". The MD5 literals are the output of md5sum of the same byte strings, upper-cased.
//
// Contrast with LC-1 of T-0170: the Classic tiles table has no unique key and keeps duplicates; here
// map_index is UNIQUE (:589), so a tile exported twice gives ONE map row.
#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/core/version.h"
#include "cg/geo/datum.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/region/storage_export_to_mbtiles.h"
#include "cg/storage/sqlite/mbtiles_connection.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_info.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

using cg::core::ErrorCode;
using cg::geo::LonLatRect;
using cg::geo::PointI;
using cg::region::SqliteStorageMbtilesTileMill;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

struct Cfg {
    QString name = QStringLiteral("map");
    QString description = QStringLiteral("desc");
    QString attribution = QStringLiteral("attr");
    bool isLayer = false;
    QString format = QStringLiteral("png");
    bool xyz = false;
    bool force = true;
    bool replace = true;
};

struct Tile {
    PointI xy;
    quint8 zoom;
    QByteArray data;
};

struct TileRow {
    int zoom = 0;
    int column = 0;
    int row = 0;
    QByteArray data;
    bool operator==(const TileRow&) const = default;
};

void PrintTo(const TileRow& t, std::ostream* os) {
    *os << t.zoom << "/" << t.column << "/" << t.row << "/" << t.data.toHex().constData();
}

const LonLatRect kRectA{-122.5, 47.5, -122.25, 47.25}; // left, top, right, bottom
const LonLatRect kRectB{-121.5, 48.0, -121.0, 47.0};
const LonLatRect kRectRound{0.123456789, 2.5, 1.000000004, -0.000000006};

const QByteArray kPng = QByteArrayLiteral("\x89PNG\r\n\x1a\n\0IEND"); // interior zero byte
const QByteArray kJpg = QByteArrayLiteral("\xff\xd8JFIF-tile-two");
const QByteArray kBlob3 = QByteArrayLiteral("third");
const QByteArray kBlob4 = QByteArrayLiteral("fourth-payload");

// md5sum of the byte strings above (printf | md5sum), upper-cased
const QString kPngId = QStringLiteral("88F953D72F637D4855F5C7163FC4CC86");
const QString kJpgId = QStringLiteral("8B69A57E814A420E72C10A604907A215");
const QString kBlob3Id = QStringLiteral("DD5C8BF51558FFCBE5007071908E9524");
const QString kBlob4Id = QStringLiteral("DAFF55C3A86C4A4A836F596CD657F767");
const QByteArray kAbc = QByteArrayLiteral("abc");
const QString kAbcId = QStringLiteral("900150983CD24FB0D6963F7D28E17F72"); // RFC 1321 test vector

QString fileOf(const QTemporaryDir& d) {
    return d.path() + QStringLiteral("/t.mbtiles");
}

std::unique_ptr<SqliteStorageMbtilesTileMill> makeWriter(const QTemporaryDir& d, const Cfg& c) {
    return std::make_unique<SqliteStorageMbtilesTileMill>(
        d.path() + QStringLiteral("/"), QStringLiteral("t.mbtiles"), c.name, c.description, c.attribution,
        c.isLayer, c.format, c.xyz, c.force, c.replace);
}

cg::core::Result<void> exportOnce(const QTemporaryDir& d, const Cfg& c, const LonLatRect& rect,
                                  const std::vector<quint8>& zooms, const std::vector<Tile>& tiles) {
    auto w = makeWriter(d, c);
    if (auto r = w->open(rect, zooms); !r)
        return r;
    for (const Tile& t : tiles) {
        if (auto r = w->add(t.xy, t.zoom, t.data); !r)
            return r;
    }
    return w->close();
}

std::vector<QStringList> query(const QString& path, const QByteArray& sql, int cols) {
    std::vector<QStringList> rows;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    EXPECT_TRUE(db.has_value());
    if (!db)
        return rows;
    auto stmt = db->prepare(sql);
    EXPECT_TRUE(stmt.has_value());
    if (!stmt)
        return rows;
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row)
            break;
        QStringList row;
        for (int i = 0; i < cols; ++i)
            row.push_back(stmt->columnText(i));
        rows.push_back(row);
    }
    return rows;
}

void execSql(const QString& path, const QByteArray& sql) {
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    ASSERT_TRUE(db.has_value());
    ASSERT_TRUE(db->exec(sql).has_value()) << sql.constData();
}

// the view `tiles`, sorted so that the expectation does not depend on insertion order
std::vector<TileRow> tilesOf(const QString& path) {
    std::vector<TileRow> rows;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    EXPECT_TRUE(db.has_value());
    if (!db)
        return rows;
    auto stmt = db->prepare("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles "
                            "ORDER BY zoom_level, tile_column, tile_row");
    EXPECT_TRUE(stmt.has_value());
    if (!stmt)
        return rows;
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row)
            break;
        rows.push_back({stmt->columnInt(0), stmt->columnInt(1), stmt->columnInt(2), stmt->columnBlob(3)});
    }
    return rows;
}

QString metaValue(const QString& path, const QString& name) {
    const auto rows = query(path, ("SELECT value FROM metadata WHERE name='" + name + "'").toUtf8(), 1);
    return rows.size() == 1 ? rows[0][0] : QStringLiteral("<%1 rows>").arg(rows.size());
}

QStringList metaNames(const QString& path) {
    QStringList names;
    for (const auto& r : query(path, "SELECT name FROM metadata ORDER BY rowid", 1))
        names.push_back(r[0]);
    return names;
}

QStringList masterNames(const QString& path) {
    QStringList names;
    for (const auto& r : query(path, "SELECT name FROM sqlite_master ORDER BY rowid", 1))
        names.push_back(r[0]);
    return names;
}

// :661-672
const QStringList kMetaNames = {"name",        "type",   "version", "description", "format", "bounds",
                                "attribution", "scheme", "minzoom", "maxzoom",     "center", "template"};

// the order of the fifteen statements :551-615
const QStringList kMasterNames = {"grid_key",  "grid_key_lookup", "grid_utfgrid", "grid_utfgrid_lookup",
                                  "images",    "images_id",       "keymap",       "keymap_lookup",
                                  "map",       "map_index",       "metadata",     "name",
                                  "grid_data", "grids",           "tiles"};

} // namespace

// ---- 1. schema text (:551-615) -----------------------------------------------------------------------

TEST(MbtilesTileMill, SqliteMasterHoldsTheFifteenObjectsWithTheirTexts) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());

    const auto rows = query(fileOf(d), "SELECT type, name, sql FROM sqlite_master ORDER BY rowid", 3);
    const std::vector<QStringList> expected = {
        {"table", "grid_key", "CREATE TABLE grid_key (grid_id TEXT, key_name TEXT)"},
        {"index", "grid_key_lookup", "CREATE UNIQUE INDEX grid_key_lookup ON grid_key (grid_id, key_name)"},
        {"table", "grid_utfgrid", "CREATE TABLE grid_utfgrid (grid_id TEXT, grid_utfgrid BLOB)"},
        {"index", "grid_utfgrid_lookup", "CREATE UNIQUE INDEX grid_utfgrid_lookup ON grid_utfgrid (grid_id)"},
        {"table", "images", "CREATE TABLE images (tile_data blob, tile_id text)"},
        {"index", "images_id", "CREATE UNIQUE INDEX images_id ON images (tile_id)"},
        {"table", "keymap", "CREATE TABLE keymap (key_name TEXT, key_json TEXT)"},
        {"index", "keymap_lookup", "CREATE UNIQUE INDEX keymap_lookup ON keymap (key_name)"},
        {"table", "map",
         "CREATE TABLE map (\n   zoom_level INTEGER,\n   tile_column INTEGER,\n   tile_row INTEGER,\n"
         "   tile_id TEXT,\n   grid_id TEXT\n)"},
        {"index", "map_index", "CREATE UNIQUE INDEX map_index ON map (zoom_level, tile_column, tile_row)"},
        {"table", "metadata", "CREATE TABLE metadata (name text, value text)"},
        {"index", "name", "CREATE UNIQUE INDEX name ON metadata (name)"},
        {"view", "grid_data",
         "CREATE VIEW grid_data AS\n    SELECT\n        map.zoom_level AS zoom_level,\n"
         "        map.tile_column AS tile_column,\n        map.tile_row AS tile_row,\n"
         "        keymap.key_name AS key_name,\n        keymap.key_json AS key_json\n    FROM map\n"
         "    JOIN grid_key ON map.grid_id = grid_key.grid_id\n"
         "    JOIN keymap ON grid_key.key_name = keymap.key_name"},
        {"view", "grids",
         "CREATE VIEW grids AS\n    SELECT\n        map.zoom_level AS zoom_level,\n"
         "        map.tile_column AS tile_column,\n        map.tile_row AS tile_row,\n"
         "        grid_utfgrid.grid_utfgrid AS grid\n    FROM map\n"
         "    JOIN grid_utfgrid ON grid_utfgrid.grid_id = map.grid_id"},
        {"view", "tiles",
         "CREATE VIEW tiles AS\n    SELECT\n        map.zoom_level AS zoom_level,\n"
         "        map.tile_column AS tile_column,\n        map.tile_row AS tile_row,\n"
         "        images.tile_data AS tile_data\n    FROM map\n    JOIN images ON images.tile_id = "
         "map.tile_id"},
    };
    EXPECT_EQ(rows, expected);
}

// ---- 2. the twelve metadata rows (:655-672) ----------------------------------------------------------

TEST(MbtilesTileMill, MetadataRowsInOrderWithExactValues) {
    struct Case {
        Cfg cfg;
        LonLatRect rect;
        std::vector<quint8> zooms;
        QStringList values; // the value of each of the twelve rows, in kMetaNames order
    };
    Cfg a;
    a.name = QStringLiteral("Город");
    a.description = QStringLiteral("Описание карты");
    a.attribution = QStringLiteral("© Автор");
    Cfg b;
    b.name = QString();
    b.description = QString();
    b.attribution = QString();
    b.isLayer = true;
    b.xyz = true;
    b.format = QStringLiteral("jpeg");
    const std::vector<Case> cases = {
        {a,
         kRectA,
         {3, 4, 5},
         {"Город", "baselayer", "1.0.0", "Описание карты", "png",
          "-122.50000000,47.25000000,-122.25000000,47.50000000", "© Автор", "tms", "3", "5",
          "-122.37500000,47.37500000,3", ""}},
        {b,
         kRectRound,
         {7, 9, 12},
         {"Unnamed map", "overlay", "1.0.0",
          QStringLiteral("Created by chart-grabber-cli ") + QString::fromLatin1(cg::core::versionString()),
          "jpeg", "0.12345679,-0.00000001,1.00000000,2.50000000", "", "xyz", "7", "12",
          "0.56172840,1.25000000,7", ""}},
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, c.cfg, c.rect, c.zooms, {}).has_value());
        const auto rows = query(fileOf(d), "SELECT name, value FROM metadata ORDER BY rowid", 2);
        ASSERT_EQ(rows.size(), 12U);
        for (qsizetype i = 0; i < 12; ++i) {
            EXPECT_EQ(rows[static_cast<size_t>(i)][0], kMetaNames[i]);
            EXPECT_EQ(rows[static_cast<size_t>(i)][1], c.values[i]) << kMetaNames[i].toStdString();
        }
        // 'template' is an empty TEXT value (:672), not NULL
        EXPECT_EQ(query(fileOf(d), "SELECT typeof(value) FROM metadata WHERE name='template'", 1),
                  (std::vector<QStringList>{{"text"}}));
    }
}

TEST(MbtilesTileMill, AppendMergesBoundsZoomsAndCentersOnTheNewRect) {
    struct Case {
        std::vector<quint8> zooms;
        QString minzoom, maxzoom, center;
    };
    // first export: rect A, zooms 3..5. Second: rect B. Union(A, B) = left -122.5 (A), bottom 47.0 (B),
    // right -121.0 (B), top 48.0 (B). Centre of B is (-121.25, 47.5); the union's would be -121.75
    // (LC-3, :671 vs :666).
    const std::vector<Case> cases = {
        {{4, 4}, "3", "5", "-121.25000000,47.50000000,3"}, // existing 3 is smaller, existing 5 is larger
        {{1, 9}, "1", "9", "-121.25000000,47.50000000,1"}, // new values win
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());
        Cfg second;
        second.force = false;
        ASSERT_TRUE(exportOnce(d, second, kRectB, c.zooms, {{PointI(2, 2), 3, kJpg}}).has_value());
        const QString f = fileOf(d);
        EXPECT_EQ(metaValue(f, "bounds"), "-122.50000000,47.00000000,-121.00000000,48.00000000");
        EXPECT_EQ(metaValue(f, "minzoom"), c.minzoom);
        EXPECT_EQ(metaValue(f, "maxzoom"), c.maxzoom);
        EXPECT_EQ(metaValue(f, "center"), c.center);
        EXPECT_EQ(metaValue(f, "version"), "1.0.0");
        EXPECT_EQ(metaNames(f), kMetaNames);
        EXPECT_EQ(tilesOf(f).size(), 2U);
    }
}

TEST(MbtilesTileMill, AppendRefusesAFileOfAnotherFormatTypeOrScheme) {
    Cfg fmt;
    fmt.force = false;
    fmt.format = QStringLiteral("jpeg");
    Cfg type;
    type.force = false;
    type.isLayer = true;
    Cfg scheme;
    scheme.force = false;
    scheme.xyz = true;
    const std::vector<std::pair<Cfg, QString>> cases = {
        {fmt, "Invalid \"format\" value! Expected \"jpeg\" but found \"png\""},
        {type, "Invalid \"type\" value! Expected \"overlay\" but found \"baselayer\""},
        {scheme, "Invalid \"scheme\" value! Expected \"xyz\" but found \"tms\""},
    };
    for (const auto& [cfg, message] : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {}).has_value());
        auto w = makeWriter(d, cfg);
        const std::vector<quint8> zooms = {3, 5};
        auto r = w->open(kRectA, zooms);
        ASSERT_FALSE(r.has_value()) << message.toStdString();
        EXPECT_EQ(r.error().code(), ErrorCode::Unsupported);
        EXPECT_EQ(r.error().what(), message);
    }
}

// ---- 3. images / map and the tile id (:711-757, u_MD5Func.pas:36-42) ---------------------------------

// T-0179 / FEAT-REG-069: legacy binds a nil buffer for a zero-size tile (:730) -> SQL NULL
TEST(MbtilesTileMill, ZeroByteTileIsStoredAsNullInImagesNotAsEmptyBlob) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, QByteArray()}}).has_value());
    EXPECT_EQ(query(fileOf(d), "SELECT typeof(tile_data) FROM images", 1),
              (std::vector<QStringList>{{"null"}}));
}

TEST(MbtilesTileMill, IdenticalBytesShareOneImagesRowAndTheIdIsUpperCaseMd5) {
    // tms, zoom 3: y = 8 - y - 1
    const std::vector<Tile> tiles = {
        {PointI(1, 1), 3, kPng}, {PointI(2, 2), 3, kJpg}, {PointI(4, 2), 3, kPng}};
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, tiles).has_value());
    const QString f = fileOf(d);

    // two distinct byte strings, one images row each (md5 of the bytes, not of the position)
    const std::vector<QStringList> images = {{kPngId}, {kJpgId}};
    EXPECT_EQ(query(f, "SELECT tile_id FROM images ORDER BY tile_id", 1), images);

    // three map rows in insertion order, two of them with the same id; grid_id is never written
    const std::vector<QStringList> map = {
        {"3", "1", "6", kPngId}, {"3", "2", "5", kJpgId}, {"3", "4", "5", kPngId}};
    EXPECT_EQ(query(f, "SELECT zoom_level, tile_column, tile_row, tile_id FROM map ORDER BY rowid", 4), map);
    EXPECT_EQ(query(f, "SELECT count(*) FROM map WHERE grid_id IS NULL", 1),
              (std::vector<QStringList>{{"3"}}));
}

TEST(MbtilesTileMill, TileIdIsTheUpperCaseMd5OfThePayload) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5},
                           {{PointI(0, 0), 3, kAbc}, {PointI(1, 0), 3, kBlob3}, {PointI(2, 0), 3, kBlob4}})
                    .has_value());
    const std::vector<QStringList> map = {
        {"3", "0", "7", kAbcId}, {"3", "1", "7", kBlob3Id}, {"3", "2", "7", kBlob4Id}};
    EXPECT_EQ(
        query(fileOf(d), "SELECT zoom_level, tile_column, tile_row, tile_id FROM map ORDER BY rowid", 4),
        map);
}

// ---- 4. the view tiles and the y flip (:358-367) -----------------------------------------------------

TEST(MbtilesTileMill, TilesViewReturnsFlippedYForTmsAndUnchangedForXyz) {
    const std::vector<Tile> tiles = {{PointI(1, 0), 2, kPng},
                                     {PointI(5, 6), 3, kJpg},
                                     {PointI(0, 7), 3, kBlob3},
                                     {PointI(0, 0), 0, kBlob4}};
    for (const bool xyz : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Cfg c;
        c.xyz = xyz;
        ASSERT_TRUE(exportOnce(d, c, kRectA, {0, 3}, tiles).has_value());
        // sorted by zoom, column, row. tms: (1 << z) - y - 1 = 4-0-1, 8-6-1, 8-7-1, 1-0-1
        const std::vector<TileRow> expected =
            xyz ? std::vector<TileRow>{{0, 0, 0, kBlob4}, {2, 1, 0, kPng}, {3, 0, 7, kBlob3}, {3, 5, 6, kJpg}}
                : std::vector<TileRow>{
                      {0, 0, 0, kBlob4}, {2, 1, 3, kPng}, {3, 0, 0, kBlob3}, {3, 5, 1, kJpg}};
        EXPECT_EQ(tilesOf(fileOf(d)), expected) << (xyz ? "xyz" : "tms");
    }
}

// ---- 5. duplicates: map_index is UNIQUE (:589), the opposite of LC-1 of T-0170 ----------------------

TEST(MbtilesTileMill, SameTileTwiceGivesOneMapRowReplaceKeepsTheLastIgnoreTheFirst) {
    for (const bool replace : {true, false}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Cfg c;
        c.replace = replace;
        ASSERT_TRUE(
            exportOnce(d, c, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}, {PointI(1, 1), 3, kJpg}}).has_value())
            << replace;
        const QString f = fileOf(d);
        const std::vector<QStringList> map = {{"3", "1", "6", replace ? kJpgId : kPngId}};
        EXPECT_EQ(query(f, "SELECT zoom_level, tile_column, tile_row, tile_id FROM map", 4), map) << replace;
        // one images row per distinct id in both modes
        const std::vector<QStringList> images = {{kPngId}, {kJpgId}};
        EXPECT_EQ(query(f, "SELECT tile_id FROM images ORDER BY tile_id", 1), images) << replace;
        const std::vector<TileRow> tiles = {{3, 1, 6, replace ? kJpg : kPng}};
        EXPECT_EQ(tilesOf(f), tiles) << replace;
    }
}

TEST(MbtilesTileMill, ImagesRowOfAnIdSeenTwiceIsReinsertedByReplaceAndKeptByIgnore) {
    // same bytes at two places: images_id is UNIQUE, so OR REPLACE deletes and re-inserts the row (new
    // rowid, last in rowid order), OR IGNORE keeps the first row in place (:618-619)
    for (const bool replace : {true, false}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Cfg c;
        c.replace = replace;
        ASSERT_TRUE(exportOnce(d, c, kRectA, {3, 5},
                               {{PointI(1, 1), 3, kPng}, {PointI(2, 2), 3, kJpg}, {PointI(3, 3), 3, kPng}})
                        .has_value())
            << replace;
        const std::vector<QStringList> images = {replace ? QStringList{kJpgId} : QStringList{kPngId},
                                                 replace ? QStringList{kPngId} : QStringList{kJpgId}};
        EXPECT_EQ(query(fileOf(d), "SELECT tile_id FROM images ORDER BY rowid", 1), images) << replace;
    }
}

TEST(MbtilesTileMill, SameTileAppendedIntoAnExistingFileFollowsReplaceTiles) {
    for (const bool replace : {true, false}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());
        Cfg c;
        c.force = false;
        c.replace = replace;
        ASSERT_TRUE(exportOnce(d, c, kRectA, {3, 5}, {{PointI(1, 1), 3, kBlob3}}).has_value()) << replace;
        const std::vector<TileRow> tiles = {{3, 1, 6, replace ? kBlob3 : kPng}};
        EXPECT_EQ(tilesOf(fileOf(d)), tiles) << replace;
    }
}

// ---- 6. no index at close, commit of the last rows (:698-709) ----------------------------------------

TEST(MbtilesTileMill, CloseCreatesNoIndexBeyondTheDdl) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    {
        auto w = makeWriter(d, Cfg{});
        const std::vector<quint8> zooms = {3, 5};
        ASSERT_TRUE(w->open(kRectA, zooms).has_value());
        ASSERT_TRUE(w->add(PointI(1, 1), 3, kPng).has_value());
        // destroyed without close(): the DDL is outside the transaction and already in the file
    }
    EXPECT_EQ(masterNames(fileOf(d)), kMasterNames);
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());
    EXPECT_EQ(masterNames(fileOf(d)), kMasterNames);
}

TEST(MbtilesTileMill, RowsAfterTheLastCommitAreCommittedByClose) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    {
        auto w = makeWriter(d, Cfg{});
        const std::vector<quint8> zooms = {3, 5};
        ASSERT_TRUE(w->open(kRectA, zooms).has_value());
        ASSERT_TRUE(w->add(PointI(1, 1), 3, kPng).has_value());
        ASSERT_TRUE(w->commitAndBeginTran().has_value());
        ASSERT_TRUE(w->add(PointI(2, 2), 3, kJpg).has_value());
        ASSERT_TRUE(w->close().has_value());
        EXPECT_TRUE(w->close().has_value()); // a second close is harmless
    }
    const std::vector<TileRow> expected = {{3, 1, 6, kPng}, {3, 2, 5, kJpg}};
    EXPECT_EQ(tilesOf(fileOf(d)), expected);
}

TEST(MbtilesTileMill, RowsAfterTheLastCommitAreLostWithoutClose) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    {
        auto w = makeWriter(d, Cfg{});
        const std::vector<quint8> zooms = {3, 5};
        ASSERT_TRUE(w->open(kRectA, zooms).has_value());
        ASSERT_TRUE(w->add(PointI(1, 1), 3, kPng).has_value());
        ASSERT_TRUE(w->commitAndBeginTran().has_value());
        ASSERT_TRUE(w->add(PointI(2, 2), 3, kJpg).has_value());
    }
    const std::vector<TileRow> expected = {{3, 1, 6, kPng}};
    EXPECT_EQ(tilesOf(fileOf(d)), expected);
}

// ---- 7. insertion order and preparation order (:687-689, :719-756) -----------------------------------
// A foreign file is appended to (force = false), so a step failure is reachable: the DDL keeps existing
// objects (IF NOT EXISTS).

TEST(MbtilesTileMill, ImagesIsInsertedBeforeMapAndAFailureStopsTheSecondInsert) {
    struct Case {
        const char* label;
        QByteArray trigger;
        QStringList imagesIds;
        std::vector<QStringList> map;
    };
    const std::vector<Case> cases = {
        // the images insert fails -> map is untouched
        {"trigger on images",
         "CREATE TRIGGER stop BEFORE INSERT ON images BEGIN SELECT RAISE(ABORT, 'stop'); END",
         {},
         {}},
        // the map insert fails -> the images row of the first step is already there
        {"trigger on map",
         "CREATE TRIGGER stop BEFORE INSERT ON map BEGIN SELECT RAISE(ABORT, 'stop'); END",
         {kPngId},
         {}},
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {}).has_value());
        execSql(fileOf(d), c.trigger);
        Cfg second;
        second.force = false;
        auto w = makeWriter(d, second);
        const std::vector<quint8> zooms = {3, 5};
        ASSERT_TRUE(w->open(kRectA, zooms).has_value()) << c.label;
        auto r = w->add(PointI(1, 1), 3, kPng);
        ASSERT_FALSE(r.has_value()) << c.label;
        ASSERT_TRUE(w->close().has_value()) << c.label;
        QStringList images;
        for (const auto& row : query(fileOf(d), "SELECT tile_id FROM images", 1))
            images.push_back(row[0]);
        EXPECT_EQ(images, c.imagesIds) << c.label;
        EXPECT_EQ(query(fileOf(d), "SELECT zoom_level, tile_column, tile_row, tile_id FROM map", 4), c.map)
            << c.label;
    }
}

TEST(MbtilesTileMill, MapStatementIsPreparedFirstAndTheSecondIsSkippedWhenItFails) {
    // A foreign file whose map and images tables lack the columns of the INSERT texts; the same-named
    // indexes make the DDL skip its own CREATE (IF NOT EXISTS). Both prepares would fail, the error
    // is the one of the first prepared statement.
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    {
        auto db = SqliteDb::open(fileOf(d), SqliteDb::OpenMode::ReadWriteCreate);
        ASSERT_TRUE(db.has_value());
        for (const char* sql : {"CREATE TABLE map (a)", "CREATE INDEX map_index ON map (a)",
                                "CREATE TABLE images (b)", "CREATE INDEX images_id ON images (b)"})
            ASSERT_TRUE(db->exec(sql).has_value()) << sql;
    }
    Cfg c;
    c.force = false;
    auto w = makeWriter(d, c);
    const std::vector<quint8> zooms = {3, 5};
    auto r = w->open(kRectA, zooms);
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().what().contains("zoom_level")) << r.error().what().toStdString(); // map's column
    EXPECT_FALSE(r.error().what().contains("tile_data")) << r.error().what().toStdString();
}

// ---- 8. read-back with the landed cache connector ----------------------------------------------------

TEST(MbtilesTileMill, FileIsReadBackByMbtilesConnectionWithTheSameBytesAtTheSameXyz) {
    using namespace cg;
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    const geo::ProjectionSetPtr projection =
        factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
    const core::ContentTypeInfo png{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                    core::ContentKind::Bitmap};
    for (const bool xyz : {false, true}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Cfg c;
        c.xyz = xyz;
        ASSERT_TRUE(
            exportOnce(d, c, kRectA, {3, 5}, {{PointI(1, 2), 3, kPng}, {PointI(6, 5), 4, kJpg}}).has_value());
        auto opened = storage::sqlite::MbtilesConnection::open(
            true, fileOf(d), png, core::ContentTypeRegistry::standard(), projection, nullptr);
        ASSERT_TRUE(opened.has_value()) << xyz;
        auto conn = std::move(*opened);
        auto a = conn->fetchOne(PointI(1, 2), 3, storage::LoadMode::WithData);
        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(a->type, storage::TileInfoType::Exists) << xyz;
        EXPECT_EQ(a->data, kPng) << xyz;
        auto b = conn->fetchOne(PointI(6, 5), 4, storage::LoadMode::WithData);
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(b->data, kJpg) << xyz;
    }
}
