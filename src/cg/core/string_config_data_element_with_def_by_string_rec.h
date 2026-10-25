// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_StringConfigDataElementWithDefByStringRec.pas
//
// Default text = a Delphi resource string (PResStringRec). Legacy LoadResString is hooked by
// dxgettext (gnugettext.pas:4384), so the text is translated and re-evaluated on a language
// change exactly like the get-text class: same seam, QCoreApplication::translate("gettext", text).

#include "cg/core/string_config_data_element_with_def_base.h"

namespace cg::core {

// FEAT-UI-207
class StringConfigDataElementWithDefByStringRec final : public StringConfigDataElementWithDefBase {
public:
    ~StringConfigDataElementWithDefByStringRec() override;
    StringConfigDataElementWithDefByStringRec(std::shared_ptr<ILanguageManager> languageManager,
                                              bool useStore, QString storeIdentifier, bool isStoreDefault,
                                              QString resString);
    // Ports the two-argument overload: not stored.
    StringConfigDataElementWithDefByStringRec(std::shared_ptr<ILanguageManager> languageManager,
                                              QString resString);

protected:
    [[nodiscard]] QString getDefValueForCurrentLang() const override;

private:
    QString resString_;
};

} // namespace cg::core
