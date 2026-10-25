// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002. Ports u_LanguageListStatic.pas, u_LanguageManager.pas (non-UI half);
// docs/specs/maptype.md §2.5.2, §7.2 (language list = en + the six stock .mo names).
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/core/platform/test_support.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::core::LanguageListStatic;
using cg::core::LanguageManager;
using namespace Qt::StringLiterals;

namespace {

void touch(const QDir& dir, const QString& name) {
    QFile f(dir.filePath(name));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
}

// The six real stock files of the live install's lang/ folder (empty here: only names matter).
void makeStockLangDir(const QTemporaryDir& tmp) {
    for (const char* code : {"uk", "ru", "es", "tr", "fa", "fr"}) { // not in sorted order
        touch(QDir(tmp.path()), QString::fromLatin1(code) + u".mo"_s);
    }
}

std::shared_ptr<ConfigProviderIni> emptyIni() {
    auto ini = IniFile::fromBytes({});
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));
}

TEST(LanguageListStatic, FindCodeIgnoresCaseAndKeepsInputOrder) {
    const LanguageListStatic list({u"en"_s, u"ru"_s, u"es"_s});
    EXPECT_EQ(list.count(), 3);
    EXPECT_EQ(list.code(2), u"es"_s);
    EXPECT_EQ(list.findCode(u"RU"_s), 1);
    EXPECT_EQ(list.findCode(u"es"_s), 2);
    EXPECT_FALSE(list.findCode(u"de"_s).has_value());
    EXPECT_FALSE(list.findCode(QString()).has_value());
}

TEST(LanguageListStatic, DuplicateAndOutOfRangeThrowLikeLegacyStringList) {
    EXPECT_THROW(LanguageListStatic({u"en"_s, u"EN"_s}), std::invalid_argument); // L-128
    const LanguageListStatic list({u"en"_s});
    EXPECT_THROW((void)list.code(1), std::out_of_range);
    EXPECT_THROW((void)list.code(-1), std::out_of_range);
}

TEST(LanguageManager, ListIsEnPlusTheSixStockMoNamesInNameOrder) {
    const QTemporaryDir tmp;
    makeStockLangDir(tmp);
    touch(QDir(tmp.path()), u"readme.txt"_s);
    ASSERT_TRUE(QDir(tmp.path()).mkdir(u"sub.mo"_s)); // a directory is not a language file
    const LanguageManager m(tmp.path());
    const auto list = m.languageList();
    QStringList codes;
    for (int i = 0; i < list->count(); ++i) {
        codes.append(list->code(i));
    }
    EXPECT_EQ(codes, (QStringList{u"en"_s, u"es"_s, u"fa"_s, u"fr"_s, u"ru"_s, u"tr"_s, u"uk"_s}));
    EXPECT_EQ(m.currentLanguageIndex(), 0);
    EXPECT_EQ(m.currentLanguageCode(), u"en"_s);
}

TEST(LanguageManager, MissingFolderLeavesOnlyEnglishAndUppercaseExtensionIsStripped) {
    const LanguageManager none(u"/nonexistent/cg-lang"_s);
    EXPECT_EQ(none.languageList()->count(), 1);

    const QTemporaryDir tmp;
    touch(QDir(tmp.path()), u"DE.MO"_s);
    const LanguageManager m(tmp.path());
    EXPECT_EQ(m.languageList()->count(), 2);
    EXPECT_EQ(m.languageList()->code(1), u"DE"_s);
}

// D-50: legacy raises in the list constructor (L-128); the manager skips the duplicate code instead.
TEST(LanguageManager, DuplicateCodesAreSkippedFirstInNameOrderWins) {
    const QTemporaryDir tmp;
    if (!cg::core::platform::directoryIsCaseSensitive(tmp.path())) {
        GTEST_SKIP() << "the folder is case-insensitive (NTFS): en.mo and EN.MO cannot both exist";
    }
    for (const char* n : {"en.mo", "EN.MO", "ru.mo", "RU.mo", "fr.mo"}) {
        touch(QDir(tmp.path()), QString::fromLatin1(n));
    }
    const LanguageManager m(tmp.path());
    const auto list = m.languageList();
    QStringList codes;
    for (int i = 0; i < list->count(); ++i) {
        codes.append(list->code(i));
    }
    // "RU.mo" sorts before "ru.mo" (equal ignoring case, then by bytes): its spelling is kept.
    EXPECT_EQ(codes, (QStringList{u"en"_s, u"fr"_s, u"RU"_s}));
}

