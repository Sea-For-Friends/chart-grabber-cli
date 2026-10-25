// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/proj_converter_factory_adapter (new code; inherits
// Src/CoordConvert/u_ProjConverterByProj4.pas:127-150 LonLat2XY and :152-175 XY2LonLat).
#include "cg/app/proj_converter_factory_adapter.h"
#include "cg/geo/proj/proj4_defines.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using cg::app::ProjConverterFactoryAdapter;

namespace {

std::shared_ptr<const ProjConverterFactoryAdapter> makeAdapter() {
    return std::make_shared<const ProjConverterFactoryAdapter>(
        std::make_shared<const cg::geo::ProjConverterFactory>());
}

} // namespace

TEST(ProjConverterFactoryAdapter, Epsg3857ConvertsBothWays) {
    const auto conv = makeAdapter()->byEpsg(3857);
    ASSERT_NE(conv, nullptr);
    // cs2cs oracle of tests/geo/proj_converter_test.cpp (Epsg3857MatchesTheBuiltIn)
    const cg::script::DoublePoint xy = conv->lonLat2XY({37.5, 55.0});
    EXPECT_NEAR(xy.x, 4174480.904747759, 1e-6);
    EXPECT_NEAR(xy.y, 7361866.113051188, 1e-6);
    const cg::script::DoublePoint back = conv->xy2LonLat({4174480.904747759, 7361866.113051188});
    EXPECT_NEAR(back.x, 37.5, 1e-9); // x = lon
    EXPECT_NEAR(back.y, 55.0, 1e-9); // y = lat
}

TEST(ProjConverterFactoryAdapter, InitStringConvertsLikeTheEpsgCode) {
    const auto conv = makeAdapter()->byInitString(QByteArray(cg::geo::kEpsg3857));
    ASSERT_NE(conv, nullptr);
    const cg::script::DoublePoint xy = conv->lonLat2XY({37.5, 55.0});
    EXPECT_NEAR(xy.x, 4174480.904747759, 1e-6);
    EXPECT_NEAR(xy.y, 7361866.113051188, 1e-6);
}

TEST(ProjConverterFactoryAdapter, UnknownCodeOrInitStringGivesNull) {
    const auto adapter = makeAdapter();
    EXPECT_EQ(adapter->byInitString("+proj=totally_not_a_real_projection"), nullptr);
    EXPECT_EQ(adapter->byEpsg(-1), nullptr);
}

// The text of u_ProjConverterByProj4.pas:143-145 / :167-170 up to the ": <PROJ message>" tail, which the
// ticket leaves out (geo::IProjConverter has no PROJ message).
TEST(ProjConverterFactoryAdapter, FailingLonLat2XYThrowsWithTheTextOfLegacy) {
    const auto conv = makeAdapter()->byEpsg(3857);
    ASSERT_NE(conv, nullptr);
    try {
        (void)conv->lonLat2XY({12.5, 100.0}); // latitude beyond the pole: PROJ refuses
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "LonLat2XY(12.500000; 100.000000) failed");
    }
}

TEST(ProjConverterFactoryAdapter, FailingXY2LonLatThrowsWithTheTextOfLegacy) {
    // Orthographic projection of the sphere: a point 1e8 m from the centre is beyond the horizon.
    const auto conv = makeAdapter()->byInitString(
        "+proj=ortho +a=6378137 +b=6378137 +lat_0=0 +lon_0=0 +x_0=0 +y_0=0 +units=m +no_defs");
    ASSERT_NE(conv, nullptr);
    try {
        (void)conv->xy2LonLat({1e8, 2.0});
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "XY2LonLat(100000000.000000; 2.000000) failed");
    }
}
