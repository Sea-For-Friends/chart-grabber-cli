// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/tile_count.h"

#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"

#include <QtGlobal>

#include <array>
#include <stdexcept>
#include <utility>

namespace cg::geo {
namespace {

// u_GeometryFunc.pas:757/790: `VRectSize.X * VRectSize.Y` and
// `(VTileRect.Right - VTileRect.Left) * (VTileRect.Bottom - VTileRect.Top)` are both a Delphi
// 32-bit `Integer` multiply (no overflow checking) that only widens to Int64 on assignment to
// Result -- i.e. it silently wraps modulo 2^32 for a rect wider*taller than ~46341x46341 tiles.
// Signed overflow is undefined behavior in C++, so the wraparound is reproduced through an
// unsigned 32-bit intermediate (defined modulo-2^32 arithmetic) and then reinterpreted as a
// two's-complement qint32 (well-defined by the guaranteed two's-complement representation, C++20
// [basic.fundamental]) -- bit-identical to the Delphi result, no deviation to record.
qint64 wrappingRectArea(int width, int height) noexcept {
    const quint32 wrapped = static_cast<quint32>(width) * static_cast<quint32>(height);
    return static_cast<qint32>(wrapped);
}

qint64 calcTileCountRecursive(const IProjection& projection, const IGeometryProjectedSinglePolygon& polygon,
                              const RectI& tileRect) {
    const int width = tileRect.width();
    const int height = tileRect.height();
    const DoubleRect pixelRect = toDoubleRect(projection.tileRect2PixelRect(tileRect));
    const RectWithPolygonIntersection intersection = polygon.checkRectIntersection(pixelRect);
    if (intersection == RectWithPolygonIntersection::NoIntersect) {
        return 0;
    }
    if (intersection == RectWithPolygonIntersection::RectInPolygon) {
        return wrappingRectArea(width, height);
    }

    RectI first = tileRect;
    RectI second = tileRect;
    if (width > height) {
        if (width <= 1) {
            return 1;
        }
        first.right = tileRect.left + width / 2;
        second.left = first.right;
    } else {
        if (height <= 1) {
            return 1;
        }
        first.bottom = tileRect.top + height / 2;
        second.top = first.bottom;
    }
    return calcTileCountRecursive(projection, polygon, first) +
           calcTileCountRecursive(projection, polygon, second);
}

// Wraps a plain reference in a non-owning shared_ptr: get() aliases `obj`, the control block is
// empty (use_count() == 0, destructor is a no-op). Standard "aliasing constructor with an empty
// owner" idiom -- lets splitProjectedPolygon(), which per spec only receives a `const IProjection&`
// / `const IGeometryProjectedPolygon&`, reuse the real TileIteratorByRect/TileIteratorByPolygon
// (which need a ProjectionPtr / shared_ptr<const IGeometryProjectedPolygon> to match
// tile_iterator.h's already-landed slice-A signatures) for the one synchronous call below, with no
// ownership implied and nothing outliving this function.
template <typename T> std::shared_ptr<const T> aliasNonOwning(const T& obj) {
    return std::shared_ptr<const T>(std::shared_ptr<void>(), &obj);
}

} // namespace

qint64 calcTileCountInProjectedSinglePolygon(const IProjection& projection,
                                             const IGeometryProjectedSinglePolygon& polygon) {
    if (const auto rect = tryProjectedPolygonToTileRect(projection, polygon)) {
        return wrappingRectArea(rect->width(), rect->height());
    }
    const RectI startRect =
        roundRect(projection.pixelRectFloat2TileRectFloat(polygon.bounds()), RectRounding::Outside);
    return calcTileCountRecursive(projection, polygon, startRect);
}

qint64 calcTileCountInProjectedPolygon(const IProjection& projection,
                                       const IGeometryProjectedPolygon& polygon) {
    if (const auto* single = dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon)) {
        return calcTileCountInProjectedSinglePolygon(projection, *single);
    }
    if (const auto* multi = dynamic_cast<const IGeometryProjectedMultiPolygon*>(&polygon)) {
        // u_GeometryFunc.pas:802-823: a plain sum -- overlapping parts double-count their shared
        // tiles. Legacy behavior, kept; docs/parity/deviations.md D-5.
        qint64 total = 0;
        for (std::size_t i = 0; i < multi->count(); ++i) {
            total += calcTileCountInProjectedSinglePolygon(projection, *multi->item(i));
        }
        return total;
    }
    return 0;
}

