// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-001, FEAT-GEO-014. New coverage only; docs/specs/geo.md §11.2
// projection_set_test.cpp (8), §2.6.
#include "cg/geo/datum.h"
#include "cg/geo/projection_set.h"

#include <gtest/gtest.h>

#include <memory>

using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::ProjectionSetFactory;
using cg::geo::standardProjectionSetList;

namespace {
ProjectionSetFactory makeFactory() {
    auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    return ProjectionSetFactory{HashSeed{}, datums};
}
} // namespace

TEST(ProjectionSet, ZoomCountIs24) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byCode(3857, kTileSplitQuadrate256x256)->zoomCount(), 24);
}

TEST(ProjectionSet, ZoomZeroHasOneTile) {
    auto factory = makeFactory();
    const auto set = factory.byCode(3857, kTileSplitQuadrate256x256);
    EXPECT_EQ(set->zoom(0)->tileRect().width(), 1);
}

TEST(ProjectionSet, ValidateZoomClampsTo23) {
    auto factory = makeFactory();
    const auto set = factory.byCode(3857, kTileSplitQuadrate256x256);
    quint8 z = 200;
    set->validateZoom(z);
    EXPECT_EQ(z, 23);
}

TEST(ProjectionSet, CheckZoomRejects24) {
    auto factory = makeFactory();
    const auto set = factory.byCode(3857, kTileSplitQuadrate256x256);
    EXPECT_TRUE(set->checkZoom(23));
    EXPECT_FALSE(set->checkZoom(24));
}

TEST(ProjectionSet, SuitableZoomIsIdentityWithinTheSet) {
    auto factory = makeFactory();
    const auto set = factory.byCode(3857, kTileSplitQuadrate256x256);
    EXPECT_EQ(set->suitableZoom(*set->zoom(10)), 10);
}

TEST(ProjectionSet, SuitableProjectionAcrossSets) {
    auto factory = makeFactory();
    const auto google = factory.byCode(3857, kTileSplitQuadrate256x256);
    const auto yandex = factory.byCode(3395, kTileSplitQuadrate256x256);
    // All built-in sets share the 256x256 pyramid, so the nearest-pixelsFloat() level is the same
    // index (docs/specs/geo.md §2.6).
    const auto suitable = google->suitableProjection(*yandex->zoom(12));
    EXPECT_EQ(suitable->zoom(), 12);
}

TEST(ProjectionSet, IsProjectionFromThisSet) {
    auto factory = makeFactory();
    const auto google = factory.byCode(3857, kTileSplitQuadrate256x256);
    const auto yandex = factory.byCode(3395, kTileSplitQuadrate256x256);
    EXPECT_TRUE(google->isProjectionFromThisSet(*google->zoom(5)));
    EXPECT_FALSE(google->isProjectionFromThisSet(*yandex->zoom(5)));
}

TEST(ProjectionSet, StandardListOrderIsGoogleYandexGeographic) {
    // u_ProjectionSetListStaticSimple.pas:45-58 registers exactly THREE entries (ESRI 53004 is
    // reachable by code/config but was never in the selectable list) -- Tech Lead ruling
    // 2026-09-18, docs/decisions/tech-lead-rulings.md geo.md §13 Q-4. T-0016 review finding 12: the
    // old name promised a fourth (Esri) entry the list does not register; renamed.
    auto factory = makeFactory();
    const auto list = standardProjectionSetList(factory);
    ASSERT_EQ(list.size(), 3U);
    EXPECT_EQ(list[0].set, factory.byCode(3857, kTileSplitQuadrate256x256));
    EXPECT_TRUE(list[0].caption.contains(QStringLiteral("3857")));
    EXPECT_EQ(list[1].set, factory.byCode(3395, kTileSplitQuadrate256x256));
    EXPECT_TRUE(list[1].caption.contains(QStringLiteral("3395")));
    EXPECT_EQ(list[2].set, factory.byCode(4326, kTileSplitQuadrate256x256));
    EXPECT_TRUE(list[2].caption.contains(QStringLiteral("4326")));
}
