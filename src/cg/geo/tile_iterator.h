// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileIterator.pas, u_TileIteratorByRect.pas, u_TileIteratorSpiralByRect.pas,
// u_TileIteratorByPolygon.pas (docs/specs/geo.md §4.2).
//
// FEAT-GEO-015

#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/projection.h"
#include "cg/geo/tile_rect.h"

#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <optional>

namespace cg::geo {

// Ports the ITileIteratorByRows / ITileIteratorByCols marker interfaces (i_TileIterator.pas:43,47):
// legacy declares ITileIteratorByRows on TTileIteratorByRect and declares nothing on
// TTileIteratorByPolygon, which is actually column-major -- a labelling bug (docs/specs/geo.md
// §4.2, §10 D-4). ScanOrder fixes the label without changing any iteration order.
enum class ScanOrder : std::uint8_t { Rows, Cols, Spiral };

class ITileIterator {
public:
    virtual ~ITileIterator() = default;
    [[nodiscard]] virtual bool next(PointI& tile) = 0;
    virtual void reset() = 0;
    // Non-const: TileIteratorByPolygon computes this lazily and memoises it.
    [[nodiscard]] virtual qint64 tilesTotal() = 0;
    [[nodiscard]] virtual const TileRect& tilesRect() const = 0;
    [[nodiscard]] virtual ScanOrder scanOrder() const noexcept = 0;
};
using TileIteratorPtr = std::unique_ptr<ITileIterator>;

// FEAT-GEO-015. u_TileIteratorByRect.pas:47-158. Row-major: X sweeps [rect.left, rect.right),
// wrapping to the next Y; the wrap always resets to rect.left, never to a caller-supplied start X
// -- "from here, row-major, N tiles", not "this sub-rectangle" (docs/specs/geo.md §4.2.1). A
// non-(-1) tilesToProcess overwrites tilesTotal(), but only when the rect is non-empty (legacy
// leaves an empty rect's total at 0 regardless of tilesToProcess).
class TileIteratorByRect final : public ITileIterator {
public:
    explicit TileIteratorByRect(TileRect rect, qint64 tilesToProcess = -1,
                                std::optional<PointI> startPoint = std::nullopt);

    [[nodiscard]] bool next(PointI& tile) override;
    void reset() override;
    [[nodiscard]] qint64 tilesTotal() override { return tilesTotal_; }
    [[nodiscard]] const TileRect& tilesRect() const override { return rect_; }
    [[nodiscard]] ScanOrder scanOrder() const noexcept override { return ScanOrder::Rows; }

private:
    TileRect rect_;
    qint64 tilesTotal_;
    PointI startPoint_;
    PointI current_;
    qint64 processedCount_ = 0;
    bool eoi_ = false;
};

// FEAT-GEO-015. u_TileIteratorSpiralByRect.pas. Clockwise from the centre, ring by ring; ring R
// holds 8*R tiles (R>0) or 1 (R==0), split into eight R-tile segments whose per-segment delta
// formulas are load-bearing (docs/specs/geo.md §4.2.2). No tilesToProcess/startPoint support in
// legacy; tilesTotal() is the plain rect area (inherited shape), never consulted by next().
// Complexity is O(maxRing^2) generated for O(area) yielded on a non-square rect; accepted, this is
// legacy behavior and the rects involved are viewport-sized.
class TileIteratorSpiralByRect final : public ITileIterator {
public:
    explicit TileIteratorSpiralByRect(const TileRect& rect); // centre = rect centre
    TileIteratorSpiralByRect(TileRect rect, PointI centre);

    [[nodiscard]] bool next(PointI& tile) override;
    void reset() override;
    [[nodiscard]] qint64 tilesTotal() override { return tilesTotal_; }
    [[nodiscard]] const TileRect& tilesRect() const override { return rect_; }
    [[nodiscard]] ScanOrder scanOrder() const noexcept override { return ScanOrder::Spiral; }

private:
    [[nodiscard]] bool checkPoint(PointI p) const noexcept;
    [[nodiscard]] static int getMaxRing(PointI centre, const RectI& rect) noexcept;
    [[nodiscard]] static int getTilesInRingCount(int ring) noexcept;
    [[nodiscard]] static PointI getDeltaByRingAndIndex(int ring, int index) noexcept;

    TileRect rect_;
    qint64 tilesTotal_;
    PointI centre_;
    int maxRadius_;
    int currentRing_ = 0;
    int indexInRing_ = 0;
    bool eoi_ = false;
};

// FEAT-GEO-015. u_TileIteratorByPolygon.pas. Column-major: X outer, Y inner, top-to-bottom within
// a column, clipped against a projected polygon (docs/specs/geo.md §4.2.3). Each candidate tile is
// converted to its pixel rect and tested with isRectIntersectPolygon; the three-tier dispatch
// (single polygon direct / MRU-cached part / linear scan setting the MRU) is kept exactly as
// legacy, including the deliberate avoidance of polygon->isRectIntersectPolygon() for the whole
// shape.
class TileIteratorByPolygon final : public ITileIterator {
public:
    TileIteratorByPolygon(ProjectionPtr projection,
                          const std::shared_ptr<const IGeometryProjectedPolygon>& polygon,
                          qint64 tilesToProcess = -1, std::optional<PointI> startPoint = std::nullopt);

    [[nodiscard]] bool next(PointI& tile) override;
    void reset() override;
    [[nodiscard]] qint64 tilesTotal() override;
    [[nodiscard]] const TileRect& tilesRect() const override { return rect_; }
    [[nodiscard]] ScanOrder scanOrder() const noexcept override { return ScanOrder::Cols; }

private:
    void makeEmptySetup();
    [[nodiscard]] bool internalIntersectPolygon(const DoubleRect& r);

    ProjectionPtr projection_;
    std::shared_ptr<const IGeometryProjectedPolygon> polygon_;
    std::shared_ptr<const IGeometryProjectedSinglePolygon> singleLine_;
    std::shared_ptr<const IGeometryProjectedMultiPolygon> multiProjected_;
    std::shared_ptr<const IGeometryProjectedSinglePolygon> lastUsedLine_;
    TileRect rect_;
    PointI startPoint_;
    PointI current_;
    qint64 tilesTotal_;
    bool stopOnProcessedTilesCount_ = false;
    qint64 processedTilesCount_ = 0;
};

} // namespace cg::geo
