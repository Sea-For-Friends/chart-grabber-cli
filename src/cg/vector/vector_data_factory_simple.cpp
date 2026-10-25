// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/vector_data_factory_simple.h"

#include "cg/vector/vector_data_item_base.h"

#include <QtGlobal>

#include <stdexcept>
#include <utility>

namespace cg::vector {

// FEAT-GEO-037
VectorDataItemMainInfoFactory::VectorDataItemMainInfoFactory(HtmlToHintTextConverterPtr hintConverter)
    : hintConverter_(std::move(hintConverter)) {
    Q_ASSERT(hintConverter_ != nullptr);
}

VectorDataItemMainInfoPtr VectorDataItemMainInfoFactory::buildMainInfo(IdData* /*idData*/,
                                                                       const QString& name,
                                                                       const QString& desc) const {
    core::HashValue hash = core::hashOf(QStringView{name}); // empty name: 0 (nil buffer)
    core::hashUpdate(hash, QStringView{desc});              // empty desc leaves the hash unchanged
    return std::make_shared<VectorDataItemMainInfo>(hash, hintConverter_, name, desc);
}

// FEAT-GEO-037
VectorDataItemPtr VectorDataFactorySimple::buildItem(const VectorDataItemMainInfoPtr& mainInfo,
                                                     const AppearancePtr& appearance,
                                                     const geo::GeometryLonLatPtr& geometry) const {
    if (!geometry || !mainInfo) {
        throw std::invalid_argument("VectorDataFactorySimple::buildItem: null mainInfo or geometry");
    }
    core::HashValue hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    if (appearance) {
        core::hashUpdate(hash, appearance->hash());
    }
    return std::make_shared<VectorDataItem>(hash, appearance, mainInfo, geometry);
}

} // namespace cg::vector
