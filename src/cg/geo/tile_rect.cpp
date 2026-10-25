// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/tile_rect.h"

#include <algorithm>

namespace cg::geo {

// u_TileRect.pas wraps the Win32 GDI rect functions (IntersectRect/UnionRect); reproduced here
// with their exact edge cases: an empty operand does not contribute to a union, and no overlap
// yields no result at all rather than a degenerate rect.

RectI TileRect::unionWith(const RectI& r) const noexcept {
    if (rect_.isEmpty())
        return r;
    if (r.isEmpty())
        return rect_;
    return {std::min(rect_.left, r.left), std::min(rect_.top, r.top), std::max(rect_.right, r.right),
            std::max(rect_.bottom, r.bottom)};
}

std::optional<RectI> TileRect::intersectWith(const RectI& r) const noexcept {
    const RectI result{std::max(rect_.left, r.left), std::max(rect_.top, r.top),
                       std::min(rect_.right, r.right), std::min(rect_.bottom, r.bottom)};
    if (result.isEmpty()) {
        return std::nullopt;
    }
    return result;
}

bool TileRect::isIntersectWith(const RectI& r) const noexcept {
    return intersectWith(r).has_value();
}

} // namespace cg::geo
