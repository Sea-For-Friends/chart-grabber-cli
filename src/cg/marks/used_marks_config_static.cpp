// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/used_marks_config_static.h"

// Ports u_UsedMarksConfigStatic.pas (T-0147): constructor :41-59, getters :61-74.

namespace cg::marks {

UsedMarksConfigStatic::UsedMarksConfigStatic(bool isUseMarks, bool ignoreMarksVisible,
                                             bool ignoreCategoriesVisible)
    : isUseMarks_(isUseMarks), ignoreMarksVisible_(ignoreMarksVisible),
      ignoreCategoriesVisible_(ignoreCategoriesVisible) {}

UsedMarksConfigStatic::~UsedMarksConfigStatic() = default;

bool UsedMarksConfigStatic::isUseMarks() const {
    return isUseMarks_;
}

bool UsedMarksConfigStatic::ignoreCategoriesVisible() const {
    return ignoreCategoriesVisible_;
}

bool UsedMarksConfigStatic::ignoreMarksVisible() const {
    return ignoreMarksVisible_;
}

} // namespace cg::marks
