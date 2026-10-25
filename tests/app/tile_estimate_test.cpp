// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of src/cg/app/tile_estimate (new code; inherits
// Src/RegionProcess/Download/frm_ProgressDownload.pas:395-408 GetLenEnd, fr_TilesDownload.pas:205-241
// GetZoomInfo, u_ThreadDownloadTiles.pas:318-321 classification). T-0190, proofs 1-4, 6, 7 (estimate half).
// No FEAT tag: the estimate is new behaviour.
//
// Geometry and storage contents: see tile_estimate_rig.h (z6 rectangle: 80 tiles, 10 present of sizes summing
// to 15000, 6 markers).
#include "cg/app/tile_estimate.h"

#include <QLocale>

#include <gtest/gtest.h>

#include <stop_token>

#include "tile_estimate_rig.h"

using namespace cg::app;
using namespace cg::app::test;
using cg::core::ErrorCode;
using cg::geo::LonLat;
using cg::geo::PointI;

namespace {

class TileEstimateTest : public EstimateRigTest {
protected:
    [[nodiscard]] cg::core::Result<std::vector<ZoomEstimate>>
    run(const std::vector<quint8>& zooms, const std::shared_ptr<const cg::geo::IGeometryLonLatPolygon>& area,
        int assumeKb = 7, std::stop_token stop = {}, bool replace = false,
        const cg::storage::TileDate& keepNewerThan = {}, bool retryMissing = false,
        const cg::storage::TileDate& retryOlderThan = {}) {
        return estimateTiles(*map_, *area, zooms, app(), assumeKb, replace, keepNewerThan, retryMissing,
                             retryOlderThan, stop);
    }
};

} // namespace

// Proof 1: tiles is the count the real iterator yields; bbox is the legacy frame's count. Rectangle: z6 80
// tiles, z3 2 tiles, bbox the same (a tile-aligned-by-rounding rectangle has no tile outside its box).
TEST_F(TileEstimateTest, RectangleTilesAndBboxMatchTheIteratorAndTheHandProduct) {
    const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);
    const auto result = run({6, 3}, area);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].zoom, 6);
    EXPECT_EQ((*result)[0].tiles, 80);
    EXPECT_EQ((*result)[0].bboxTiles, 80); // (24 - 8) * (31 - 26)
    EXPECT_EQ(static_cast<size_t>((*result)[0].tiles), iterate(6, area).size());
    EXPECT_EQ((*result)[1].zoom, 3);
    EXPECT_EQ((*result)[1].tiles, 2);
    EXPECT_EQ((*result)[1].bboxTiles, 2); // (3 - 1) * (4 - 3)
    EXPECT_EQ(static_cast<size_t>((*result)[1].tiles), iterate(3, area).size());
}

// Proof 1: a diagonal corridor (parallelogram whose bounding box is the rectangle above): tiles is what the
// iterator yields, strictly fewer than the 80 of the box; bbox is the box.
TEST_F(TileEstimateTest, DiagonalCorridorHasFewerTilesThanItsBox) {
    const auto area =
        polygon({LonLat{-135.0, 30.0}, LonLat{-120.0, 30.0}, LonLat{-45.0, 10.0}, LonLat{-60.0, 10.0}});
    ASSERT_NE(area, nullptr);
    const auto result = run({6}, area);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    const size_t iterated = iterate(6, area).size();
    EXPECT_EQ(static_cast<size_t>((*result)[0].tiles), iterated);
    EXPECT_GT(iterated, 0U);
    EXPECT_LT((*result)[0].tiles, 80);
    EXPECT_EQ((*result)[0].bboxTiles, 80);
    EXPECT_GE((*result)[0].bboxTiles, (*result)[0].tiles);
    // the corridor's sample walks exactly the tiles of the iterator (all of them: fewer than 500)
    EXPECT_EQ(storage_->calls.size(), iterated);
}

