// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkCategoryFactoryConfig.pas (docs/specs/marks.md section 2.3). Ticket T-0091 is a
// slice of the marks module. Reads and writes the real [MarkNewCategory] section of the fixture.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/marks/mark_category_factory_config.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "../corpus_skip.h"
#include "fixture_path.h"

namespace m = cg::marks;
namespace c = cg::core;
using namespace Qt::StringLiterals;

namespace {

// Empty language folder: only the default (source) texts are used.
std::shared_ptr<c::ILanguageManager> makeLanguages(QTemporaryDir& dir) {
    return std::make_shared<c::LanguageManager>(dir.path());
}

std::shared_ptr<c::ConfigProviderIni> wrapText(const QByteArray& text) {
    auto parsed = c::IniFile::fromBytes(text);
    EXPECT_TRUE(parsed.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*parsed)));
}

} // namespace

// u_MarkCategoryFactoryConfig.pas:64-78: defaults set by the constructor (name from u_ResStrings.pas:186).
TEST(MarkCategoryFactoryConfig, ConstructorDefaults) {
    QTemporaryDir dir;
    m::MarkCategoryFactoryConfig config(makeLanguages(dir));
    EXPECT_EQ(config.defaultName()->value(), u"New Category"_s);
    EXPECT_EQ(config.defaultName()->defaultValue(), u"New Category"_s);
    EXPECT_EQ(config.afterScale(), 3);
    EXPECT_EQ(config.beforeScale(), 23);
}

// The real [MarkNewCategory] section (INI keys DefaultName_, AfterScale, BeforeScale) is read and
// written back with the same bytes: the whole fixture file is compared, not just the section.
TEST(MarkCategoryFactoryConfig, FixtureSectionRoundTripIsByteIdentical) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    auto file = c::IniFile::fromFile(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    ASSERT_TRUE(file.has_value());
    const QByteArray original = file->toBytes();
    const auto root = c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*file)));

    QTemporaryDir dir;
    m::MarkCategoryFactoryConfig config(makeLanguages(dir));
    config.readConfig(root->subItem(u"MarkNewCategory"));
    EXPECT_EQ(config.defaultName()->value(), u"New Category"_s);
    EXPECT_EQ(config.afterScale(), 3);
    EXPECT_EQ(config.beforeScale(), 23);

    config.writeConfig(root->orCreateSubItem(u"MarkNewCategory"));
    EXPECT_EQ(root->file().toBytes(), original);
}

// Read then write of hand-made sections. Expected bytes follow the Pascal write rules
// (u_StringConfigDataElementWithDefBase.pas:123-145): a non-default name is written under DefaultName
// and DefaultName_ is deleted; the default name is written under DefaultName_ and DefaultName is
// deleted. A missing key keeps its default (AfterScale 3, BeforeScale 23); a new key is appended.
TEST(MarkCategoryFactoryConfig, SectionWriteFollowsStoreRules) {
    struct Row {
        const char* label;
        QByteArray input;
        QString expName;
        int expAfter;
        int expBefore;
        QByteArray expected;
    };
    const std::vector<Row> rows = {
        {"non-default name, AfterScale stored", "[MarkNewCategory]\r\nDefaultName=Trips\r\nAfterScale=5\r\n",
         u"Trips"_s, 5, 23, "[MarkNewCategory]\r\nDefaultName=Trips\r\nAfterScale=5\r\nBeforeScale=23\r\n"},
        {"default name stored under DefaultName, BeforeScale stored",
         "[MarkNewCategory]\r\nDefaultName=New Category\r\nBeforeScale=9\r\n", u"New Category"_s, 3, 9,
         "[MarkNewCategory]\r\nBeforeScale=9\r\nDefaultName_=New Category\r\nAfterScale=3\r\n"},
        {"no section at all, empty file (no BOM: the IniFile of an existing file)", "", u"New Category"_s, 3,
         23, "[MarkNewCategory]\r\nDefaultName_=New Category\r\nAfterScale=3\r\nBeforeScale=23\r\n"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        QTemporaryDir dir;
        m::MarkCategoryFactoryConfig config(makeLanguages(dir));
        const auto root = wrapText(row.input);
        config.readConfig(root->subItem(u"MarkNewCategory"));
        EXPECT_EQ(config.defaultName()->value(), row.expName);
        EXPECT_EQ(config.afterScale(), row.expAfter);
        EXPECT_EQ(config.beforeScale(), row.expBefore);

        config.writeConfig(root->orCreateSubItem(u"MarkNewCategory"));
        EXPECT_EQ(root->file().toBytes(), row.expected);
    }
}

// A change of either scale, or of the child DefaultName, is one change notification of this element;
// writing the same value again is not a change (u_MarkCategoryFactoryConfig.pas:126-150).
TEST(MarkCategoryFactoryConfig, ChangesNotifyOnlyOnARealChange) {
    QTemporaryDir dir;
    m::MarkCategoryFactoryConfig config(makeLanguages(dir));
    int notified = 0;
    const auto subscription = config.changed().subscribe([&notified] { ++notified; });

    config.setAfterScale(3);
    EXPECT_EQ(notified, 0);
    config.setAfterScale(4);
    EXPECT_EQ(notified, 1);
    config.setBeforeScale(23);
    EXPECT_EQ(notified, 1);
    config.setBeforeScale(9);
    EXPECT_EQ(notified, 2);
    config.defaultName()->setValue(u"Trips"_s); // the child's change reaches this element
    EXPECT_EQ(notified, 3);
    config.defaultName()->setValue(u"Trips"_s);
    EXPECT_EQ(notified, 3);
    EXPECT_EQ(config.afterScale(), 4);
    EXPECT_EQ(config.beforeScale(), 9);
}

// u_MarkCategoryFactoryConfig.pas:85-89: a non-nil provider marks the element changed even when both scales
// equal the current values; a nil provider does not.
TEST(MarkCategoryFactoryConfig, ReadConfigNotifiesEvenWithEqualValuesButNotForNilProvider) {
    QTemporaryDir dir;
    m::MarkCategoryFactoryConfig config(makeLanguages(dir));
    int notified = 0;
    const auto subscription = config.changed().subscribe([&notified] { ++notified; });
    config.readConfig(nullptr);
    EXPECT_EQ(notified, 0);
    config.readConfig(wrapText("[S]\r\nAfterScale=3\r\nBeforeScale=23\r\n")->subItem(u"S"_s));
    EXPECT_EQ(notified, 1);
    EXPECT_EQ(config.afterScale(), 3);
    EXPECT_EQ(config.beforeScale(), 23);
}
