// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-REG-010 (bounding-box tile count; contributes), FEAT-REG-014 (split cap; contributes).
// Ports: Src/RegionProcess/Download/fr_TilesDownload.pas (GetZoomInfo :205-241, OnMapChange :362-394,
// GetAllowDownload :428-431, getters :433-512). T-0181, proofs RD-8 1-3.
//
// Geometry (Google/3857, 256 px tiles): pixel x = (lon + 180) / 360 * 256 * 2^z, so every lon below that is a
// multiple of 45 / 2^k is exact; the expected pixel and tile rectangles were computed once by an independent
// script (round Outside = floor(v + 0.001) / ceil(v - 0.001), u_GeoFunc.pas:135-139; tile rect = left div
// 256, (right + 255) div 256) and are literals here.
//   z3, lon -135..-45, lat 30..10 : pixels 256..768 x 844.95..966.82 -> {256,844,768,967}
//   z3, lon -135.0703125..-44.9296875: x 255.6..768.4 -> {255,844,769,967}
//   z6, lon -135..-45, lat 30..10 : {2048,6759,6144,7735}, tiles {8,26,24,31}
//   z23, lon 10..10.001, lat 0.001..0.0004: {1133394147,1073735858,1133400113,1073739438}
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/tile_downloader_state.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/region/tiles_download.h"
#include "cg/storage/cache_type.h"
#include "cg/storage/simple_tile_storage_config.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "region_fake_map_type.h"
#include "tiles_download_rig.h"

using cg::geo::LonLat;
using cg::geo::LonLatRect;
using cg::geo::RectI;
using cg::region::allowDownload;
using cg::region::boundingBoxTileCount;
using cg::region::clampSplitCount;
using cg::region::maxSplitCount;
using cg::region::TilesDownloadParams;
using cg::region::test::FakeMapType;
using cg::region::test::RecordingSubsystem;

namespace {

struct MapRig {
    std::shared_ptr<FakeMapType> map = std::make_shared<FakeMapType>();
    std::shared_ptr<RecordingSubsystem> subsystem = std::make_shared<RecordingSubsystem>();
    cg::geo::GeometryProjectedFactory projected;

    explicit MapRig(bool allowAdd = true, quint32 maxConnect = 4) {
        cg::geo::ProjectionSetFactory setFactory{
            cg::geo::HashSeed{}, std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})};
        map->projectionSetValue =
            setFactory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
        cg::storage::TileStorageAbilities abilities = cg::storage::TileStorageAbilities::readWrite();
        abilities.allowAdd = allowAdd;
        map->storageConfigValue = std::make_shared<cg::storage::SimpleTileStorageConfig>(
            std::make_shared<const cg::storage::SimpleTileStorageConfigStatic>(
                cg::storage::SimpleTileStorageConfigStatic{cg::storage::CacheTypeId::Sqlite,
                                                           QStringLiteral("tiles"), QByteArray("image/png"),
                                                           QByteArray(".png"), abilities}));
        cg::net::TileDownloaderSettings settings;
        settings.maxConnectToServerCount = maxConnect; // the constructor does not clamp (setters do)
        map->tileDownloaderConfigValue = std::make_shared<cg::net::TileDownloaderConfig>(
            std::make_shared<cg::net::InetConfig>(), settings);
        map->tileDownloadSubsystemValue = subsystem;
    }
};

std::shared_ptr<const cg::geo::IGeometryLonLatPolygon> rect(double lonL, double latTop, double lonR,
                                                            double latBottom) {
    cg::geo::GeometryLonLatFactory f;
    return f.createPolygonByRect(LonLatRect{lonL, latTop, lonR, latBottom});
}

std::shared_ptr<const cg::geo::IGeometryLonLatPolygon> polygon(std::vector<LonLat> points) {
    cg::geo::GeometryLonLatFactory f;
    return std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatPolygon>(f.createPolygon(points));
}

} // namespace

// Proof 1: a fresh record answers what a fresh frame answers (every getter of :433-507 with nothing
// checked): all check boxes false, GetLoadTneOlderDate = NaN, interval 0 (:482-489), prefix '' (:491-498),
// GetSplitCount = 1 (:500-507); the map and the zoom list are not chosen yet (:472-475, :509-512).
TEST(TilesDownload, RecordDefaultsAreWhatAFreshFrameReturns) {
    const TilesDownloadParams p;
    EXPECT_EQ(p.mapType, nullptr);
    EXPECT_TRUE(p.zoomArray.empty());
    EXPECT_FALSE(p.isStartPaused);
    EXPECT_FALSE(p.isCloseOnFinish);
    EXPECT_FALSE(p.isIgnoreTne);
    EXPECT_FALSE(p.loadTneOlderDate.isValid());
    EXPECT_FALSE(p.isReplace);
    EXPECT_FALSE(p.isReplaceIfDifSize);
    EXPECT_FALSE(p.isReplaceIfOlder);
    EXPECT_EQ(p.sessionAutosaveInterval, 0);
    EXPECT_TRUE(p.sessionAutosaveFilePrefix.isEmpty());
    EXPECT_EQ(p.splitCount, 1);
}

