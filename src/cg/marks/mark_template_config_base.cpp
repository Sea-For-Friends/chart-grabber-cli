// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_template_config_base.h"

#include "cg/core/string_config_data_element_with_def_by_string_rec.h"

#include <stdexcept>

namespace cg::marks {

std::shared_ptr<core::IStringConfigDataElement>
makeTemplateFormatString(const std::shared_ptr<core::ILanguageManager>& languageManager,
                         const QString& resString) {
    if (languageManager == nullptr) {
        throw std::invalid_argument("MarkTemplateConfig: nil language manager");
    }
    return std::make_shared<core::StringConfigDataElementWithDefByStringRec>(
        languageManager, true, QStringLiteral("FormatString"), true, resString);
}

} // namespace cg::marks
