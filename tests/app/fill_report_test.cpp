// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of src/cg/app/fill_report (new code; inherits
// Src/RegionProcess/Download/u_ThreadDownloadTiles.pas:320-321 classification: IsExists / IsExistsTNE /
// neither). T-0190, proofs 5, 6, 7 (fill half). No FEAT tag. Contents of the storage: see tile_estimate_rig.h
// (z6: 80 tiles, 10 present summing 15000, 6 markers, 3 NotExists entries + 61 unknown = 64 missing).
#include "cg/app/fill_report.h"

#include <QLocale>

#include <gtest/gtest.h>

#include <stop_token>

#include "tile_estimate_rig.h"

using namespace cg::app;
using namespace cg::app::test;
using cg::core::ErrorCode;

namespace {

class FillReportTest : public EstimateRigTest {
protected:
    [[nodiscard]] cg::core::Result<std::vector<ZoomFill>> run(const std::vector<quint8>& zooms,
                                                              std::stop_token stop = {}) {
        const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);
        return fillReport(*map_, *area, zooms, app(), stop);
    }
};

} // namespace

// Proof 5: present / marked missing / missing / bytes exact for each zoom; every tile of the area is looked
// up once (tiles = iterated), NotExists and unknown tiles are "missing", neighbours and other zooms do not
// count.
TEST_F(FillReportTest, CountsAndBytesAreExactPerZoom) {
    fillZ6(*storage_);
    storage_->put(3, 1, 3, presentTile(4000));
    storage_->put(3, 2, 3, notExistsTile());
    const auto result = run({6, 3});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    const ZoomFill& z6 = (*result)[0];
    EXPECT_EQ(z6.zoom, 6);
    EXPECT_EQ(z6.tiles, 80);
    EXPECT_EQ(z6.present, 10);
    EXPECT_EQ(z6.markedMissing, 6);
    EXPECT_EQ(z6.missing, 64);
    EXPECT_EQ(z6.bytes, 15000);
    const ZoomFill& z3 = (*result)[1];
    EXPECT_EQ(z3.zoom, 3);
    EXPECT_EQ(z3.tiles, 2);
    EXPECT_EQ(z3.present, 1);
    EXPECT_EQ(z3.markedMissing, 0);
    EXPECT_EQ(z3.missing, 1);
    EXPECT_EQ(z3.bytes, 4000);
    EXPECT_EQ(storage_->calls.size(), 82U);
    expectMetadataOnlyCalls();
}

// Proof 5: an empty cache is all missing, no bytes.
TEST_F(FillReportTest, EmptyCacheIsAllMissing) {
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)[0].tiles, 2);
    EXPECT_EQ((*result)[0].present, 0);
    EXPECT_EQ((*result)[0].markedMissing, 0);
    EXPECT_EQ((*result)[0].missing, 2);
    EXPECT_EQ((*result)[0].bytes, 0);
}

// T-0259: a zoom whose legacy count is 0 (area under one pixel, tests/app/tile_estimate_test.cpp T-0241) is
// not walked: tiles 0, nothing missing, no storage lookup; the row is still there. Zoom 2 of the same area
// counts one tile.
TEST_F(FillReportTest, ZoomWithLegacyCountZeroShowsNoTilesAndNothingMissing) {
    const auto area = rect(-1.5, 50.9, -1.0, 50.5);
    const auto result = fillReport(*map_, *area, std::vector<quint8>{0, 1, 2}, app(), {});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3U);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ((*result)[i].zoom, i);
        EXPECT_EQ((*result)[i].tiles, 0) << "zoom " << i;
        EXPECT_EQ((*result)[i].missing, 0) << "zoom " << i;
    }
    EXPECT_EQ((*result)[2].tiles, 1);
    EXPECT_EQ((*result)[2].missing, 1);
    EXPECT_EQ(storage_->calls.size(), 1U);
}

// Proof 6: a storage error is returned at once.
TEST_F(FillReportTest, StorageErrorIsReturnedAndEndsTheJob) {
    storage_->failAtCall = 4;
    const auto result = run({3, 6});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::IoError);
    EXPECT_EQ(result.error().what(), QStringLiteral("scripted failure at call 4"));
    EXPECT_EQ(storage_->calls.size(), 4U);
}

// Proof 6: stop requested before the call returns at once; during the walk it ends between tiles.
TEST_F(FillReportTest, StopRequestedBeforeTheCallReturnsAtOnce) {
    std::stop_source source;
    source.request_stop();
    const auto result = run({6}, source.get_token());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Cancelled);
    EXPECT_EQ(storage_->calls.size(), 0U);
}

TEST_F(FillReportTest, StopRequestedDuringTheWalkEndsItBetweenTiles) {
    std::stop_source source;
    storage_->onCall = [&source](int ordinal) {
        if (ordinal == 5) {
            source.request_stop();
        }
    };
    const auto result = run({6}, source.get_token());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Cancelled);
    EXPECT_EQ(storage_->calls.size(), 5U);
}

namespace {

class FillTableLocale : public ::testing::Test {
protected:
    void SetUp() override {
        saved_ = QLocale();
        QLocale::setDefault(QLocale::c());
    }
    void TearDown() override { QLocale::setDefault(saved_); }
    cg::geo::ValueToStringConverter converter{cg::geo::ValueFormatSettings{}};

private:
    QLocale saved_;
};

} // namespace

// Proof 5/7: golden text and the total row. Sizes: 4000 / 1024 = 3.9 KB, 15000 / 1024 = 14.6 KB,
// total 19000 / 1024 = 18.6 KB.
TEST_F(FillTableLocale, FillTableGolden) {
    ZoomFill a;
    a.zoom = 3;
    a.tiles = 2;
    a.present = 1;
    a.missing = 1;
    a.bytes = 4000;
    ZoomFill b;
    b.zoom = 6;
    b.tiles = 80;
    b.present = 10;
    b.markedMissing = 6;
    b.missing = 64;
    b.bytes = 15000;
    const QStringList expected{
        QStringLiteral(" zoom  tiles  present  marked missing  missing     size"),
        QStringLiteral("    3      2        1               0        1   3.9 KB"),
        QStringLiteral("    6     80       10               6       64  14.6 KB"),
        QStringLiteral("total     82       11               6       65  18.6 KB"),
    };
    EXPECT_EQ(fillTable({a, b}, converter).join(u'\n').toStdString(), expected.join(u'\n').toStdString());
}
