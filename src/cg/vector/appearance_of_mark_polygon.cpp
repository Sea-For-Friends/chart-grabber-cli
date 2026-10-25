// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/appearance_of_mark_polygon.h"

namespace cg::vector {

AppearanceOfMarkPolygon::AppearanceOfMarkPolygon(core::HashValue hashCommon, core::HashValue hashBorder,
                                                 core::HashValue hashFill, Color32 lineColor, int lineWidth,
                                                 Color32 fillColor)
    : hashCommon_(hashCommon), hashBorder_(hashBorder), hashFill_(hashFill), lineColor_(lineColor),
      lineWidth_(lineWidth), fillColor_(fillColor) {}

core::HashValue AppearanceOfMarkPolygon::hash() const noexcept {
    return hashCommon_;
}

bool AppearanceOfMarkPolygon::isEqual(const IAppearance* value) const {
    return value != nullptr && hashCommon_ == value->hash();
}

core::HashValue AppearanceOfMarkPolygon::hashBorder() const noexcept {
    return hashBorder_;
}
Color32 AppearanceOfMarkPolygon::lineColor() const {
    return lineColor_;
}
int AppearanceOfMarkPolygon::lineWidth() const {
    return lineWidth_;
}
core::HashValue AppearanceOfMarkPolygon::hashFill() const noexcept {
    return hashFill_;
}
Color32 AppearanceOfMarkPolygon::fillColor() const {
    return fillColor_;
}

} // namespace cg::vector
