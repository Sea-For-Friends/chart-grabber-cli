// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-022. Ports the sniffer, metadata reader and content-type / projection chains of
// u_TileStorageSQLiteFileImporter.pas:234-527 and :763-805; docs/specs/storage.md §2.17.4-§2.17.5.
// Every file is built here with SqliteDb::exec (a table with no row has no columns for the sniffer,
// so each table gets one row).
#include "cg/core/content_type.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/sqlite/sqlite_file_detect.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "fixture_path.h"

using cg::core::ContentTypeRegistry;
using namespace cg::storage::sqlite;

namespace {

const char* const kMbTiles[] = {
    "CREATE TABLE metadata (name text, value text)", "INSERT INTO metadata VALUES ('name', 'x')",
    "CREATE TABLE tiles (zoom_level int, tile_column int, tile_row int, tile_data blob)",
    "INSERT INTO tiles VALUES (1, 0, 0, x'89504E470D0A1A0A00')"};
const char* const kRMapsTiles[] = {"CREATE TABLE tiles (x int, y int, z int, s int, image blob)",
                                   "INSERT INTO tiles VALUES (0, 0, 1, 0, x'FFD8FF00')"};

// Builds `name` in `dir` from the statements; returns its path.
QString makeDb(const QTemporaryDir& dir, const QString& name, std::initializer_list<const char*> statements,
               const char* const* extra = nullptr, int extraCount = 0) {
    const QString path = dir.filePath(name);
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWriteCreate);
    EXPECT_TRUE(db.has_value());
    if (!db) {
        return path;
    }
    for (const char* sql : statements) {
        EXPECT_TRUE(db->exec(QByteArrayView(sql)).has_value()) << sql;
    }
    for (int i = 0; i < extraCount; ++i) {
        EXPECT_TRUE(db->exec(QByteArrayView(extra[i])).has_value()) << extra[i];
    }
    return path;
}

std::optional<SqliteFileFormat> detect(const QString& path) {
    auto r = detectSqliteFileFormat(path);
    EXPECT_TRUE(r.has_value());
    return r ? *r : std::nullopt;
}

} // namespace

TEST(SqliteFileDetect, MbTiles) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = makeDb(dir, QStringLiteral("a.mbtiles"), {}, kMbTiles, 4);
    EXPECT_EQ(detect(path), SqliteFileFormat::MbTiles);
    // The same content under another extension is nothing.
    ASSERT_TRUE(QFile::copy(path, dir.filePath(QStringLiteral("a.sqlite"))));
    EXPECT_EQ(detect(dir.filePath(QStringLiteral("a.sqlite"))), std::nullopt);
}

TEST(SqliteFileDetect, RMapsNeedsInfoMinMaxZoom) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Upper-case extension and column names: compared case-insensitively.
    const QString path = makeDb(
        dir, QStringLiteral("a.RMAPS"),
        {"CREATE TABLE info (MinZoom int, MaxZoom int)", "INSERT INTO info VALUES (1, 2)"}, kRMapsTiles, 2);
    EXPECT_EQ(detect(path), SqliteFileFormat::RMaps);
    const QString noInfo = makeDb(dir, QStringLiteral("b.rmaps"), {}, kRMapsTiles, 2);
    EXPECT_EQ(detect(noInfo), std::nullopt);
}

TEST(SqliteFileDetect, SqliteDbRefinement) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    EXPECT_EQ(detect(makeDb(dir, QStringLiteral("plain.sqlitedb"), {}, kRMapsTiles, 2)),
              SqliteFileFormat::RMaps);
    EXPECT_EQ(
        detect(makeDb(dir, QStringLiteral("osm.sqlitedb"),
                      {"CREATE TABLE info (ellipsoid int)", "INSERT INTO info VALUES (0)"}, kRMapsTiles, 2)),
        SqliteFileFormat::OsmAnd);
    EXPECT_EQ(
        detect(makeDb(dir, QStringLiteral("locus.sqlitedb"),
                      {"CREATE TABLE info (zooms text)", "INSERT INTO info VALUES ('1')"}, kRMapsTiles, 2)),
        SqliteFileFormat::Locus);
    // Both column sets: Locus is tested first and wins.
    EXPECT_EQ(
        detect(makeDb(dir, QStringLiteral("both.sqlitedb"),
                      {"CREATE TABLE info (ellipsoid int, provider int)", "INSERT INTO info VALUES (0, 1)"},
                      kRMapsTiles, 2)),
        SqliteFileFormat::Locus);
}

TEST(SqliteFileDetect, OruxMapsNeedsSiblingXml) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const char* const tiles[] = {"CREATE TABLE tiles (x int, y int, z int, image blob)",
                                 "INSERT INTO tiles VALUES (0, 0, 1, x'FFD8FF00')"};
    const QString path = makeDb(dir, QStringLiteral("OruxMapsImages.db"), {}, tiles, 2);
    EXPECT_EQ(detect(path), std::nullopt) << "a .db with no sibling *.otrk2.xml";
    QFile xml(dir.filePath(QStringLiteral("map.otrk2.xml")));
    ASSERT_TRUE(xml.open(QIODevice::WriteOnly));
    xml.close();
    EXPECT_EQ(detect(path), SqliteFileFormat::OruxMaps);
}

