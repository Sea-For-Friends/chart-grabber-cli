// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-200; docs/specs/core.md 2.14, test plan 7.7
#include "cg/core/config_provider_ini.h"
#include "cg/core/config_provider_ini_section.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>

#include "fixture_path.h"

using cg::core::ConfigProviderIni;
using cg::core::ConfigProviderIniSection;
using cg::core::IConfigProviderPtr;
using cg::core::IConfigWriterPtr;
using cg::core::IniFile;

namespace {

QByteArray readFixtureBytes(const QString& relativePath) {
    QFile file(cgFixture(relativePath));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << relativePath.toStdString();
    return file.readAll();
}

std::shared_ptr<ConfigProviderIni> loadQuirksProvider() {
    auto parsed = IniFile::fromBytes(readFixtureBytes(QStringLiteral("ini/quirks.ini")));
    EXPECT_TRUE(parsed.has_value());
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(parsed.value_or(IniFile{})));
}

} // namespace

TEST(ConfigProviderIni, Q25_RootHasNoValues) {
    const auto root = loadQuirksProvider();
    EXPECT_FALSE(root->value(u"Simple").isValid());
    EXPECT_TRUE(root->binary(u"BinHex").isEmpty());
    EXPECT_EQ(root->readString(u"anything", QStringLiteral("def")), QStringLiteral("def"));
}

TEST(ConfigProviderIni, Q25_RootValueNamesIsEmpty) {
    const auto root = loadQuirksProvider();
    EXPECT_TRUE(root->valueNames().isEmpty());
}

TEST(ConfigProviderIni, RootSubItemNamesAreSectionNames) {
    const auto root = loadQuirksProvider();
    const QStringList names = root->subItemNames();
    EXPECT_TRUE(names.contains(QStringLiteral("Plain")));
    EXPECT_TRUE(names.contains(QStringLiteral("Types")));
    EXPECT_TRUE(names.contains(QStringLiteral("Tree")));
}

TEST(ConfigProviderIni, RootSubItemMissingSectionIsNull) {
    const auto root = loadQuirksProvider();
    EXPECT_EQ(root->subItem(u"NoSuchSection"), nullptr);
    EXPECT_NE(root->subItem(u"Plain"), nullptr);
}

TEST(ConfigProviderIni, Q26_RootValueWritesAreNoops) {
    const auto root = loadQuirksProvider();
    root->writeValue(u"X", QVariant(QStringLiteral("1")));
    root->deleteValue(u"X");
    root->deleteValues();
    EXPECT_FALSE(root->value(u"X").isValid());
    // The section tree is untouched by any of the above.
    EXPECT_NE(root->subItem(u"Plain"), nullptr);
}

TEST(ConfigProviderIni, OrCreateSubItemMaterialisesOnFirstWrite) {
    const auto root = loadQuirksProvider();
    EXPECT_EQ(root->subItem(u"Brand-New"), nullptr);
    const IConfigWriterPtr created = root->orCreateSubItem(u"Brand-New");
    ASSERT_NE(created, nullptr);
    // Not materialised yet: nothing was written through it.
    EXPECT_EQ(root->subItem(u"Brand-New"), nullptr);
    created->writeString(u"Key", QStringLiteral("value"));
    EXPECT_NE(root->subItem(u"Brand-New"), nullptr);
    EXPECT_EQ(root->subItem(u"Brand-New")->readString(u"Key", QString()), QStringLiteral("value"));
}

TEST(ConfigProviderIniSection, SubItemNameIsSectionUnderscoreIdent) {
    const auto root = loadQuirksProvider();
    const auto tree = std::dynamic_pointer_cast<const ConfigProviderIniSection>(root->subItem(u"Tree"));
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->sectionName(), QStringLiteral("Tree"));
    const auto child = std::dynamic_pointer_cast<const ConfigProviderIniSection>(tree->subItem(u"Child"));
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->sectionName(), QStringLiteral("Tree_Child"));
}

TEST(ConfigProviderIniSection, SubItemNamesStripThePrefix) {
    const auto root = loadQuirksProvider();
    const auto tree = root->subItem(u"Tree");
    ASSERT_NE(tree, nullptr);
    // D-1: the correct prefix strip, not legacy's list-of-empty-strings bug.
    EXPECT_TRUE(tree->subItemNames().contains(QStringLiteral("Child")));
}

