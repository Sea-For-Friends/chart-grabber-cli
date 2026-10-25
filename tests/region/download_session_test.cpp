// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/download_session (Src/RegionProcess/Download/u_DownloadSession.pas :41-228,
// :480-663). FEAT-REG-015 (autoCloseAtFinish), FEAT-REG-020 (the two version fields): contribute only.
// processedFromLastSuccessfulPoint has no getter in legacy; its branches are proven by T-0165 (ticket finding
// b).
#include "cg/geo/geometry_factory.h"
#include "cg/region/download_session.h"

#include <gtest/gtest.h>

#include <cstdlib>

#include "region_fake_map_type.h"

using cg::region::DownloadSession;
using cg::storage::TileDate;

namespace {

const cg::geo::PointI kUnset{-1, -1};

TileDate dt(int y, int m, int d, int h) {
    return TileDate(QDate(y, m, d), QTime(h, 0, 0));
}

} // namespace

// _InitSession :201-228 through the empty Create :155-159.
TEST(DownloadSession, EmptyConstructorInitialState) {
    DownloadSession s;
    EXPECT_EQ(s.mapType(), nullptr);
    EXPECT_EQ(s.versionForCheck(), nullptr);
    EXPECT_EQ(s.versionForDownload(), nullptr);
    EXPECT_EQ(s.zoom(), 0);
    EXPECT_TRUE(s.zoomArr().empty());
    EXPECT_EQ(s.polygon(), nullptr);
    EXPECT_FALSE(s.secondLoadTne());
    EXPECT_FALSE(s.replaceTneOlderDate().isValid()); // NaN
    EXPECT_FALSE(s.replaceExistTiles());
    EXPECT_FALSE(s.checkExistTileSize());
    EXPECT_FALSE(s.checkExistTileDate());
    ASSERT_TRUE(s.checkTileDate().isValid()); // Now
    EXPECT_LT(std::abs(s.checkTileDate().secsTo(cg::storage::tileDateNow())), 60);
    EXPECT_EQ(s.elapsedTime(), 0.0);
    EXPECT_EQ(s.processed(), 0);
    EXPECT_EQ(s.downloadedSize(), 0u);
    EXPECT_EQ(s.downloadedCount(), 0);
    EXPECT_EQ(s.lastProcessedPoint(), kUnset);
    EXPECT_EQ(s.lastProcessedCount(), 0);
    EXPECT_EQ(s.lastSuccessfulPoint(), kUnset);
    EXPECT_FALSE(s.autoCloseAtFinish());
    EXPECT_EQ(s.autosaveInterval(), 0);
    EXPECT_TRUE(s.autosavePrefix().isEmpty());
    EXPECT_EQ(s.workersCount(), 1);
    EXPECT_EQ(s.workerIndex(), 0);
}

// Create :161-199, argument order of the Pascal.
TEST(DownloadSession, FullConstructorStoresEachArgument) {
    auto map = std::make_shared<cg::region::test::FakeMapType>();
    map->guidValue = QUuid::fromString(QStringLiteral("{11111111-2222-3333-4444-555555555555}"));
    auto verDownload = std::make_shared<const cg::storage::MapVersionInfo>(QStringLiteral("v-down"), 7u);
    auto verBase = std::make_shared<const cg::storage::MapVersionInfo>(QStringLiteral("v-check"), 9u);
    auto verCheck =
        std::make_shared<const cg::storage::MapVersionRequest>(cg::storage::MapVersionRequest{verBase, true});
    const cg::geo::GeometryLonLatFactory factory;
    cg::region::PolygonPtr polygon = factory.createPolygonByRect({10.0, 20.0, 30.0, 5.0});

    std::vector<quint8> zoomArr{3, 9, 5};
    const TileDate replaceOlder = dt(2001, 2, 3, 4);
    const TileDate checkDate = dt(2012, 11, 10, 9);
    DownloadSession s(map, verCheck, verDownload, 12, zoomArr, polygon, true, replaceOlder, false, true,
                      false, checkDate, true, 17, QStringLiteral("Город-pre"), 5, 3);

    EXPECT_EQ(s.mapType()->guid(), map->guidValue);
    EXPECT_EQ(s.mapType(), map);
    EXPECT_EQ(s.versionForCheck(), verCheck);
    EXPECT_EQ(s.versionForDownload(), verDownload);
    EXPECT_EQ(s.zoom(), 12);
    EXPECT_EQ(s.polygon(), polygon);
    EXPECT_TRUE(s.secondLoadTne());
    EXPECT_EQ(s.replaceTneOlderDate(), replaceOlder);
    EXPECT_FALSE(s.replaceExistTiles());
    EXPECT_TRUE(s.checkExistTileSize());
    EXPECT_FALSE(s.checkExistTileDate());
    EXPECT_EQ(s.checkTileDate(), checkDate);
    EXPECT_TRUE(s.autoCloseAtFinish());
    EXPECT_EQ(s.autosaveInterval(), 17);
    EXPECT_EQ(s.autosavePrefix(), QStringLiteral("Город-pre"));
    EXPECT_EQ(s.workersCount(), 5);
    EXPECT_EQ(s.workerIndex(), 3);
    // the rest comes from _InitSession
    EXPECT_EQ(s.lastProcessedPoint(), kUnset);
    EXPECT_EQ(s.lastSuccessfulPoint(), kUnset);
    EXPECT_EQ(s.lastProcessedCount(), 0);

    // GetZoomArr / Copy: neither side shares storage.
    EXPECT_EQ(s.zoomArr(), (std::vector<quint8>{3, 9, 5}));
    auto copy = s.zoomArr();
    copy[0] = 99;
    EXPECT_EQ(s.zoomArr()[0], 3);
    zoomArr[1] = 77;
    EXPECT_EQ(s.zoomArr()[1], 9);
}

