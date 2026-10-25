// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-002, FEAT-UI-207. Ports u_StringByLanguageWithStaticList.pas,
// u_StringConfigDataElementWithDefBase/WithLanguage/ByGetText/ByStringRec.pas;
// docs/specs/maptype.md §2.10 (MapName / MapName_ write rule), §7.3.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/core/string_by_language.h"
#include "cg/core/string_config_data_element_with_def_by_get_text.h"
#include "cg/core/string_config_data_element_with_def_by_string_rec.h"
#include "cg/core/string_config_data_element_with_language.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTranslator>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using namespace cg::core;
using namespace Qt::StringLiterals;

namespace {

// en, then es, fa, fr, ru, tr, uk -- the stock lang folder, files empty (only names matter).
struct LangFixture {
    QTemporaryDir dir;
    std::shared_ptr<LanguageManager> manager;
    LangFixture() {
        for (const char* code : {"es", "fa", "fr", "ru", "tr", "uk"}) {
            QFile f(QDir(dir.path()).filePath(QString::fromLatin1(code) + u".mo"_s));
            EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        }
        manager = std::make_shared<LanguageManager>(dir.path());
    }
};

std::shared_ptr<ConfigProviderIni> iniFrom(const QByteArray& text) {
    auto ini = IniFile::fromBytes(text);
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));
}

IStringByLanguagePtr names() {
    // en, es, fa, fr, ru, tr, uk
    return std::make_shared<StringByLanguageWithStaticList>(
        QStringList{u"Map"_s, u"Mapa"_s, u"Naqshe"_s, u"Carte"_s, u"Karta"_s, u"Harita"_s, u"Mapa-uk"_s});
}

TEST(StringByLanguage, IndexZeroNegativeAndOutOfRangeFallBackToTheDefault) {
    const StringByLanguageWithStaticList s({u"en"_s, u"ru"_s, u"fr"_s});
    struct Row {
        int index;
        const char* expected;
    };
    for (const Row& r :
         {Row{0, "en"}, Row{1, "ru"}, Row{2, "fr"}, Row{3, "en"}, Row{-1, "en"}, Row{99, "en"}}) {
        EXPECT_EQ(s.getString(r.index), QString::fromLatin1(r.expected)) << r.index;
    }
    EXPECT_EQ(s.getDefault(), u"en"_s);
    EXPECT_THROW((void)StringByLanguageWithStaticList(QStringList()).getString(1), std::out_of_range); // L-30
}

// The MapName / MapName_ rule: isStoreDefault writes the label key for the default value.
TEST(StringConfigDataElement, StoreDefaultWritesLabelKeyForDefaultAndRealKeyOtherwise) {
    const LangFixture f;
    StringConfigDataElementWithLanguage name(f.manager, true, u"MapName"_s, true, names());
    const auto root = iniFrom({});

    name.writeConfig(root->orCreateSubItem(u"S"));
    auto s = root->subItem(u"S");
    EXPECT_EQ(s->valueNames(), (QStringList{u"MapName_"_s}));
    EXPECT_EQ(s->readString(u"MapName_", {}), u"Map"_s);

    name.setValue(u"My map"_s);
    name.writeConfig(root->orCreateSubItem(u"S"));
    s = root->subItem(u"S");
    EXPECT_EQ(s->valueNames(), (QStringList{u"MapName"_s}));
    EXPECT_EQ(s->readString(u"MapName", {}), u"My map"_s);

    name.setValue(u"Map"_s); // back to the default: the real key goes, the label returns
    name.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->subItem(u"S")->valueNames(), (QStringList{u"MapName_"_s}));
}

TEST(StringConfigDataElement, WithoutStoreDefaultTheKeyIsDeletedForTheDefaultValue) {
    const LangFixture f;
    StringConfigDataElementWithLanguage el(f.manager, true, u"ParentSubMenu"_s, false, names());
    const auto root = iniFrom("[S]\nParentSubMenu=old\n");
    el.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_TRUE(root->subItem(u"S")->valueNames().isEmpty()); // default value: key removed, no label
    el.setValue(u"Sub"_s);
    el.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->subItem(u"S")->readString(u"ParentSubMenu", {}), u"Sub"_s);
}

TEST(StringConfigDataElement, NotStoredElementWritesAndReadsNothing) {
    const LangFixture f;
    StringConfigDataElementWithLanguage el(f.manager, false, u"InfoUrl"_s, false, names());
    const auto root = iniFrom("[S]\nInfoUrl=zzz\n");
    el.writeConfig(root->orCreateSubItem(u"S"));
    el.readConfig(root->subItem(u"S"));
    EXPECT_EQ(el.value(), u"Map"_s);
    EXPECT_EQ(root->subItem(u"S")->readString(u"InfoUrl", {}), u"zzz"_s);
}

TEST(StringConfigDataElement, ReadTakesTheKeyNeverTheLabelAndNotifiesOnlyOnChange) {
    const LangFixture f;
    StringConfigDataElementWithLanguage el(f.manager, true, u"MapName"_s, true, names());
    int changes = 0;
    const auto sub = el.changed().subscribe([&] { ++changes; });
    el.readConfig(iniFrom("[S]\nMapName_=Label\n")->subItem(u"S"));
    EXPECT_EQ(el.value(), u"Map"_s); // MapName_ is never read back
    EXPECT_EQ(changes, 0);
    el.readConfig(iniFrom("[S]\nMapName=Custom\n")->subItem(u"S"));
    EXPECT_EQ(el.value(), u"Custom"_s);
    EXPECT_EQ(el.defaultValue(), u"Map"_s);
    EXPECT_EQ(changes, 1);
}

