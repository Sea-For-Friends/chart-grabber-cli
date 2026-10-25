// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/language_manager.h"

#include <QDir>
#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace cg::core {

namespace {

constexpr QLatin1StringView kDefLangCode{"en"};
constexpr QLatin1StringView kLangFileExt{".mo"};

// Ports GetListOfLanguages: StringReplace removes every ".mo", ignoring case. Legacy walks the
// folder in NTFS order (case-insensitive by name); the port sorts by name ignoring case, then by
// bytes, so the order is the same on a case-sensitive file system. A code already listed (equal
// ignoring case, "en" included) is skipped, first one wins (D-50, legacy L-128).
QStringList installedLanguages(const QString& langRootPath) {
    QStringList files = QDir(langRootPath).entryList(QDir::Files, QDir::Unsorted);
    std::ranges::sort(files, [](const QString& l, const QString& r) {
        const int c = l.compare(r, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : l < r;
    });
    QStringList result{QString(kDefLangCode)};
    for (const QString& name : std::as_const(files)) {
        if (name.endsWith(kLangFileExt, Qt::CaseInsensitive)) {
            QString code = name;
            code.remove(kLangFileExt, Qt::CaseInsensitive);
            if (!result.contains(code, Qt::CaseInsensitive)) {
                result.append(code);
            }
        }
    }
    return result;
}

// Ports the start-up fix-up of LoadLangs (:218-225): a locale name that is not listed becomes the
// bare language code of its locale (name -> LCID -> first sorted name of that LCID); no such
// locale -> "(Unknown)", which no list contains.
QString mapStartLocale(const QString& name, const ILanguageListStatic& list) {
    if (list.findCode(name)) {
        return name;
    }
    const QString lang = name.left(name.indexOf(u'_')).toLower();
    return QLocale::codeToLanguage(lang) != QLocale::AnyLanguage ? lang : QString();
}

} // namespace

LanguageManager::LanguageManager(const QString& langRootPath, const QString& initialLocaleName) {
    list_ = std::make_shared<const LanguageListStatic>(installedLanguages(langRootPath));
    currentLocaleName_ = mapStartLocale(initialLocaleName, *list_);
}

LanguageManager::~LanguageManager() = default;

void LanguageManager::doReadConfig(const IConfigProviderPtr& src) {
    if (src != nullptr) {
        setCurrentLanguageCodeLocked(src->readString(u"Lang", QString()));
    }
}

void LanguageManager::doWriteConfig(const IConfigWriterPtr& dst) const {
    if (dst != nullptr) {
        dst->writeString(u"Lang", currentLanguageCodeLocked());
    }
}

int LanguageManager::currentLanguageIndexLocked() const {
    const std::optional<int> found = list_->findCode(currentLocaleName_);
    return found.value_or(0);
}

QString LanguageManager::currentLanguageCodeLocked() const {
    const int index = currentLanguageIndexLocked();
    return index >= 0 ? list_->code(index) : QString();
}

int LanguageManager::currentLanguageIndex() const {
    const ReadGuard guard = readLock();
    return currentLanguageIndexLocked();
}

QString LanguageManager::currentLanguageCode() const {
    const ReadGuard guard = readLock();
    return currentLanguageCodeLocked();
}

ILanguageListStaticPtr LanguageManager::languageList() const {
    return list_;
}

void LanguageManager::setCurrentLanguageIndex(int index) {
    const WriteGuard guard = writeLock();
    setCurrentLanguageIndexLocked(index);
}

void LanguageManager::setCurrentLanguageCode(const QString& code) {
    const WriteGuard guard = writeLock();
    setCurrentLanguageCodeLocked(code);
}

// Ports SetCurrentLanguageCode: an unknown code is ignored.
void LanguageManager::setCurrentLanguageCodeLocked(const QString& code) {
    if (const std::optional<int> index = list_->findCode(code)) {
        setCurrentLanguageIndexLocked(*index);
    }
}

// Ports SetCurrentLanguageIndex, with `currentLocaleName_` standing for the gettext locale:
// UseLanguage(code) stores the name; a change of that name is the change event.
void LanguageManager::setCurrentLanguageIndexLocked(int index) {
    const QString prev = currentLocaleName_;
    currentLocaleName_ = index >= 0 ? list_->code(index) : QString(kDefLangCode);
    if (prev != currentLocaleName_) {
        setChanged();
    }
}

} // namespace cg::core