std::optional<RectI> tryProjectedPolygonToTileRect(const IProjection& projection,
                                                   const IGeometryProjectedPolygon& polygon) {
    const auto* single = dynamic_cast<const IGeometryProjectedSinglePolygon*>(&polygon);
    if (!single || single->holeCount() != 0) {
        return std::nullopt;
    }
    const auto points = single->outerBorder()->points();
    if (points.size() != 4) {
        return std::nullopt;
    }
    std::array<PointI, 4> corner{};
    for (std::size_t i = 0; i < 4; ++i) {
        corner[i] = roundPoint(projection.pixelPosFloat2TilePosFloat(points[i]), PointRounding::ToTopLeft);
    }
    const bool isRect = (corner[0].x() == corner[3].x() && corner[2].x() == corner[1].x() &&
                         corner[0].y() == corner[1].y() && corner[2].y() == corner[3].y()) ||
                        (corner[0].x() == corner[1].x() && corner[2].x() == corner[3].x() &&
                         corner[3].y() == corner[0].y() && corner[1].y() == corner[2].y());
    if (!isRect) {
        return std::nullopt;
    }
    return roundRect(projection.pixelRectFloat2TileRectFloat(single->bounds()), RectRounding::Outside);
}

PolygonSplit splitProjectedPolygon(const IProjection& projection, const IGeometryProjectedPolygon& polygon,
                                   int splitCount, qint64 tilesTotal) {
    PolygonSplit result;
    if (splitCount <= 0 || tilesTotal <= 0) {
        Q_ASSERT(false);
        return result;
    }

    int effectiveSplitCount = splitCount;
    qint64 partTilesCount = 0;
    do {
        partTilesCount = tilesTotal / effectiveSplitCount;
        if (partTilesCount == 0) {
            --effectiveSplitCount;
        }
    } while (!(partTilesCount > 0 || effectiveSplitCount <= 0));

    if (effectiveSplitCount <= 0) {
        return result;
    }

    result.startPoints.assign(static_cast<std::size_t>(effectiveSplitCount), PointI{});
    result.tilesCount.assign(static_cast<std::size_t>(effectiveSplitCount), 0);

    const ProjectionPtr aliasedProjection = aliasNonOwning(projection);
    TileIteratorPtr iterator;
    if (const auto tileRect = tryProjectedPolygonToTileRect(projection, polygon)) {
        // legacy comment (u_GeometryFunc.pas:972): "ToDo: make calculations faster than Iterator
        // do" -- kept as brute force, exactly as legacy.
        iterator = std::make_unique<TileIteratorByRect>(TileRect(aliasedProjection, *tileRect));
    } else {
        iterator = std::make_unique<TileIteratorByPolygon>(aliasedProjection, aliasNonOwning(polygon));
    }

    int partIndex = 0;
    bool foundNextPart = true;
    PointI point;
    while (iterator->next(point)) {
        ++result.tilesCount[static_cast<std::size_t>(partIndex)];

        if (foundNextPart) {
            result.startPoints[static_cast<std::size_t>(partIndex)] = point;
            foundNextPart = false;
        }

        if (partIndex < effectiveSplitCount - 1) {
            foundNextPart = result.tilesCount[static_cast<std::size_t>(partIndex)] == partTilesCount;
            if (foundNextPart) {
                ++partIndex;
            }
        }
    }
    return result;
}

// --- TileIteratorFactory -----------------------------------------------------------------------

TileIteratorFactory::TileIteratorFactory(
    std::shared_ptr<const GeometryProjectedFactory> geometryProjectedFactory)
    : geometryProjectedFactory_(std::move(geometryProjectedFactory)) {
    Q_ASSERT(geometryProjectedFactory_);
}

