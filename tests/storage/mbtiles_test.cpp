// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-019 (MBTiles flavour). Ports
// Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionMBTiles.pas; see
// docs/specs/storage.md §2.17.1. No legacy DUnit exists for this unit (ticket's own Tests
// section) -- this is the ticket's minimal list: two tiles written to a freshly created file
// (schema, flipped tile_row, metadata byte-for-byte), a foreign-looking `scheme = xyz` file read
// back unflipped, read-back plus delete, and the `format` metadata's MIME fallback for a content
// type outside png/jpg/webp/pbf. One process per case (ctest), each on its own QTemporaryDir --
// no shared state.
#include "cg/core/content_type.h"
#include "cg/core/version.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/sqlite/mbtiles_connection.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_info.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "fixture_path.h"

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::core::ContentTypeRegistry;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::geo::RectI;
using cg::storage::LoadMode;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::sqlite::MbtilesConnection;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256); // EPSG:3857
}

QString tempMbtilesPath(const QTemporaryDir& dir, const QString& baseName = QStringLiteral("test")) {
    return QDir(dir.path()).filePath(baseName + QStringLiteral(".mbtiles"));
}

// Reads every `metadata` row directly (bypassing MbtilesConnection entirely), keyed by name.
QHash<QString, QString> readMetadata(const QString& path) {
    QHash<QString, QString> result;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    if (!db) {
        return result;
    }
    auto stmt = db->prepare(QByteArrayView("SELECT name, value FROM metadata"));
    if (!stmt) {
        return result;
    }
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row) {
            break;
        }
        result.insert(stmt->columnText(0), stmt->columnText(1));
    }
    return result;
}

struct RawTileRow {
    int zoom = 0;
    int column = 0;
    int row = 0;
    QByteArray data;
};

// Reads the `tiles` table's raw rows, bypassing any Y-flip MbtilesConnection would apply on read.
std::vector<RawTileRow> readRawTiles(const QString& path) {
    std::vector<RawTileRow> rows;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    if (!db) {
        return rows;
    }
    auto stmt = db->prepare(
        QByteArrayView("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles ORDER BY tile_row"));
    if (!stmt) {
        return rows;
    }
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row) {
            break;
        }
        rows.push_back(
            RawTileRow{stmt->columnInt(0), stmt->columnInt(1), stmt->columnInt(2), stmt->columnBlob(3)});
    }
    return rows;
}

bool sqlite3CliAvailable() {
    static const bool available = !QStandardPaths::findExecutable(QStringLiteral("sqlite3")).isEmpty();
    return available;
}

QString runSqlite3Cli(const QString& dbPath, const QString& sql) {
    QProcess proc;
    proc.start(QStringLiteral("sqlite3"), {dbPath, sql});
    proc.waitForFinished(5000);
    return QString::fromUtf8(proc.readAllStandardOutput());
}

} // namespace

// --- schema, flipped tile_row, metadata byte-for-byte --------------------------------------

