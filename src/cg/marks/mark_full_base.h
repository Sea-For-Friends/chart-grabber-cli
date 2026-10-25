// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkFullBase.pas
//
// TMarkMainInfo: the main info of a mark; unlike the plain vector main info it carries the category
// (IVectorDataItemWithCategory) and makes its hint text from name and description in one call.

#include "cg/vector/category.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>

namespace cg::marks {

class MarkMainInfo final : public vector::IVectorDataItemMainInfo,
                           public vector::IVectorDataItemWithCategory {
public:
    // Precondition (legacy Assert): hintConverter non-null. category may be null.
    MarkMainInfo(core::HashValue hash, vector::HtmlToHintTextConverterPtr hintConverter, QString name,
                 vector::CategoryPtr category, QString desc);

    [[nodiscard]] core::HashValue hash() const noexcept override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] QString desc() const override;
    [[nodiscard]] bool isEqual(const vector::IVectorDataItemMainInfo* item) const override;
    [[nodiscard]] QString hintText() const override;
    [[nodiscard]] QString infoUrl() const override;
    [[nodiscard]] QString infoHtml() const override;
    [[nodiscard]] QString infoCaption() const override;
    [[nodiscard]] vector::CategoryPtr category() const override;

private:
    core::HashValue hash_;
    vector::HtmlToHintTextConverterPtr hintConverter_;
    QString name_;
    QString desc_;
    vector::CategoryPtr category_;
};

} // namespace cg::marks
