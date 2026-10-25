// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-REG-014 (DownloadTaskProvider: per-zoom tile iterators for N workers, with resume seek).
// Ports: Src/RegionProcess/Download/u_DownloadTaskProvider.pas (GetTasksList :170-244).
//
// Geometry used (Google/3857, 256 px tiles; hand-derived from lon -> x = floor((lon+180)/360*2^z) and the
// Mercator y): the rectangle lon -170..-100, lat -10..30 covers tiles
//   z3: x 0..1, y 3..4   (4 tiles)      z4: x 0..3, y 6..8   (12 tiles)      z5: x 0..7, y 13..16 (32 tiles).
// The one-tile area lon -170..-160, lat 10..20 covers (0,3) at z3 and x 1..3, y 28..30 (9 tiles) at z6.

#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/region/download_task_provider.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "region_fake_map_type.h"

using cg::geo::LonLat;
using cg::geo::LonLatRect;
using cg::geo::PointI;
using cg::geo::RectI;
using cg::geo::ScanOrder;
using cg::geo::TileIteratorPtr;
using cg::region::DownloadTaskProvider;
using cg::region::PolygonPtr;
using cg::region::test::FakeMapType;

namespace {

using Tiles = std::vector<std::pair<int, int>>;

PolygonPtr rectPolygon(double lonL, double latTop, double lonR, double latBottom) {
    cg::geo::GeometryLonLatFactory f;
    return f.createPolygonByRect(LonLatRect{lonL, latTop, lonR, latBottom});
}

// Right triangle (-170,-10) (-100,-10) (-170,30): not a rectangle, so TileIteratorByPolygon is used.
PolygonPtr trianglePolygon() {
    cg::geo::GeometryLonLatFactory f;
    return std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatPolygon>(
        f.createPolygon(std::vector<LonLat>{{-170, -10}, {-100, -10}, {-170, 30}}));
}

struct Rig {
    std::shared_ptr<FakeMapType> map = std::make_shared<FakeMapType>();
    std::shared_ptr<const cg::geo::GeometryProjectedFactory> projected =
        std::make_shared<const cg::geo::GeometryProjectedFactory>();
    std::shared_ptr<const cg::geo::TileIteratorFactory> iterators =
        std::make_shared<const cg::geo::TileIteratorFactory>(projected);

    Rig() {
        cg::geo::ProjectionSetFactory setFactory{
            cg::geo::HashSeed{}, std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})};
        map->projectionSetValue =
            setFactory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
    }

    std::shared_ptr<DownloadTaskProvider> make(PolygonPtr polygon, int parts, std::vector<quint8> zooms,
                                               quint8 lastZoom, PointI lastPoint = {-1, -1},
                                               qint64 lastCount = -1) const {
        return std::make_shared<DownloadTaskProvider>(map, std::move(polygon), iterators, projected, parts,
                                                      std::move(zooms), lastZoom, lastPoint, lastCount);
    }
};

Tiles drain(cg::geo::ITileIterator& it) {
    Tiles out;
    PointI p;
    while (it.next(p)) {
        out.emplace_back(p.x(), p.y());
    }
    return out;
}

struct Result {
    qint64 total = 0;
    std::vector<TileIteratorPtr> tasks;
    std::vector<int> zooms() const {
        std::vector<int> z;
        for (const auto& t : tasks) {
            z.push_back(t->tilesRect().projection()->zoom());
        }
        return z;
    }
};

Result tasksOf(cg::region::IDownloadTaskProvider& provider, int worker) {
    Result r;
    provider.getTasksList(worker, r.total, r.tasks);
    return r;
}

} // namespace

// Proof 1: fresh start (lastProcessedZoom = zoomArr[0], point -1/-1, count -1): one iterator per zoom in
// array order, tilesTotal is the sum of the zoom totals (u_DownloadTaskProvider.pas:193-241).
TEST(DownloadTaskProvider, FreshStartRectangleOneIteratorPerZoomInArrayOrder) {
    Rig rig;
    auto provider = rig.make(rectPolygon(-170, 30, -100, -10), 1, {3, 4, 5}, 3);
    Result r = tasksOf(*provider, 0);
    EXPECT_EQ(r.zooms(), (std::vector<int>{3, 4, 5}));
    EXPECT_EQ(r.total, 4 + 12 + 32);
    ASSERT_EQ(r.tasks.size(), 3u);
    EXPECT_EQ(r.tasks[0]->scanOrder(), ScanOrder::Rows);
    EXPECT_EQ(r.tasks[0]->tilesRect().rect(), (RectI{0, 3, 2, 5}));
    EXPECT_EQ(r.tasks[1]->tilesRect().rect(), (RectI{0, 6, 4, 9}));
    EXPECT_EQ(r.tasks[2]->tilesRect().rect(), (RectI{0, 13, 8, 17}));
    EXPECT_EQ(drain(*r.tasks[0]), (Tiles{{0, 3}, {1, 3}, {0, 4}, {1, 4}}));
    EXPECT_EQ(drain(*r.tasks[1]).size(), 12u);
    EXPECT_EQ(drain(*r.tasks[2]).size(), 32u);
}