TileIteratorPtr
TileIteratorFactory::makeTileIterator(const ProjectionPtr& projection,
                                      const std::shared_ptr<const IGeometryLonLatPolygon>& lonLatPolygon,
                                      qint64 tilesToProcess, std::optional<PointI> startPoint) const {
    std::vector<DoublePoint> scratch;
    const std::shared_ptr<const IGeometryProjectedPolygon> projected =
        geometryProjectedFactory_->createPolygonByLonLatPolygon(*projection, *lonLatPolygon, scratch);

    // u_TileIteratorFactory.pas:91: TryProjectedPolygonToTileRect(AProjection, VProjected) is called
    // even when VProjected is nil -- Supports(nil, ...) is simply False, so VTileRect stays nil and
    // the code falls through to TTileIteratorByPolygon.Create(AProjection, nil, ...), which does the
    // empty setup for a nil polygon (no crash, tilesTotal() == 0; see tile_iterator.cpp's null
    // branch). Dereferencing *projected here instead would be UB, so the null case is special-cased
    // to skip straight to TileIteratorByPolygon, which already reproduces that same
    // empty-setup-for-a-null-polygon behavior.
    if (projected) {
        if (const auto tileRect = tryProjectedPolygonToTileRect(*projection, *projected)) {
            return std::make_unique<TileIteratorByRect>(TileRect(projection, *tileRect), tilesToProcess,
                                                        startPoint);
        }
    }
    return std::make_unique<TileIteratorByPolygon>(projection, projected, tilesToProcess, startPoint);
}

// --- TileIteratorDataProvider ------------------------------------------------------------------

TileIteratorDataProvider::TileIteratorDataProvider(ProjectionPtr projection,
                                                   std::shared_ptr<const IGeometryProjectedPolygon> polygon,
                                                   int partsCount)
    : projection_(std::move(projection)), polygon_(std::move(polygon)), partsCountRequested_(partsCount) {
    Q_ASSERT(partsCountRequested_ >= 1);
}

void TileIteratorDataProvider::prepareCountLocked() const {
    if (tilesTotal_ == -1) {
        tilesTotal_ = calcTileCountInProjectedPolygon(*projection_, *polygon_);
    }
}

qint64 TileIteratorDataProvider::tilesTotal() const {
    {
        core::ReadLock lock(mutex_);
        if (tilesTotal_ != -1) {
            return tilesTotal_;
        }
    }
    core::WriteLock lock(mutex_);
    prepareCountLocked();
    return tilesTotal_;
}

void TileIteratorDataProvider::ensureSplit() const {
    if (prepared_.load(std::memory_order_acquire)) {
        return;
    }
    core::WriteLock lock(mutex_);
    if (prepared_.load(std::memory_order_relaxed)) {
        return;
    }

    prepareCountLocked();
    if (partsCountRequested_ == 1 || tilesTotal_ < 2) {
        const RectI tileRect =
            roundRect(projection_->pixelRectFloat2TileRectFloat(polygon_->bounds()), RectRounding::Outside);
        startPoints_ = {tileRect.topLeft()};
        tilesCount_ = {tilesTotal_};
        partsCount_ = 1;
    } else {
        PolygonSplit split =
            splitProjectedPolygon(*projection_, *polygon_, partsCountRequested_, tilesTotal_);
        startPoints_ = std::move(split.startPoints);
        tilesCount_ = std::move(split.tilesCount);
        partsCount_ = static_cast<int>(startPoints_.size());
    }
    prepared_.store(true, std::memory_order_release);
}

void TileIteratorDataProvider::checkIndex(int index) const {
    if (index < 0 || index >= partsCount_) {
        throw std::out_of_range("TileIteratorDataProvider: part index out of range");
    }
}

int TileIteratorDataProvider::partsCount() const {
    ensureSplit();
    core::ReadLock lock(mutex_);
    return partsCount_;
}

qint64 TileIteratorDataProvider::tilesToProcess(int partIndex) const {
    ensureSplit();
    core::ReadLock lock(mutex_);
    checkIndex(partIndex);
    return tilesCount_[static_cast<std::size_t>(partIndex)];
}

PointI TileIteratorDataProvider::startPoint(int partIndex) const {
    ensureSplit();
    core::ReadLock lock(mutex_);
    checkIndex(partIndex);
    return startPoints_[static_cast<std::size_t>(partIndex)];
}

} // namespace cg::geo
