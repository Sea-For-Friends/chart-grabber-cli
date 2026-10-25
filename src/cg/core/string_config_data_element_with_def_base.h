// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_StringConfigDataElementWithDefBase.pas
//
// A string setting whose default depends on the current language. When the language changes and
// the value still equals the old default, the value follows the new default. Persistence
// (useStore): with isStoreDefault the key `K` holds a non-default value and `K_` a read-only
// label of the default one (never read back); without it `K` is deleted for the default value.
//
// Delphi's AfterConstruction becomes init(): every concrete subclass calls it as the last step of
// its constructor (a C++ base constructor cannot call the virtual getDefValueForCurrentLang).

#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/core/string_config_data_element.h"

#include <memory>

namespace cg::core {

// FEAT-UI-207
// NOLINTNEXTLINE(readability-identifier-naming): abstract class with logic, see config_element.h
class StringConfigDataElementWithDefBase : public IStringConfigDataElement {
public:
    ~StringConfigDataElementWithDefBase() override;

    [[nodiscard]] QString value() const override;
    void setValue(const QString& value) override;
    [[nodiscard]] QString defaultValue() const override;

protected:
    StringConfigDataElementWithDefBase(std::shared_ptr<ILanguageManager> languageManager, bool useStore,
                                       QString storeIdentifier, bool isStoreDefault);

    [[nodiscard]] const std::shared_ptr<ILanguageManager>& languageManager() const {
        return languageManager_;
    }
    [[nodiscard]] virtual QString getDefValueForCurrentLang() const = 0;
    // Concrete destructors call this first, while their own members are still alive (legacy
    // removes the listener in Destroy, before any field is freed).
    void unsubscribeLangChange() { langChangeSubscription_.reset(); }
    void init(); // ports AfterConstruction; also subscribes to the language manager

    void doReadConfig(const IConfigProviderPtr& src) override;
    void doWriteConfig(const IConfigWriterPtr& dst) const override;

private:
    void onLangChange();
    void setValueLocked(const QString& value);

    std::shared_ptr<ILanguageManager> languageManager_;
    bool useStore_;
    QString storeIdentifier_;
    bool isStoreDefault_;
    QString defValue_;
    QString value_;
    Subscription langChangeSubscription_;
};

} // namespace cg::core
