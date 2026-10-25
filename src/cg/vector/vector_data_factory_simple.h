// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_VectorDataFactorySimple.pas
//
// FEAT-GEO-037.

#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/vector_data_factory.h"

namespace cg::vector {

class VectorDataItemMainInfoFactory final
    : public IVectorDataItemMainInfoFactory { // TVectorDataItemMainInfoFactory
public:
    explicit VectorDataItemMainInfoFactory(HtmlToHintTextConverterPtr hintConverter); // non-null

    // idData is ignored.
    [[nodiscard]] VectorDataItemMainInfoPtr buildMainInfo(IdData* idData, const QString& name,
                                                          const QString& desc) const override;

private:
    HtmlToHintTextConverterPtr hintConverter_;
};

class VectorDataFactorySimple final : public IVectorDataFactory { // TVectorDataFactorySimple
public:
    // Null mainInfo or geometry is a programmer error: std::invalid_argument in every build (D-52).
    [[nodiscard]] VectorDataItemPtr buildItem(const VectorDataItemMainInfoPtr& mainInfo,
                                              const AppearancePtr& appearance,
                                              const geo::GeometryLonLatPtr& geometry) const override;
};

} // namespace cg::vector
