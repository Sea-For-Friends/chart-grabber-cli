// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-019 (OruxMaps flavour). Ports
// Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFileConnectionOruxMaps.pas; see
// docs/specs/storage.md §2.17.3. No legacy DUnit exists for this unit (ticket's own Tests
// section) -- this is the ticket's minimal list: without a sibling `*.otrk2.xml` the open fails
// with legacy's exact message; with a calibration already decoded (see below), a tile and a rect
// read back through the per-zoom base-point offset; every write on the opened connection is a
// no-op `false`, never an exception. One process per case (ctest), each on its own QTemporaryDir.
//
// On a first open (`existingInfo == nullptr`) the connection parses the sibling `*.otrk2.xml` and
// stores `base_points` itself, as legacy does (FetchMetadata, :202-218); the tests below write a
// small real-shaped XML for that. The defect regression test needs a layer Zoom the XML parser
// never yields (> 23), so it hands OruxmapsConnection::open() a pre-populated `existingInfo` whose
// base64 blob `encodeCalibration()` builds independently of `oruxmaps_connection.cpp`'s decoder
// (ticket rule 10).
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/sqlite/oruxmaps_connection.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_rect_info.h"

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <vector>

#include "fixture_path.h"

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::core::ContentTypeRegistry;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::LonLat;
using cg::geo::PointI;
using cg::geo::PointRounding;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::geo::RectI;
using cg::geo::roundPoint;
using cg::storage::LoadMode;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::sqlite::OruxmapsConnection;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteFileInfo;
using cg::storage::sqlite::SqliteFileInfoPtr;
using cg::storage::sqlite::SqliteStmt;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256); // EPSG:3857
}

QString tempDbPath(const QTemporaryDir& dir) {
    return QDir(dir.path()).filePath(QStringLiteral("sample.db"));
}

// Independently reproduces the byte layout TOruxMapsCalibrationInfo.SaveToStream/
// SaveToBase64String write (Includes/OruxMapsXmlFile.pas:180-267): a length-prefixed UTF-8
// MapName, an int32 layer count, then per layer two more length-prefixed strings (Datum,
// Projection), an int32 Zoom and four little-endian doubles (MinLat, MaxLat, MinLon, MaxLon).
struct CalibrationLayer {
    qint32 zoom = 0;
    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;
};

QByteArray encodeCalibration(const std::vector<CalibrationLayer>& layers) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::DoublePrecision);

    const auto writeString = [&out](const QByteArray& s) {
        out << static_cast<qint32>(s.size());
        if (!s.isEmpty()) {
            out.writeRawData(s.constData(), s.size());
        }
    };

    writeString(QByteArrayLiteral("")); // MapName, unused by BasePointsStrToArray
    out << static_cast<qint32>(layers.size());
    for (const CalibrationLayer& layer : layers) {
        writeString(QByteArrayLiteral("")); // Datum
        writeString(QByteArrayLiteral("")); // Projection
        out << layer.zoom;
        out << layer.minLat;
        out << layer.maxLat;
        out << layer.minLon;
        out << layer.maxLon;
    }
    return bytes.toBase64();
}

SqliteFileInfoPtr makeFileInfoWithCalibration(const QString& path,
                                              const std::vector<CalibrationLayer>& layers) {
    auto info = std::make_shared<SqliteFileInfo>(path, QDateTime::currentDateTime());
    info->addOrSetMetadataValue(QStringLiteral("base_points"),
                                QString::fromLatin1(encodeCalibration(layers)));
    return info;
}

// A real-shaped OruxMaps calibration: top <MapCalibration layers="true" layerLevel="0"> holding one
// <OruxTracker><MapCalibration layers="false" layerLevel="N"> per zoom (Includes/OruxMapsXmlFile.pas
// ParseLayer/Parse).
void writeOtrk2Xml(const QTemporaryDir& dir, const QByteArray& content) {
    QFile xml(QDir(dir.path()).filePath(QStringLiteral("sample.otrk2.xml")));
    ASSERT_TRUE(xml.open(QIODevice::WriteOnly));
    xml.write(content);
}

QByteArray calibrationXml(int level, const LonLat& origin) {
    const QByteArray lat = QByteArray::number(origin.lat, 'f', 6);
    const QByteArray lon = QByteArray::number(origin.lon, 'f', 6);
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<OruxTracker versionCode=\"3.0\">\n"
           " <MapCalibration layers=\"true\" layerLevel=\"0\">\n"
           "  <MapName><![CDATA[sample]]></MapName>\n"
           "  <OruxTracker>\n"
           "   <MapCalibration layers=\"false\" layerLevel=\"" +
           QByteArray::number(level) +
           "\">\n"
           "    <MapChunks xMax=\"1\" yMax=\"1\" datum=\"WGS84\" projection=\"Mercator\"/>\n"
           "    <MapBounds minLat=\"" +
           lat + "\" maxLat=\"" + lat + "\" minLon=\"" + lon + "\" maxLon=\"" + lon +
           "\"/>\n"
           "   </MapCalibration>\n"
           "  </OruxTracker>\n"
           " </MapCalibration>\n"
           "</OruxTracker>\n";
}

