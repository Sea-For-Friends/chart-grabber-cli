// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/string_config_data_element_with_language.h"

#include <utility>

namespace cg::core {

StringConfigDataElementWithLanguage::StringConfigDataElementWithLanguage(
    std::shared_ptr<ILanguageManager> languageManager, bool useStore, QString storeIdentifier,
    bool isStoreDefault, IStringByLanguagePtr defValuesByLanguage)
    : StringConfigDataElementWithDefBase(std::move(languageManager), useStore, std::move(storeIdentifier),
                                         isStoreDefault),
      defValuesByLanguage_(std::move(defValuesByLanguage)) {
    init();
}

QString StringConfigDataElementWithLanguage::getDefValueForCurrentLang() const {
    return defValuesByLanguage_->getString(languageManager()->currentLanguageIndex());
}

StringConfigDataElementWithLanguage::~StringConfigDataElementWithLanguage() {
    unsubscribeLangChange();
}

} // namespace cg::core
