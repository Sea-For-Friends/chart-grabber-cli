// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of src/cg/region/storage_export_to_mbtiles (SqliteStorageMbtilesClassic and its base),
// ported from Src/RegionProcess/Export/MBTiles/u_StorageExportToMBTiles.pas :173-546. T-0170 (E2 of
// docs/specs/region-export.md); T-0172 tags the feature id.
//
// Rig: write into a QTemporaryDir, read the finished file back with storage::sqlite::SqliteDb.
// Expected values are literals taken from the Pascal. The expected sqlite_master texts were made by
// running the Pascal DDL / index texts (:404-410, :415) once in the sqlite3 command-line tool (SQLite
// stores them without "IF NOT EXISTS"), not by reading what the class produced.
//
// Register rows (the L rows of T-0170): LC-1 = the tiles table has no unique key, so OR REPLACE /
// OR IGNORE never fire and duplicates are kept (:409, :412-415); LC-2 = metadata values are spliced
// between quotes without escaping (:250); LC-3 = the centre of an appended file is taken from the NEW
// rect with the merged minimum zoom (:465 vs :474).
#include "cg/core/content_type.h"
#include "cg/core/platform/test_support.h"
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
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <unistd.h>
#include <utility>
#include <vector>

using cg::core::ErrorCode;
using cg::geo::LonLatRect;
using cg::geo::PointI;
using cg::region::SqliteStorageMbtilesClassic;
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

// -122.x / 47.x: negative longitude, exact binary fractions.
const LonLatRect kRectA{-122.5, 47.5, -122.25, 47.25}; // left, top, right, bottom
const LonLatRect kRectB{-121.5, 48.0, -121.0, 47.0};   // overlaps nothing of A, extends it
const LonLatRect kRectC{-122.5, 47.0, -121.0, 46.5};
const LonLatRect kRectRound{0.123456789, 2.5, 1.000000004, -0.000000006};

const QByteArray kPng = QByteArrayLiteral("\x89PNG\r\n\x1a\n\0IEND"); // interior zero byte
const QByteArray kJpg = QByteArrayLiteral("\xff\xd8JFIF-tile-two");
const QByteArray kBlob3 = QByteArrayLiteral("third");
const QByteArray kBlob4 = QByteArrayLiteral("fourth-payload");

QString fileOf(const QTemporaryDir& d) {
    return d.path() + QStringLiteral("/t.mbtiles");
}

std::unique_ptr<SqliteStorageMbtilesClassic> makeWriter(const QTemporaryDir& d, const Cfg& c) {
    return std::make_unique<SqliteStorageMbtilesClassic>(
        d.path() + QStringLiteral("/"), QStringLiteral("t.mbtiles"), c.name, c.description, c.attribution,
        c.isLayer, c.format, c.xyz, c.force, c.replace);
}

// open + adds + close; returns the first error.
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

std::vector<TileRow> tilesOf(const QString& path) {
    std::vector<TileRow> rows;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    EXPECT_TRUE(db.has_value());
    if (!db)
        return rows;
    auto stmt = db->prepare("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles ORDER BY rowid");
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

const QStringList kMetaNames = {"name",        "type",    "version", "description", "format", "bounds",
                                "attribution", "minzoom", "maxzoom", "center",      "scheme"};

} // namespace

// ---- 1. schema text and the moment of the index (:403-415, :500-517) ---------------------------------

TEST(MbtilesClassic, SqliteMasterAfterCloseHasTheThreeDdlTextsAndTheIndexLast) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());

    const auto rows = query(fileOf(d), "SELECT type, name, sql FROM sqlite_master ORDER BY rowid", 3);
    const std::vector<QStringList> expected = {
        {"table", "metadata", "CREATE TABLE metadata (name text, value text)"},
        {"index", "metadata_idx", "CREATE UNIQUE INDEX metadata_idx ON metadata (name)"},
        {"table", "tiles",
         "CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob)"},
        {"index", "tiles_idx", "CREATE INDEX tiles_idx on tiles (zoom_level, tile_column, tile_row)"},
    };
    EXPECT_EQ(rows, expected);
}