// Creates the OruxMaps `tiles(x, y, z, image)` schema and inserts one raw row, bypassing
// OruxmapsConnection entirely (so the test does not depend on the write path it also proves fails).
void createDbWithTile(const QString& path, int x, int y, int z, const QByteArray& data) {
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWriteCreate);
    ASSERT_TRUE(db.has_value());
    ASSERT_TRUE(db->exec(QByteArrayView("CREATE TABLE IF NOT EXISTS tiles (x int, y int, z int, image blob)"))
                    .has_value());
    auto stmt = db->prepare(QByteArrayView("INSERT INTO tiles (x, y, z, image) VALUES (?,?,?,?)"));
    ASSERT_TRUE(stmt.has_value());
    ASSERT_TRUE(stmt->bindInt(1, x).has_value());
    ASSERT_TRUE(stmt->bindInt(2, y).has_value());
    ASSERT_TRUE(stmt->bindInt(3, z).has_value());
    ASSERT_TRUE(stmt->bindBlob(4, data).has_value());
    auto step = stmt->step();
    ASSERT_TRUE(step.has_value());
    ASSERT_EQ(*step, SqliteStmt::StepResult::Done);
}

} // namespace

// --- refuses without a sibling .otrk2.xml (:200-215) ----------------------------------------

TEST(OruxmapsConnection, RefusesWithoutOtrk2Xml) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempDbPath(dir);
    createDbWithTile(path, 0, 0, 1, QByteArrayLiteral("irrelevant"));
    // No *.otrk2.xml sibling written.

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = OruxmapsConnection::open(path, mainContentType, ContentTypeRegistry::standard(),
                                           makeProjectionSet(), /*existingInfo=*/nullptr);
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().what(), QStringLiteral("OruxMaps: .otrk2.xml file not found!"));
}

// --- a calibrated tile and rect read back through the per-zoom offset (:244-330) ------------

TEST(OruxmapsConnection, ReadsCalibratedTileAndRect) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempDbPath(dir);

    const ProjectionSetPtr projSet = makeProjectionSet();
    constexpr quint8 kCalibratedZoom = 3;
    constexpr quint8 kUncalibratedZoom = 5; // no layer for this zoom -> the statement binds z = -1

    const LonLat origin{10.0, 45.0};
    const PointI basePoint =
        roundPoint(projSet->zoom(kCalibratedZoom)->lonLat2TilePosFloat(origin), PointRounding::ToTopLeft);

    const QByteArray tileData = QByteArrayLiteral("orux-tile-bytes");
    // Stored (offset-relative) row: public tile (basePoint + (2,1)) at z=3 lives on disk at (2,1).
    createDbWithTile(path, 2, 1, kCalibratedZoom, tileData);

    writeOtrk2Xml(dir, calibrationXml(kCalibratedZoom, origin)); // first open: base_points comes from it

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = OruxmapsConnection::open(path, mainContentType, ContentTypeRegistry::standard(), projSet,
                                           /*existingInfo=*/nullptr);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);
    ASSERT_TRUE(conn->isEnabled());

    const PointI publicXy(basePoint.x() + 2, basePoint.y() + 1);
    auto read = conn->fetchOne(publicXy, kCalibratedZoom, LoadMode::WithData);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->type, TileInfoType::Exists);
    EXPECT_EQ(read->data, tileData);

    // A zoom with no calibration layer binds z = -1, which this fixture never wrote -> not found,
    // not an error (matches the tile-data path's own "disabled/missing -> successful NotExists").
    auto readUncalibrated = conn->fetchOne(publicXy, kUncalibratedZoom, LoadMode::WithData);
    ASSERT_TRUE(readUncalibrated.has_value());
    EXPECT_EQ(readUncalibrated->type, TileInfoType::NotExists);

    std::stop_source stopSource;
    const RectI rect{basePoint.x(), basePoint.y(), basePoint.x() + 4, basePoint.y() + 4};
    auto rectInfo = conn->fetchRectInfo(rect, kCalibratedZoom, stopSource.get_token());
    ASSERT_TRUE(rectInfo.has_value());
    ASSERT_NE(*rectInfo, nullptr);
    const auto* entry = (*rectInfo)->at(publicXy);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type, TileInfoType::Exists);
    EXPECT_EQ(entry->size, static_cast<quint32>(tileData.size()));

    // --- every write on an opened OruxMaps connection is a harmless `false`, never raised
    // (CreateTables/UpdateMetadata do raise, but insert/update/delete never reach them: legacy
    // leaves FInsertTileStmt/FUpdateTileStmt/FDeleteTileStmt nil, and so does this port).
    auto inserted = conn->insertOrUpdate(PointI(0, 0), kCalibratedZoom, QByteArrayLiteral("x"), true);
    ASSERT_TRUE(inserted.has_value());
    EXPECT_FALSE(*inserted);
    auto deleted = conn->deleteTile(publicXy, kCalibratedZoom);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_FALSE(*deleted);
    // The tile inserted above is untouched by the no-op delete.
    auto stillThere = conn->fetchOne(publicXy, kCalibratedZoom, LoadMode::WithData);
    ASSERT_TRUE(stillThere.has_value());
    EXPECT_EQ(stillThere->type, TileInfoType::Exists);
}