TEST(MbtilesConnection, WritesTwoTilesFlippedRowsAndMetadataMatchingSpec) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempMbtilesPath(dir);

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = MbtilesConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                          ContentTypeRegistry::standard(), makeProjectionSet(),
                                          /*existingInfo=*/nullptr);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    // scheme defaults to "tms" (missing/anything-but-"xyz" => inverted), so tile_row is flipped:
    // tile_row = (1 << zoom) - y - 1.
    const quint8 zoom = 2; // 1 << 2 = 4 rows/columns
    const PointI xyA(1, 0);
    const QByteArray dataA = QByteArrayLiteral("tile-a-bytes");
    const PointI xyB(2, 3);
    const QByteArray dataB = QByteArrayLiteral("tile-b-bytes-longer");

    auto insertedA = conn->insertOrUpdate(xyA, zoom, dataA, /*overwrite=*/false);
    ASSERT_TRUE(insertedA.has_value());
    EXPECT_TRUE(*insertedA);
    auto insertedB = conn->insertOrUpdate(xyB, zoom, dataB, /*overwrite=*/false);
    ASSERT_TRUE(insertedB.has_value());
    EXPECT_TRUE(*insertedB);

    // --- raw tiles rows: tile_column unchanged, tile_row flipped ---
    const auto rawRows = readRawTiles(path);
    ASSERT_EQ(rawRows.size(), 2U);
    // ORDER BY tile_row ascending: B (row 0) then A (row 3).
    EXPECT_EQ(rawRows[0].zoom, zoom);
    EXPECT_EQ(rawRows[0].column, xyB.x());
    EXPECT_EQ(rawRows[0].row, (1 << zoom) - xyB.y() - 1);
    EXPECT_EQ(rawRows[0].data, dataB);
    EXPECT_EQ(rawRows[1].zoom, zoom);
    EXPECT_EQ(rawRows[1].column, xyA.x());
    EXPECT_EQ(rawRows[1].row, (1 << zoom) - xyA.y() - 1);
    EXPECT_EQ(rawRows[1].data, dataA);

    // --- metadata: name/description/format/scheme/crs (§2.17.1) ---
    const QHash<QString, QString> metadata = readMetadata(path);
    EXPECT_EQ(metadata.value(QStringLiteral("name")), QFileInfo(path).completeBaseName());
    EXPECT_EQ(metadata.value(QStringLiteral("description")),
              QStringLiteral("Created by chart-grabber-cli ") +
                  QString::fromLatin1(cg::core::versionString()));
    EXPECT_EQ(metadata.value(QStringLiteral("format")), QStringLiteral("png"));
    EXPECT_EQ(metadata.value(QStringLiteral("scheme")), QStringLiteral("tms"));
    EXPECT_EQ(metadata.value(QStringLiteral("crs")), QStringLiteral("EPSG:3857"));
    EXPECT_EQ(metadata.value(QStringLiteral("minzoom")), QStringLiteral("2"));
    EXPECT_EQ(metadata.value(QStringLiteral("maxzoom")), QStringLiteral("2"));

    // --- bounds/center: recompute the same way updateMetadata() does (same projection calls),
    // then compare the WRITTEN string against that -- pins the maintenance mechanism without
    // hand-transcribing Web Mercator floating output.
    const ProjectionSetPtr projSet = makeProjectionSet();
    const auto rectA = projSet->zoom(zoom)->tilePos2LonLatRect(xyA);
    const auto rectB = projSet->zoom(zoom)->tilePos2LonLatRect(xyB);
    // union(A, B): A is inserted first (seeds bounds), B second (may expand it) -- exactly
    // updateBoundsWithRect's own union order.
    double left = rectA.left, top = rectA.top, right = rectA.right, bottom = rectA.bottom;
    left = std::min(left, rectB.left);
    right = std::max(right, rectB.right);
    top = std::max(top, rectB.top);
    bottom = std::min(bottom, rectB.bottom);
    const QString expectedBounds = QStringLiteral("%1,%2,%3,%4")
                                       .arg(left, 0, 'f', 8)
                                       .arg(bottom, 0, 'f', 8)
                                       .arg(right, 0, 'f', 8)
                                       .arg(top, 0, 'f', 8);
    EXPECT_EQ(metadata.value(QStringLiteral("bounds")), expectedBounds);

    const QString expectedCenter = QStringLiteral("%1,%2,%3")
                                       .arg((left + right) / 2.0, 0, 'f', 8)
                                       .arg((top + bottom) / 2.0, 0, 'f', 8)
                                       .arg(zoom);
    EXPECT_EQ(metadata.value(QStringLiteral("center")), expectedCenter);

    // --- read-back through the connection itself ---
    auto readA = conn->fetchOne(xyA, zoom, LoadMode::WithData);
    ASSERT_TRUE(readA.has_value());
    EXPECT_EQ(readA->type, TileInfoType::Exists);
    EXPECT_EQ(readA->data, dataA);
    EXPECT_EQ(readA->contentType, mainContentType.type); // no magic bytes match plain text -> fallback

    // --- delete ---
    auto deleted = conn->deleteTile(xyA, zoom);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(*deleted);
    auto afterDelete = conn->fetchOne(xyA, zoom, LoadMode::WithData);
    ASSERT_TRUE(afterDelete.has_value());
    EXPECT_EQ(afterDelete->type, TileInfoType::NotExists);
    // B is untouched.
    auto readB = conn->fetchOne(xyB, zoom, LoadMode::WithData);
    ASSERT_TRUE(readB.has_value());
    EXPECT_EQ(readB->type, TileInfoType::Exists);
    EXPECT_EQ(readB->data, dataB);

    // --- bonus (ticket's Tests section): if the sqlite3 CLI is present, cross-check the schema
    // text and metadata rows through it too. Review finding 5: this is wrapped in an `if` (not
    // GTEST_SKIP()) so the CLI's absence only skips this bonus block, not the whole test -- every
    // assertion above still runs and is reported as PASSED, not SKIPPED.
    if (sqlite3CliAvailable()) {
        const QString schema = runSqlite3Cli(path, QStringLiteral(".schema"));
        EXPECT_TRUE(schema.contains(QStringLiteral("CREATE TABLE metadata (name text, value text)")));
        EXPECT_TRUE(schema.contains(QStringLiteral("CREATE UNIQUE INDEX metadata_idx ON metadata (name)")));
        EXPECT_TRUE(schema.contains(QStringLiteral("CREATE TABLE tiles (zoom_level integer, tile_column "
                                                   "integer, tile_row integer, tile_data blob)")));
        EXPECT_TRUE(schema.contains(
            QStringLiteral("CREATE UNIQUE INDEX tiles_idx on tiles (zoom_level, tile_column, tile_row)")));

        const QString cliMetadata =
            runSqlite3Cli(path, QStringLiteral("SELECT name,value FROM metadata WHERE name='scheme'"));
        EXPECT_EQ(cliMetadata.trimmed(), QStringLiteral("scheme|tms"));
    }
}