TEST(MbtilesClassic, NoTilesIndexBeforeCloseAndTheMetadataIsAlreadyCommitted) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    {
        auto w = makeWriter(d, Cfg{});
        const std::vector<quint8> zooms = {3, 5};
        ASSERT_TRUE(w->open(kRectA, zooms).has_value());
        ASSERT_TRUE(w->add(PointI(1, 1), 3, kPng).has_value());
        // destroyed without close(): the open transaction is not committed (:500-517 index at the end)
    }
    const auto rows = query(fileOf(d), "SELECT name FROM sqlite_master ORDER BY rowid", 1);
    const std::vector<QStringList> expected = {{"metadata"}, {"metadata_idx"}, {"tiles"}};
    EXPECT_EQ(rows, expected);
    EXPECT_EQ(metaNames(fileOf(d)), kMetaNames); // writeMetadata committed (:306-330)
}

// ---- 2. the eleven metadata rows (:452-479, :332-356) ------------------------------------------------

TEST(MbtilesClassic, MetadataRowsInOrderWithExactValues) {
    struct Case {
        Cfg cfg;
        LonLatRect rect;
        std::vector<quint8> zooms;
        QStringList values; // the value of each of the eleven rows, in kMetaNames order
    };
    Cfg a;
    a.name = QStringLiteral("Город");
    a.description = QStringLiteral("Описание карты");
    a.attribution = QStringLiteral("© Автор");
    a.format = QStringLiteral("png");
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
         {"Город", "baselayer", "1.3", "Описание карты", "png",
          "-122.50000000,47.25000000,-122.25000000,47.50000000", "© Автор", "3", "5",
          "-122.37500000,47.37500000,3", "tms"}},
        {b,
         kRectRound,
         {7, 9, 12},
         {"Unnamed map", "overlay", "1.3",
          QStringLiteral("Created by chart-grabber-cli ") + QString::fromLatin1(cg::core::versionString()),
          "jpeg", "0.12345679,-0.00000001,1.00000000,2.50000000", "", "7", "12", "0.56172840,1.25000000,7",
          "xyz"}},
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, c.cfg, c.rect, c.zooms, {}).has_value());
        const auto rows = query(fileOf(d), "SELECT name, value FROM metadata ORDER BY rowid", 2);
        ASSERT_EQ(rows.size(), 11U);
        for (qsizetype i = 0; i < 11; ++i) {
            EXPECT_EQ(rows[static_cast<size_t>(i)][0], kMetaNames[i]);
            EXPECT_EQ(rows[static_cast<size_t>(i)][1], c.values[i]) << kMetaNames[i].toStdString();
        }
    }
}

// ---- 3. tiles: content and the y flip (:358-367, :519-546) -------------------------------------------

TEST(MbtilesClassic, TilesStoredWithFlippedYForTmsAndUnchangedForXyz) {
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
        const std::vector<TileRow> expected =
            xyz ? std::vector<TileRow>{{2, 1, 0, kPng}, {3, 5, 6, kJpg}, {3, 0, 7, kBlob3}, {0, 0, 0, kBlob4}}
                // (1 << z) - y - 1: 4-0-1, 8-6-1, 8-7-1, 1-0-1
                : std::vector<TileRow>{
                      {2, 1, 3, kPng}, {3, 5, 1, kJpg}, {3, 0, 0, kBlob3}, {0, 0, 0, kBlob4}};
        EXPECT_EQ(tilesOf(fileOf(d)), expected) << (xyz ? "xyz" : "tms");
    }
}

// T-0179 / FEAT-REG-069: legacy binds a nil buffer for a zero-size tile (:537) -> SQL NULL
TEST(MbtilesClassic, ZeroByteTileIsStoredAsNullNotAsEmptyBlob) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, QByteArray()}}).has_value());
    EXPECT_EQ(query(fileOf(d), "SELECT typeof(tile_data) FROM tiles", 1),
              (std::vector<QStringList>{{"null"}}));
}