// Proof 2: fewer than 500 tiles, every tile is looked up once: exact shares, mean of the present sizes,
// bytes = missing count x mean (GetLenEnd: mean x tiles still to load). A marker is not a present tile; a
// NotExists / unknown tile is neither; tiles outside the area or at another zoom are not looked at.
TEST_F(TileEstimateTest, SmallZoomIsSampledInFullAndBytesAreMissingTimesMean) {
    fillZ6(*storage_);
    const auto result = run({6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    const ZoomEstimate& e = (*result)[0];
    EXPECT_EQ(storage_->calls.size(), 80U);
    EXPECT_DOUBLE_EQ(e.sharePresent, 10.0 / 80.0);
    EXPECT_DOUBLE_EQ(e.shareMarkedMissing, 6.0 / 80.0);
    EXPECT_EQ(e.bytesToDownload, 96000); // 64 missing x 15000 / 10
    EXPECT_FALSE(e.assumed);
    expectMetadataOnlyCalls();
}

// T-0217 item 5: --replace requests the present tiles again (u_ThreadDownloadTiles.pas:322-325), so they are
// part of "to download": (80 - 6 markers) x 1500 mean. The "present" share stays what the cache holds.
TEST_F(TileEstimateTest, ReplaceCountsPresentTilesAsToDownload) {
    fillZ6(*storage_);
    const auto result = run({6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 7, {}, true);
    ASSERT_TRUE(result.has_value());
    const ZoomEstimate& e = (*result)[0];
    EXPECT_DOUBLE_EQ(e.sharePresent, 10.0 / 80.0);
    EXPECT_DOUBLE_EQ(e.shareReplaced, 10.0 / 80.0);
    EXPECT_EQ(e.bytesToDownload, 111000);
}

// T-0217 item 5: with a date (:328-332) a present tile that is not older than it is skipped. Of the ten
// present tiles two are newer (June 2024), one is older (2023), seven have no load date (requested again, as
// the worker's dateAtLeast treats an invalid date): 72 x 1500. The same date without --replace changes
// nothing.
TEST_F(TileEstimateTest, ReplaceWithADateSkipsTilesNotOlderThanIt) {
    fillZ6(*storage_);
    const auto dated = [&](int x, int y, quint32 size, const QDate& day) {
        cg::storage::TileInfo info = presentTile(size);
        info.loadDate = QDateTime(day, QTime(12, 0));
        storage_->put(6, x, y, info);
    };
    dated(23, 30, 700, QDate(2024, 6, 1));
    dated(8, 26, 500, QDate(2024, 6, 2));
    dated(15, 28, 3300, QDate(2023, 6, 1));
    const QDateTime cutoff(QDate(2024, 1, 1), QTime(0, 0));
    const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);

    const auto replaced = run({6}, area, 7, {}, true, cutoff);
    ASSERT_TRUE(replaced.has_value());
    EXPECT_DOUBLE_EQ((*replaced)[0].shareReplaced, 8.0 / 80.0);
    EXPECT_EQ((*replaced)[0].bytesToDownload, 108000);

    const auto plain = run({6}, area, 7, {}, false, cutoff);
    ASSERT_TRUE(plain.has_value());
    EXPECT_DOUBLE_EQ((*plain)[0].shareReplaced, 0.0);
    EXPECT_EQ((*plain)[0].bytesToDownload, 96000);
}

// T-0252: --retry-missing requests the TNE markers again (u_ThreadDownloadTiles.pas:335-338): without the
// flag the bytes stay 96000; with it the six markers count as to download: = (64 + 6) x 1500.
TEST_F(TileEstimateTest, RetryMissingCountsMarkersAsToDownload) {
    fillZ6(*storage_);
    const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);
    const auto plain = run({6}, area);
    ASSERT_TRUE(plain.has_value());
    EXPECT_DOUBLE_EQ((*plain)[0].shareRetried, 0.0);
    EXPECT_EQ((*plain)[0].bytesToDownload, 96000);

    const auto retried = run({6}, area, 7, {}, false, {}, true);
    ASSERT_TRUE(retried.has_value());
    EXPECT_DOUBLE_EQ((*retried)[0].shareRetried, 6.0 / 80.0);
    EXPECT_DOUBLE_EQ((*retried)[0].shareMarkedMissing, 6.0 / 80.0); // the share of the cache is unchanged
    EXPECT_EQ((*retried)[0].bytesToDownload, 105000);
}

// T-0252: with a date (:340-344) a marker that is not older than it is skipped. Of the six markers one is
// newer (June 2024), one is older (2023), four have no load date (requested again, as the worker's
// dateAtLeast treats an invalid date): 64 + 5 = 69 x 1500. The date without --retry-missing changes nothing.
TEST_F(TileEstimateTest, RetryMissingWithADateSkipsMarkersNotOlderThanIt) {
    fillZ6(*storage_);
    const auto dated = [&](int x, int y, const QDate& day) {
        cg::storage::TileInfo info = markerTile();
        info.loadDate = QDateTime(day, QTime(12, 0));
        storage_->put(6, x, y, info);
    };
    dated(8, 30, QDate(2024, 6, 1));
    dated(9, 26, QDate(2023, 6, 1));
    const QDateTime cutoff(QDate(2024, 1, 1), QTime(0, 0));
    const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);

    const auto retried = run({6}, area, 7, {}, false, {}, true, cutoff);
    ASSERT_TRUE(retried.has_value());
    EXPECT_DOUBLE_EQ((*retried)[0].shareRetried, 5.0 / 80.0);
    EXPECT_EQ((*retried)[0].bytesToDownload, 103500);

    const auto plain = run({6}, area, 7, {}, false, {}, false, cutoff);
    ASSERT_TRUE(plain.has_value());
    EXPECT_DOUBLE_EQ((*plain)[0].shareRetried, 0.0);
    EXPECT_EQ((*plain)[0].bytesToDownload, 96000);
}