// --- scheme = xyz reads unflipped -----------------------------------------------------------

TEST(MbtilesConnection, SchemeXyzReadsUnflipped) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempMbtilesPath(dir);

    // Build the file by hand (not through MbtilesConnection, which always writes scheme="tms")
    // so the schema and metadata match a foreign/XYZ-scheme MBTiles file exactly.
    {
        auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWriteCreate);
        ASSERT_TRUE(db.has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE metadata (name text, value text)")).has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView("CREATE UNIQUE INDEX metadata_idx ON metadata (name)")).has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView("CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row "
                                    "integer, tile_data blob)"))
                .has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView(
                         "CREATE UNIQUE INDEX tiles_idx on tiles (zoom_level, tile_column, tile_row)"))
                .has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO metadata (name, value) VALUES ('scheme', 'xyz')"))
                        .has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO metadata (name, value) VALUES ('format', 'png')"))
                        .has_value());
        // A negative `minzoom`, as a foreign/corrupt file could have (MB:407-409) -- review finding
        // 2's vector, extended into this test since it already builds the file by hand.
        ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO metadata (name, value) VALUES ('minzoom', '-1')"))
                        .has_value());
        // A row at (zoom=2, column=1, row=2) -- in an XYZ (not inverted) file, tile_row IS the y
        // coordinate directly, no transform.
        auto insert = db->prepare(QByteArrayView(
            "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (2, 1, 2, ?)"));
        ASSERT_TRUE(insert.has_value());
        ASSERT_TRUE(insert->bindBlob(1, QByteArrayLiteral("xyz-tile")).has_value());
        auto step = insert->step();
        ASSERT_TRUE(step.has_value());
        EXPECT_EQ(*step, SqliteStmt::StepResult::Done);
    }

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = MbtilesConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                          ContentTypeRegistry::standard(), makeProjectionSet(),
                                          /*existingInfo=*/nullptr);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    // Not inverted: PointI(1, 2) maps directly to tile_column=1, tile_row=2 -- no flip.
    auto read = conn->fetchOne(PointI(1, 2), 2, LoadMode::WithData);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->type, TileInfoType::Exists);
    EXPECT_EQ(read->data, QByteArrayLiteral("xyz-tile"));

    // The row that WOULD have matched under TMS inversion (row = (1<<2)-2-1 = 1) must NOT exist.
    auto wrongRow = conn->fetchOne(PointI(1, 1), 2, LoadMode::WithData);
    ASSERT_TRUE(wrongRow.has_value());
    EXPECT_EQ(wrongRow->type, TileInfoType::NotExists);

    // Review finding 2: `center`'s zoom is written through GetCenterStr's `ACenterZoom: Byte`
    // parameter (MB:311, :437); the file's `minzoom` (-1, seeded above) feeds minZoom_, so the
    // write below must truncate it to a Byte ("...,255"), not write "...,-1". A fresh tile forces
    // updateMetadata() to reconcile bounds/minzoom/maxzoom against the DB and rewrite `center`
    // (no prior `bounds`/`center`/`maxzoom` row exists, so isLonLatRectEmpty/TryStrToInt-failure
    // both trigger the reconciliation unconditionally).
    auto inserted = conn->insertOrUpdate(PointI(3, 3), 2, QByteArrayLiteral("new-xyz-tile"),
                                         /*overwrite=*/false);
    ASSERT_TRUE(inserted.has_value());
    EXPECT_TRUE(*inserted);
    const QString center = readMetadata(path).value(QStringLiteral("center"));
    EXPECT_TRUE(center.endsWith(QStringLiteral(",255")))
        << "center=" << center.toStdString(); // truncated Byte(-1) == 255, not "-1"
}

