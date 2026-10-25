// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_AppearanceOfVectorItem.pas
//
// The one structural difference from legacy (Tech Lead ruling Q-4, spec vector.md §2.3.2, §5.1
// note): in Delphi the five facets inherit IAppearance and the implementing classes return a
// different GetHash through each interface. C++ cannot, so the facets do not derive from
// IAppearance and the facet hash has the name of the legacy private getter. For a line the
// common hash is the facet hash: cross-cast to IAppearance. LV-2 (L-127) applies to IsEqual.

#include "cg/core/color32.h"
#include "cg/core/hash.h"
#include "cg/vector/mark_picture.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::vector {

using core::Color32;

class IAppearancePointCaption {
public:
    virtual ~IAppearancePointCaption() = default;
    [[nodiscard]] virtual core::HashValue hashCaption() const noexcept = 0;
    [[nodiscard]] virtual Color32 textColor() const = 0;
    [[nodiscard]] virtual Color32 textBgColor() const = 0;
    [[nodiscard]] virtual int fontSize() const = 0;
};
using AppearancePointCaptionPtr = std::shared_ptr<const IAppearancePointCaption>;

class IAppearancePointIcon {
public:
    virtual ~IAppearancePointIcon() = default;
    [[nodiscard]] virtual core::HashValue hashIcon() const noexcept = 0;
    [[nodiscard]] virtual int markerSize() const = 0;
    [[nodiscard]] virtual QString picName() const = 0;
    [[nodiscard]] virtual MarkPicturePtr pic() const = 0;
};
using AppearancePointIconPtr = std::shared_ptr<const IAppearancePointIcon>;

class IAppearanceLine {
public:
    virtual ~IAppearanceLine() = default;
    [[nodiscard]] virtual Color32 lineColor() const = 0;
    [[nodiscard]] virtual int lineWidth() const = 0;
};
using AppearanceLinePtr = std::shared_ptr<const IAppearanceLine>;

class IAppearancePolygonBorder {
public:
    virtual ~IAppearancePolygonBorder() = default;
    [[nodiscard]] virtual core::HashValue hashBorder() const noexcept = 0;
    [[nodiscard]] virtual Color32 lineColor() const = 0;
    [[nodiscard]] virtual int lineWidth() const = 0;
};
using AppearancePolygonBorderPtr = std::shared_ptr<const IAppearancePolygonBorder>;

class IAppearancePolygonFill {
public:
    virtual ~IAppearancePolygonFill() = default;
    [[nodiscard]] virtual core::HashValue hashFill() const noexcept = 0;
    [[nodiscard]] virtual Color32 fillColor() const = 0;
};
using AppearancePolygonFillPtr = std::shared_ptr<const IAppearancePolygonFill>;

} // namespace cg::vector