// The array order is kept as given (not sorted): zooms 5, 3, 4 with lastProcessedZoom 3.
TEST(DownloadTaskProvider, IteratorsFollowTheGivenZoomArrayOrder) {
    Rig rig;
    auto provider = rig.make(rectPolygon(-170, 30, -100, -10), 1, {5, 3, 4}, 3);
    Result r = tasksOf(*provider, 0);
    EXPECT_EQ(r.zooms(), (std::vector<int>{5, 3, 4}));
    EXPECT_EQ(r.total, 32 + 4 + 12);
}

TEST(DownloadTaskProvider, FreshStartPolygonUsesThePolygonIterator) {
    Rig rig;
    auto provider = rig.make(trianglePolygon(), 1, {3, 4, 5}, 3);
    Result r = tasksOf(*provider, 0);
    EXPECT_EQ(r.zooms(), (std::vector<int>{3, 4, 5}));
    for (const auto& t : r.tasks) {
        EXPECT_EQ(t->scanOrder(), ScanOrder::Cols);
    }
    qint64 drained = 0;
    for (auto& t : r.tasks) {
        drained += static_cast<qint64>(drain(*t).size());
    }
    EXPECT_EQ(drained, r.total);
    EXPECT_EQ(r.total, 34);
}

// Proof 2: the workers' tiles are a partition of the single-worker tile set; tilesTotal of a worker is what
// its iterators yield.
TEST(DownloadTaskProvider, WorkersPartitionTheSingleWorkerTileSet) {
    struct Case {
        PolygonPtr polygon;
        int parts;
    };
    Rig rig;
    for (const PolygonPtr& polygon : {rectPolygon(-170, 30, -100, -10), trianglePolygon()}) {
        for (int parts : {2, 3}) {
            auto single = rig.make(polygon, 1, {3, 4, 5}, 3);
            Result one = tasksOf(*single, 0);
            std::vector<std::set<std::pair<int, int>>> whole;
            for (auto& t : one.tasks) {
                Tiles v = drain(*t);
                whole.emplace_back(v.begin(), v.end());
                EXPECT_EQ(whole.back().size(), v.size());
            }

            auto provider = rig.make(polygon, parts, {3, 4, 5}, 3);
            std::vector<std::set<std::pair<int, int>>> united(3);
            std::size_t yielded = 0;
            for (int w = 0; w < parts; ++w) {
                Result r = tasksOf(*provider, w);
                qint64 workerTiles = 0;
                ASSERT_EQ(r.tasks.size(), 3u);
                for (std::size_t i = 0; i < r.tasks.size(); ++i) {
                    for (const auto& tile : drain(*r.tasks[i])) {
                        united[i].insert(tile);
                        ++workerTiles;
                        ++yielded;
                    }
                }
                EXPECT_EQ(r.total, workerTiles) << "worker " << w << " of " << parts;
            }
            EXPECT_EQ(united, whole) << "parts " << parts;
            std::size_t wholeCount = 0;
            for (const auto& s : whole) {
                wholeCount += s.size();
            }
            EXPECT_EQ(yielded, wholeCount) << "a tile was handed out twice, parts " << parts;
        }
    }
}

// Proof 3: z3 area is one tile, 1 part only; z6 splits 9 tiles into 4 parts (2,2,2,3).
TEST(DownloadTaskProvider, WorkerBeyondPartsCountOfOneZoomSkipsOnlyThatZoom) {
    Rig rig;
    auto provider = rig.make(rectPolygon(-170, 20, -160, 10), 4, {3, 6}, 3);

    Result w0 = tasksOf(*provider, 0);
    EXPECT_EQ(w0.zooms(), (std::vector<int>{3, 6}));
    EXPECT_EQ(w0.total, 1 + 2);

    // worker 1 == partsCount() (1) of z3: the boundary of ">= partsCount()"
    Result w1 = tasksOf(*provider, 1);
    EXPECT_EQ(w1.zooms(), (std::vector<int>{6}));
    EXPECT_EQ(w1.total, 2);

    Result w2 = tasksOf(*provider, 2);
    EXPECT_EQ(w2.zooms(), (std::vector<int>{6}));
    EXPECT_EQ(w2.total, 2);
    EXPECT_EQ(drain(*w2.tasks[0]).size(), 2u);

    Result w3 = tasksOf(*provider, 3);
    EXPECT_EQ(w3.zooms(), (std::vector<int>{6}));
    EXPECT_EQ(w3.total, 3);
}