// Proof 1: OnMapChange :376 VCount := MaxConnectToServerCount.
TEST(TilesDownload, MaxSplitCountIsTheMapsMaxConnectToServerCount) {
    EXPECT_EQ(maxSplitCount(*MapRig(true, 1).map), 1);
    EXPECT_EQ(maxSplitCount(*MapRig(true, 4).map), 4);
    EXPECT_EQ(maxSplitCount(*MapRig(true, 100).map), 100);
}

// Proof 1: :385-391 and the dfm (sePartsCount MinValue 2, MaxValue 64). Rows are max 1 / 4 / 100 by
// requested 0 / 1 / 2 / 5 / 80.
TEST(TilesDownload, ClampSplitCountRows) {
    struct Row {
        quint32 max;
        int requested;
        int expected;
    };
    const Row rows[] = {
        {1, 0, 1},   {1, 1, 1},   {1, 2, 1},   {1, 5, 1},   {1, 80, 1},    // :389-391 split off
        {4, 0, 1},   {4, 1, 1},   {4, 2, 2},   {4, 5, 4},   {4, 80, 4},    // :385-388 clamp to VCount
        {100, 0, 1}, {100, 1, 1}, {100, 2, 2}, {100, 5, 5}, {100, 80, 64}, // dfm MaxValue 64
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(testing::Message() << "max " << row.max << " requested " << row.requested);
        EXPECT_EQ(clampSplitCount(*MapRig(true, row.max).map, row.requested), row.expected);
    }
}

// Proof 2: GetAllowDownload :428-431, AllowAdd AND State.GetStatic.Enabled; all four combinations.
TEST(TilesDownload, AllowDownloadNeedsAddAllowedAndSubsystemEnabled) {
    struct Row {
        bool allowAdd;
        bool stateEnabled;
        bool expected;
    };
    const Row rows[] = {
        {true, true, true}, {false, true, false}, {true, false, false}, {false, false, false}};
    for (const Row& row : rows) {
        SCOPED_TRACE(testing::Message() << "allowAdd " << row.allowAdd << " enabled " << row.stateEnabled);
        MapRig rig(row.allowAdd);
        if (!row.stateEnabled) {
            rig.subsystem->stateInternal->disable(QStringLiteral("disabled by the test"));
        }
        EXPECT_EQ(allowDownload(*rig.map), row.expected);
    }
}

namespace {

struct BoxRow {
    const char* name;
    quint8 zoom;
    double lonL, latTop, lonR, latBottom;
    RectI pixelRect;
    RectI tileRect;
    qint64 count;
};

} // namespace

// Proof 3: GetZoomInfo :205-241. A rectangle exactly on tile borders gives the exact count; a rectangle
// that is a fraction of a pixel past a border (255.6 and 768.4 px) gives one more column on each side, which
// is rrOutside (:234); the zoom above 23 is clamped by ValidateZoom (:222).
TEST(TilesDownload, BoundingBoxOfRectanglesRoundsOutside) {
    const BoxRow rows[] = {
        {"on the tile borders", 3, -135, 30, -45, 10, RectI{256, 844, 768, 967}, RectI{1, 3, 3, 4}, 2},
        {"a fraction past the borders", 3, -135.0703125, 30, -44.9296875, 10, RectI{255, 844, 769, 967},
         RectI{0, 3, 4, 4}, 4},
        {"zoom 24 is zoom 23", 24, 10, 0.001, 10.001, 0.0004,
         RectI{1133394147, 1073735858, 1133400113, 1073739438}, RectI{4427320, 4194280, 4427345, 4194295},
         375},
        {"zoom 23 itself", 23, 10, 0.001, 10.001, 0.0004,
         RectI{1133394147, 1073735858, 1133400113, 1073739438}, RectI{4427320, 4194280, 4427345, 4194295},
         375},
    };
    MapRig rig;
    for (const BoxRow& row : rows) {
        SCOPED_TRACE(row.name);
        const auto poly = rect(row.lonL, row.latTop, row.lonR, row.latBottom);
        const auto info = boundingBoxTileCount(row.zoom, *rig.map, poly.get(), rig.projected);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->pixelRect, row.pixelRect);
        EXPECT_EQ(info->tileRect, row.tileRect);
        EXPECT_EQ(info->tilesCount, row.count);
    }
}

// Proof 3: ATilesCount is the area of the bounding box (:236-237), not the tiles the polygon touches. A thin
// sliver along the diagonal of the 16 x 5 tile box of zoom 6 still counts all 80.
TEST(TilesDownload, DiagonalThinPolygonCountsTheWholeBox) {
    MapRig rig;
    const auto poly = polygon({{-135, 10}, {-45, 30}, {-45, 29.99}});
    ASSERT_NE(poly, nullptr);
    const auto info = boundingBoxTileCount(6, *rig.map, poly.get(), rig.projected);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->pixelRect, (RectI{2048, 6759, 6144, 7735}));
    EXPECT_EQ(info->tileRect, (RectI{8, 26, 24, 31}));
    EXPECT_EQ(info->tilesCount, 80);
}

// Proof 3: no polygon (:226) gives no answer (Result := False).
TEST(TilesDownload, NullPolygonGivesNoCount) {
    MapRig rig;
    EXPECT_EQ(boundingBoxTileCount(3, *rig.map, nullptr, rig.projected), std::nullopt);
}
