// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_CoordConverterSimpleByProjectionSet.pas (the script's `Converter`); docs/specs/maptype.md 2.12.4.
// Expected numbers come from the Web Mercator formulas evaluated independently (Python, R = 6378137).
#include "cg/maptype/coord_converter_simple_by_projection_set.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "request_builder_test_util.h"

using cg::maptype::CoordConverterSimpleByProjectionSet;
using cg::maptype::test::googleProjectionSet;
using cg::script::DoublePoint;
using cg::script::Point;
using cg::script::Rect;

namespace {

CoordConverterSimpleByProjectionSet converter() {
    return CoordConverterSimpleByProjectionSet(googleProjectionSet());
}

// :168-179 and :119-130: the sizes of a valid zoom, 0 beyond the 24 levels.
TEST(CoordConverterByProjectionSet, TilesAndPixelsAtZoom) {
    const auto c = converter();
    EXPECT_EQ(c.tilesAtZoom(3), 8);
    EXPECT_EQ(c.pixelsAtZoom(3), 2048);
    EXPECT_EQ(c.tilesAtZoom(23), 8388608);
    EXPECT_EQ(c.tilesAtZoom(24), 0);
    EXPECT_EQ(c.pixelsAtZoom(200), 0);
}

// :132-146: zoom <= 23 reads a tile position, zoom > 23 a pixel position of zoom - 8.
TEST(CoordConverterByProjectionSet, Pos2LonLatTileBranchAndPixelBranch) {
    const auto c = converter();
    DoublePoint p = c.pos2LonLat({4, 2}, 3);
    EXPECT_DOUBLE_EQ(p.x, 0.0);
    EXPECT_DOUBLE_EQ(p.y, 66.51326044311186);
    p = c.pos2LonLat({1, 1}, 23); // tile (1,1) at zoom 23
    EXPECT_DOUBLE_EQ(p.x, -179.99995708465576);
    p = c.pos2LonLat({1, 1}, 24); // pixel (1,1) at zoom 16
    EXPECT_DOUBLE_EQ(p.x, -179.99997854232788);
    p = c.pos2LonLat({67108864, 67108864}, 27); // the centre pixel of zoom 19
    EXPECT_DOUBLE_EQ(p.x, 0.0);
    EXPECT_NEAR(p.y, 0.0, 1e-12);
    p = c.pos2LonLat({0, 0}, 31); // the last valid pixel zoom, 31 - 8 = 23
    EXPECT_DOUBLE_EQ(p.x, -180.0);
    EXPECT_DOUBLE_EQ(p.y, 85.0511287798066);
}

// :96-110: the point is rounded to the top-left (floor).
TEST(CoordConverterByProjectionSet, LonLat2PosTileBranchAndPixelBranch) {
    const auto c = converter();
    Point t = c.lonLat2Pos({10.5, 20.25}, 4);
    EXPECT_EQ(t.x, 8);
    EXPECT_EQ(t.y, 7);
    t = c.lonLat2Pos({10.5, 20.25}, 28); // pixel of zoom 20
    EXPECT_EQ(t.x, 142047095);
    EXPECT_EQ(t.y, 118793694);
}

TEST(CoordConverterByProjectionSet, MetresUseTheProjectionType) {
    const auto c = converter();
    DoublePoint m = c.lonLat2Metr({180.0, 45.0});
    EXPECT_DOUBLE_EQ(m.x, 20037508.342789244);
    EXPECT_NEAR(m.y, 5621521.486192066, 1e-6);
    // :112-117 inherits the legacy sign defect of the projection type (x * mul - 0.5, no clamp): (0,0) metres
    // reads relative (-0.5, 0.5), i.e. lon (-0.5 - 0.5) * 360.
    const DoublePoint ll = c.metr2LonLat({0.0, 0.0});
    EXPECT_DOUBLE_EQ(ll.x, -360.0);
    EXPECT_NEAR(ll.y, 0.0, 1e-12);
}

// :148-166: a zoom outside 0..23 leaves Result unassigned in legacy; the port returns zeros.
TEST(CoordConverterByProjectionSet, TilePosToPixels) {
    const auto c = converter();
    const Point p = c.tilePos2PixelPos({3, 2}, 2);
    EXPECT_EQ(p.x, 768);
    EXPECT_EQ(p.y, 512);
    const Rect r = c.tilePos2PixelRect({3, 2}, 2);
    EXPECT_EQ(r.left, 768);
    EXPECT_EQ(r.top, 512);
    EXPECT_EQ(r.right, 1024);
    EXPECT_EQ(r.bottom, 768);
    const Point none = c.tilePos2PixelPos({3, 2}, 24);
    EXPECT_EQ(none.x, 0);
    EXPECT_EQ(none.y, 0);
    const Rect noRect = c.tilePos2PixelRect({3, 2}, 24);
    EXPECT_EQ(noRect.right, 0);
    EXPECT_EQ(noRect.bottom, 0);
}

// Zooms[AZoom - 8] with AZoom >= 32 is past the 24-entry TList: legacy raises EListError 'List index out of
// bounds (N)'.
TEST(CoordConverterByProjectionSet, PixelZoomPastTheTableRaisesTheListIndexError) {
    const auto c = converter();
    const auto messageOf = [](auto call) {
        try {
            call();
        } catch (const std::out_of_range& e) {
            return std::string(e.what());
        }
        return std::string("no exception");
    };
    EXPECT_EQ(messageOf([&] { (void)c.pos2LonLat({0, 0}, 32); }), "List index out of bounds (24)");
    EXPECT_EQ(messageOf([&] { (void)c.lonLat2Pos({0.0, 0.0}, 255); }), "List index out of bounds (247)");
}

// Precondition guard: legacy Assert(Assigned) is off in the shipped build and Zooms[0] would fault
// (unreachable: a ZMP always has a set).
TEST(CoordConverterByProjectionSet, NullProjectionSetThrows) {
    EXPECT_THROW(CoordConverterSimpleByProjectionSet(nullptr), std::invalid_argument);
}

} // namespace