TEST(ConfigProviderIniSection, ValueNamesAreKeysInFileOrder) {
    const auto root = loadQuirksProvider();
    const auto dup = root->subItem(u"Dup");
    ASSERT_NE(dup, nullptr);
    EXPECT_EQ(dup->valueNames(), (QStringList{QStringLiteral("Key")})); // first duplicate only
}

TEST(ConfigProviderIniSection, ReadStringUsesDefaultOnlyWhenAbsent) {
    const auto root = loadQuirksProvider();
    const auto plain = root->subItem(u"Plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->readString(u"Empty", QStringLiteral("def")), QString()); // present but empty
    EXPECT_EQ(plain->readString(u"Missing", QStringLiteral("def")), QStringLiteral("def"));
}

TEST(ConfigProviderIniSection, ReadIntBoolFloatMatchIniFileRules) {
    const auto root = loadQuirksProvider();
    const auto types = root->subItem(u"Types");
    ASSERT_NE(types, nullptr);
    EXPECT_EQ(types->readInt(u"IntHex", 0), 31);
    EXPECT_EQ(types->readInt(u"IntBad", -1), -1);
    EXPECT_TRUE(types->readBool(u"BoolOne", false));
    // Q14: "True" is not an integer, so ReadBool falls back to whichever default was passed.
    EXPECT_FALSE(types->readBool(u"BoolTrue", false));
    EXPECT_TRUE(types->readBool(u"BoolTrue", true));
    EXPECT_DOUBLE_EQ(types->readFloat(u"FloatComma", 0.0), 3.25);
}

TEST(ConfigProviderIniSection, ReadUuidAcceptsOnlyTheStringToGuidForm) {
    // u_ConfigProviderHelpers.pas:430-448 + Delphi StringToGUID: exactly "{8-4-4-4-12}", 38 characters,
    // hex digits of either case; no trimming, no brace-less form; anything else -> the default.
    const auto root = loadQuirksProvider();
    const auto section = root->orCreateSubItem(u"UuidSection");
    const QUuid uuid = QUuid::createUuid();
    const QUuid other = QUuid::createUuid();
    const QString braced = uuid.toString(QUuid::WithBraces);
    section->writeString(u"Braced", braced);
    section->writeString(u"Lower", braced.toLower());
    section->writeString(u"Bare", uuid.toString(QUuid::WithoutBraces));
    section->writeString(u"Blanks", QStringLiteral(" ") + braced + QStringLiteral(" "));
    section->writeString(u"Short", braced.left(37));
    section->writeString(u"Long", braced + QStringLiteral("0"));
    section->writeString(u"NoDash", QString(braced).remove(u'-'));
    section->writeString(u"NonHex", QString(braced).replace(1, 1, u'G'));
    section->writeString(u"Bad", QStringLiteral("not-a-guid"));
    EXPECT_EQ(section->readUuid(u"Braced", other), uuid);
    EXPECT_EQ(section->readUuid(u"Lower", other), uuid);
    // The INI layer trims the value itself (TIniFile), so blanks never reach StringToGUID here.
    EXPECT_EQ(section->readUuid(u"Blanks", other), uuid);
    for (const auto* key : {u"Bare", u"Short", u"Long", u"NoDash", u"NonHex", u"Bad", u"Missing"}) {
        EXPECT_EQ(section->readUuid(key, other), other) << QString::fromUtf16(key).toStdString();
    }
}

TEST(ConfigProviderIniSection, ReadUuidDistinguishesStoredNullFromParseFailure) {
    // Rework finding 4: a legitimately stored null GUID must round-trip, not be conflated with
    // "the string failed to parse" (u_ConfigProviderHelpers.pas:430-448 only falls back to the
    // default on StringToGUID's exception).
    const auto root = loadQuirksProvider();
    const auto section = root->orCreateSubItem(u"UuidNullSection");
    const QUuid nonDefaultFallback = QUuid::createUuid();
    section->writeString(u"StoredNull", QUuid().toString(QUuid::WithBraces));
    section->writeString(u"Garbage", QStringLiteral("not-a-guid"));
    EXPECT_EQ(section->readUuid(u"StoredNull", nonDefaultFallback), QUuid());
    EXPECT_EQ(section->readUuid(u"Garbage", nonDefaultFallback), nonDefaultFallback);
}

TEST(ConfigProviderIniSection, WriteUuidIsUpperCaseBraced) {
    const auto root = loadQuirksProvider();
    const auto section = root->orCreateSubItem(u"UuidWrite");
    const QUuid uuid = QUuid::createUuid();
    section->writeUuid(u"Id", uuid);
    const QString stored = section->readString(u"Id", QString());
    EXPECT_TRUE(stored.startsWith(QLatin1Char('{')));
    EXPECT_EQ(stored, stored.toUpper());
    EXPECT_EQ(section->readUuid(u"Id", QUuid()), uuid);
}

TEST(ConfigProviderIniSection, ReadStringListSkipsEmptiesAndTrims) {
    const auto root = loadQuirksProvider();
    const auto section = root->orCreateSubItem(u"ListSection");
    section->writeInt(u"Items_Count", 4);
    section->writeString(u"Items_0", QStringLiteral("  a  "));
    section->writeString(u"Items_1", QString());
    section->writeString(u"Items_2", QStringLiteral("b"));
    section->writeString(u"Items_3", QStringLiteral("   "));
    EXPECT_EQ(section->readStringList(u"Items"), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

TEST(ConfigProviderIniSection, WriteStringListWritesCountAndItems) {
    const auto root = loadQuirksProvider();
    const auto section = root->orCreateSubItem(u"ListWrite");
    section->writeStringList(u"Items", {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")});
    EXPECT_EQ(section->readInt(u"Items_Count", -1), 3);
    EXPECT_EQ(section->readString(u"Items_0", QString()), QStringLiteral("x"));
    EXPECT_EQ(section->readString(u"Items_2", QString()), QStringLiteral("z"));

    const auto empty = root->orCreateSubItem(u"ListWriteEmpty");
    empty->writeStringList(u"Items", QStringList());
    EXPECT_EQ(empty->readInt(u"Items_Count", -1), 0);
    EXPECT_FALSE(empty->value(u"Items_0").isValid());
}

TEST(ConfigProviderIniSection, DeleteValueDeleteValuesDeleteSubItem) {
    const auto root = loadQuirksProvider();
    const auto section = root->orCreateSubItem(u"DeleteMe");
    section->writeString(u"A", QStringLiteral("1"));
    section->writeString(u"B", QStringLiteral("2"));
    section->deleteValue(u"A");
    EXPECT_FALSE(section->value(u"A").isValid());
    EXPECT_TRUE(section->value(u"B").isValid());
    section->deleteValues();
    EXPECT_FALSE(section->value(u"B").isValid());

    const auto tree = root->subItem(u"Tree");
    ASSERT_NE(tree, nullptr);
    IConfigWriterPtr treeWriter = root->orCreateSubItem(u"Tree");
    treeWriter->deleteSubItem(u"Child");
    EXPECT_EQ(root->subItem(u"Tree_Child"), nullptr);
}

TEST(ConfigProviderIniSection, ChildKeepsParentAndIniFileAlive) {
    IConfigProviderPtr child;
    {
        auto root = loadQuirksProvider();
        child = root->subItem(u"Plain"); // root (and its shared_ptr<IniFile>) goes out of scope here
    }
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->readString(u"Simple", QString()), QStringLiteral("value"));
}

TEST(ConfigProviderIni, ThreeLevelSubTreeNaming) {
    const auto root = loadQuirksProvider();
    const auto a = root->orCreateSubItem(u"A");
    const auto ab = a->orCreateSubItem(u"B");
    const auto abc = ab->orCreateSubItem(u"C");
    // A section only exists once something is written into it -- sections are flat, "A_B_C" style
    // strings, not a real hierarchy, so ancestor sections must be materialised too if the test is
    // going to navigate back down to them from `root`.
    a->writeString(u"Marker", QStringLiteral("a"));
    ab->writeString(u"Marker", QStringLiteral("ab"));
    abc->writeString(u"Deep", QStringLiteral("value"));
    const auto abSection = std::dynamic_pointer_cast<ConfigProviderIniSection>(ab);
    const auto abcSection = std::dynamic_pointer_cast<ConfigProviderIniSection>(abc);
    ASSERT_NE(abSection, nullptr);
    ASSERT_NE(abcSection, nullptr);
    EXPECT_EQ(abSection->sectionName(), QStringLiteral("A_B"));
    EXPECT_EQ(abcSection->sectionName(), QStringLiteral("A_B_C"));
    const auto foundA = root->subItem(u"A");
    ASSERT_NE(foundA, nullptr);
    const auto foundAB = foundA->subItem(u"B");
    ASSERT_NE(foundAB, nullptr);
    const auto foundABC = foundAB->subItem(u"C");
    ASSERT_NE(foundABC, nullptr);
    EXPECT_EQ(foundABC->readString(u"Deep", QString()), QStringLiteral("value"));
}
