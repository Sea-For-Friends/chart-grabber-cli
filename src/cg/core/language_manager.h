// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_LanguageManager.pas, u_LanguageManager.pas (non-UI half, ticket T-0043)
//
// The list of installed languages (`en` first, then one entry per `*.mo` file of the lang folder)
// and the current language, persisted as `Lang` in the section the owner supplies ([View]).
// Legacy keeps the current language inside the gettext global `DefaultInstance`; here it is the
// `currentLocaleName_` member -- the single seam to the ui i18n ticket, which binds the real
// translation catalogue. ORDER (legacy u_LanguageManager.pas:245-253): the catalogue is bound
// BEFORE setChanged(), i.e. inside setCurrentLanguageIndexLocked(), never in a changed() listener
// -- elements subscribed earlier recompute their defaults in that notification and would read the
// old catalogue. Not ported (ui side): the refresh of open forms in
// DoBeforeChangeNotify, GetLangNameByIndex / u_LanguagesEx names, the start-up adoption of the OS
// locale (`UseLanguage` through the LCID tables) and the `.mo` code filter by those tables.

#include "cg/core/config_element.h"
#include "cg/core/language_list_static.h"

#include <QString>

#include <memory>

namespace cg::core {

// FEAT-NET-002
// beforeChanged() / afterChanged(), inherited from Changeable, are the legacy BeforeChangeNotifier /
// AfterChangeNotifier that u_MapTypeGUIConfigList.pas:101-104 subscribes to. Both fire after the
// write lock is released (u_ConfigDataElementBase.pas:185-189), in the order before -> changed ->
// after (u_ChangeableBase.pas:87-95). The new language is already current when before fires.
class ILanguageManager : public ConfigElement {
public:
    [[nodiscard]] virtual QString currentLanguageCode() const = 0;
    virtual void setCurrentLanguageCode(const QString& code) = 0;
    [[nodiscard]] virtual int currentLanguageIndex() const = 0;
    virtual void setCurrentLanguageIndex(int index) = 0;
    [[nodiscard]] virtual ILanguageListStaticPtr languageList() const = 0;
};

class LanguageManager final : public ILanguageManager {
public:
    // initialLocaleName: what gettext's DefaultInstance.GetCurrentLocaleName reports at start (the
    // OS locale, e.g. "ru_RU"). Mapped per ADR-0007 section 2 (legacy LoadLangs :218-225 does the same
    // only for the primary country of a language): a name that is listed is kept; otherwise it
    // becomes its bare language code, for every country ("pt_BR" -> "pt", "ru_RU" -> "ru");
    // a name that is no known language becomes "" (legacy: "(Unknown)"). A mapped name with no
    // .mo stays current, reads as index 0 and is written back as "en".
    explicit LanguageManager(const QString& langRootPath, const QString& initialLocaleName = QString());
    ~LanguageManager() override;

    [[nodiscard]] QString currentLanguageCode() const override;
    void setCurrentLanguageCode(const QString& code) override;
    [[nodiscard]] int currentLanguageIndex() const override;
    void setCurrentLanguageIndex(int index) override;
    [[nodiscard]] ILanguageListStaticPtr languageList() const override;

protected:
    void doReadConfig(const IConfigProviderPtr& src) override;
    void doWriteConfig(const IConfigWriterPtr& dst) const override;

private:
    [[nodiscard]] int currentLanguageIndexLocked() const;
    [[nodiscard]] QString currentLanguageCodeLocked() const;
    void setCurrentLanguageCodeLocked(const QString& code);
    void setCurrentLanguageIndexLocked(int index);

    ILanguageListStaticPtr list_;
    QString currentLocaleName_;
};

} // namespace cg::core
