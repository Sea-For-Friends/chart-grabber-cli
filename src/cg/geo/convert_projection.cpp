// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/convert_projection.h"

#include <utility>

namespace cg::geo {

ConvertProjectionToProjection::ConvertProjectionToProjection(ProjectionPtr source, ProjectionPtr target)
    : source_(std::move(source)), target_(std::move(target)),
      sameType_(source_->projectionType()->isSame(*target_->projectionType())) {}

DoublePoint ConvertProjectionToProjection::route(const ProjectionPtr& from, const ProjectionPtr& to,
                                                 DoublePoint fromPixel) const {
    if (sameType_) {
        return to->relative2PixelPosFloat(from->pixelPosFloat2Relative(fromPixel));
    }
    return to->lonLat2PixelPosFloat(from->pixelPosFloat2LonLat(fromPixel));
}

DoubleRect ConvertProjectionToProjection::pixelRectVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                                       const DoubleRect& r) const {
    const DoublePoint tl = route(from, to, r.topLeft());
    const DoublePoint br = route(from, to, r.bottomRight());
    return {tl.x(), tl.y(), br.x(), br.y()};
}

DoublePoint ConvertProjectionToProjection::tilePosVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                                      DoublePoint p) const {
    const DoublePoint fromPixel = from->tilePosFloat2PixelPosFloat(p);
    const DoublePoint toPixel = route(from, to, fromPixel);
    return to->pixelPosFloat2TilePosFloat(toPixel);
}

DoubleRect ConvertProjectionToProjection::tilePosToPixelRectVia(const ProjectionPtr& from,
                                                                const ProjectionPtr& to, PointI p) const {
    return pixelRectVia(from, to, from->tilePos2PixelRectFloat(p));
}

DoubleRect ConvertProjectionToProjection::tileRectVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                                      const DoubleRect& r) const {
    const DoubleRect fromPixelRect = from->tileRectFloat2PixelRectFloat(r);
    const DoubleRect toPixelRect = pixelRectVia(from, to, fromPixelRect);
    return to->pixelRectFloat2TileRectFloat(toPixelRect);
}

// --- forward: source -> target -----------------------------------------------------------------

DoublePoint ConvertProjectionToProjection::forwardPixelPos(DoublePoint p) const {
    return route(source_, target_, p);
}
DoublePoint ConvertProjectionToProjection::forwardPixelPos(PointI p) const {
    return forwardPixelPos(DoublePoint(p));
}

DoubleRect ConvertProjectionToProjection::forwardPixelRect(const DoubleRect& r) const {
    return pixelRectVia(source_, target_, r);
}
DoubleRect ConvertProjectionToProjection::forwardPixelRect(const RectI& r) const {
    return forwardPixelRect(toDoubleRect(r));
}

DoublePoint ConvertProjectionToProjection::forwardTilePos(DoublePoint p) const {
    return tilePosVia(source_, target_, p);
}
DoublePoint ConvertProjectionToProjection::forwardTilePos(PointI p) const {
    return forwardTilePos(DoublePoint(p));
}

DoubleRect ConvertProjectionToProjection::forwardTilePosToPixelRect(PointI p) const {
    return tilePosToPixelRectVia(source_, target_, p);
}
DoubleRect ConvertProjectionToProjection::forwardTilePosToTileRect(PointI p) const {
    return target_->pixelRectFloat2TileRectFloat(forwardTilePosToPixelRect(p));
}

DoubleRect ConvertProjectionToProjection::forwardTileRect(const DoubleRect& r) const {
    return tileRectVia(source_, target_, r);
}
DoubleRect ConvertProjectionToProjection::forwardTileRect(const RectI& r) const {
    return forwardTileRect(toDoubleRect(r));
}

// --- backward: target -> source (the same ten, source/target swapped) --------------------------

DoublePoint ConvertProjectionToProjection::backwardPixelPos(DoublePoint p) const {
    return route(target_, source_, p);
}
DoublePoint ConvertProjectionToProjection::backwardPixelPos(PointI p) const {
    return backwardPixelPos(DoublePoint(p));
}

DoubleRect ConvertProjectionToProjection::backwardPixelRect(const DoubleRect& r) const {
    return pixelRectVia(target_, source_, r);
}
DoubleRect ConvertProjectionToProjection::backwardPixelRect(const RectI& r) const {
    return backwardPixelRect(toDoubleRect(r));
}

DoublePoint ConvertProjectionToProjection::backwardTilePos(DoublePoint p) const {
    return tilePosVia(target_, source_, p);
}
DoublePoint ConvertProjectionToProjection::backwardTilePos(PointI p) const {
    return backwardTilePos(DoublePoint(p));
}

DoubleRect ConvertProjectionToProjection::backwardTilePosToPixelRect(PointI p) const {
    return tilePosToPixelRectVia(target_, source_, p);
}
DoubleRect ConvertProjectionToProjection::backwardTilePosToTileRect(PointI p) const {
    return source_->pixelRectFloat2TileRectFloat(backwardTilePosToPixelRect(p));
}

DoubleRect ConvertProjectionToProjection::backwardTileRect(const DoubleRect& r) const {
    return tileRectVia(target_, source_, r);
}
DoubleRect ConvertProjectionToProjection::backwardTileRect(const RectI& r) const {
    return backwardTileRect(toDoubleRect(r));
}

} // namespace cg::geo
