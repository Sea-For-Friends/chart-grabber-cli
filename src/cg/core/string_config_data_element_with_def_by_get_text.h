// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_StringConfigDataElementWithDefByGetText.pas
//
// Default text = gettext translation of a message id. The translation call is the ui i18n seam
// (open question OQ-9 / T-0043 handoff): here it is QCoreApplication::translate("gettext", id),
// which returns the id itself while no translator is installed -- legacy's result for an
// untranslated string.

#include "cg/core/string_config_data_element_with_def_base.h"

namespace cg::core {

// FEAT-UI-207
class StringConfigDataElementWithDefByGetText final : public StringConfigDataElementWithDefBase {
public:
    ~StringConfigDataElementWithDefByGetText() override;
    StringConfigDataElementWithDefByGetText(std::shared_ptr<ILanguageManager> languageManager, bool useStore,
                                            QString storeIdentifier, bool isStoreDefault,
                                            QString getTextMsgId);
    // Ports the two-argument overload: not stored.
    StringConfigDataElementWithDefByGetText(std::shared_ptr<ILanguageManager> languageManager,
                                            QString getTextMsgId);

protected:
    [[nodiscard]] QString getDefValueForCurrentLang() const override;

private:
    QString getTextMsgId_;
};

} // namespace cg::core
