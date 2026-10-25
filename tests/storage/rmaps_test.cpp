// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-020, FEAT-STO-021 (RMaps / OsmAnd / Locus flavour). Ports
// Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionRMaps.pas; see
// docs/specs/storage.md §2.17.2. No legacy DUnit exists for this unit (ticket's own Tests
// section) -- this is the ticket's minimal list: each flavour's `info` row against the spec, a
// tile written at z=10 landing at the flavour's own inverted (or not) z, Locus's swapped min/max
// plus kept `zooms`, read-back and delete, and an unsupported format id being rejected. One
// process per case (ctest), each on its own QTemporaryDir -- no shared state.
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/sqlite/rmaps_connection.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_info.h"

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::core::ContentTypeRegistry;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kProjEpsgYandex;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::storage::LoadMode;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::sqlite::RmapsConnection;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteFileFormat;
using cg::storage::sqlite::SqliteStmt;

namespace {

ProjectionSetPtr makeProjectionSet(int epsg = kProjEpsgGoogle) {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(epsg, kTileSplitQuadrate256x256);
}

QString tempPath(const QTemporaryDir& dir, const QString& baseName = QStringLiteral("test")) {
    return QDir(dir.path()).filePath(baseName + QStringLiteral(".sqlitedb"));
}

// Reads the `info` table's one row directly (bypassing RmapsConnection entirely), keyed by column
// name (lowercase, matching legacy's own LowerCase(VName) -- storage.md §2.17.2's schema already
// declares every column in lowercase, so this is a no-op in practice but kept for parity with
// FetchMetadata's own lowering).
QHash<QString, QString> readInfoRow(const QString& path) {
    QHash<QString, QString> result;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    if (!db) {
        return result;
    }
    auto stmt = db->prepare(QByteArrayView("SELECT * FROM info LIMIT 1"));
    if (!stmt) {
        return result;
    }
    auto step = stmt->step();
    if (!step || *step != SqliteStmt::StepResult::Row) {
        return result;
    }
    const int count = stmt->columnCount();
    for (int i = 0; i < count; ++i) {
        result.insert(QString::fromUtf8(stmt->columnName(i)).toLower(),
                      stmt->isNull(i) ? QString() : stmt->columnText(i));
    }
    return result;
}

struct RawTileRow {
    int x = 0;
    int y = 0;
    int z = 0;
    int s = 0;
    QByteArray data;
};

// Reads the `tiles` table's raw rows, bypassing any Z-inversion RmapsConnection would apply.
std::vector<RawTileRow> readRawTiles(const QString& path) {
    std::vector<RawTileRow> rows;
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
    if (!db) {
        return rows;
    }
    auto stmt = db->prepare(QByteArrayView("SELECT x, y, z, s, image FROM tiles ORDER BY z"));
    if (!stmt) {
        return rows;
    }
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row) {
            break;
        }
        rows.push_back(RawTileRow{stmt->columnInt(0), stmt->columnInt(1), stmt->columnInt(2),
                                  stmt->columnInt(3), stmt->columnBlob(4)});
    }
    return rows;
}

constexpr int invertZ(int z) {
    return 17 - z;
}

} // namespace

// --- RMaps: info row, z-inversion, read-back, delete ----------------------------------------

TEST(RmapsConnection, RMapsCreatesInfoRowInvertsZReadsBackAndDeletes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempPath(dir);

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = RmapsConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                        ContentTypeRegistry::standard(), makeProjectionSet(),
                                        /*existingInfo=*/nullptr, SqliteFileFormat::RMaps);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    const quint8 zoom = 10; // always inverted for RMaps: file z = 17 - 10 = 7
    const PointI xyA(1, 0);
    const QByteArray dataA = QByteArrayLiteral("rmaps-tile-a");
    const PointI xyB(2, 3);
    const QByteArray dataB = QByteArrayLiteral("rmaps-tile-b-longer");

    auto insertedA = conn->insertOrUpdate(xyA, zoom, dataA, /*overwrite=*/false);
    ASSERT_TRUE(insertedA.has_value());
    EXPECT_TRUE(*insertedA);
    auto insertedB = conn->insertOrUpdate(xyB, zoom, dataB, /*overwrite=*/false);
    ASSERT_TRUE(insertedB.has_value());
    EXPECT_TRUE(*insertedB);

    const auto rawRows = readRawTiles(path);
    ASSERT_EQ(rawRows.size(), 2U);
    for (const RawTileRow& row : rawRows) {
        EXPECT_EQ(row.z, invertZ(zoom)); // 7
        EXPECT_EQ(row.s, 0);
    }

    const QHash<QString, QString> info = readInfoRow(path);
    EXPECT_EQ(info.value(QStringLiteral("minzoom")), QString::number(invertZ(zoom)));
    EXPECT_EQ(info.value(QStringLiteral("maxzoom")), QString::number(invertZ(zoom)));
    const int epsg = makeProjectionSet()->zoom(0)->projectionType()->projectionEpsg();
    EXPECT_EQ(info.value(QStringLiteral("sasgis")),
              QStringLiteral("{\"epsg\":%1,\"format\":\"image/png\"}").arg(epsg));

    // --- read-back through the connection, at the business (non-inverted) zoom ---
    auto readA = conn->fetchOne(xyA, zoom, LoadMode::WithData);
    ASSERT_TRUE(readA.has_value());
    EXPECT_EQ(readA->type, TileInfoType::Exists);
    EXPECT_EQ(readA->data, dataA);

    // --- delete ---
    auto deleted = conn->deleteTile(xyA, zoom);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(*deleted);
    auto afterDelete = conn->fetchOne(xyA, zoom, LoadMode::WithData);
    ASSERT_TRUE(afterDelete.has_value());
    EXPECT_EQ(afterDelete->type, TileInfoType::NotExists);
    auto readB = conn->fetchOne(xyB, zoom, LoadMode::WithData);
    ASSERT_TRUE(readB.has_value());
    EXPECT_EQ(readB->type, TileInfoType::Exists);
    EXPECT_EQ(readB->data, dataB);
}