TEST(StringConfigDataElement, LanguageChangeMovesTheValueOnlyWhileItEqualsTheDefault) {
    const LangFixture f;
    StringConfigDataElementWithLanguage follows(f.manager, false, {}, false, names());
    StringConfigDataElementWithLanguage custom(f.manager, false, {}, false, names());
    custom.setValue(u"Mine"_s);
    int changes = 0;
    const auto sub = follows.changed().subscribe([&] { ++changes; });

    f.manager->setCurrentLanguageCode(u"ru"_s);
    EXPECT_EQ(follows.value(), u"Karta"_s);
    EXPECT_EQ(follows.defaultValue(), u"Karta"_s);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(custom.value(), u"Mine"_s);
    EXPECT_EQ(custom.defaultValue(), u"Karta"_s);

    f.manager->setCurrentLanguageCode(u"fr"_s);
    EXPECT_EQ(follows.value(), u"Carte"_s);
    EXPECT_EQ(custom.value(), u"Mine"_s);
}

TEST(StringConfigDataElement, NoLongerListensAfterDestruction) {
    const LangFixture f;
    {
        StringConfigDataElementWithLanguage gone(f.manager, false, {}, false, names());
    }
    EXPECT_EQ(f.manager->changed().subscriberCount(), 0U);
    f.manager->setCurrentLanguageCode(u"ru"_s); // no destroyed element is called (ASan/TSan)
}

// Test translator: answers by the manager's current language, like a bound catalogue.
class StubTranslator final : public QTranslator {
public:
    explicit StubTranslator(std::shared_ptr<LanguageManager> m) : manager_(std::move(m)) {}
    [[nodiscard]] bool isEmpty() const override { return false; }
    [[nodiscard]] QString translate(const char* context, const char* source, const char*,
                                    int) const override {
        if (QByteArrayView(context) != "gettext" || manager_->currentLanguageCode() != u"ru"_s) {
            return {};
        }
        return QByteArrayView(source) == "Point" ? u"Tochka"_s : u"Metka"_s;
    }

private:
    std::shared_ptr<LanguageManager> manager_;
};

TEST(StringConfigDataElement, UntranslatedGetTextAndStringRecYieldTheMessageId) {
    const LangFixture f;
    StringConfigDataElementWithDefByGetText g(f.manager, true, u"DefaultName"_s, true, u"Placemark %0:d"_s);
    EXPECT_EQ(g.value(), u"Placemark %0:d"_s);
    StringConfigDataElementWithDefByGetText g2(f.manager, u"Hello"_s);
    EXPECT_EQ(g2.defaultValue(), u"Hello"_s);
    const auto root = iniFrom("[S]\nKeep=1\n");
    g2.writeConfig(root->orCreateSubItem(u"S")); // two-argument overload: not stored
    EXPECT_EQ(root->subItem(u"S")->valueNames(), (QStringList{u"Keep"_s}));

    StringConfigDataElementWithDefByStringRec r(f.manager, true, u"DefaultName"_s, true, u"Point"_s);
    EXPECT_EQ(r.value(), u"Point"_s);
    r.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->subItem(u"S")->valueNames(), (QStringList{u"Keep"_s, u"DefaultName_"_s}));
    StringConfigDataElementWithDefByStringRec r2(f.manager, u"Sensor"_s);
    EXPECT_EQ(r2.value(), u"Sensor"_s);
}

// Resource-string and get-text defaults are translated and follow a language change; a value the
// user set stays (legacy: dxgettext hooks LoadResString).
TEST(StringConfigDataElement, TranslatedDefaultsFollowTheLanguageAndKeepAUserValue) {
    int argc = 0;
    const QCoreApplication app(argc, nullptr);
    const LangFixture f;
    StubTranslator translator(f.manager);
    ASSERT_TRUE(QCoreApplication::installTranslator(&translator));

    StringConfigDataElementWithDefByStringRec rec(f.manager, true, u"DefaultName"_s, true, u"Point"_s);
    StringConfigDataElementWithDefByStringRec custom(f.manager, true, u"DefaultName"_s, true, u"Point"_s);
    StringConfigDataElementWithDefByGetText text(f.manager, u"Hello"_s);
    custom.setValue(u"Mine"_s);
    EXPECT_EQ(rec.value(), u"Point"_s);

    f.manager->setCurrentLanguageCode(u"ru"_s);
    EXPECT_EQ(rec.defaultValue(), u"Tochka"_s);
    EXPECT_EQ(rec.value(), u"Tochka"_s);
    EXPECT_EQ(text.value(), u"Metka"_s);
    EXPECT_EQ(custom.defaultValue(), u"Tochka"_s);
    EXPECT_EQ(custom.value(), u"Mine"_s);

    f.manager->setCurrentLanguageCode(u"en"_s);
    EXPECT_EQ(rec.value(), u"Point"_s);
    EXPECT_EQ(custom.value(), u"Mine"_s);
    QCoreApplication::removeTranslator(&translator);
}

TEST(StringConfigDataElement, WriteToANullWriterIsIgnored) { // defensive, not a deviation
    const LangFixture f;
    StringConfigDataElementWithLanguage el(f.manager, true, u"MapName"_s, true, names());
    el.writeConfig(nullptr);
    SUCCEED();
}

} // namespace
