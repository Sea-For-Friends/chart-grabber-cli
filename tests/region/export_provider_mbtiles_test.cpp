// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/export_provider_mbtiles (Src/RegionProcess/Export/MBTiles/
// u_ExportProviderMBTiles.pas PrepareTask :188-256, with the getters of fr_ExportMBTiles.pas). T-0183.
// FEAT-REG-069 (Export to MBTiles) is contributed to, not closed: the recompose path is absent.
//
// Rig: FakeMapType whose tile storage is a RamTileStorage holding real PNG / JPEG fixture bytes at the
// tiles (x 1..2, y 1) of zoom 2 (the Google grid: the rectangle lon -80..80, lat 50..-50 covers z2 tiles
// x 1..2, y 1..2). The task writes into a QTemporaryDir and the file is read back with SqliteDb and
// with storage::sqlite::MbtilesConnection.
#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/region/export_provider_mbtiles.h"
#include "cg/region/region_process_progress_info.h"
#include "cg/storage/map_version.h"
#include "cg/storage/ram_tile_storage.h"
#include "cg/storage/sqlite/mbtiles_connection.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage_abilities.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <stop_token>
#include <utility>
#include <vector>

#include "../corpus_skip.h"
#include "region_fake_map_type.h"

using namespace cg;
using geo::LonLat;
using geo::PointI;
using region::ExportMbtilesParams;
using region::ExportProviderMbtiles;
using region::RegionProcessProgressInfo;
using region::test::FakeMapType;
using storage::sqlite::SqliteDb;
using storage::sqlite::SqliteStmt;