// --- Locus: swapped min/max, kept `zooms`, write-side column swap ---------------------------

TEST(RmapsConnection, LocusSwapsMinMaxAndKeepsZooms) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempPath(dir);

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/jpg"), QByteArrayLiteral(".jpg"),
                                          ContentKind::Bitmap};
    auto opened = RmapsConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                        ContentTypeRegistry::standard(), makeProjectionSet(),
                                        /*existingInfo=*/nullptr, SqliteFileFormat::Locus);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    // Business zoom 5 first, then 10: minZoom_/maxZoom_ track {5,10}; the `zooms` list keeps both,
    // sorted ascending in BUSINESS order (RMaps.pas:151-168's insertion-sort), each element
    // inverted independently when written out (:185-197) -- so the string is "12;7", not "7;12".
    auto insertedLow = conn->insertOrUpdate(PointI(0, 0), 5, QByteArrayLiteral("z5"), /*overwrite=*/false);
    ASSERT_TRUE(insertedLow.has_value());
    EXPECT_TRUE(*insertedLow);
    auto insertedHigh = conn->insertOrUpdate(PointI(1, 1), 10, QByteArrayLiteral("z10"), /*overwrite=*/false);
    ASSERT_TRUE(insertedHigh.has_value());
    EXPECT_TRUE(*insertedHigh);

    const auto rawRows = readRawTiles(path);
    ASSERT_EQ(rawRows.size(), 2U);
    EXPECT_EQ(rawRows[0].z, invertZ(10)); // 7, ORDER BY z ascending
    EXPECT_EQ(rawRows[1].z, invertZ(5));  // 12

    const QHash<QString, QString> info = readInfoRow(path);
    // Write-side swap (RMaps.pas:684-688): the `minzoom` column holds InvertZ(business max), the
    // `maxzoom` column holds InvertZ(business min).
    EXPECT_EQ(info.value(QStringLiteral("minzoom")), QString::number(invertZ(10))); // 7
    EXPECT_EQ(info.value(QStringLiteral("maxzoom")), QString::number(invertZ(5)));  // 12
    EXPECT_EQ(info.value(QStringLiteral("zooms")), QStringLiteral("12;7"));

    // Read-back at the business zooms still works (invertedZ_ applied on the read path too).
    auto readLow = conn->fetchOne(PointI(0, 0), 5, LoadMode::WithData);
    ASSERT_TRUE(readLow.has_value());
    EXPECT_EQ(readLow->type, TileInfoType::Exists);
    EXPECT_EQ(readLow->data, QByteArrayLiteral("z5"));
}

// --- OsmAnd: default tilenumbering=BigPlanet inverts Z; ellipsoid flag from EPSG 3395 ---------

