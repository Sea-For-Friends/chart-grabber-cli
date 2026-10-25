// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_VectorDataItemBase.pas
//
// FEAT-GEO-037.

#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/vector_data_item_simple.h"

namespace cg::vector {

class VectorDataItemMainInfo final : public IVectorDataItemMainInfo { // TVectorDataItemMainInfo
public:
    VectorDataItemMainInfo(core::HashValue hash, HtmlToHintTextConverterPtr hintConverter, QString name,
                           QString desc);

    [[nodiscard]] core::HashValue hash() const noexcept override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] QString desc() const override;
    [[nodiscard]] bool isEqual(const IVectorDataItemMainInfo* item) const override;
    [[nodiscard]] QString hintText() const override;
    [[nodiscard]] QString infoUrl() const override;
    [[nodiscard]] QString infoHtml() const override;
    [[nodiscard]] QString infoCaption() const override;

private:
    core::HashValue hash_;
    HtmlToHintTextConverterPtr hintConverter_;
    QString name_;
    QString desc_;
};

class VectorDataItem final : public IVectorDataItem { // TVectorDataItem
public:
    // Preconditions (legacy Assert): mainInfo and geometry non-null; appearance may be null.
    VectorDataItem(core::HashValue hash, AppearancePtr appearance, VectorDataItemMainInfoPtr mainInfo,
                   geo::GeometryLonLatPtr geometry);

    [[nodiscard]] core::HashValue hash() const noexcept override;
    [[nodiscard]] VectorDataItemMainInfoPtr mainInfo() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] QString desc() const override;
    [[nodiscard]] geo::GeometryLonLatPtr geometry() const override;
    [[nodiscard]] AppearancePtr appearance() const override;
    [[nodiscard]] bool isEqual(const IVectorDataItem* item) const override;
    [[nodiscard]] QString hintText() const override;
    [[nodiscard]] QString infoUrl() const override;
    [[nodiscard]] QString infoHtml() const override;
    [[nodiscard]] QString infoCaption() const override;

private:
    core::HashValue hash_;
    geo::GeometryLonLatPtr geometry_;
    VectorDataItemMainInfoPtr mainInfo_;
    AppearancePtr appearance_;
};

} // namespace cg::vector
