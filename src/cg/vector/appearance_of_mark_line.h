// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_AppearanceOfMarkLine.pas
//
// FEAT-GEO-037 (enabler). The line has no facet hash: its hash() is the IAppearance one.

#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_vector_item.h"

namespace cg::vector {

class AppearanceOfMarkLine final : public IAppearance, public IAppearanceLine {
public:
    AppearanceOfMarkLine(core::HashValue hash, Color32 lineColor, int lineWidth);

    [[nodiscard]] core::HashValue hash() const noexcept override;
    [[nodiscard]] bool isEqual(const IAppearance* value) const override; // hash only (L-126)

    [[nodiscard]] Color32 lineColor() const override;
    [[nodiscard]] int lineWidth() const override;

private:
    core::HashValue hash_;
    Color32 lineColor_;
    int lineWidth_;
};

} // namespace cg::vector