// Proof 2: each zoom uses the mean of its own sample (z3: 4000 from one tile; z6: 1500), not the job's mean.
TEST_F(TileEstimateTest, EachZoomUsesItsOwnMeanSize) {
    fillZ6(*storage_);
    storage_->put(3, 1, 3, presentTile(4000));
    const auto result = run({3, 6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom));
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ((*result)[0].sharePresent, 0.5);
    EXPECT_EQ((*result)[0].bytesToDownload, 4000); // 2 tiles x (1 - 0.5) x 4000
    EXPECT_FALSE((*result)[0].assumed);
    EXPECT_EQ((*result)[1].bytesToDownload, 96000);
    EXPECT_FALSE((*result)[1].assumed);
}

// Proof 2/4: a zoom without any present tile takes the job's overall mean (1500) when another zoom has a
// sample; it is NOT "assumed". Zooms are answered in the given (unsorted) order.
TEST_F(TileEstimateTest, ZoomWithoutSampleTakesTheJobMeanNotTheAssumedSize) {
    fillZ6(*storage_);
    const auto result = run({6, 3}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 7);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].zoom, 6);
    EXPECT_EQ((*result)[0].bytesToDownload, 96000);
    EXPECT_EQ((*result)[1].zoom, 3);
    EXPECT_DOUBLE_EQ((*result)[1].sharePresent, 0.0);
    EXPECT_EQ((*result)[1].bytesToDownload, 3000); // 2 x 1500, not 2 x 7 x 1024 = 14336
    EXPECT_FALSE((*result)[1].assumed);
}

// Proof 3: 1200 tiles (z10, x 100..139, y 500..529): step ceil(1200 / 500) = 3, so 400 lookups, of the tiles
// at iteration index 0, 3, 6, ... The tiles between (index % 3 != 0) are present with a decoy size: a wrong
// step reads them. Of the 400 sampled, 100 are present (2000), 100 markers, 200 unknown: shares 0.25 / 0.25.
TEST_F(TileEstimateTest, TwelveHundredTilesAreSampledEveryThirdTile) {
    const auto area = rect(-144.84375, 4.214943141390642, -130.78125, -6.315298538330036);
    const std::vector<PointI> tiles = iterate(10, area);
    ASSERT_EQ(tiles.size(), 1200U);
    for (size_t i = 0; i < tiles.size(); ++i) {
        const PointI t = tiles[i];
        if (i % 3 != 0) {
            storage_->put(10, t.x(), t.y(), presentTile(50000));
        } else if (i % 12 == 0) {
            storage_->put(10, t.x(), t.y(), presentTile(2000));
        } else if (i % 12 == 3) {
            storage_->put(10, t.x(), t.y(), markerTile());
        }
    }
    const auto result = run({10}, area);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].tiles, 1200);
    ASSERT_EQ(storage_->calls.size(), 400U);
    for (size_t k = 0; k < storage_->calls.size(); ++k) {
        EXPECT_EQ(storage_->calls[k].x, tiles[3 * k].x()) << "call " << k;
        EXPECT_EQ(storage_->calls[k].y, tiles[3 * k].y()) << "call " << k;
        EXPECT_EQ(storage_->calls[k].zoom, 10);
    }
    EXPECT_DOUBLE_EQ((*result)[0].sharePresent, 0.25);
    EXPECT_DOUBLE_EQ((*result)[0].shareMarkedMissing, 0.25);
    EXPECT_EQ((*result)[0].bytesToDownload, 1200000); // 1200 x 0.5 x 2000
    expectMetadataOnlyCalls();
}

