// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ConvertProjectionToProjection.pas, i_ConvertProjectionToProjectionFactory.pas
//
// These two legacy units are interfaces with no implementation anywhere in the legacy tree (no
// u_ConvertProjectionToProjection*.pas exists, and their only consumers receive the factory by
// injection from a provider that does not exist either -- docs/specs/geo.md §2.8). The behavior
// they describe (reproject a tile position/rect from one pyramid to another) IS needed by
// `render`, so it is supplied here as a value type instead: no factory (constructing one is two
// shared_ptr copies -- `idioms.md` §13 rules out a factory with one implementation and one user).

#include "cg/geo/geo_types.h"
#include "cg/geo/projection.h"

#include <memory>

namespace cg::geo {

class ConvertProjectionToProjection {
public:
    ConvertProjectionToProjection(ProjectionPtr source, ProjectionPtr target);

    [[nodiscard]] const ProjectionPtr& source() const noexcept { return source_; }
    [[nodiscard]] const ProjectionPtr& target() const noexcept { return target_; }

    // source -> target
    [[nodiscard]] DoublePoint forwardPixelPos(DoublePoint p) const;
    [[nodiscard]] DoublePoint forwardPixelPos(PointI p) const;
    [[nodiscard]] DoubleRect forwardPixelRect(const DoubleRect& r) const;
    [[nodiscard]] DoubleRect forwardPixelRect(const RectI& r) const;
    [[nodiscard]] DoublePoint forwardTilePos(DoublePoint p) const;
    [[nodiscard]] DoublePoint forwardTilePos(PointI p) const;
    [[nodiscard]] DoubleRect forwardTilePosToTileRect(PointI p) const;
    [[nodiscard]] DoubleRect forwardTilePosToPixelRect(PointI p) const; // legacy typo "PixeRect"
    [[nodiscard]] DoubleRect forwardTileRect(const DoubleRect& r) const;
    [[nodiscard]] DoubleRect forwardTileRect(const RectI& r) const;

    // target -> source: the same ten, named backward*
    [[nodiscard]] DoublePoint backwardPixelPos(DoublePoint p) const;
    [[nodiscard]] DoublePoint backwardPixelPos(PointI p) const;
    [[nodiscard]] DoubleRect backwardPixelRect(const DoubleRect& r) const;
    [[nodiscard]] DoubleRect backwardPixelRect(const RectI& r) const;
    [[nodiscard]] DoublePoint backwardTilePos(DoublePoint p) const;
    [[nodiscard]] DoublePoint backwardTilePos(PointI p) const;
    [[nodiscard]] DoubleRect backwardTilePosToTileRect(PointI p) const;
    [[nodiscard]] DoubleRect backwardTilePosToPixelRect(PointI p) const;
    [[nodiscard]] DoubleRect backwardTileRect(const DoubleRect& r) const;
    [[nodiscard]] DoubleRect backwardTileRect(const RectI& r) const;

private:
    // Route: source->pixelPosFloat2LonLat()->target->lonLat2PixelPosFloat() when the two
    // projection *types* differ, and the cheaper relative-space route
    // (pixelPosFloat2Relative -> relative2PixelPosFloat) when the types are isSame() -- the same
    // shortcut LocalCoordConverterFactory::changeProjectionWithFreezeAtCenter takes
    // (u_LocalCoordConverterFactorySimpe.pas:241-280).
    [[nodiscard]] DoublePoint route(const ProjectionPtr& from, const ProjectionPtr& to,
                                    DoublePoint fromPixel) const;

    // The forward*/backward* pairs are the same ten operations with (from, to) swapped
    // (T-0016 review finding 14); these four take the direction as parameters so forward*/backward*
    // are one-line wrappers instead of ten duplicated bodies.
    [[nodiscard]] DoubleRect pixelRectVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                          const DoubleRect& r) const;
    [[nodiscard]] DoublePoint tilePosVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                         DoublePoint p) const;
    [[nodiscard]] DoubleRect tilePosToPixelRectVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                                   PointI p) const;
    [[nodiscard]] DoubleRect tileRectVia(const ProjectionPtr& from, const ProjectionPtr& to,
                                         const DoubleRect& r) const;

    ProjectionPtr source_;
    ProjectionPtr target_;
    bool sameType_;
};

} // namespace cg::geo