// --- InvertY: legacy's masked-shift + unsigned-wrap formula, not UB (review finding 1) --------

TEST(MbtilesConnection, InvertYMatchesLegacyMaskedShiftAndWrap) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempMbtilesPath(dir);

    // A foreign/corrupt .mbtiles can carry an out-of-range `zoom_level`: the enumeration path
    // (MbtilesEnumTilesStatement::result) feeds it straight from the file into InvertY. Build such
    // rows by hand, the same way SchemeXyzReadsUnflipped does, since
    // MbtilesConnection::insertOrUpdate() would reject zoom >= 31 via checkZoom() -- this is only
    // reachable on the read/enumerate side.
    struct RawRow {
        quint8 zoom;
        int column;
        int row;
        int expectedY; // static_cast<int>((1u << (zoom & 31u)) - quint32(row) - 1u)
    };
    const std::vector<RawRow> rows = {
        {0, 10, 0, 0},           // zoom 0: (1<<0)-0-1 = 0
        {2, 11, 1, 2},           // a normal zoom: (1<<2)-1-1 = 2
        {31, 12, 0, 2147483647}, // zoom 31: (1u<<31)-0-1 = INT_MAX -- no UB
        {32, 13, 3, -3},         // zoom 32: shift count masks to (32 & 31) = 0 -> 1u-3u-1u wraps to -3
    };

    {
        auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWriteCreate);
        ASSERT_TRUE(db.has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE metadata (name text, value text)")).has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView("CREATE UNIQUE INDEX metadata_idx ON metadata (name)")).has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView("CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row "
                                    "integer, tile_data blob)"))
                .has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView(
                         "CREATE UNIQUE INDEX tiles_idx on tiles (zoom_level, tile_column, tile_row)"))
                .has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO metadata (name, value) VALUES ('scheme', 'tms')"))
                        .has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("INSERT INTO metadata (name, value) VALUES ('format', 'png')"))
                        .has_value());
        for (const RawRow& row : rows) {
            auto insert = db->prepare(QByteArrayView(
                "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?, ?, ?, ?)"));
            ASSERT_TRUE(insert.has_value());
            ASSERT_TRUE(insert->bindInt(1, row.zoom).has_value());
            ASSERT_TRUE(insert->bindInt(2, row.column).has_value());
            ASSERT_TRUE(insert->bindInt(3, row.row).has_value());
            ASSERT_TRUE(insert->bindBlob(4, QByteArrayLiteral("x")).has_value());
            auto step = insert->step();
            ASSERT_TRUE(step.has_value());
            EXPECT_EQ(*step, SqliteStmt::StepResult::Done);
        }
    }

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = MbtilesConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                          ContentTypeRegistry::standard(), makeProjectionSet(),
                                          /*existingInfo=*/nullptr);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    // Enumerate (TEnumTilesConnectionStatementMBTiles / MB:541-559): applies InvertY to the raw
    // `tile_row` using the `zoom_level` read straight from that same row.
    QHash<quint8, int> actualY;
    QHash<quint8, int> actualX;
    for (;;) {
        TileInfo info;
        PointI xy;
        quint8 zoom = 0;
        auto next = conn->fetchNext(info, xy, zoom);
        ASSERT_TRUE(next.has_value());
        if (!*next) {
            break;
        }
        actualY.insert(zoom, xy.y());
        actualX.insert(zoom, xy.x());
    }

    for (const RawRow& row : rows) {
        SCOPED_TRACE(testing::Message() << "zoom " << static_cast<int>(row.zoom));
        ASSERT_TRUE(actualY.contains(row.zoom));
        EXPECT_EQ(actualX.value(row.zoom), row.column);
        EXPECT_EQ(actualY.value(row.zoom), row.expectedY);
    }
}