// --- §7.5 item 6: a real OruxMaps .db plus its .otrk2.xml, made by the OruxMaps application ---

TEST(OruxmapsConnection, RealOruxMapsSample) {
    const QString path = cgStorageFixture(QStringLiteral("cache/foreign/sample_oruxmaps.db"));
    if (!QFileInfo::exists(path)) {
        GTEST_SKIP() << "missing fixture: " << path.toStdString()
                     << " (docs/specs/storage.md §7.5 item 6: an OruxMaps .db plus its .otrk2.xml, "
                        "small, any map -- requested from the user, see "
                        "tests/fixtures/cache/README.md)";
    }
    // Next (T-0022C4): open with existingInfo=nullptr and read one tile.
}

// --- a sibling XML that does not describe a calibration (:213) -------------------------------

TEST(OruxmapsConnection, RejectsUnreadableXml) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempDbPath(dir);
    createDbWithTile(path, 0, 0, 1, QByteArrayLiteral("irrelevant"));
    writeOtrk2Xml(dir, QByteArrayLiteral("<OruxTracker/>")); // no <MapCalibration>: Parse is False

    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened = OruxmapsConnection::open(path, mainContentType, ContentTypeRegistry::standard(),
                                           makeProjectionSet(), /*existingInfo=*/nullptr);
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().what(), QStringLiteral("OruxMaps: Error reading XML file!"));
}

// --- L-80: a zoom outside 0..23 whose low bits alias a valid one (legacy indexes out of bounds) ---

TEST(OruxmapsConnection, AliasedZoomStaysInBounds) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = tempDbPath(dir);
    const ProjectionSetPtr projSet = makeProjectionSet();
    constexpr quint8 kAliasedZoom = 5; // Byte(261) == 5 and 261 & 31 == 5

    const LonLat origin{10.0, 45.0};
    const PointI base =
        roundPoint(projSet->zoom(kAliasedZoom)->lonLat2TilePosFloat(origin), PointRounding::ToTopLeft);
    const QByteArray tileData = QByteArrayLiteral("zoom5-tile");
    createDbWithTile(path, 2, 1, kAliasedZoom, tileData);
    createDbWithTile(path, 3, 4, 261, QByteArrayLiteral("zoom261-tile"));

    const CalibrationLayer layer{261, origin.lat, origin.lat, origin.lon, origin.lon};
    auto fileInfo = makeFileInfoWithCalibration(path, {layer});
    const ContentTypeInfo mainContentType{QByteArrayLiteral("image/png"), QByteArrayLiteral(".png"),
                                          ContentKind::Bitmap};
    auto opened =
        OruxmapsConnection::open(path, mainContentType, ContentTypeRegistry::standard(), projSet, fileInfo);
    ASSERT_TRUE(opened.has_value());
    auto conn = std::move(*opened);

    auto read = conn->fetchOne(PointI(base.x() + 2, base.y() + 1), kAliasedZoom, LoadMode::WithData);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->type, TileInfoType::Exists);
    EXPECT_EQ(read->data, tileData);

    std::vector<PointI> seen;
    for (;;) {
        TileInfo info;
        PointI xy;
        quint8 zoom = 0;
        auto next = conn->fetchNext(info, xy, zoom);
        ASSERT_TRUE(next.has_value());
        if (!*next) {
            break;
        }
        EXPECT_EQ(zoom, kAliasedZoom);
        seen.push_back(xy);
    }
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_EQ(seen[0], PointI(base.x() + 2, base.y() + 1));
    EXPECT_EQ(seen[1], PointI(base.x() + 3, base.y() + 4));
}