// Proof 4: resume. the deviation of T-0226 (deviations.md), was LR-2 / L-265: legacy's seek consumes one unit
// of the iterator budget, so the last tile of the resumed zoom was never requested; the budget gets one more
// unit.
TEST(DownloadTaskProvider, ResumeSkipsLowerZoomsButCountsThemAndSeeksAtTheResumedZoom) {
    Rig rig;
    // z4 tiles in order: (0,6)(1,6)(2,6)(3,6)(0,7)(1,7)(2,7)(3,7)(0,8)(1,8)(2,8)(3,8); (1,7) is the 6th.
    auto provider = rig.make(rectPolygon(-170, 30, -100, -10), 1, {3, 4, 5}, 4, PointI{1, 7}, 6);
    Result r = tasksOf(*provider, 0);
    EXPECT_EQ(r.zooms(), (std::vector<int>{4, 5}));
    EXPECT_EQ(r.total, 4 + 12 + 32);
    // budget 12 - 6 (+1 for the seek, the deviation of T-0226 (deviations.md)) from (1,7): (1,7) is consumed
    // by the seek, then the six tiles to the end, (3,8) included.
    EXPECT_EQ(drain(*r.tasks[0]), (Tiles{{2, 7}, {3, 7}, {0, 8}, {1, 8}, {2, 8}, {3, 8}}));
    // zoom above the resumed one: untouched, from the start of the area, full count
    Tiles z5 = drain(*r.tasks[1]);
    EXPECT_EQ(z5.size(), 32u);
    EXPECT_EQ(z5.front(), (std::pair<int, int>{0, 13}));
}

// the deviation of T-0226 (deviations.md): the resume point is the part's last tile: the seek takes it,
// nothing follows.
TEST(DownloadTaskProvider, ResumeAtTheLastTileOfThePartProducesNoTile) {
    Rig rig;
    auto provider = rig.make(rectPolygon(-170, 30, -100, -10), 1, {4}, 4, PointI{3, 8}, 12);
    Result r = tasksOf(*provider, 0);
    EXPECT_EQ(r.total, 12);
    EXPECT_EQ(drain(*r.tasks[0]), Tiles{});
}

// the deviation of T-0226 (deviations.md): two workers over the 12 z4 tiles; each worker's part is 6 tiles.
// Worker 0 = (0,6)..(1,7), worker 1 = (2,7)..(3,8). The resumed tail reaches the part's last tile and not one
// tile of the other part.
TEST(DownloadTaskProvider, ResumeWithTwoWorkersRequestsTheWholeTailOfEachPartAndNothingOfTheOther) {
    Rig rig;
    auto w0 = rig.make(rectPolygon(-170, 30, -100, -10), 2, {4}, 4, PointI{1, 6}, 2);
    EXPECT_EQ(drain(*tasksOf(*w0, 0).tasks[0]), (Tiles{{2, 6}, {3, 6}, {0, 7}, {1, 7}}));
    auto w1 = rig.make(rectPolygon(-170, 30, -100, -10), 2, {4}, 4, PointI{3, 7}, 2);
    EXPECT_EQ(drain(*tasksOf(*w1, 1).tasks[0]), (Tiles{{0, 8}, {1, 8}, {2, 8}, {3, 8}}));
}

// the deviation of T-0226 (deviations.md): the polygon iterator (the triangle) ends its resumed budget on the
// part's last tile too.
TEST(DownloadTaskProvider, ResumeOfAPolygonPartIncludesItsLastTile) {
    Rig rig;
    // z4 triangle tiles, column by column: worker 0 holds (0,6)(0,7)(0,8)(1,6), worker 1 holds
    // (1,7)(1,8)(2,7)(2,8)(3,8) (9 tiles split 4 + 5).
    auto w1 = rig.make(trianglePolygon(), 2, {4}, 4, PointI{2, 7}, 3);
    EXPECT_EQ(drain(*tasksOf(*w1, 1).tasks[0]), (Tiles{{2, 8}, {3, 8}}));
    auto w0 = rig.make(trianglePolygon(), 2, {4}, 4, PointI{0, 8}, 3);
    EXPECT_EQ(drain(*tasksOf(*w0, 0).tasks[0]), (Tiles{{1, 6}}));
}