// Proof 4: an empty cache: every zoom is assumed, bytes = tiles x assumeTileKb x 1024.
TEST_F(TileEstimateTest, EmptyCacheUsesTheAssumedTileSizeEverywhere) {
    const auto result = run({3, 6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 7);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].bytesToDownload, 14336);  // 2 x 7 x 1024
    EXPECT_EQ((*result)[1].bytesToDownload, 573440); // 80 x 7 x 1024
    EXPECT_TRUE((*result)[0].assumed);
    EXPECT_TRUE((*result)[1].assumed);
    EXPECT_DOUBLE_EQ((*result)[1].sharePresent, 0.0);
    EXPECT_DOUBLE_EQ((*result)[1].shareMarkedMissing, 0.0);
}

// T-0243 (weather-sources.md 5.1): assumeEmptyCache -- a cache full of tiles and markers is not looked at (no
// storage call), nothing counts as present, marked, replaced or retried even with --replace /
// --retry-missing, every figure is the assumed size; the tile counts are still those of the area.
TEST_F(TileEstimateTest, AssumeEmptyCacheLooksAtNoStorageAndAssumesEveryTile) {
    fillZ6(*storage_);
    const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);
    const std::vector<quint8> zooms = {6, 3};
    const auto result = estimateTiles(*map_, *area, zooms, app(), 7, true, {}, true, {}, {}, true);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_TRUE(storage_->calls.empty());
    const ZoomEstimate& e = (*result)[0];
    EXPECT_EQ(e.tiles, 80);
    EXPECT_EQ(e.bboxTiles, 80);
    EXPECT_DOUBLE_EQ(e.sharePresent, 0.0);
    EXPECT_DOUBLE_EQ(e.shareMarkedMissing, 0.0);
    EXPECT_DOUBLE_EQ(e.shareReplaced, 0.0);
    EXPECT_DOUBLE_EQ(e.shareRetried, 0.0);
    EXPECT_EQ(e.bytesToDownload, 573440); // 80 x 7 x 1024, not the 111000 of the sampled cache
    EXPECT_EQ(e.bytesOnDisk, 573440);
    EXPECT_TRUE(e.assumed);
    EXPECT_EQ((*result)[1].tiles, 2);
    EXPECT_EQ((*result)[1].bytesToDownload, 14336); // 2 x 7 x 1024
    EXPECT_TRUE((*result)[1].assumed);
}