// The second bool pattern, so that a swap of two bools in the constructor is seen from both sides.
TEST(DownloadSession, FullConstructorBoolPatternInverted) {
    DownloadSession s(nullptr, nullptr, nullptr, 1, {}, nullptr, false, TileDate(), true, false, true,
                      dt(2000, 1, 1, 1), false, 1, QString(), 2, 1);
    EXPECT_FALSE(s.secondLoadTne());
    EXPECT_TRUE(s.replaceExistTiles());
    EXPECT_FALSE(s.checkExistTileSize());
    EXPECT_TRUE(s.checkExistTileDate());
    EXPECT_FALSE(s.autoCloseAtFinish());
}

// SetLastProcessedPoint :630-639: ++count always.
TEST(DownloadSession, SetLastProcessedPointCountsEveryCall) {
    DownloadSession s;
    s.setLastSuccessfulPoint({4, 4});
    s.setLastProcessedPoint({1, 2});
    EXPECT_EQ(s.lastProcessedCount(), 1);
    s.setLastProcessedPoint({1, 2}); // same as the old one
    EXPECT_EQ(s.lastProcessedCount(), 2);
    s.setLastProcessedPoint({4, 4}); // equal to lastSuccessfulPoint
    EXPECT_EQ(s.lastProcessedCount(), 3);
    EXPECT_EQ(s.lastProcessedPoint(), (cg::geo::PointI{4, 4}));
    s.setLastProcessedPoint({7, 8});
    EXPECT_EQ(s.lastProcessedCount(), 4);
    EXPECT_EQ(s.lastProcessedPoint(), (cg::geo::PointI{7, 8}));
}

// SetZoom :654-663.
TEST(DownloadSession, SetZoomDifferentResetsState) {
    DownloadSession s;
    s.setZoom(5);
    s.setLastProcessedPoint({10, 20});
    s.setLastSuccessfulPoint({30, 40});
    s.setLastProcessedPoint({11, 21});
    ASSERT_EQ(s.lastProcessedCount(), 2);
    s.setZoom(6);
    EXPECT_EQ(s.zoom(), 6);
    EXPECT_EQ(s.lastProcessedPoint(), kUnset);
    EXPECT_EQ(s.lastSuccessfulPoint(), kUnset);
    EXPECT_EQ(s.lastProcessedCount(), 0);
}

TEST(DownloadSession, SetZoomSameKeepsState) {
    DownloadSession s;
    s.setZoom(5);
    s.setLastProcessedPoint({10, 20});
    s.setLastSuccessfulPoint({30, 40});
    s.setLastProcessedPoint({11, 21});
    s.setZoom(5);
    EXPECT_EQ(s.zoom(), 5);
    EXPECT_EQ(s.lastProcessedPoint(), (cg::geo::PointI{11, 21}));
    EXPECT_EQ(s.lastSuccessfulPoint(), (cg::geo::PointI{30, 40}));
    EXPECT_EQ(s.lastProcessedCount(), 2);
}

// SetLastSuccessfulPoint :641-647 (the reset of processedFromLastSuccessfulPoint is not observable here).
TEST(DownloadSession, SetLastSuccessfulPointStores) {
    DownloadSession s;
    s.setLastSuccessfulPoint({3, 4});
    EXPECT_EQ(s.lastSuccessfulPoint(), (cg::geo::PointI{3, 4}));
    s.setLastProcessedPoint({3, 4});
    s.setLastSuccessfulPoint({3, 4}); // equal to lastProcessedPoint
    EXPECT_EQ(s.lastSuccessfulPoint(), (cg::geo::PointI{3, 4}));
    EXPECT_EQ(s.lastProcessedCount(), 1); // untouched by this setter
}

// Plain setters :600-652.
TEST(DownloadSession, PlainSettersRoundTrip) {
    DownloadSession s;
    s.setProcessed(1234567890123LL);
    s.setDownloadedSize(9876543210ULL);
    s.setDownloadedCount(4321);
    s.setElapsedTime(1.25);
    s.setAutoCloseAtFinish(true);
    s.setAutosavePrefix(QStringLiteral("префикс"));
    s.setAutosaveInterval(42);
    EXPECT_EQ(s.processed(), 1234567890123LL);
    EXPECT_EQ(s.downloadedSize(), 9876543210ULL);
    EXPECT_EQ(s.downloadedCount(), 4321);
    EXPECT_EQ(s.elapsedTime(), 1.25);
    EXPECT_TRUE(s.autoCloseAtFinish());
    EXPECT_EQ(s.autosavePrefix(), QStringLiteral("префикс"));
    EXPECT_EQ(s.autosaveInterval(), 42);
    EXPECT_EQ(s.lastProcessedCount(), 0);
}
