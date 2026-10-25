// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002. Ports u_MapCoverageInfo.pas; docs/specs/maptype.md §2.6.6, §7.3 (MapCenter).
// No stock ZMP carries a MapCenter key (grep of tests/fixtures/zmp/sas.maps), so the vectors are
// hand-derived from the legacy parse rules.
#include "cg/maptype/map_coverage_info.h"

#include <QString>

#include <gtest/gtest.h>

#include <cmath>

using cg::maptype::MapCoverageInfo;

namespace {

TEST(MapCoverageInfo, ParseVectors) {
    struct Row {
        const char* text;
        bool valid;
        double lon, lat;
        int zoom;
    };
    const Row rows[] = {
        {"37.6,55.7,10", true, 37.6, 55.7, 10},
        {" 37.6 , -55.7 , 3 ", true, 37.6, -55.7, 3},
        {"\t1e1,2,0\t", true, 10.0, 2.0, 0},
        {"1,2,$1F", true, 1.0, 2.0, 31}, // TryStrToInt takes Delphi hex
        {"1,2,256", true, 1.0, 2.0, 0},  // Integer 256 read through a Byte (L-122)
        {"1,2,-1", true, 1.0, 2.0, 255}, // idem
        {"", false, 0, 0, 0},
        {"37.6,55.7", false, 0, 0, 0},    // two items
        {"1,2,3,4", false, 0, 0, 0},      // four items
        {"37,6,55.7,10", false, 0, 0, 0}, // decimal comma splits the item
        {"a,2,3", false, 0, 0, 0},
        {"1,,3", false, 0, 0, 0},
        {"1,2,3.5", false, 0, 0, 0}, // zoom must be an integer
        {"1,2,", false, 0, 0, 0},
        {"1.,.5,7", true, 1.0, 0.5, 7},
        {"1,2,99999999999", false, 0, 0, 0},
    };
    for (const Row& r : rows) {
        const MapCoverageInfo info{QString::fromLatin1(r.text)};
        SCOPED_TRACE(r.text);
        if (r.valid) {
            EXPECT_DOUBLE_EQ(info.centerPos().x(), r.lon);
            EXPECT_DOUBLE_EQ(info.centerPos().y(), r.lat);
            EXPECT_EQ(info.centerZoom(), r.zoom);
        } else {
            EXPECT_TRUE(std::isnan(info.centerPos().x()));
            EXPECT_TRUE(std::isnan(info.centerPos().y()));
            EXPECT_EQ(info.centerZoom(), 0);
        }
    }
}

} // namespace
