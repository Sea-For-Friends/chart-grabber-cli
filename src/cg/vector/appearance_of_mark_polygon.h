// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_AppearanceOfMarkPolygon.pas
//
// FEAT-GEO-037 (enabler). Hashes are given by the factory, nothing is computed here.

#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_vector_item.h"

namespace cg::vector {

class AppearanceOfMarkPolygon final : public IAppearance,
                                      public IAppearancePolygonBorder,
                                      public IAppearancePolygonFill {
public:
    AppearanceOfMarkPolygon(core::HashValue hashCommon, core::HashValue hashBorder, core::HashValue hashFill,
                            Color32 lineColor, int lineWidth, Color32 fillColor);

    [[nodiscard]] core::HashValue hash() const noexcept override;        // GetHashCommon
    [[nodiscard]] bool isEqual(const IAppearance* value) const override; // hash only (L-126)

    [[nodiscard]] core::HashValue hashBorder() const noexcept override;
    [[nodiscard]] Color32 lineColor() const override;
    [[nodiscard]] int lineWidth() const override;

    [[nodiscard]] core::HashValue hashFill() const noexcept override;
    [[nodiscard]] Color32 fillColor() const override;

private:
    core::HashValue hashCommon_;
    core::HashValue hashBorder_;
    core::HashValue hashFill_;
    Color32 lineColor_;
    int lineWidth_;
    Color32 fillColor_;
};

} // namespace cg::vector