TEST(SqliteFileDetect, SasGisWins) {
    // info.sasgis beats the `format` column, the ellipsoid and the magic bytes.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(
        makeDb(dir, QStringLiteral("a.sqlitedb"),
               {"CREATE TABLE info (ellipsoid int, format text, sasgis text)",
                "INSERT INTO info VALUES (1, 'gif', '{\"epsg\":3785,\"format\":\"image/png\"}')"},
               kRMapsTiles, 2),
        SqliteDb::OpenMode::ReadOnly);
    ASSERT_TRUE(db.has_value());
    auto metadata = readSqliteFileMetadata(*db, SqliteFileFormat::OsmAnd);
    ASSERT_TRUE(metadata.has_value());
    auto content =
        sqliteFileContentType(*db, SqliteFileFormat::OsmAnd, *metadata, ContentTypeRegistry::standard());
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(content->type, QByteArray("image/png"));
    EXPECT_EQ(content->ext, QStringLiteral(".png"));
    EXPECT_TRUE(content->isBitmap);
    EXPECT_EQ(*sqliteFileProjectionEpsg(*metadata), 3785);
}

TEST(SqliteFileDetect, MbTilesFormatAndCrs) {
    const ContentTypeRegistry& registry = ContentTypeRegistry::standard();
    SqliteFileMetadata metadata;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(makeDb(dir, QStringLiteral("c.mbtiles"), {}, kMbTiles, 4),
                             SqliteDb::OpenMode::ReadOnly);
    ASSERT_TRUE(db.has_value());

    metadata.insert(QStringLiteral("format"), QStringLiteral("png"));
    EXPECT_EQ(sqliteFileContentType(*db, SqliteFileFormat::MbTiles, metadata, registry)->type,
              QByteArray("image/png"));
    metadata.insert(QStringLiteral("format"), QStringLiteral("image/webp"));
    EXPECT_EQ(sqliteFileContentType(*db, SqliteFileFormat::MbTiles, metadata, registry)->type,
              QByteArray("image/webp"));
    metadata.insert(QStringLiteral("format"), QStringLiteral("pbf"));
    const auto unsupported = sqliteFileContentType(*db, SqliteFileFormat::MbTiles, metadata, registry);
    EXPECT_TRUE(unsupported->type.isEmpty());
    EXPECT_EQ(unsupported->ext, QStringLiteral(".pbf"));
    // No format: the first tile's magic bytes (a PNG signature).
    metadata.clear();
    EXPECT_EQ(sqliteFileContentType(*db, SqliteFileFormat::MbTiles, metadata, registry)->type,
              QByteArray("image/png"));

    metadata.insert(QStringLiteral("crs"), QStringLiteral("EPSG:3395"));
    EXPECT_EQ(*sqliteFileProjectionEpsg(metadata), 3395);
}

TEST(SqliteFileDetect, MagicFallbackDefaultAndEpsgDefaults) {
    const ContentTypeRegistry& registry = ContentTypeRegistry::standard();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Bytes no magic entry matches: the default image/jpg.
    auto db = SqliteDb::open(makeDb(dir, QStringLiteral("d.sqlitedb"),
                                    {"CREATE TABLE tiles (x int, y int, z int, s int, image blob)",
                                     "INSERT INTO tiles VALUES (0, 0, 1, 0, x'01020304')",
                                     "CREATE TABLE info (ellipsoid int)", "INSERT INTO info VALUES (1)"}),
                             SqliteDb::OpenMode::ReadOnly);
    ASSERT_TRUE(db.has_value());
    auto metadata = readSqliteFileMetadata(*db, SqliteFileFormat::OsmAnd);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(sqliteFileContentType(*db, SqliteFileFormat::OsmAnd, *metadata, registry)->type,
              QByteArray("image/jpg"));
    EXPECT_EQ(*sqliteFileProjectionEpsg(*metadata), 3395) << "ellipsoid = 1";
    EXPECT_EQ(*sqliteFileProjectionEpsg({}), 3857);
}

TEST(SqliteFileDetect, InvalidSasGisJsonIsAnError) {
    // Legacy: SO() returns nil and the next call on it raises (L-103).
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto db = SqliteDb::open(makeDb(dir, QStringLiteral("e.rmaps"),
                                    {"CREATE TABLE info (minzoom int, maxzoom int, sasgis text)",
                                     "INSERT INTO info VALUES (1, 2, 'not json')"}),
                             SqliteDb::OpenMode::ReadOnly);
    ASSERT_TRUE(db.has_value());
    const auto metadata = readSqliteFileMetadata(*db, SqliteFileFormat::RMaps);
    ASSERT_FALSE(metadata.has_value());
    EXPECT_EQ(metadata.error().code(), cg::core::ErrorCode::ParseError);
}

// --- docs/specs/storage.md §7.5 item 5: files written by their native apps ---------------------

TEST(SqliteFileDetect, ForeignFiles) {
    for (const char* name : {"cache/foreign/sample.mbtiles", "cache/foreign/sample.rmaps",
                             "cache/foreign/sample_osmand.sqlitedb"}) {
        const QString path = cgStorageFixture(QString::fromLatin1(name));
        if (!QFileInfo::exists(path)) {
            GTEST_SKIP() << "missing fixture: " << path.toStdString()
                         << " (docs/specs/storage.md §7.5 item 5, tests/fixtures/cache/README.md)";
        }
    }
}
