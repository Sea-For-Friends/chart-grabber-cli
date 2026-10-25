// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorDataFactory.pas
//
// FEAT-GEO-037.

#include "cg/geo/geometry_lonlat.h"
#include "cg/vector/appearance.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>

#include <memory>

namespace cg::vector {

struct IdData; // legacy "AIdData: Pointer"; defined with the map factory (vector_data_factory_for_map.h)

class IVectorDataItemMainInfoFactory {
public:
    virtual ~IVectorDataItemMainInfoFactory() = default;
    [[nodiscard]] virtual VectorDataItemMainInfoPtr buildMainInfo(IdData* idData, const QString& name,
                                                                  const QString& desc) const = 0;
};
using VectorDataItemMainInfoFactoryPtr = std::shared_ptr<const IVectorDataItemMainInfoFactory>;

class IVectorDataFactory {
public:
    virtual ~IVectorDataFactory() = default;
    [[nodiscard]] virtual VectorDataItemPtr buildItem(const VectorDataItemMainInfoPtr& mainInfo,
                                                      const AppearancePtr& appearance,
                                                      const geo::GeometryLonLatPtr& geometry) const = 0;
};
using VectorDataFactoryPtr = std::shared_ptr<const IVectorDataFactory>;

} // namespace cg::vector