// Proof 4: markers do not give a size: one marker and no present tile anywhere is still "assumed", and the
// marker is taken out of the tiles to load.
TEST_F(TileEstimateTest, MarkersAloneDoNotMakeAMeanSize) {
    storage_->put(6, 10, 28, markerTile());
    const auto result = run({6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 3);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE((*result)[0].assumed);
    EXPECT_DOUBLE_EQ((*result)[0].shareMarkedMissing, 1.0 / 80.0);
    EXPECT_EQ((*result)[0].bytesToDownload, 242688); // 79 x 3 x 1024
}

// Proof 6: a storage error reaches the caller at once (no partial result, no further lookups). The failing
// call is the first of the second zoom (ordinal 3 after the two tiles of z3).
TEST_F(TileEstimateTest, StorageErrorIsReturnedAndEndsTheJob) {
    storage_->failAtCall = 3;
    const auto result = run({3, 6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::IoError);
    EXPECT_EQ(result.error().what(), QStringLiteral("scripted failure at call 3"));
    EXPECT_EQ(storage_->calls.size(), 3U);
}

// Proof 6: stop requested before the call: Cancelled, no lookup at all.
TEST_F(TileEstimateTest, StopRequestedBeforeTheCallReturnsAtOnce) {
    std::stop_source source;
    source.request_stop();
    const auto result = run({6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 7, source.get_token());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Cancelled);
    EXPECT_EQ(storage_->calls.size(), 0U);
}

// Proof 6: stop requested during the walk (while the third lookup runs): the fourth tile is not looked up.
TEST_F(TileEstimateTest, StopRequestedDuringTheWalkEndsItBetweenTiles) {
    std::stop_source source;
    storage_->onCall = [&source](int ordinal) {
        if (ordinal == 3) {
            source.request_stop();
        }
    };
    const auto result = run({6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 7, source.get_token());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Cancelled);
    EXPECT_EQ(storage_->calls.size(), 3U);
}

// S-13: the caller refuses when no zoom has a tile.
TEST(TileEstimateHelpers, AnyTileIsFalseOnlyWhenEveryZoomIsEmpty) {
    ZoomEstimate empty;
    ZoomEstimate some;
    some.tiles = 5;
    EXPECT_FALSE(anyTile({}));
    EXPECT_FALSE(anyTile({empty, empty}));
    EXPECT_TRUE(anyTile({empty, some}));
    EXPECT_TRUE(anyTile({some, empty}));
}

namespace {

class TableLocale : public ::testing::Test {
protected:
    void SetUp() override {
        saved_ = QLocale();
        QLocale::setDefault(QLocale::c()); // dataSizeConvert formats through the default locale
    }
    void TearDown() override { QLocale::setDefault(saved_); }
    cg::geo::ValueToStringConverter converter{cg::geo::ValueFormatSettings{}};

private:
    QLocale saved_;
};

} // namespace

// Proof 7: the golden text of a small estimate. Layout (handoff): columns zoom, SAS z (1-based), tiles, bbox,
// present, marked missing, to download; shares in percent with one decimal; bytes through dataSizeConvert
// with
// "~" (4000 / 1024 = 3.9 KB; 97000 / 1024 = 94.7 KB; total 101000 / 1024 = 98.6 KB); two blanks between
// right-aligned columns; a total row summing tiles, bbox and bytes.
TEST_F(TableLocale, EstimateTableGolden) {
    ZoomEstimate a;
    a.zoom = 3;
    a.tiles = 2;
    a.bboxTiles = 2;
    a.sharePresent = 0.5;
    a.bytesToDownload = 4000;
    ZoomEstimate b;
    b.zoom = 6;
    b.tiles = 80;
    b.bboxTiles = 120;
    b.sharePresent = 0.125;
    b.shareMarkedMissing = 0.075;
    b.bytesToDownload = 97000;
    const QStringList expected{
        QStringLiteral(" zoom  SAS z  tiles  bbox  present  marked missing  to download"),
        QStringLiteral("    3      4      2     2    50.0%            0.0%      ~3.9 KB"),
        QStringLiteral("    6      7     80   120    12.5%            7.5%     ~94.7 KB"),
        QStringLiteral("total            82   122                              ~98.6 KB"),
    };
    EXPECT_EQ(estimateTable({a, b}, converter).join(u'\n').toStdString(), expected.join(u'\n').toStdString());
}

// Proof 7: an assumed row carries "(assumed)" after its bytes, and so does the total (560 KB = 573440 /
// 1024).
TEST_F(TableLocale, EstimateTableMarksAssumedSizes) {
    ZoomEstimate a;
    a.zoom = 6;
    a.tiles = 80;
    a.bboxTiles = 80;
    a.bytesToDownload = 573440;
    a.assumed = true;
    const QStringList expected{
        QStringLiteral(" zoom  SAS z  tiles  bbox  present  marked missing          to download"),
        QStringLiteral("    6      7     80    80     0.0%            0.0%  ~560.0 KB (assumed)"),
        QStringLiteral("total            80    80                           ~560.0 KB (assumed)"),
    };
    EXPECT_EQ(estimateTable({a}, converter).join(u'\n').toStdString(), expected.join(u'\n').toStdString());
}

namespace {
using BytesOnDiskTest = TileEstimateTest;
}

// T-0202 proof 3a: bytesOnDisk = round(tiles x (1 - shareMarkedMissing) x mean), mean as bytesToDownload's.
// z6: 80 tiles, 6 markers (0.075), mean 1500 -> 80 x 0.925 x 1500 = 111000 (the present tiles count: they are
// on disk; only the markers are not). z3 has no sample: the job mean 1500 -> 2 x 1 x 1500 = 3000.
TEST_F(BytesOnDiskTest, CountsEveryTileThatIsNotMarkedMissingAtTheMeanSize) {
    fillZ6(*storage_);
    const auto result = run({6, 3}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)[0].bytesOnDisk, 111000);
    EXPECT_EQ((*result)[1].bytesOnDisk, 3000);
    EXPECT_EQ((*result)[0].bytesToDownload, 96000); // the text figure is untouched
}

// With no present tile anywhere the mean is --assume-tile-kb (7 KB = 7168): 6 markers, 80 x 0.925 x 7168 =
// 530432.
TEST_F(BytesOnDiskTest, UsesTheAssumedTileSizeWhenNoTileIsPresent) {
    for (const auto& [x, y] : {std::pair{8, 30}, {9, 26}, {14, 29}, {18, 27}, {21, 28}, {23, 26}}) {
        storage_->put(6, x, y, markerTile());
    }
    const auto result = run({6}, rect(kLonLeft, kLatTop, kLonRight, kLatBottom), 7);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE((*result)[0].assumed);
    EXPECT_EQ((*result)[0].bytesOnDisk, 530432);
}

// T-0241: legacy rule, kept. CreateProjectedPolygonByLonLatPolygon (u_GeometryProjectedFactory.pas:438-473)
// drops every vertex within 1 pixel of the previous one (TEnumDoublePointFilterEqual.Next,
// u_EnumDoublePointFilterEqual.pas: 93-125), and a one-vertex polygon has an empty bounding rectangle, which
// IntersecProjectedRect (u_GeoFunc.pas: 503-506, 528-538) reports as no intersection, so
// CalcTileCountInProjectedSinglePolygon (u_GeometryFunc.pas:733-800) gives 0 tiles. 0.5 x 0.4 degrees at 50.5
// N is 0.36 x 0.45 pixel at zoom 0 and 0.71 x 0.90 pixel at zoom 1, so those two zooms count 0; at zoom 2
// (1.4 x 1.8 pixel) the vertices survive. The legacy bbox frame (bboxTiles) still counts the one tile, and
// the tile iterator, which tests rectangles with the non-strict IsRectIntersectPolygon, still yields it (a
// legacy difference between the count and the walk, reproduced as it is).
TEST_F(TileEstimateTest, RectangleSmallerThanOnePixelHasNoTilesLikeLegacyButTheBboxStillCountsOne) {
    const auto area = rect(-1.5, 50.9, -1.0, 50.5);
    const auto result = run({0, 1, 2, 3}, area);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 4U);
    const qint64 expectedTiles[] = {0, 0, 1, 1};
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ((*result)[i].tiles, expectedTiles[i]) << "zoom " << i;
        EXPECT_EQ((*result)[i].bboxTiles, 1) << "zoom " << i;
        EXPECT_EQ(iterate(static_cast<quint8>(i), area).size(), 1U) << "zoom " << i;
    }
}

// T-0241: the same corner of the world, wide enough (1.5 degrees = 1.07 pixel at zoom 0) to keep its
// vertices: the one tile is counted, so the empty answer above comes from the 1-pixel filter and nothing
// else.
TEST_F(TileEstimateTest, RectangleJustOverOnePixelWideCountsItsTileAtZoomZero) {
    const auto area = rect(-1.5, 50.9, 0.0, 50.5);
    const auto result = run({0}, area);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].tiles, 1);
    EXPECT_EQ((*result)[0].bboxTiles, 1);
    EXPECT_EQ(iterate(0, area).size(), 1U);
}
