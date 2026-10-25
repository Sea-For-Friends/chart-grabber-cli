// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-GEO-037: colour config helpers (u_ConfigProviderHelpers.pas ReadColor32 / WriteColor32) and
// core/color32.h. Expectations are derived by hand from the Pascal.

#include "cg/core/color32.h"
#include "cg/core/config_color.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

#include "../corpus_skip.h"
#include "fixture_path.h"

using namespace cg::core;

namespace {

std::shared_ptr<ConfigProviderIni> iniFrom(const QByteArray& text) {
    auto ini = IniFile::fromBytes(text);
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));
}

Color32 readFrom(const char* body, Color32 def) {
    const auto root = iniFrom(QByteArray("[S]\n") + body);
    return readColor32(root->subItem(u"S"), u"C", def);
}

constexpr Color32 kDef = 0x11223344U; // alpha 0x11, R 0x22, G 0x33, B 0x44; WinColor = 0x443322

} // namespace

TEST(Color32, AccessorsFollowGr32) {
    EXPECT_EQ(color32FromWinColor(0x0000FF), 0xFFFF0000U); // TColor red -> ARGB red
    EXPECT_EQ(color32FromWinColor(0x443322), 0xFF223344U);
    EXPECT_EQ(winColor(0x11223344U), 0x443322);
    EXPECT_EQ(alphaComponent(0xA6FFFF00U), 0xA6);
    EXPECT_EQ(setAlpha(0xFF223344U, 0x80), 0x80223344U);
    EXPECT_EQ(setAlpha(0xFF223344U, -1), 0x00223344U);
    EXPECT_EQ(setAlpha(0x00223344U, 256), 0xFF223344U);
}

TEST(ReadColor32, Table) {
    struct Row {
        const char* body;
        Color32 expected;
    };
    const Row rows[] = {
        {"", kDef},                          // missing -> default
        {"CHex=$A6FFFF00\n", 0xA6FFFF00U},   // the real format
        {"CHex=$a6ffff00\n", 0xA6FFFF00U},   // hex digits of either case
        {"CHex=$FF\n", 0x000000FFU},         // no alpha digits: taken as is (alpha 0)
        {"CHex=$FFFFFFFF\n", 0xFFFFFFFFU},   // above MaxInt: wraps through Integer, same bits
        {"CHex=100\n", 100U},                // TryStrToInt accepts decimal
        {"CHex=-1\n", 0xFFFFFFFFU},          // negative decimal -> same bits
        {"CHex=A6FFFF00\n", kDef},           // no '$': not an integer -> default
        {"CHex=garbage\n", kDef},            // unparsable -> default (no fallback to other keys)
        {"CHex=$1FFFFFFFF\n", kDef},         // out of 32 bits -> default
        {"CHex=garbage\nC=255\n", kDef},     // a broken Hex does not fall through to 'C'
        {"CHex=\nC=255\n", 0x11FF0000U},     // empty Hex -> legacy keys; TColor 255 = red, alpha of default
        {"CAlfa=128\n", 0x80223344U},        // alpha only: colour of the default
        {"C=255\nCAlfa=300\n", 0xFFFF0000U}, // alpha clamped to 255
        {"C=16711680\nCAlfa=-5\n", 0x000000FFU}, // TColor 0xFF0000 = blue, alpha clamped to 0
        {"C=-16777201\n", 0x110F0000U}, // D-55 : negative TColor = raw low 24 bits (legacy: GetSysColor)
        {"C=abc\nCAlfa=x\n", kDef},     // unparsable integers -> StrToIntDef defaults
    };
    for (const Row& row : rows) {
        EXPECT_EQ(readFrom(row.body, kDef), row.expected) << row.body;
    }
}

TEST(ReadColor32, NullProviderGivesDefault) {
    EXPECT_EQ(readColor32(nullptr, u"C", kDef), kDef);
}

TEST(WriteColor32, TextIsDollarAndEightUpperCaseDigits) {
    struct Row {
        Color32 value;
        const char* text;
    };
    const Row rows[] = {{0xA6FFFF00U, "CHex=$A6FFFF00"},
                        {0x00000000U, "CHex=$00000000"},
                        {0x00000ABCU, "CHex=$00000ABC"},
                        {0xFFFFFFFFU, "CHex=$FFFFFFFF"},
                        {0xdeadbeefU, "CHex=$DEADBEEF"}};
    for (const Row& row : rows) {
        const auto root = iniFrom("");
        const auto section = root->orCreateSubItem(u"S");
        writeColor32(*section, u"C", row.value);
        EXPECT_TRUE(root->file().toBytes().contains(QByteArray(row.text))) << row.text;
        EXPECT_EQ(readColor32(root->subItem(u"S"), u"C", kDef), row.value);
    }
}

TEST(ColorConfig, RealSectionFileRoundTripsEveryColourKey) {
    CG_SKIP_IF_NO_FIXTURE(cgFixturesDir() + QStringLiteral("/marks/SASPlanet.marks-sections.ini"));
    QFile file(cgFixturesDir() + QStringLiteral("/marks/SASPlanet.marks-sections.ini"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    const auto source = iniFrom(bytes);
    int colours = 0;
    for (const QString& sectionName : source->subItemNames()) {
        const auto section = source->subItem(sectionName);
        for (const QString& key : source->file().keyNames(sectionName)) {
            if (!key.endsWith(QStringLiteral("Hex"))) {
                continue;
            }
            const QString id = key.chopped(3);
            const Color32 value = readColor32(section, id, 0x01020304U);
            EXPECT_NE(value, 0x01020304U) << key.toStdString();
            const auto out = iniFrom("");
            writeColor32(*out->orCreateSubItem(u"S"), id, value);
            EXPECT_EQ(out->file().readString(u"S", key, QString()),
                      source->file().readString(sectionName, key, QString()))
                << key.toStdString();
            ++colours;
        }
    }
    EXPECT_EQ(colours, 5); // TextColor, ShadowColor, LineColor (x2), FillColor
}