namespace {

using TileMap = std::map<std::pair<int, int>, QByteArray>; // x, y of zoom 2

geo::ProjectionSetPtr googleSet() {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
}

// CG_FIXTURES_DIR comes from CMake; the nocmake fallback finds tests/fixtures from the working directory.
QString fixturesDir() {
#ifdef CG_FIXTURES_DIR
    return QString::fromUtf8(CG_FIXTURES_DIR);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.filePath(QStringLiteral("tests/fixtures"));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("tests/fixtures");
#endif
}

QByteArray readFixture(const char* name) {
    QFile f(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/") + QString::fromLatin1(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << name;
    return f.readAll();
}

const QByteArray& png() {
    static const QByteArray d = readFixture("test.png");
    return d;
}
const QByteArray& png2() {
    static const QByteArray d = readFixture("test2.png");
    return d;
}
const QByteArray& jpg() {
    static const QByteArray d = readFixture("test.jpg");
    return d;
}

// One version factory / request for every map of a test.
struct Versions {
    std::shared_ptr<maptype::MapVersionFactoryChangeable> factory =
        std::make_shared<maptype::MapVersionFactoryChangeable>(
            std::make_shared<storage::MapVersionFactory>());
    std::shared_ptr<maptype::MapVersionRequestChangeable> request =
        std::make_shared<maptype::MapVersionRequestChangeable>(
            std::make_shared<maptype::MapVersionRequestConfig>(QStringLiteral("base-ver")), factory);
};

Versions& versions() {
    static Versions v;
    return v;
}

// A map of the given content type with a RAM storage that holds the given tiles of zoom 2.
std::shared_ptr<FakeMapType> makeMap(const char* mime, const TileMap& tiles = {}, bool bitmap = true) {
    const core::ContentTypeInfo* info = core::ContentTypeRegistry::standard().infoByType(mime);
    EXPECT_NE(info, nullptr) << mime;
    auto cache = std::make_shared<storage::TileInfoMemCache>(100, std::chrono::milliseconds(60'000),
                                                             storage::ClearStrategy::Never);
    auto ram = std::make_shared<storage::RamTileStorage>(
        storage::tileStorageTypeAbilities(storage::TileStorageKind::Ram),
        storage::tileStorageTypeAbilities(storage::TileStorageKind::Ram).base,
        std::make_shared<storage::MapVersionFactory>(), googleSet(),
        std::make_shared<storage::TilePyramidNotifier>(), *info, cache);
    const auto base = versions().request->getStatic()->baseVersion;
    for (const auto& [xy, data] : tiles) {
        auto saved =
            ram->saveTile(PointI(xy.first, xy.second), 2, base, storage::fromPosixSeconds(1700000000),
                          QByteArrayView(info->type), data, true);
        EXPECT_TRUE(saved.has_value() && *saved);
    }
    auto m = std::make_shared<FakeMapType>();
    m->contentTypeValue = info;
    m->isBitmapTilesValue = bitmap;
    m->projectionSetValue = googleSet();
    m->versionRequestValue = versions().request;
    m->tileStorageValue = ram;
    return m;
}

using PolygonPtr = std::shared_ptr<const geo::IGeometryLonLatPolygon>;

PolygonPtr rectangle() { // z2 tiles x 1..2, y 1..2
    geo::GeometryLonLatPolygonBuilder builder;
    builder.addOuter({-80, 50, 80, -50}, std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{
                                             {-80, 50}, {80, 50}, {80, -50}, {-80, -50}}));
    return std::static_pointer_cast<const geo::IGeometryLonLatPolygon>(builder.makeStaticAndClear());
}

ExportProviderMbtiles makeProvider() {
    return ExportProviderMbtiles(std::make_shared<const geo::TileIteratorFactory>(
        std::make_shared<const geo::GeometryProjectedFactory>()));
}

core::Result<void> run(const ExportMbtilesParams& p) {
    auto progress = std::make_shared<RegionProcessProgressInfo>(std::stop_token{});
    auto task = makeProvider().prepareTask(p, rectangle(), progress);
    EXPECT_NE(task, nullptr);
    return task->processRegion();
}

ExportMbtilesParams baseParams(const QTemporaryDir& d) {
    ExportMbtilesParams p;
    p.path = d.path() + QStringLiteral("/out/t.mbtiles");
    p.zooms = {2};
    p.name = QStringLiteral("map");
    p.description = QStringLiteral("desc");
    p.attribution = QStringLiteral("attr");
    return p;
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

QString meta(const QString& path, const QString& name) {
    const auto rows = query(path, ("SELECT value FROM metadata WHERE name='" + name + "'").toUtf8(), 1);
    return rows.size() == 1 ? rows[0][0] : QStringLiteral("<%1 rows>").arg(rows.size());
}

// Every tile of zoom 2, read through the landed cache connector.
TileMap readBack(const QString& file, const char* mime = "image/png") {
    TileMap result;
    const core::ContentTypeInfo& fileType = *core::ContentTypeRegistry::standard().infoByType(mime);
    auto opened = storage::sqlite::MbtilesConnection::open(
        true, file, fileType, core::ContentTypeRegistry::standard(), googleSet(), nullptr);
    EXPECT_TRUE(opened.has_value());
    if (!opened)
        return result;
    auto conn = std::move(*opened);
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            auto info = conn->fetchOne(PointI(x, y), 2, storage::LoadMode::WithData);
            EXPECT_TRUE(info.has_value());
            if (info && info->type == storage::TileInfoType::Exists)
                result[{x, y}] = info->data;
        }
    }
    return result;
}

} // namespace

TEST(ExportProviderMbtiles, OverlayOnlyExportsTheOverlayAsALayerInItsOwnFormat) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.overlay = makeMap("image/jpg", {{{1, 1}, jpg()}, {{2, 1}, jpg()}});
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(meta(p.path, "type"), "overlay");
    EXPECT_EQ(meta(p.path, "format"), "jpg");
    // the tiles come from the overlay's storage although no map is set
    EXPECT_EQ(readBack(p.path, "image/jpg"), (TileMap{{{1, 1}, jpg()}, {{2, 1}, jpg()}}));
}

TEST(ExportProviderMbtiles, MapOnlyExportsABaseLayerInTheMapsFormat) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.map = makeMap("image/png", {{{1, 1}, png()}, {{2, 1}, png2()}});
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(meta(p.path, "type"), "baselayer");
    EXPECT_EQ(meta(p.path, "format"), "png");
    EXPECT_EQ(readBack(p.path), (TileMap{{{1, 1}, png()}, {{2, 1}, png2()}}));
}

