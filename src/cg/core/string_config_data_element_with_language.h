// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_StringConfigDataElementWithLanguage.pas
//
// Default text per language from an IStringByLanguage, indexed by the manager's current language.

#include "cg/core/string_by_language.h"
#include "cg/core/string_config_data_element_with_def_base.h"

namespace cg::core {

// FEAT-UI-207
class StringConfigDataElementWithLanguage final : public StringConfigDataElementWithDefBase {
public:
    ~StringConfigDataElementWithLanguage() override;
    StringConfigDataElementWithLanguage(std::shared_ptr<ILanguageManager> languageManager, bool useStore,
                                        QString storeIdentifier, bool isStoreDefault,
                                        IStringByLanguagePtr defValuesByLanguage);

protected:
    [[nodiscard]] QString getDefValueForCurrentLang() const override;

private:
    IStringByLanguagePtr defValuesByLanguage_;
};

} // namespace cg::core
