// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/region_process_task_abstract (Src/RegionProcess/u_RegionProcessTaskAbstract.pas).
// No FEAT tag: shared base unit (T-0169).
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"
#include "cg/region/region_process_task_abstract.h"

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <vector>

using cg::geo::PointI;
using cg::region::RegionProcessProgressInfo;
using cg::region::RegionProcessTaskAbstract;

namespace {

class StubTask final : public RegionProcessTaskAbstract {
public:
    StubTask(std::shared_ptr<cg::region::IRegionProcessProgressInfoInternal> info,
             std::shared_ptr<const cg::geo::IGeometryLonLatPolygon> polygon,
             std::shared_ptr<const cg::geo::TileIteratorFactory> factory)
        : RegionProcessTaskAbstract(std::move(info), std::move(polygon), std::move(factory)) {}
    cg::core::Result<void> processRegion() override { return {}; }
    using RegionProcessTaskAbstract::makeTileIterator;
    using RegionProcessTaskAbstract::polygLL;
    using RegionProcessTaskAbstract::progressFormUpdateOnProgress;
    using RegionProcessTaskAbstract::progressInfo;
    using RegionProcessTaskAbstract::stopToken;
};

std::shared_ptr<const cg::geo::TileIteratorFactory> makeFactory() {
    return std::make_shared<const cg::geo::TileIteratorFactory>(
        std::make_shared<const cg::geo::GeometryProjectedFactory>());
}

} // namespace

// ProgressFormUpdateOnProgress :104-113.
TEST(RegionProcessTaskAbstract, ProgressUpdateRows) {
    auto info = std::make_shared<RegionProcessProgressInfo>(std::stop_token{});
    StubTask task(info, nullptr, makeFactory());

    task.progressFormUpdateOnProgress(50, 200);
    EXPECT_EQ(info->processedRatio(), 0.25);
    EXPECT_EQ(info->secondLine(), QStringLiteral("Processed 50"));

    info->setProcessedRatio(0.5);            // a ratio that must be overwritten by 0
    task.progressFormUpdateOnProgress(7, 0); // IfThen(.., .., 0) :110
    EXPECT_EQ(info->processedRatio(), 0.0);
    EXPECT_EQ(info->secondLine(), QStringLiteral("Processed 7"));

    task.progressFormUpdateOnProgress(300, 200); // 1.5 clamped by SetProcessedRatio
    EXPECT_EQ(info->processedRatio(), 1.0);
    EXPECT_EQ(info->secondLine(), QStringLiteral("Processed 300"));

    EXPECT_TRUE(info->caption().isEmpty());
    EXPECT_TRUE(info->firstLine().isEmpty());
}

// Destroy :87-94 calls Finish, also for a task whose ProcessRegion never ran. The stop token is the
// progress object's (:80-81).
TEST(RegionProcessTaskAbstract, DestructorFinishesProgressAndTokenIsShared) {
    std::stop_source source;
    auto info = std::make_shared<RegionProcessProgressInfo>(source.get_token());
    {
        StubTask task(info, nullptr, makeFactory());
        EXPECT_FALSE(info->finished());
        EXPECT_EQ(&task.progressInfo(), info.get());
        EXPECT_FALSE(task.stopToken().stop_requested());
        source.request_stop();
        EXPECT_TRUE(task.stopToken().stop_requested());
        EXPECT_EQ(task.stopToken(), info->stopToken());
    }
    EXPECT_TRUE(info->finished());
}

// MakeTileIterator :96-102: the polygon's tiles. Google zoom 2 is 4x4 tiles; a lon/lat rectangle built
// from the full tile extent covers all of them: first tile (0,0), 16 in total.
TEST(RegionProcessTaskAbstract, MakeTileIteratorWalksThePolygon) {
    cg::geo::ProjectionSetFactory setFactory{
        cg::geo::HashSeed{}, std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})};
    const cg::geo::ProjectionPtr projection =
        setFactory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256)->zoom(2);
    const cg::geo::LonLatRect ll = projection->tileRect2LonLatRect(projection->tileRect());
    cg::geo::GeometryLonLatPolygonBuilder builder;
    builder.addOuter(
        ll, std::make_shared<const std::vector<cg::geo::LonLat>>(std::vector<cg::geo::LonLat>{
                {ll.left, ll.top}, {ll.right, ll.top}, {ll.right, ll.bottom}, {ll.left, ll.bottom}}));
    const auto polygon =
        std::static_pointer_cast<const cg::geo::IGeometryLonLatPolygon>(builder.makeStaticAndClear());

    auto info = std::make_shared<RegionProcessProgressInfo>(std::stop_token{});
    StubTask task(info, polygon, makeFactory());
    EXPECT_EQ(task.polygLL(), polygon);

    const auto iter = task.makeTileIterator(projection);
    ASSERT_TRUE(iter);
    EXPECT_EQ(iter->tilesTotal(), 16);
    PointI first;
    ASSERT_TRUE(iter->next(first));
    EXPECT_EQ(first, (PointI{0, 0}));
}