// ---- 4. append into an existing file (:429-447) ------------------------------------------------------

TEST(MbtilesClassic, AppendMergesBoundsZoomsAndCentersOnTheNewRect) {
    struct Case {
        std::vector<quint8> zooms;
        QString minzoom, maxzoom, center;
    };
    // first export: rect A, zooms 3..5. Second: rect B. Union(A, B) = left -122.5 (A), bottom 47.0 (B),
    // right -121.0 (B), top 48.0 (B). Centre of B is (-121.25, 47.5); the union's would be -121.75 (LC-3).
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
        EXPECT_EQ(metaNames(f), kMetaNames); // INSERT OR REPLACE keeps the relative order
        EXPECT_EQ(tilesOf(f).size(), 2U);
    }
}

TEST(MbtilesClassic, AppendReadsTheExistingMetadataLeniently) {
    struct Case {
        const char* label;
        QByteArray update; // changes the first export's metadata before the second run
        LonLatRect rect;
        std::vector<quint8> zooms;
        QString bounds, minzoom, maxzoom, center;
    };
    const QString cWithA = "-122.50000000,46.50000000,-121.00000000,47.50000000"; // A united with C
    const QString cOnly = "-122.50000000,46.50000000,-121.00000000,47.00000000";  // kRectC alone
    const std::vector<Case> cases = {
        // existing bounds left,bottom,right,top = -123,46,-122,47.5 (items trimmed) united with C
        {"bounds items trimmed",
         "UPDATE metadata SET value=' -123 , 46 ,-122, 47.5 ' WHERE name='bounds'",
         kRectC,
         {4, 4},
         "-123.00000000,46.00000000,-121.00000000,47.50000000",
         "3",
         "5",
         "-121.75000000,46.75000000,3"},
        {"bounds with three parts",
         "UPDATE metadata SET value='1,2,3' WHERE name='bounds'",
         kRectC,
         {4, 4},
         cOnly,
         "3",
         "5",
         "-121.75000000,46.75000000,3"},
        {"bounds with five parts",
         "UPDATE metadata SET value='1,2,3,4,5' WHERE name='bounds'",
         kRectC,
         {4, 4},
         cOnly,
         "3",
         "5",
         "-121.75000000,46.75000000,3"},
        {"bounds with a non-number",
         "UPDATE metadata SET value='-123,46,-122,x' WHERE name='bounds'",
         kRectC,
         {4, 4},
         cOnly,
         "3",
         "5",
         "-121.75000000,46.75000000,3"},
        {"minzoom not a number",
         "UPDATE metadata SET value='abc' WHERE name='minzoom'",
         kRectC,
         {4, 4},
         cWithA,
         "4",
         "5",
         "-121.75000000,46.75000000,4"},
        {"maxzoom not a number",
         "UPDATE metadata SET value='x' WHERE name='maxzoom'",
         kRectC,
         {4, 4},
         cWithA,
         "3",
         "4",
         "-121.75000000,46.75000000,3"},
        {"key and value trimmed and lower-cased",
         "UPDATE metadata SET name=' MinZoom ', value=' 2 ' WHERE name='minzoom'",
         kRectC,
         {4, 4},
         cWithA,
         "2",
         "5",
         "-121.75000000,46.75000000,2"},
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {}).has_value());
        execSql(fileOf(d), c.update);
        Cfg second;
        second.force = false;
        ASSERT_TRUE(exportOnce(d, second, c.rect, c.zooms, {}).has_value()) << c.label;
        const QString f = fileOf(d);
        EXPECT_EQ(metaValue(f, "bounds"), c.bounds) << c.label;
        EXPECT_EQ(metaValue(f, "minzoom"), c.minzoom) << c.label;
        EXPECT_EQ(metaValue(f, "maxzoom"), c.maxzoom) << c.label;
        EXPECT_EQ(metaValue(f, "center"), c.center) << c.label;
    }
}

