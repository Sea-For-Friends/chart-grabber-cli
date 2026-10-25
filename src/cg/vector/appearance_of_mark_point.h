// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_AppearanceOfMarkPoint.pas
//
// FEAT-GEO-037 (enabler). Immutable; hashes are given by the factory, nothing is computed here.

#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_vector_item.h"

namespace cg::vector {

class AppearanceOfMarkPoint final : public IAppearance,
                                    public IAppearancePointCaption,
                                    public IAppearancePointIcon {
public:
    // A non-null `pic` replaces `picName` by the picture's name (legacy constructor).
    AppearanceOfMarkPoint(core::HashValue hashCommon, core::HashValue hashCaption, core::HashValue hashIcon,
                          Color32 textColor, Color32 textBgColor, int fontSize, int markerSize,
                          QString picName, MarkPicturePtr pic);

    [[nodiscard]] core::HashValue hash() const noexcept override;        // GetHashCommon
    [[nodiscard]] bool isEqual(const IAppearance* value) const override; // hash only (L-126)

    [[nodiscard]] core::HashValue hashCaption() const noexcept override;
    [[nodiscard]] Color32 textColor() const override;
    [[nodiscard]] Color32 textBgColor() const override;
    [[nodiscard]] int fontSize() const override;

    [[nodiscard]] core::HashValue hashIcon() const noexcept override;
    [[nodiscard]] int markerSize() const override;
    [[nodiscard]] QString picName() const override;
    [[nodiscard]] MarkPicturePtr pic() const override;

private:
    core::HashValue hashCommon_;
    core::HashValue hashCaption_;
    core::HashValue hashIcon_;
    Color32 textColor_;
    Color32 textBgColor_;
    int fontSize_;
    int markerSize_;
    QString picName_;
    MarkPicturePtr pic_;
};

} // namespace cg::vector
