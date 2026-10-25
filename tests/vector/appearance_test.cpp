// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_AppearanceOfMarkPoint/Line/Polygon.pas;
// docs/specs/vector.md §2.3, §7.2, §8.1 (LV-2, LV-5).
#include "cg/vector/appearance_of_mark_line.h"
#include "cg/vector/appearance_of_mark_point.h"
#include "cg/vector/appearance_of_mark_polygon.h"

#include <gtest/gtest.h>

#include "fake_mark_picture.h"

using namespace cg::vector;

namespace {

AppearanceOfMarkPoint point(cg::core::HashValue common, const QString& picName, const MarkPicturePtr& pic) {
    return AppearanceOfMarkPoint(common, 11, 22, 0xFF112233U, 0x80445566U, 12, 32, picName, pic);
}

TEST(AppearanceOfMarkPoint, StoresWhatItIsGivenAndKeepsThreeHashesApart) {
    const auto p = point(100, QStringLiteral("icon.png"), nullptr);
    EXPECT_EQ(p.hash(), 100U);
    EXPECT_EQ(p.hashCaption(), 11U);
    EXPECT_EQ(p.hashIcon(), 22U);
    EXPECT_EQ(p.textColor(), 0xFF112233U);
    EXPECT_EQ(p.textBgColor(), 0x80445566U);
    EXPECT_EQ(p.fontSize(), 12);
    EXPECT_EQ(p.markerSize(), 32);
    EXPECT_EQ(p.picName(), QStringLiteral("icon.png"));
    EXPECT_EQ(p.pic(), nullptr);
}

TEST(AppearanceOfMarkPoint, PictureNameOverridesPicName) {
    const MarkPicturePtr pic = std::make_shared<test::FakeMarkPicture>(QStringLiteral("real.png"), 7);
    const auto p = point(100, QStringLiteral("given.png"), pic);
    EXPECT_EQ(p.picName(), QStringLiteral("real.png"));
    EXPECT_EQ(p.pic(), pic);
}

TEST(AppearanceOfMarkLine, StoresWhatItIsGiven) {
    const AppearanceOfMarkLine l(5, 0xFFAABBCCU, 3);
    EXPECT_EQ(l.hash(), 5U);
    EXPECT_EQ(l.lineColor(), 0xFFAABBCCU);
    EXPECT_EQ(l.lineWidth(), 3);
}

TEST(AppearanceOfMarkPolygon, StoresWhatItIsGivenAndKeepsThreeHashesApart) {
    const AppearanceOfMarkPolygon g(30, 31, 32, 0xFF000001U, 4, 0x40000002U);
    EXPECT_EQ(g.hash(), 30U);
    EXPECT_EQ(g.hashBorder(), 31U);
    EXPECT_EQ(g.hashFill(), 32U);
    EXPECT_EQ(g.lineColor(), 0xFF000001U);
    EXPECT_EQ(g.lineWidth(), 4);
    EXPECT_EQ(g.fillColor(), 0x40000002U);
}

// LV-5 (L-126): isEqual is hash-only - no identity shortcut, no field compare, no type check.
TEST(Appearance, IsEqualIsHashOnly) {
    const auto p = point(100, QStringLiteral("a"), nullptr);
    const auto p2 = AppearanceOfMarkPoint(100, 1, 2, 0, 0, 0, 0, QStringLiteral("b"), nullptr);
    const AppearanceOfMarkLine line(100, 1, 1);
    const AppearanceOfMarkPolygon polygon(100, 1, 2, 1, 1, 1);
    const AppearanceOfMarkLine otherLine(101, 1, 1);
    EXPECT_FALSE(p.isEqual(nullptr));
    EXPECT_TRUE(p.isEqual(&p));
    EXPECT_TRUE(p.isEqual(&p2));         // every field differs, hash equal
    EXPECT_TRUE(line.isEqual(&polygon)); // different kinds
    EXPECT_TRUE(polygon.isEqual(&p));
    EXPECT_FALSE(line.isEqual(&otherLine));
}

// Facet cross-casts (Supports(A, IAppearance...) of legacy). LV-2 (L-127): a facet has no isEqual,
// and its hash is not the common hash.
TEST(Appearance, FacetCrossCasts) {
    const std::shared_ptr<const IAppearance> point =
        std::make_shared<AppearanceOfMarkPoint>(100, 11, 22, 1, 2, 3, 4, QStringLiteral("n"), nullptr);
    const std::shared_ptr<const IAppearance> line = std::make_shared<AppearanceOfMarkLine>(5, 1, 1);
    const std::shared_ptr<const IAppearance> polygon =
        std::make_shared<AppearanceOfMarkPolygon>(30, 31, 32, 1, 2, 3);

    const auto caption = std::dynamic_pointer_cast<const IAppearancePointCaption>(point);
    const auto icon = std::dynamic_pointer_cast<const IAppearancePointIcon>(point);
    ASSERT_TRUE(caption && icon);
    EXPECT_EQ(caption->hashCaption(), 11U);
    EXPECT_EQ(icon->hashIcon(), 22U);
    EXPECT_EQ(point->hash(), 100U);
    EXPECT_FALSE(std::dynamic_pointer_cast<const IAppearanceLine>(point));
    EXPECT_FALSE(std::dynamic_pointer_cast<const IAppearancePolygonBorder>(point));

    const auto lineFacet = std::dynamic_pointer_cast<const IAppearanceLine>(line);
    ASSERT_TRUE(lineFacet);
    EXPECT_EQ(dynamic_cast<const IAppearance&>(*lineFacet).hash(),
              5U); // the line's facet hash is the common one
    EXPECT_FALSE(std::dynamic_pointer_cast<const IAppearancePointCaption>(line));
    EXPECT_FALSE(std::dynamic_pointer_cast<const IAppearancePolygonBorder>(line));

    const auto border = std::dynamic_pointer_cast<const IAppearancePolygonBorder>(polygon);
    const auto fill = std::dynamic_pointer_cast<const IAppearancePolygonFill>(polygon);
    ASSERT_TRUE(border && fill);
    EXPECT_EQ(border->hashBorder(), 31U);
    EXPECT_EQ(fill->hashFill(), 32U);
    EXPECT_FALSE(std::dynamic_pointer_cast<const IAppearanceLine>(polygon));
    EXPECT_FALSE(std::dynamic_pointer_cast<const IAppearancePointIcon>(polygon));
}

} // namespace
