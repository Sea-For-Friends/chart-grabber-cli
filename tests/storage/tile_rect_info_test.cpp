// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-011. Ports ITileRectInfo (i_TileInfoBasic.pas), u_TileRectInfoShort.pas;
// docs/specs/storage.md §2.5; docs/handoffs/T-0021A.md Rework 1 (T-0021A review finding 2)
#include "cg/storage/tile_rect_info.h"

#include <gtest/gtest.h>

using cg::geo::PointI;
using cg::geo::RectI;
using cg::storage::TileInfoType;
using cg::storage::TileRectInfo;
using cg::storage::TileRectInfoEntry;

namespace {

// A 3x2 rect: [10, 13) x [20, 22) -- left=10, top=20, right=13, bottom=22.
constexpr RectI kRect{10, 20, 13, 22};

std::vector<TileRectInfoEntry> makeItems(std::size_t count) {
    std::vector<TileRectInfoEntry> items(count);
    for (std::size_t i = 0; i < count; ++i) {
        items[i].type = TileInfoType::Exists;
        items[i].size = static_cast<quint32>(i);
    }
    return items;
}

TEST(TileRectInfo, RectAndZoomAreStoredAsGiven) {
    const TileRectInfo info(kRect, 7, makeItems(6));
    EXPECT_EQ(info.rect(), kRect);
    EXPECT_EQ(info.zoom(), 7);
}

// Ports TTileRectInfoShort.TileInRectToIndex: row-major, (x - left) + (y - top) * width.

TEST(TileRectInfo, IndexOfTopLeftCornerIsZero) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_EQ(info.indexOf(PointI(10, 20)), 0);
}

TEST(TileRectInfo, IndexOfLeftEdgeIsInclusive) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_GE(info.indexOf(PointI(10, 21)), 0); // left column, x == left, is in-rect
}

TEST(TileRectInfo, IndexOfTopEdgeIsInclusive) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_GE(info.indexOf(PointI(11, 20)), 0); // top row, y == top, is in-rect
}

TEST(TileRectInfo, IndexOfRightEdgeIsExclusive) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_EQ(info.indexOf(PointI(13, 20)), -1); // x == right, half-open -- outside
}

TEST(TileRectInfo, IndexOfBottomEdgeIsExclusive) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_EQ(info.indexOf(PointI(10, 22)), -1); // y == bottom, half-open -- outside
}

TEST(TileRectInfo, IndexOfOutsideOnXAxisIsMinusOne) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_EQ(info.indexOf(PointI(9, 20)), -1);  // left of rect
    EXPECT_EQ(info.indexOf(PointI(13, 20)), -1); // at/right of rect
}

TEST(TileRectInfo, IndexOfOutsideOnYAxisIsMinusOne) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_EQ(info.indexOf(PointI(10, 19)), -1); // above rect
    EXPECT_EQ(info.indexOf(PointI(10, 22)), -1); // at/below rect
}

TEST(TileRectInfo, IndexOfRowMajorFormula) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    // width = 3; (12,21) -> (12-10) + (21-20)*3 = 2 + 3 = 5
    EXPECT_EQ(info.indexOf(PointI(12, 21)), 5);
}

// at(): dereferences indexOf() into items_, nullptr when out-of-rect or short items_.

TEST(TileRectInfo, AtReturnsMatchingEntryInsideRect) {
    auto items = makeItems(6);
    items[5].size = 999;
    const TileRectInfo info(kRect, 0, items);
    const TileRectInfoEntry* e = info.at(PointI(12, 21));
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->size, 999u);
}

TEST(TileRectInfo, AtReturnsNullptrOutsideRect) {
    const TileRectInfo info(kRect, 0, makeItems(6));
    EXPECT_EQ(info.at(PointI(0, 0)), nullptr);
}

TEST(TileRectInfo, AtBoundsCheckHoldsWithItemsSizedExactlyToRectArea) {
    // The constructor's Q_ASSERT enforces items_.size() >= area (debug-only, T-0021A review's
    // "Confirmed correct" note); the exact-fit case (no slack) is the one that would trip at()'s
    // own `idx >= items_.size()` guard first if that guard were ever removed, so pin it here: the
    // last in-rect tile (index 5, the final element of a 6-item exact-fit vector) still resolves,
    // and the guard is what makes that safe rather than an out-of-bounds read.
    const TileRectInfo info(kRect, 0, makeItems(6));         // area == 3*2 == 6, exact fit
    const TileRectInfoEntry* last = info.at(PointI(12, 21)); // index 5, last valid entry
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(last->size, 5u);
}

} // namespace
