// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_ImportConfig.pas; docs/specs/vector.md §2.8, §7.2, §8.1 (LV-1 = L-130).
#include "cg/vector/appearance_of_mark_line.h"
#include "cg/vector/appearance_of_mark_point.h"
#include "cg/vector/appearance_of_mark_polygon.h"
#include "cg/vector/import_config.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace cg::vector;

namespace {

AppearancePtr pointAppearance() {
    return std::make_shared<AppearanceOfMarkPoint>(1, 2, 3, 0xFF000001U, 0xFF000002U, 12, 32,
                                                   QStringLiteral("p.png"), nullptr);
}
AppearancePtr lineAppearance() {
    return std::make_shared<AppearanceOfMarkLine>(4, 0xFF000003U, 5);
}
AppearancePtr polyAppearance() {
    return std::make_shared<AppearanceOfMarkPolygon>(6, 7, 8, 0xFF000004U, 9, 0xFF000005U);
}

TEST(ImportParams, PointParamsStoreEveryFlagInItsOwnField) {
    struct Row {
        bool c, bg, font, marker, pic;
    };
    for (const Row r : {Row{true, false, false, false, false}, Row{false, true, false, false, false},
                        Row{false, false, true, false, false}, Row{false, false, false, true, false},
                        Row{false, false, false, false, true}}) {
        const ImportPointParams p(pointAppearance(), r.c, r.bg, r.font, r.marker, r.pic);
        EXPECT_EQ(p.isForceTextColor(), r.c);
        EXPECT_EQ(p.isForceTextBgColor(), r.bg);
        EXPECT_EQ(p.isForceFontSize(), r.font);
        EXPECT_EQ(p.isForceMarkerSize(), r.marker);
        EXPECT_EQ(p.isForcePicName(), r.pic);
    }
}

TEST(ImportParams, PointParamsFacetsAreTheAppearance) {
    const auto a = pointAppearance();
    const ImportPointParams p(a, false, false, false, false, false);
    EXPECT_EQ(p.appearance(), a);
    ASSERT_NE(p.captionAppearance(), nullptr);
    ASSERT_NE(p.iconAppearance(), nullptr);
    EXPECT_EQ(p.captionAppearance()->fontSize(), 12);
    EXPECT_EQ(p.iconAppearance()->markerSize(), 32);
}

// LV-1 (L-130): the colour argument is lost, the width argument is stored twice.
TEST(ImportParams, LineParamsReproduceLv1) {
    struct Row {
        bool colorArg, widthArg, color, width;
    };
    for (const Row r : {Row{true, false, false, false}, Row{false, true, true, true},
                        Row{true, true, true, true}, Row{false, false, false, false}}) {
        const ImportLineParams p(lineAppearance(), r.colorArg, r.widthArg);
        EXPECT_EQ(p.isForceLineColor(), r.color);
        EXPECT_EQ(p.isForceLineWidth(), r.width);
    }
}

TEST(ImportParams, PolyParamsReproduceLv1AndKeepFillFlag) {
    struct Row {
        bool colorArg, widthArg, fillArg, color, width;
    };
    for (const Row r : {Row{true, false, false, false, false}, Row{false, true, false, true, true},
                        Row{true, true, true, true, true}, Row{false, false, true, false, false}}) {
        const ImportPolyParams p(polyAppearance(), r.colorArg, r.widthArg, r.fillArg);
        EXPECT_EQ(p.isForceLineColor(), r.color);
        EXPECT_EQ(p.isForceLineWidth(), r.width);
        EXPECT_EQ(p.isForceFillColor(), r.fillArg);
    }
}

TEST(ImportParams, LineAndPolyFacetsAreTheAppearance) {
    const ImportLineParams l(lineAppearance(), false, false);
    ASSERT_NE(l.lineAppearance(), nullptr);
    EXPECT_EQ(l.lineAppearance()->lineWidth(), 5);
    const ImportPolyParams g(polyAppearance(), false, false, false);
    ASSERT_NE(g.borderAppearance(), nullptr);
    ASSERT_NE(g.fillAppearance(), nullptr);
    EXPECT_EQ(g.borderAppearance()->lineWidth(), 9);
    EXPECT_EQ(g.fillAppearance()->fillColor(), 0xFF000005U);
}

TEST(ImportParams, NullAppearanceGivesNullFacets) {
    const ImportPointParams p(nullptr, true, true, true, true, true);
    EXPECT_EQ(p.appearance(), nullptr);
    EXPECT_EQ(p.captionAppearance(), nullptr);
    EXPECT_EQ(p.iconAppearance(), nullptr);
    EXPECT_TRUE(p.isForcePicName());
    const ImportLineParams l(nullptr, true, true);
    EXPECT_EQ(l.lineAppearance(), nullptr);
    const ImportPolyParams g(nullptr, true, true, true);
    EXPECT_EQ(g.borderAppearance(), nullptr);
    EXPECT_EQ(g.fillAppearance(), nullptr);
}

TEST(ImportParams, WrongKindAppearanceThrows) {
    EXPECT_THROW(ImportPointParams(lineAppearance(), false, false, false, false, false),
                 std::invalid_argument);
    EXPECT_THROW(ImportPointParams(polyAppearance(), false, false, false, false, false),
                 std::invalid_argument);
    EXPECT_THROW(ImportLineParams(pointAppearance(), false, false), std::invalid_argument);
    EXPECT_THROW(ImportLineParams(polyAppearance(), false, false), std::invalid_argument);
    EXPECT_THROW(ImportPolyParams(pointAppearance(), false, false, false), std::invalid_argument);
    EXPECT_THROW(ImportPolyParams(lineAppearance(), false, false, false), std::invalid_argument);
}

TEST(ImportConfig, CategoryParamsStoreEveryFlagInItsOwnField) {
    for (int i = 0; i < 4; ++i) {
        const ImportCategoryParams p(i == 0, i == 1, i == 2, i == 3);
        EXPECT_EQ(p.isAddAllInRootCategory(), i == 0);
        EXPECT_EQ(p.isIgnoreMarkIfSubCategotyNotEixts(), i == 1);
        EXPECT_EQ(p.isCreateSubCategory(), i == 2);
        EXPECT_EQ(p.isIgnoreMarkIfExistsWithSameNameInCategory(), i == 3);
    }
}

TEST(ImportConfig, StoresItsFiveMembersAndAllowsNulls) {
    const CategoryPtr root = std::make_shared<Category>(QStringLiteral("root"));
    const ImportCategoryParamsPtr cat = std::make_shared<ImportCategoryParams>(true, false, true, false);
    const ImportPointParamsPtr pt =
        std::make_shared<ImportPointParams>(pointAppearance(), false, false, false, false, false);
    const ImportLineParamsPtr ln = std::make_shared<ImportLineParams>(lineAppearance(), false, false);
    const ImportPolyParamsPtr pg = std::make_shared<ImportPolyParams>(polyAppearance(), false, false, false);
    const ImportConfig c(root, cat, pt, ln, pg);
    EXPECT_EQ(c.rootCategory(), root);
    EXPECT_EQ(c.categoryParams(), cat);
    EXPECT_EQ(c.pointParams(), pt);
    EXPECT_EQ(c.lineParams(), ln);
    EXPECT_EQ(c.polyParams(), pg);
    const ImportConfig empty(nullptr, nullptr, nullptr, nullptr, nullptr);
    EXPECT_EQ(empty.rootCategory(), nullptr);
    EXPECT_EQ(empty.pointParams(), nullptr);
    EXPECT_EQ(empty.lineParams(), nullptr);
    EXPECT_EQ(empty.polyParams(), nullptr);
    EXPECT_EQ(empty.categoryParams(), nullptr);
}

} // namespace