TEST(MbtilesClassic, AppendRefusesAFileOfAnotherFormatTypeOrScheme) {
    struct Case {
        const char* label;
        QByteArray update;
        Cfg second;
        QString message;
    };
    Cfg fmt;
    fmt.force = false;
    fmt.format = QStringLiteral("jpeg");
    Cfg type;
    type.force = false;
    type.isLayer = true;
    Cfg scheme;
    scheme.force = false;
    scheme.xyz = true;
    Cfg trimmed;
    trimmed.force = false;
    const std::vector<Case> cases = {
        {"format", "", fmt, "Invalid \"format\" value! Expected \"jpeg\" but found \"png\""},
        {"type", "", type, "Invalid \"type\" value! Expected \"overlay\" but found \"baselayer\""},
        {"scheme", "", scheme, "Invalid \"scheme\" value! Expected \"xyz\" but found \"tms\""},
        {"file key and value trimmed and lower-cased",
         "UPDATE metadata SET name=' Format ', value=' JPEG ' WHERE name='format'", trimmed,
         "Invalid \"format\" value! Expected \"png\" but found \"jpeg\""},
    };
    for (const Case& c : cases) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {}).has_value());
        if (!c.update.isEmpty())
            execSql(fileOf(d), c.update);
        auto w = makeWriter(d, c.second);
        const std::vector<quint8> zooms = {3, 5};
        auto r = w->open(kRectA, zooms);
        ASSERT_FALSE(r.has_value()) << c.label;
        EXPECT_EQ(r.error().code(), ErrorCode::Unsupported) << c.label;
        EXPECT_EQ(r.error().what(), c.message) << c.label;
    }
}

// ---- 5. LC-1 and forceDropTarget (:409, :412-415, :380-388) ------------------------------------------

TEST(MbtilesClassic, DuplicateTilesAreKeptBecauseTheTilesTableHasNoUniqueKey) {
    for (const bool replace : {true, false}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        Cfg c;
        c.replace = replace;
        // LC-1 (register row of T-0170): same tile twice in one run ...
        ASSERT_TRUE(
            exportOnce(d, c, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}, {PointI(1, 1), 3, kJpg}}).has_value())
            << replace;
        // ... and once more into the existing file
        c.force = false;
        ASSERT_TRUE(exportOnce(d, c, kRectA, {3, 5}, {{PointI(1, 1), 3, kBlob3}}).has_value()) << replace;
        const std::vector<TileRow> expected = {{3, 1, 6, kPng}, {3, 1, 6, kJpg}, {3, 1, 6, kBlob3}};
        EXPECT_EQ(tilesOf(fileOf(d)), expected) << replace;
    }
}

TEST(MbtilesClassic, ReplaceTilesPicksOrReplaceOtherwiseOrIgnoreOnAFileWithAUniqueKey) {
    // A foreign file whose tiles table does have a unique key: the statement text decides.
    for (const bool replace : {true, false}) {
        QTemporaryDir d;
        ASSERT_TRUE(d.isValid());
        ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());
        execSql(fileOf(d), "CREATE UNIQUE INDEX tiles_key ON tiles (zoom_level, tile_column, tile_row)");
        Cfg c;
        c.force = false;
        c.replace = replace;
        ASSERT_TRUE(exportOnce(d, c, kRectA, {3, 5}, {{PointI(1, 1), 3, kJpg}}).has_value()) << replace;
        const std::vector<TileRow> expected = {{3, 1, 6, replace ? kJpg : kPng}};
        EXPECT_EQ(tilesOf(fileOf(d)), expected) << replace;
    }
}

TEST(MbtilesClassic, ForceDropTargetDeletesTheOldFile) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {{PointI(1, 1), 3, kPng}}).has_value());
    Cfg c;
    c.name = QStringLiteral("second");
    ASSERT_TRUE(exportOnce(d, c, kRectB, {7, 8}, {{PointI(2, 2), 7, kJpg}}).has_value());
    const std::vector<TileRow> expected = {{7, 2, 125, kJpg}}; // 128 - 2 - 1
    EXPECT_EQ(tilesOf(fileOf(d)), expected);
    EXPECT_EQ(metaValue(fileOf(d), "name"), "second");
    EXPECT_EQ(metaValue(fileOf(d), "minzoom"), "7"); // not merged with the old 3
}

