// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/string_config_data_element_with_def_by_string_rec.h"

#include <QCoreApplication>

#include <utility>

namespace cg::core {

StringConfigDataElementWithDefByStringRec::StringConfigDataElementWithDefByStringRec(
    std::shared_ptr<ILanguageManager> languageManager, bool useStore, QString storeIdentifier,
    bool isStoreDefault, QString resString)
    : StringConfigDataElementWithDefBase(std::move(languageManager), useStore, std::move(storeIdentifier),
                                         isStoreDefault),
      resString_(std::move(resString)) {
    init();
}

StringConfigDataElementWithDefByStringRec::StringConfigDataElementWithDefByStringRec(
    std::shared_ptr<ILanguageManager> languageManager, QString resString)
    : StringConfigDataElementWithDefByStringRec(std::move(languageManager), false, QString(), false,
                                                std::move(resString)) {}

QString StringConfigDataElementWithDefByStringRec::getDefValueForCurrentLang() const {
    return QCoreApplication::translate("gettext", resString_.toUtf8().constData());
}

StringConfigDataElementWithDefByStringRec::~StringConfigDataElementWithDefByStringRec() {
    unsubscribeLangChange();
}

} // namespace cg::core
