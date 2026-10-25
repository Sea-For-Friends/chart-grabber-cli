// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_AppearanceOfMarkFactory.pas, u_AppearanceOfMarkFactory.pas
//
// FEAT-GEO-037. Builds the three mark appearances and computes their hashes; the hash recipes are
// byte-exact with legacy because the hashes order and select survivors in duplicate removal.
// No interning cache (spec vector.md §2.3.4, DV-1): every call creates a new object.

#include "cg/vector/appearance.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/mark_picture.h"

#include <QString>

#include <memory>

namespace cg::vector {

class IAppearanceOfMarkFactory {
public:
    virtual ~IAppearanceOfMarkFactory() = default;
    [[nodiscard]] virtual AppearancePtr createPointAppearance(Color32 textColor, Color32 textBgColor,
                                                              int fontSize, const QString& picName,
                                                              const MarkPicturePtr& pic,
                                                              int markerSize) const = 0;
    [[nodiscard]] virtual AppearancePtr createLineAppearance(Color32 lineColor, int lineWidth) const = 0;
    [[nodiscard]] virtual AppearancePtr createPolygonAppearance(Color32 lineColor, int lineWidth,
                                                                Color32 fillColor) const = 0;
};
using AppearanceOfMarkFactoryPtr = std::shared_ptr<const IAppearanceOfMarkFactory>;

class AppearanceOfMarkFactory final : public IAppearanceOfMarkFactory {
public:
    AppearanceOfMarkFactory() = default;

    // FEAT-GEO-037
    [[nodiscard]] AppearancePtr createPointAppearance(Color32 textColor, Color32 textBgColor, int fontSize,
                                                      const QString& picName, const MarkPicturePtr& pic,
                                                      int markerSize) const override;
    [[nodiscard]] AppearancePtr createLineAppearance(Color32 lineColor, int lineWidth) const override;
    [[nodiscard]] AppearancePtr createPolygonAppearance(Color32 lineColor, int lineWidth,
                                                        Color32 fillColor) const override;
};

} // namespace cg::vector