TEST(DownloadTaskProvider, ResumeWithoutCountKeepsTheBudgetButStillSeeks) {
    Rig rig;
    // 2 workers: worker 0 has 6 of the 12 z4 tiles, start (0,6). Point (2,6), count -1 / 0: the budget stays
    // 6 (it would be 7 if a count <= 0 were subtracted), the seek spends one unit on (2,6): 5 tiles follow.
    for (qint64 count : {qint64{-1}, qint64{0}}) {
        auto provider = rig.make(rectPolygon(-170, 30, -100, -10), 2, {4}, 4, PointI{2, 6}, count);
        Result r = tasksOf(*provider, 0);
        EXPECT_EQ(r.total, 6) << count;
        EXPECT_EQ(drain(*r.tasks[0]), (Tiles{{3, 6}, {0, 7}, {1, 7}, {2, 7}, {3, 7}})) << count;
    }
}

// Coordinate 0 is a valid point (">= 0", :213). Area lon -170..-100, lat 80..84 at z3 is tiles (0,0), (1,0).
TEST(DownloadTaskProvider, LastPointWithZeroCoordinatesStillSeeks) {
    Rig rig;
    auto provider = rig.make(rectPolygon(-170, 84, -100, 80), 1, {3}, 3, PointI{0, 0}, 0);
    Result r = tasksOf(*provider, 0);
    EXPECT_EQ(r.total, 2);
    EXPECT_EQ(drain(*r.tasks[0]), (Tiles{{1, 0}}));
}

TEST(DownloadTaskProvider, NegativeCoordinateOfTheLastPointMeansNoSeek) {
    Rig rig;
    for (PointI point : {PointI{-1, 7}, PointI{1, -1}, PointI{-1, -1}}) {
        auto provider = rig.make(rectPolygon(-170, 30, -100, -10), 1, {4}, 4, point, 4);
        Result r = tasksOf(*provider, 0);
        EXPECT_EQ(r.total, 12) << point.x() << "," << point.y();
        Tiles tiles = drain(*r.tasks[0]);
        EXPECT_EQ(tiles.size(), 12u) << point.x() << "," << point.y();
        EXPECT_EQ(tiles.front(), (std::pair<int, int>{0, 6}));
    }
}

// Proof 5: two threads on one provider. "Prepared once" is not observable from outside; this proves only the
// absence of a race (TSan binary) and of a divergent result.
TEST(DownloadTaskProvider, ConcurrentFirstCallsGiveTheSingleThreadedResult) {
    Rig rig;
    using Shape = std::vector<std::tuple<int, RectI, ScanOrder>>;
    auto shapeOf = [](const Result& r) {
        Shape s;
        for (const auto& t : r.tasks) {
            s.emplace_back(t->tilesRect().projection()->zoom(), t->tilesRect().rect(), t->scanOrder());
        }
        return s;
    };
    Result reference;
    {
        auto provider = rig.make(trianglePolygon(), 2, {3, 4, 5}, 3);
        reference = tasksOf(*provider, 1);
    }
    const Shape refShape = shapeOf(reference);

    using Clock = std::chrono::steady_clock;
    int overlapped = 0;
    // A round is about a quarter of a millisecond and thread start-up jitter decides whether the two calls
    // overlap, so rounds continue until five overlapped (bounded).
    for (int round = 0; round < 3000 && overlapped < 5; ++round) {
        auto provider = rig.make(trianglePolygon(), 2, {3, 4, 5}, 3);
        std::latch start(2);
        std::atomic<Clock::rep> begin[2];
        std::atomic<Clock::rep> end[2];
        Result results[2];
        auto work = [&](int i) {
            start.arrive_and_wait();
            begin[i] = Clock::now().time_since_epoch().count();
            results[i] = tasksOf(*provider, 1);
            end[i] = Clock::now().time_since_epoch().count();
        };
        std::thread a(work, 0);
        std::thread b(work, 1);
        a.join();
        b.join();
        if (begin[0] < end[1] && begin[1] < end[0]) {
            ++overlapped;
        }
        for (const Result& r : results) {
            EXPECT_EQ(r.total, reference.total);
            EXPECT_EQ(shapeOf(r), refShape);
        }
    }
    EXPECT_GE(overlapped, 5) << "the two calls never overlapped";
}
