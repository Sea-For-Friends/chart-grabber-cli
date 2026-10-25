// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorDataItemSimple.pas
//
// FEAT-GEO-037. A vector item is a geometry plus main info (name, description) plus an optional
// appearance; point / line / polygon is decided by the dynamic type of geometry().

#include "cg/core/hash.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/vector/appearance.h"
#include "cg/vector/category.h"

#include <QString>

#include <memory>

namespace cg::vector {

class IVectorDataItemMainInfo {
public:
    virtual ~IVectorDataItemMainInfo() = default;
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual QString desc() const = 0;
    [[nodiscard]] virtual bool isEqual(const IVectorDataItemMainInfo* item) const = 0;
    [[nodiscard]] virtual QString hintText() const = 0;
    [[nodiscard]] virtual QString infoUrl() const = 0;
    [[nodiscard]] virtual QString infoHtml() const = 0;
    [[nodiscard]] virtual QString infoCaption() const = 0;
};
using VectorDataItemMainInfoPtr = std::shared_ptr<const IVectorDataItemMainInfo>;

class IVectorDataItem {
public:
    virtual ~IVectorDataItem() = default;
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual VectorDataItemMainInfoPtr mainInfo() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual QString desc() const = 0;
    [[nodiscard]] virtual geo::GeometryLonLatPtr geometry() const = 0;
    [[nodiscard]] virtual AppearancePtr appearance() const = 0; // may be null
    [[nodiscard]] virtual bool isEqual(const IVectorDataItem* item) const = 0;
    [[nodiscard]] virtual QString hintText() const = 0;
    [[nodiscard]] virtual QString infoUrl() const = 0;
    [[nodiscard]] virtual QString infoHtml() const = 0;
    [[nodiscard]] virtual QString infoCaption() const = 0;
};
using VectorDataItemPtr = std::shared_ptr<const IVectorDataItem>;

// A separate interface, as in legacy (no common base).
class IVectorDataItemWithCategory {
public:
    virtual ~IVectorDataItemWithCategory() = default;
    [[nodiscard]] virtual CategoryPtr category() const = 0;
};

} // namespace cg::vector