TEST(MbtilesClassic, ForceDropTargetOnAFileThatCannotBeDeletedIsAnIoError) {
    if (cg::core::platform::directoryPermissionsIgnored())
        GTEST_SKIP() << "root ignores directory permissions";
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ASSERT_TRUE(exportOnce(d, Cfg{}, kRectA, {3, 5}, {}).has_value());
    ASSERT_TRUE(QFile::setPermissions(d.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    auto w = makeWriter(d, Cfg{});
    const std::vector<quint8> zooms = {3, 5};
    auto r = w->open(kRectA, zooms);
    QFile::setPermissions(d.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::IoError);
    EXPECT_EQ(r.error().what(), "Can't delete database: " + fileOf(d));
}

// ---- 6. LC-2: no escaping of metadata values (:248-251) ----------------------------------------------

TEST(MbtilesClassic, ApostropheInTheNameFailsOpenAndLeavesTablesWithNoMetadata) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    Cfg c;
    c.name = QStringLiteral("O'Brien"); // LC-2 (register row of T-0170)
    {
        auto w = makeWriter(d, c);
        const std::vector<quint8> zooms = {3, 5};
        EXPECT_FALSE(w->open(kRectA, zooms).has_value());
    }
    ASSERT_TRUE(QFile::exists(fileOf(d)));
    const auto rows = query(fileOf(d), "SELECT name FROM sqlite_master ORDER BY rowid", 1);
    const std::vector<QStringList> expected = {{"metadata"}, {"metadata_idx"}, {"tiles"}};
    EXPECT_EQ(rows, expected);
    EXPECT_TRUE(metaNames(fileOf(d)).isEmpty());
}

TEST(MbtilesClassic, FormatMarkersAndAmpersandInValuesAreWrittenAsTheyAre) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    Cfg c;
    c.name = QStringLiteral("%1 map");
    c.description = QStringLiteral("100%1 & %2 <b>");
    c.attribution = QStringLiteral("%2 %1 &amp; %%");
    ASSERT_TRUE(exportOnce(d, c, kRectA, {3, 5}, {}).has_value());
    EXPECT_EQ(metaValue(fileOf(d), "name"), "%1 map");
    EXPECT_EQ(metaValue(fileOf(d), "description"), "100%1 & %2 <b>");
    EXPECT_EQ(metaValue(fileOf(d), "attribution"), "%2 %1 &amp; %%");
}

// ---- 7. transactions (:235-241, :227-233, :500-517) --------------------------------------------------

TEST(MbtilesClassic, CommitAndBeginTranKeepsAllRowsAfterClose) {
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
    }
    const std::vector<TileRow> expected = {{3, 1, 6, kPng}, {3, 2, 5, kJpg}};
    EXPECT_EQ(tilesOf(fileOf(d)), expected);
}

TEST(MbtilesClassic, CloseTwiceIsHarmless) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    auto w = makeWriter(d, Cfg{});
    const std::vector<quint8> zooms = {3, 5};
    ASSERT_TRUE(w->open(kRectA, zooms).has_value());
    ASSERT_TRUE(w->add(PointI(1, 1), 3, kPng).has_value());
    ASSERT_TRUE(w->close().has_value());
    EXPECT_TRUE(w->close().has_value());
    const std::vector<TileRow> expected = {{3, 1, 6, kPng}};
    EXPECT_EQ(tilesOf(fileOf(d)), expected);
}

TEST(MbtilesClassic, DestroyingAnOpenWriterDoesNotCommitTheLastTransaction) {
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

// ---- 8. read-back with the landed cache connector ----------------------------------------------------

TEST(MbtilesClassic, FileIsReadBackByMbtilesConnectionWithTheSameBytesAtTheSameXyz) {
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