// --- format metadata: png/jpg/jpeg->jpg/webp/pbf kept, anything else -> the MIME string ----

TEST(MbtilesConnection, FormatMetadataMapping) {
    struct Row {
        QByteArray type;
        QByteArray ext;
        QByteArray expectedFormat;
    };
    const std::vector<Row> rows = {
        {"image/png", ".png", "png"},       {"image/jpg", ".jpg", "jpg"},
        {"image/webp", ".webp", "webp"},    {"application/x-protobuf", ".pbf", "pbf"},
        {"image/bmp", ".bmp", "image/bmp"}, // outside {png,jpg,webp,pbf} -> the full MIME string
    };
    // Not table-driven above: legacy's GetFormatStr special-cases a ".jpeg" default extension to
    // "jpg" (:251-254), but a content type descriptor whose OWN GetDefaultExt is ".jpeg" (rather
    // than ".jpg") then immediately fails legacy's own format self-check on the very next line
    // (":178-186" -- neither `SameText('.jpeg', '.jpg')` nor `SameText(type, 'jpg')` holds), in
    // both legacy and the port. That makes the "jpeg -> jpg" mapping unreachable for a freshly
    // created file through any content type descriptor that is internally consistent -- nothing
    // meaningful to pin here without asserting an open failure instead of the mapping.
    int caseIndex = 0;
    for (const Row& row : rows) {
        SCOPED_TRACE(testing::Message() << "case " << caseIndex++ << ": " << row.type.constData());
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = tempMbtilesPath(dir);
        const ContentTypeInfo mainContentType{row.type, row.ext, ContentKind::Bitmap};
        auto opened = MbtilesConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                              ContentTypeRegistry::standard(), makeProjectionSet(),
                                              /*existingInfo=*/nullptr);
        ASSERT_TRUE(opened.has_value());
        EXPECT_EQ(readMetadata(path).value(QStringLiteral("format")), QString::fromUtf8(row.expectedFormat));
    }
}

// --- §7.5 item 5: a real .mbtiles written by a foreign (non-SAS.Planet) tool ----------------

TEST(MbtilesConnection, ForeignMbtilesFile) {
    const QString path = cgStorageFixture(QStringLiteral("cache/foreign/sample.mbtiles"));
    if (!QFileInfo::exists(path)) {
        GTEST_SKIP() << "missing fixture: " << path.toStdString()
                     << " (docs/specs/storage.md §7.5 item 5: an .mbtiles written by a native "
                        "MBTiles tool, not by SAS.Planet -- requested from the user, see "
                        "tests/fixtures/cache/README.md)";
    }
    auto opened = MbtilesConnection::open(
        /*isReadOnly=*/true, path,
        ContentTypeInfo{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"), ContentKind::Bitmap},
        ContentTypeRegistry::standard(), makeProjectionSet(), /*existingInfo=*/nullptr);
    ASSERT_TRUE(opened.has_value());
    EXPECT_TRUE((*opened)->isEnabled());
}