TEST(RmapsConnection, OsmAndDefaultInfoRowByEpsg) {
    struct Case {
        int epsg;
        QString expectedEllipsoid;
    };
    const std::vector<Case> cases = {
        {kProjEpsgGoogle, QStringLiteral("0")},
        {kProjEpsgYandex, QStringLiteral("1")},
    };

    for (const Case& c : cases) {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = tempPath(dir, QStringLiteral("test_%1").arg(c.epsg));

        const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                              ContentKind::Bitmap};
        auto opened = RmapsConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                            ContentTypeRegistry::standard(), makeProjectionSet(c.epsg),
                                            /*existingInfo=*/nullptr, SqliteFileFormat::OsmAnd);
        ASSERT_TRUE(opened.has_value());
        auto conn = std::move(*opened);
        ASSERT_TRUE(conn->isEnabled());

        const quint8 zoom = 10;
        auto inserted =
            conn->insertOrUpdate(PointI(4, 5), zoom, QByteArrayLiteral("osmand-tile"), /*overwrite=*/false);
        ASSERT_TRUE(inserted.has_value());
        EXPECT_TRUE(*inserted);

        // SAS.Planet always writes tilenumbering="BigPlanet" itself (RMaps.pas:227-234), so its own
        // freshly created OsmAnd files always come back with invertedZ_ = true.
        const auto rawRows = readRawTiles(path);
        ASSERT_EQ(rawRows.size(), 1U);
        EXPECT_EQ(rawRows[0].z, invertZ(zoom)); // 7

        const QHash<QString, QString> info = readInfoRow(path);
        EXPECT_EQ(info.value(QStringLiteral("ellipsoid")), c.expectedEllipsoid);
        EXPECT_EQ(info.value(QStringLiteral("timecolumn")), QStringLiteral("no"));
        EXPECT_EQ(info.value(QStringLiteral("expireminutes")), QStringLiteral("0"));
        EXPECT_EQ(info.value(QStringLiteral("tilenumbering")), QStringLiteral("BigPlanet"));
        EXPECT_EQ(info.value(QStringLiteral("tilesize")), QStringLiteral("256"));
        EXPECT_EQ(info.value(QStringLiteral("minzoom")), QString::number(invertZ(zoom)));
        EXPECT_EQ(info.value(QStringLiteral("maxzoom")), QString::number(invertZ(zoom)));
    }
}

// --- OsmAnd: a foreign file whose `tilenumbering` is not "BigPlanet" reads unflipped ----------

TEST(RmapsConnection, OsmAndForeignTileNumberingReadsUnflipped) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempPath(dir);

    // Build the file by hand (not through RmapsConnection, which always writes
    // tilenumbering="BigPlanet") so it matches a foreign OsmAnd file with plain (non-inverted) Z.
    {
        auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWriteCreate);
        ASSERT_TRUE(db.has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView("CREATE TABLE info (minzoom int, maxzoom int, ellipsoid int, "
                                    "timecolumn text, expireminutes text, tilenumbering text, tilesize int, "
                                    "sasgis text)"))
                .has_value());
        ASSERT_TRUE(
            db->exec(QByteArrayView("INSERT INTO info (ellipsoid, timecolumn, tilenumbering) VALUES (0, "
                                    "\"no\", \"Simple\")"))
                .has_value());
        ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE tiles (x int, y int, z int, s int, image blob, "
                                            "PRIMARY KEY (x,y,z,s))"))
                        .has_value());
        auto insert =
            db->prepare(QByteArrayView("INSERT INTO tiles (x, y, z, s, image) VALUES (1, 2, 10, 0, ?)"));
        ASSERT_TRUE(insert.has_value());
        ASSERT_TRUE(insert->bindBlob(1, QByteArrayLiteral("foreign-osmand-tile")).has_value());
        auto step = insert->step();
        ASSERT_TRUE(step.has_value());
        EXPECT_EQ(*step, SqliteStmt::StepResult::Done);
    }

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = RmapsConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                        ContentTypeRegistry::standard(), makeProjectionSet(),
                                        /*existingInfo=*/nullptr, SqliteFileFormat::OsmAnd);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    // Not inverted: business zoom 10 maps directly to z=10 in the file, no InvertZ.
    auto read = conn->fetchOne(PointI(1, 2), 10, LoadMode::WithData);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->type, TileInfoType::Exists);
    EXPECT_EQ(read->data, QByteArrayLiteral("foreign-osmand-tile"));

    // The row that WOULD have matched under inversion (z = 17-10 = 7) must NOT exist.
    auto wrongZoom = conn->fetchOne(PointI(1, 2), 7, LoadMode::WithData);
    ASSERT_TRUE(wrongZoom.has_value());
    EXPECT_EQ(wrongZoom->type, TileInfoType::NotExists);
}

// --- an unsupported format id is rejected, matching legacy's two raise sites ------------------

TEST(RmapsConnection, RejectsUnsupportedFormat) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempPath(dir);

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = RmapsConnection::open(/*isReadOnly=*/false, path, mainContentType,
                                        ContentTypeRegistry::standard(), makeProjectionSet(),
                                        /*existingInfo=*/nullptr, SqliteFileFormat::MbTiles);
    ASSERT_FALSE(opened.has_value());
}

// --- a foreign RMaps file (§7.5 item 5) --------------------------------------------------------

TEST(RmapsConnection, ForeignRMapsFile) {
    GTEST_SKIP() << "needs tests/fixtures/cache/foreign/sample.rmaps, a .rmaps file written by a "
                    "native RMaps app (not by SAS.Planet) -- docs/specs/storage.md §7.5 item 5, "
                    "tests/fixtures/cache/README.md";
}
