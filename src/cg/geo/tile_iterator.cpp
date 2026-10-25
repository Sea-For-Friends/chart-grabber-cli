// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/tile_iterator.h"

#include "cg/geo/tile_count.h"

#include <QtGlobal>

#include <utility>

namespace cg::geo {
namespace {

qint64 calcRectTilesTotal(const RectI& r) noexcept {
    if (r.isEmpty()) {
        return 0;
    }
    qint64 total = r.width();
    total *= r.height();
    return total;
}

// Types.pas CenterPoint(ARect) = (ARect.Right - ARect.Left) div 2 + ARect.Left (and the same for
// Y): NOT (Left+Right)/2 -- the two differ for a negative odd sum (slice-A review finding 1).
PointI centerOf(const RectI& r) noexcept {
    return {(r.right - r.left) / 2 + r.left, (r.bottom - r.top) / 2 + r.top};
}

} // namespace

// --- TileIteratorByRect ---------------------------------------------------------------------

TileIteratorByRect::TileIteratorByRect(TileRect rect, qint64 tilesToProcess, std::optional<PointI> startPoint)
    : rect_(std::move(rect)), tilesTotal_(calcRectTilesTotal(rect_.rect())),
      startPoint_(startPoint.value_or(rect_.rect().topLeft())) {
    if (tilesTotal_ > 0 && tilesToProcess != -1) {
        Q_ASSERT(tilesToProcess <= tilesTotal_);
        tilesTotal_ = tilesToProcess;
    }
    reset();
}

bool TileIteratorByRect::next(PointI& tile) {
    if (eoi_) {
        return false;
    }
    tile = current_;
    const RectI r = rect_.rect();
    current_.rx()++;
    if (current_.x() >= r.right) {
        current_.rx() = r.left;
        current_.ry()++;
        if (current_.y() >= r.bottom) {
            eoi_ = true;
        }
    }
    ++processedCount_;
    eoi_ = eoi_ || (processedCount_ >= tilesTotal_);
    return true;
}

void TileIteratorByRect::reset() {
    processedCount_ = 0;
    eoi_ = rect_.rect().isEmpty() || tilesTotal_ <= 0;
    if (!eoi_) {
        current_ = startPoint_;
    }
}

// --- TileIteratorSpiralByRect ----------------------------------------------------------------

TileIteratorSpiralByRect::TileIteratorSpiralByRect(const TileRect& rect)
    : TileIteratorSpiralByRect(rect, centerOf(rect.rect())) {}

TileIteratorSpiralByRect::TileIteratorSpiralByRect(TileRect rect, PointI centre)
    : rect_(std::move(rect)), tilesTotal_(calcRectTilesTotal(rect_.rect())), centre_(centre),
      maxRadius_(getMaxRing(centre_, rect_.rect())) {
    reset();
}

bool TileIteratorSpiralByRect::checkPoint(PointI p) const noexcept {
    const RectI r = rect_.rect();
    return p.x() >= r.left && p.y() >= r.top && p.x() < r.right && p.y() < r.bottom;
}

int TileIteratorSpiralByRect::getMaxRing(PointI centre, const RectI& rect) noexcept {
    int result = centre.x() - rect.left;
    int rad = rect.right - centre.x();
    if (rad > result) {
        result = rad;
    }
    rad = centre.y() - rect.top;
    if (rad > result) {
        result = rad;
    }
    rad = rect.bottom - centre.y();
    if (rad > result) {
        result = rad;
    }
    return result;
}

int TileIteratorSpiralByRect::getTilesInRingCount(int ring) noexcept {
    if (ring == 0) {
        return 1;
    }
    if (ring > 0) {
        return (ring * 2) * 4;
    }
    return 0;
}

// GetDeltaByRingAndIndex, u_TileIteratorSpiralByRect.pas:96-143. The ring is split into eight
// segments of exactly `ring` tiles each; the +-1 offsets and the eight-way split (rather than a
// naive four-sided walk with sides of 2*ring) are load-bearing for the exact iteration order.
PointI TileIteratorSpiralByRect::getDeltaByRingAndIndex(int ring, int index) noexcept {
    if (ring <= 0) {
        return {0, 0};
    }
    const int tilesInLine = ring;
    const int lineIndex = index / tilesInLine;
    const int indexInLine = index - lineIndex * tilesInLine;
    switch (lineIndex) {
    case 0:
        return {1 + indexInLine, -ring};
    case 1:
        return {ring, 1 - ring + indexInLine};
    case 2:
        return {ring, 1 + indexInLine};
    case 3:
        return {ring - 1 - indexInLine, ring};
    case 4:
        return {-1 - indexInLine, ring};
    case 5:
        return {-ring, ring - 1 - indexInLine};
    case 6:
        return {-ring, -1 - indexInLine};
    case 7:
        return {1 - ring + indexInLine, -ring};
    default:
        return {0, 0};
    }
}

bool TileIteratorSpiralByRect::next(PointI& tile) {
    bool result = false;
    while (!eoi_ && !result) {
        const PointI delta = getDeltaByRingAndIndex(currentRing_, indexInRing_);
        tile = {centre_.x() + delta.x(), centre_.y() + delta.y()};
        ++indexInRing_;
        if (indexInRing_ >= getTilesInRingCount(currentRing_)) {
            ++currentRing_;
            indexInRing_ = 0;
            if (currentRing_ > maxRadius_) {
                eoi_ = true;
            }
        }
        result = checkPoint(tile);
    }
    return result;
}

void TileIteratorSpiralByRect::reset() {
    eoi_ = rect_.rect().isEmpty();
    currentRing_ = 0;
    indexInRing_ = 0;
}

// --- TileIteratorByPolygon -------------------------------------------------------------------

namespace {

RectI computeTilesRectRaw(const ProjectionPtr& projection,
                          const std::shared_ptr<const IGeometryProjectedPolygon>& polygon) {
    if (!projection || !polygon) {
        return {};
    }
    return roundRect(projection->pixelRectFloat2TileRectFloat(polygon->bounds()), RectRounding::Outside);
}

} // namespace

TileIteratorByPolygon::TileIteratorByPolygon(ProjectionPtr projection,
                                             const std::shared_ptr<const IGeometryProjectedPolygon>& polygon,
                                             qint64 tilesToProcess, std::optional<PointI> startPoint)
    : projection_(std::move(projection)), polygon_(polygon),
      rect_(projection_, computeTilesRectRaw(projection_, polygon)),
      startPoint_(startPoint.value_or(rect_.rect().topLeft())), tilesTotal_(tilesToProcess),
      stopOnProcessedTilesCount_(tilesToProcess != -1) {
    if (!polygon_) {
        // u_TileIteratorByPolygon.pas:95-97: Assert(False); MakeEmptySetup -- but
        // SASPlanet.dproj's Base property group (shared by every configuration, including Debug)
        // sets DCC_AssertionsAtRuntime=False, so Assert() never raises in any legacy build; legacy
        // always falls through to MakeEmptySetup for a null projected polygon. This IS a reachable,
        // legitimate case here too: T-0018B review finding 1, TileIteratorFactory::makeTileIterator
        // passes a null projected polygon straight through when nothing survives projection
        // (u_TileIteratorFactory.pas:91's Supports(nil) == False fallthrough). Q_ASSERT would be
        // fatal in this build (no NDEBUG), unlike legacy's compiled-out Assert, so it is omitted
        // here to match legacy's actual (assertions-disabled) runtime behavior: empty setup, no
        // crash.
        makeEmptySetup();
        return;
    }
    if (auto single = std::dynamic_pointer_cast<const IGeometryProjectedSinglePolygon>(polygon_)) {
        singleLine_ = std::move(single);
    } else if (auto multi = std::dynamic_pointer_cast<const IGeometryProjectedMultiPolygon>(polygon_)) {
        multiProjected_ = multi;
        if (multi->count() == 1) {
            singleLine_ = multi->item(0);
        }
    } else {
        // legacy: Assert(False); MakeEmptySetup; Exit -- neither concrete hierarchy implements
        // this branch, so it is unreachable in practice.
        Q_ASSERT(false);
        makeEmptySetup();
        return;
    }
    reset();
}

void TileIteratorByPolygon::makeEmptySetup() {
    // u_TileIteratorByPolygon.pas:235-244: also resets FTilesRect/FStartPoint to zero, so the two
    // unreachable-in-practice "neither single nor multi" / "null polygon" branches above cannot
    // leave rect_ holding the polygon's (possibly non-empty) bounds with a null multiProjected_ --
    // slice-A review finding 2.
    tilesTotal_ = 0;
    rect_ = TileRect(projection_, RectI{});
    startPoint_ = PointI{0, 0};
    reset();
}

bool TileIteratorByPolygon::internalIntersectPolygon(const DoubleRect& r) {
    if (singleLine_) {
        return singleLine_->isRectIntersectPolygon(r);
    }
    if (lastUsedLine_ && lastUsedLine_->isRectIntersectPolygon(r)) {
        return true;
    }
    for (std::size_t i = 0; i < multiProjected_->count(); ++i) {
        const auto& line = multiProjected_->item(i);
        if (line.get() != lastUsedLine_.get() && line->isRectIntersectPolygon(r)) {
            lastUsedLine_ = line;
            return true;
        }
    }
    return false;
}

bool TileIteratorByPolygon::next(PointI& tile) {
    if (stopOnProcessedTilesCount_ && processedTilesCount_ >= tilesTotal_) {
        return false;
    }
    const RectI r = rect_.rect();
    bool result = false;
    while (current_.x() < r.right) {
        while (current_.y() < r.bottom) {
            const DoubleRect pixelRect = projection_->tilePos2PixelRectFloat(current_);
            if (internalIntersectPolygon(pixelRect)) {
                tile = current_;
                result = true;
            }
            current_.ry()++;
            if (result) {
                break;
            }
        }
        if (result) {
            break;
        }
        if (current_.y() >= r.bottom) {
            current_.ry() = r.top;
            current_.rx()++;
        }
    }
    if (result) {
        ++processedTilesCount_;
    }
    return result;
}

void TileIteratorByPolygon::reset() {
    lastUsedLine_.reset();
    processedTilesCount_ = 0;
    current_ = startPoint_;
}

qint64 TileIteratorByPolygon::tilesTotal() {
    // u_TileIteratorByPolygon.pas:144-150 GetTilesTotal: lazy, memoised, delegates to
    // CalcTileCountInProjectedPolygon -- a different algorithm from next() (recursive bisection,
    // not a per-tile scan), so this may legitimately differ from the count next() yields (slice-A
    // handoff open question / slice-A review "For slice B"; docs/parity/deviations.md D-5).
    if (tilesTotal_ < 0) {
        tilesTotal_ = calcTileCountInProjectedPolygon(*projection_, *polygon_);
    }
    return tilesTotal_;
}

} // namespace cg::geo
