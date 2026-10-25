// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileRect.pas (whole)
//
// A tile-space rectangle bound to the IProjection it was computed against. A value type, not an
// interface: `ITileRect` has one implementation and no polymorphic user (docs/specs/geo.md §4.1).

#include "cg/geo/geo_types.h"
#include "cg/geo/projection.h"

#include <optional>

namespace cg::geo {

class TileRect {
public:
    TileRect(ProjectionPtr projection, RectI rect) : projection_(std::move(projection)), rect_(rect) {}

    [[nodiscard]] RectI rect() const noexcept { return rect_; }
    [[nodiscard]] quint8 zoom() const noexcept { return projection_->zoom(); }
    [[nodiscard]] const ProjectionPtr& projection() const noexcept { return projection_; }

    [[nodiscard]] bool isEqual(const RectI& r) const noexcept { return rect_ == r; }
    // u_TileRect.pas TTileRect.IsEqual(ITileRect): identity short-cut, then projection identity
    // (IProjection.IsSame), then rect equality. No hash short-cut in legacy.
    [[nodiscard]] bool isEqual(const TileRect& other) const {
        if (&other == this) {
            return true;
        }
        return projection_->isSame(*other.projection_) && rect_ == other.rect_;
    }
    [[nodiscard]] bool isPointInRect(PointI p) const noexcept {
        return p.x() >= rect_.left && p.x() < rect_.right && p.y() >= rect_.top && p.y() < rect_.bottom;
    }
    [[nodiscard]] RectI unionWith(const RectI& r) const noexcept;
    [[nodiscard]] std::optional<RectI> intersectWith(const RectI& r) const noexcept;
    [[nodiscard]] bool isIntersectWith(const RectI& r) const noexcept;

private:
    ProjectionPtr projection_;
    RectI rect_;
};

} // namespace cg::geo
