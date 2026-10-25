// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/string_config_data_element_with_def_by_get_text.h"

#include <QCoreApplication>

#include <utility>

namespace cg::core {

StringConfigDataElementWithDefByGetText::StringConfigDataElementWithDefByGetText(
    std::shared_ptr<ILanguageManager> languageManager, bool useStore, QString storeIdentifier,
    bool isStoreDefault, QString getTextMsgId)
    : StringConfigDataElementWithDefBase(std::move(languageManager), useStore, std::move(storeIdentifier),
                                         isStoreDefault),
      getTextMsgId_(std::move(getTextMsgId)) {
    init();
}

StringConfigDataElementWithDefByGetText::StringConfigDataElementWithDefByGetText(
    std::shared_ptr<ILanguageManager> languageManager, QString getTextMsgId)
    : StringConfigDataElementWithDefByGetText(std::move(languageManager), false, QString(), false,
                                              std::move(getTextMsgId)) {}

QString StringConfigDataElementWithDefByGetText::getDefValueForCurrentLang() const {
    return QCoreApplication::translate("gettext", getTextMsgId_.toUtf8().constData());
}

StringConfigDataElementWithDefByGetText::~StringConfigDataElementWithDefByGetText() {
    unsubscribeLangChange();
}

} // namespace cg::core
