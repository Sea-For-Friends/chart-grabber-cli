// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/i_HtmlToHintTextConverter.pas

#include <QString>

#include <memory>

namespace cg::vector {

class IHtmlToHintTextConverter {
public:
    virtual ~IHtmlToHintTextConverter() = default;
    [[nodiscard]] virtual QString convert(const QString& name, const QString& description) const = 0;
};
using HtmlToHintTextConverterPtr = std::shared_ptr<const IHtmlToHintTextConverter>;

} // namespace cg::vector