TEST(ExportProviderMbtiles, WebpMapIsWrittenWithTheWebpFormat) {
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.map = makeMap("image/webp");
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(meta(p.path, "format"), "webp");
}

TEST(ExportProviderMbtiles, ASelectionThatCannotBeCopiedDirectlyIsRefusedAndCreatesNothing) {
    // BMP is not a direct-copy type (directTilesCopy false): the task refuses (recompose path absent)
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.map = makeMap("image/bmp");
    const auto r = run(p);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), core::ErrorCode::Unsupported);
    EXPECT_FALSE(QFileInfo::exists(p.path));
}

TEST(ExportProviderMbtiles, TextsAndPathAreTrimmedLikeDelphiTrim) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.map = makeMap("image/png", {{{1, 1}, png()}});
    p.path = QStringLiteral("  ") + p.path + QStringLiteral(" \t");
    p.name = QStringLiteral("\x01 \tКарта \n"); // Карта, controls and blanks around
    // a no-break space is above U+0020: Delphi Trim keeps it
    p.description = QStringLiteral("  Описание  ");
    p.attribution = QStringLiteral("   Автор  "); // Автор
    ASSERT_TRUE(run(p).has_value());
    const QString file = d.path() + QStringLiteral("/out/t.mbtiles");
    ASSERT_TRUE(QFileInfo(file).isFile());
    EXPECT_EQ(meta(file, "name"), QStringLiteral("Карта"));
    EXPECT_EQ(meta(file, "description"), QStringLiteral("  Описание  "));
    EXPECT_EQ(meta(file, "attribution"), QStringLiteral("Автор"));
}

TEST(ExportProviderMbtiles, SchemeAndTileMillSwitchesReachTheTask) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.map = makeMap("image/png", {{{1, 1}, png()}});
    p.useXYZScheme = true;
    p.makeTileMillCompatibility = true;
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(meta(p.path, "scheme"), "xyz");
    // the TileMill layout has the map / images tables, the classic one has a tiles table
    EXPECT_EQ(query(p.path, "SELECT count(*) FROM sqlite_master WHERE name='images'", 1)[0][0], "1");
}

// TileMill layout: map(zoom_level, tile_column, tile_row) is unique, so REPLACE and IGNORE differ.
TEST(ExportProviderMbtiles, ForceDropRecreatesAndOtherwiseAppendsWithTheReplaceFlag) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    QTemporaryDir d;
    ASSERT_TRUE(d.isValid());
    ExportMbtilesParams p = baseParams(d);
    p.makeTileMillCompatibility = true;

    // 1. the file is created: (1,1)=png, (2,1)=png
    p.map = makeMap("image/png", {{{1, 1}, png()}, {{2, 1}, png()}});
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(readBack(p.path), (TileMap{{{1, 1}, png()}, {{2, 1}, png()}}));

    // 2. append, replace: (1,1) becomes png2, (2,1) stays
    p.forceDropTarget = false;
    p.replaceExistingTiles = true;
    p.map = makeMap("image/png", {{{1, 1}, png2()}});
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(readBack(p.path), (TileMap{{{1, 1}, png2()}, {{2, 1}, png()}}));

    // 3. append, no replace: (1,1) keeps png2; (2,1) is the old one
    p.replaceExistingTiles = false;
    p.map = makeMap("image/png", {{{1, 1}, png()}});
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(readBack(p.path), (TileMap{{{1, 1}, png2()}, {{2, 1}, png()}}));

    // 4. force drop: the file is recreated, the old tiles are gone (replaceExistingTiles = true is ignored)
    p.forceDropTarget = true;
    p.replaceExistingTiles = true;
    p.map = makeMap("image/png", {{{2, 1}, png2()}});
    ASSERT_TRUE(run(p).has_value());
    EXPECT_EQ(readBack(p.path), (TileMap{{{2, 1}, png2()}}));
}