TEST(LanguageManager, SetByCodeChangesOnceIgnoresUnknownAndNotifies) {
    const QTemporaryDir tmp;
    makeStockLangDir(tmp);
    LanguageManager m(tmp.path());
    int changes = 0;
    const auto sub = m.changed().subscribe([&] { ++changes; });
    m.setCurrentLanguageCode(u"ru"_s);
    EXPECT_EQ(m.currentLanguageIndex(), 4);
    EXPECT_EQ(m.currentLanguageCode(), u"ru"_s);
    EXPECT_EQ(changes, 1);
    m.setCurrentLanguageCode(u"ru"_s); // same: no event
    m.setCurrentLanguageCode(u"xx"_s); // unknown: ignored
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(m.currentLanguageCode(), u"ru"_s);
    m.setCurrentLanguageIndex(-1); // negative index = default language
    EXPECT_EQ(m.currentLanguageCode(), u"en"_s);
    EXPECT_EQ(changes, 2);
    EXPECT_THROW(m.setCurrentLanguageIndex(99), std::out_of_range);
}

TEST(LanguageManager, StartLocaleIsMappedToItsBareLanguageCode) {
    const QTemporaryDir tmp;
    makeStockLangDir(tmp);
    struct Row {
        const char* locale;
        int index;
        const char* code;
    };
    // listed kept; ru_RU -> ru; "RU_ru" case-insensitive; pt_BR -> pt (no pt.mo: index 0, written as en);
    // de_DE -> de (no de.mo); a name that is no language -> index 0.
    for (const Row& r :
         {Row{"fr", 3, "fr"}, Row{"ru_RU", 4, "ru"}, Row{"RU_ru", 4, "ru"}, Row{"uk_UA", 6, "uk"},
          Row{"pt_BR", 0, "en"}, Row{"de_DE", 0, "en"}, Row{"zz_ZZ", 0, "en"}, Row{"", 0, "en"}}) {
        const LanguageManager m(tmp.path(), QString::fromLatin1(r.locale));
        EXPECT_EQ(m.currentLanguageIndex(), r.index) << r.locale;
        EXPECT_EQ(m.currentLanguageCode(), QString::fromLatin1(r.code)) << r.locale;
    }
}

TEST(LanguageManager, SettingEnglishAfterAnUnlistedStartLocaleStillFiresChanged) {
    const QTemporaryDir tmp;
    LanguageManager m(tmp.path(), u"pt_BR"_s); // current locale name "pt", shown as index 0
    int changes = 0;
    const auto sub = m.changed().subscribe([&] { ++changes; });
    m.setCurrentLanguageIndex(0);
    EXPECT_EQ(changes, 1); // legacy compares locale NAMES
}

TEST(LanguageManager, ConfigRoundTripsTheLangKey) {
    const QTemporaryDir tmp;
    makeStockLangDir(tmp);
    LanguageManager a(tmp.path());
    a.setCurrentLanguageCode(u"uk"_s);
    const auto root = emptyIni();
    a.writeConfig(root->orCreateSubItem(u"View"));
    EXPECT_EQ(root->subItem(u"View")->readString(u"Lang", u"?"_s), u"uk"_s);

    LanguageManager b(tmp.path());
    b.readConfig(root->subItem(u"View"));
    EXPECT_EQ(b.currentLanguageCode(), u"uk"_s);

    LanguageManager c(tmp.path());
    c.setCurrentLanguageCode(u"fr"_s);
    c.readConfig(emptyIni()->orCreateSubItem(u"View")); // no key: ReadString('Lang','') -> unknown, kept
    EXPECT_EQ(c.currentLanguageCode(), u"fr"_s);
    c.readConfig(nullptr);
    EXPECT_EQ(c.currentLanguageCode(), u"fr"_s);
}

// Ports u_LanguageManager.pas SetCurrentLanguageIndex (:235-258) with the lock and
// notification order of u_ConfigDataElementBase.pas UnlockWrite/StartNotify (:163-173, :185-189) and
// u_ChangeableBase.pas DoChangeNotify (:87-95): the value is changed under the lock, then, with the
// lock released, before -> change -> after fire once each. The read inside `before` also proves the
// lock is free (the lock is not recursive, so a held lock would hang this test).
TEST(LanguageManager, ChangeFiresBeforeThenChangeThenAfterWithTheLockReleased) {
    const QTemporaryDir tmp;
    makeStockLangDir(tmp);
    LanguageManager m(tmp.path());
    QStringList trace;
    const auto sBefore =
        m.beforeChanged().subscribe([&] { trace.append(u"before:"_s + m.currentLanguageCode()); });
    const auto sChange = m.changed().subscribe([&] { trace.append(u"change:"_s + m.currentLanguageCode()); });
    const auto sAfter =
        m.afterChanged().subscribe([&] { trace.append(u"after:"_s + m.currentLanguageCode()); });
    m.setCurrentLanguageCode(u"ru"_s);
    EXPECT_EQ(trace, (QStringList{u"before:ru"_s, u"change:ru"_s, u"after:ru"_s}));
}

} // namespace
