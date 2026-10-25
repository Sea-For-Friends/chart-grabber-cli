// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_GeometryFunc.pas:734-1016 (CalcTileCountIn*, TryProjectedPolygonToTileRect,
// SplitProjectedPolygon), i_TileIteratorFactory.pas, u_TileIteratorFactory.pas,
// i_TileIteratorDataProvider.pas, u_TileIteratorDataProvider.pas. docs/specs/geo.md §4.3.
//
// FEAT-GEO-015

#include "cg/core/sync.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/projection.h"
#include "cg/geo/tile_iterator.h"
#include "cg/geo/tile_rect.h"

#include <QtGlobal>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace cg::geo {

class IGeometryLonLatPolygon;
class GeometryProjectedFactory;

// u_GeometryFunc.pas:734-800. The counter is a recursive binary subdivision of the tiles rect
// (not a per-tile scan): bisect the longer axis, recurse both halves, short-circuit on
// checkRectIntersection()'s NoIntersect/RectInPolygon answers. The 32-bit `Integer` product
// wraparound at u_GeometryFunc.pas:757/790 (`VRectSize.X * VRectSize.Y`, computed before widening
// to the Int64 Result) is reproduced via an unsigned 32-bit intermediate -- see
// wrappingRectArea() in tile_count.cpp; not UB, so no deviation is recorded for it.
[[nodiscard]] qint64 calcTileCountInProjectedSinglePolygon(const IProjection& projection,
                                                           const IGeometryProjectedSinglePolygon& polygon);

// u_GeometryFunc.pas:802-823. A plain sum over parts for a multi-polygon -- double-counts tiles
// shared by overlapping parts, because TileIteratorByPolygon (tile_iterator.h) emits each tile
// once. Legacy behavior, kept; docs/parity/deviations.md D-5.
[[nodiscard]] qint64 calcTileCountInProjectedPolygon(const IProjection& projection,
                                                     const IGeometryProjectedPolygon& polygon);

// u_GeometryFunc.pas:897-931. A rect only for a single, hole-free, exactly-4-vertex ring whose
// corners -- converted to tile coordinates and rounded ToTopLeft -- form an axis-aligned box in
// either vertex order. TileIteratorFactory uses this to pick row-major (TileIteratorByRect) over
// column-major-clipped (TileIteratorByPolygon): a tile-aligned rectangular selection silently
// changes scan order, and that is user-visible download ordering, not just a perf shortcut.
[[nodiscard]] std::optional<RectI> tryProjectedPolygonToTileRect(const IProjection& projection,
                                                                 const IGeometryProjectedPolygon& polygon);

struct PolygonSplit {
    std::vector<PointI> startPoints;
    std::vector<qint64> tilesCount; // D-6: qint64 throughout; legacy's TIntegerDynArray is 32-bit.
};

// u_GeometryFunc.pas:950-1016. Brute force by design: runs the whole iteration once (through the
// real TileIteratorByRect/TileIteratorByPolygon, exactly as legacy does) just to record part
// boundaries. Reduces splitCount while tilesTotal / splitCount == 0; the last part absorbs
// whatever remains after every earlier part got exactly tilesTotal/splitCount tiles; partsCount()
// (== startPoints.size()) may come back smaller than requested.
[[nodiscard]] PolygonSplit splitProjectedPolygon(const IProjection& projection,
                                                 const IGeometryProjectedPolygon& polygon, int splitCount,
                                                 qint64 tilesTotal);

// Ports ITileIteratorFactory / TTileIteratorFactory (u_TileIteratorFactory.pas). Legacy's
// MakeTileIterator takes AStartPointX/AStartPointY, each defaulting to -1, and treats *either*
// being -1 as "no start point" (TopLeft); the port folds that into one std::optional<PointI>, so
// std::nullopt IS the "-1,-1" sentinel -- there is no separate raw-int overload to leak it through.
class TileIteratorFactory {
public:
    explicit TileIteratorFactory(std::shared_ptr<const GeometryProjectedFactory> geometryProjectedFactory);

    [[nodiscard]] TileIteratorPtr
    makeTileIterator(const ProjectionPtr& projection,
                     const std::shared_ptr<const IGeometryLonLatPolygon>& lonLatPolygon,
                     qint64 tilesToProcess = -1, std::optional<PointI> startPoint = std::nullopt) const;

private:
    std::shared_ptr<const GeometryProjectedFactory> geometryProjectedFactory_;
};

// Ports ITileIteratorDataProvider / TTileIteratorDataProvider (u_TileIteratorDataProvider.pas):
// the "split this region across N workers" object region/net consume. tilesTotal() is lazy and
// memoised (GetTilesTotal); the per-part split is lazy and memoised together (GetStartPoint /
// GetTilesToProcess / GetPartsCount all trigger the one _DoPrepareSplit). Thread-safe (§8): a
// shared_mutex stands in for legacy's IReadWriteSync, guarding the same two memoised computations.
class TileIteratorDataProvider {
public:
    TileIteratorDataProvider(ProjectionPtr projection,
                             std::shared_ptr<const IGeometryProjectedPolygon> polygon, int partsCount);

    [[nodiscard]] const ProjectionPtr& projection() const noexcept { return projection_; }
    [[nodiscard]] const std::shared_ptr<const IGeometryProjectedPolygon>& polygon() const noexcept {
        return polygon_;
    }
    [[nodiscard]] qint64 tilesTotal() const;
    [[nodiscard]] int partsCount() const;
    [[nodiscard]] qint64 tilesToProcess(int partIndex) const; // throws std::out_of_range on a bad index
    [[nodiscard]] PointI startPoint(int partIndex) const;

private:
    void prepareCountLocked() const;  // caller holds a write lock
    void ensureSplit() const;         // acquires locks itself, like legacy's unguarded pre-check
    void checkIndex(int index) const; // caller holds at least a read lock

    ProjectionPtr projection_;
    std::shared_ptr<const IGeometryProjectedPolygon> polygon_;
    int partsCountRequested_;

    mutable core::Mutex mutex_;
    mutable qint64 tilesTotal_ = -1;
    mutable int partsCount_ = 0;
    mutable std::vector<PointI> startPoints_;
    mutable std::vector<qint64> tilesCount_;
    // Legacy reads FPrepared unguarded before deciding whether to call _DoPrepareSplit; an atomic
    // (rather than a plain bool) keeps that quick pre-check race-free under the C++ memory model
    // while preserving the exact same "prepare once, recheck under the write lock" shape.
    mutable std::atomic<bool> prepared_{false};
};

} // namespace cg::geo
