// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/appearance_of_mark_line.h"

namespace cg::vector {

AppearanceOfMarkLine::AppearanceOfMarkLine(core::HashValue hash, Color32 lineColor, int lineWidth)
    : hash_(hash), lineColor_(lineColor), lineWidth_(lineWidth) {}

core::HashValue AppearanceOfMarkLine::hash() const noexcept {
    return hash_;
}

bool AppearanceOfMarkLine::isEqual(const IAppearance* value) const {
    return value != nullptr && hash_ == value->hash();
}

Color32 AppearanceOfMarkLine::lineColor() const {
    return lineColor_;
}
int AppearanceOfMarkLine::lineWidth() const {
    return lineWidth_;
}

} // namespace cg::vector
