// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/u_HtmlToHintTextConverterStuped.pas
//
// Mark hint text from name + description HTML, statement by statement as legacy (spec vector.md
// §2.2.2). The legacy defects are reproduced (docs/parity/legacy-defects.md L-124, L-125).

#include "cg/vector/html_to_hint_text_converter.h"

namespace cg::vector {

// Public in legacy too: the HTML entities `&amp; &lt; ...` replaced one by one, in legacy order.
[[nodiscard]] QString stupedHtmlToTextConverter(const QString& source);

class HtmlToHintTextConverterStuped final : public IHtmlToHintTextConverter {
public:
    [[nodiscard]] QString convert(const QString& name, const QString& description) const override;

private:
    [[nodiscard]] static QString html2Txt(QString origHtml);
};

} // namespace cg::vector
