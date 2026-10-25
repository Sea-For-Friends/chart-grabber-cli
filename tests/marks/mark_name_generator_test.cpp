// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkNameGenerator.pas (docs/specs/marks.md section 2.3). Ticket T-0091 is a slice of the
// marks module. The [MarkNew*_Name] sections of the real fixture are read and written back byte for
// byte; newName() formats the counter with the fixture's format string (Delphi Format).
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/core/string_config_data_element_with_def_by_string_rec.h"
#include "cg/marks/mark_name_generator.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../corpus_skip.h"
#include "fixture_path.h"

namespace m = cg::marks;
namespace c = cg::core;
using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<c::ConfigProviderIni> loadFixture() {
    auto file = c::IniFile::fromFile(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    EXPECT_TRUE(file.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*file)));
}

std::shared_ptr<c::ConfigProviderIni> wrapText(const QByteArray& text) {
    auto parsed = c::IniFile::fromBytes(text);
    EXPECT_TRUE(parsed.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*parsed)));
}

// The format element is created by the owner (u_MarkPointTemplateConfig.pas and its line and poly
// twins); here it is built with the default text that the fixture's FormatString_ label carries.
std::shared_ptr<c::IStringConfigDataElement>
makeFormatString(const QString& defaultText, std::shared_ptr<c::ILanguageManager> languages) {
    return std::make_shared<c::StringConfigDataElementWithDefByStringRec>(
        std::move(languages), true, u"FormatString"_s, true, defaultText);
}

} // namespace

// u_MarkNameGenerator.pas:61-67 and u_MarkTemplateConfigBase.pas:67: one [X_Name] section per template
// kind. Read the FormatString and Counter, write them back: the whole fixture file must be unchanged.
TEST(MarkNameGenerator, FixtureNameSectionsRoundTripByteIdentical) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    struct Row {
        const char* section;
        QString defaultFormat;
    };
    const std::vector<Row> rows = {
        {"MarkNewPoint_Name", u"Placemark %0:d"_s},
        {"MarkNewLine_Name", u"Path %0:d"_s},
        {"MarkNewPoly_Name", u"Polygon %0:d"_s},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.section);
        const auto root = loadFixture();
        const QByteArray original = root->file().toBytes();

        QTemporaryDir dir;
        auto languages = std::make_shared<c::LanguageManager>(dir.path());
        m::MarkNameGenerator generator(makeFormatString(row.defaultFormat, languages));
        generator.readConfig(root->subItem(QString::fromLatin1(row.section)));
        EXPECT_EQ(generator.formatString()->value(), row.defaultFormat);
        EXPECT_EQ(generator.counter(), 0);

        generator.writeConfig(root->orCreateSubItem(QString::fromLatin1(row.section)));
        EXPECT_EQ(root->file().toBytes(), original);
    }
}

// Counter: read when present (u_MarkNameGenerator.pas:69-76), appended on write when absent
// (u_MarkNameGenerator.pas:78-84). The FormatString label is written first (child added first).
TEST(MarkNameGenerator, CounterReadAndWrite) {
    struct Row {
        const char* label;
        QByteArray input;
        int expCounter;
        QByteArray expected;
    };
    const std::vector<Row> rows = {
        {"counter stored", "[MarkNewPoint_Name]\r\nCounter=7\r\n", 7,
         "[MarkNewPoint_Name]\r\nCounter=7\r\nFormatString_=Placemark %0:d\r\n"},
        {"counter absent keeps zero", "[MarkNewPoint_Name]\r\n", 0,
         "[MarkNewPoint_Name]\r\nFormatString_=Placemark %0:d\r\nCounter=0\r\n"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        QTemporaryDir dir;
        auto languages = std::make_shared<c::LanguageManager>(dir.path());
        m::MarkNameGenerator generator(makeFormatString(u"Placemark %0:d"_s, languages));
        const auto root = wrapText(row.input);
        generator.readConfig(root->subItem(u"MarkNewPoint_Name"));
        EXPECT_EQ(generator.counter(), row.expCounter);
        generator.writeConfig(root->orCreateSubItem(u"MarkNewPoint_Name"));
        EXPECT_EQ(root->file().toBytes(), row.expected);
    }
}

// setCounter notifies only on a real change (u_MarkNameGenerator.pas:113-124); a change of the format
// element is one change of this element (the child's listener, as in u_MarkNameGenerator.pas:65).
TEST(MarkNameGenerator, ChangesNotifyOnlyOnARealChange) {
    QTemporaryDir dir;
    auto languages = std::make_shared<c::LanguageManager>(dir.path());
    m::MarkNameGenerator generator(makeFormatString(u"Placemark %0:d"_s, languages));
    int notified = 0;
    const auto subscription = generator.changed().subscribe([&notified] { ++notified; });

    generator.setCounter(0);
    EXPECT_EQ(notified, 0);
    generator.setCounter(5);
    EXPECT_EQ(notified, 1);
    EXPECT_EQ(generator.counter(), 5);
    generator.formatString()->setValue(u"Path %0:d"_s);
    EXPECT_EQ(notified, 2);
    EXPECT_EQ(generator.formatString()->value(), u"Path %0:d"_s);
}

// u_MarkNameGenerator.pas:101-111: Format(FormatString, [Counter]), then Inc, then SetChanged. The fixture's
// real format is "Placemark %0:d"; a format without a specifier still counts; a Format error (%s takes no
// integer) raises before Inc, so the counter and the notification count stay as they were.
TEST(MarkNameGenerator, NewNameFormatsThenIncrementsAndNotifies) {
    struct Row {
        const char* label;
        QString format;
        QString first;
        QString second;
    };
    const std::vector<Row> rows = {
        {"fixture format", u"Placemark %0:d"_s, u"Placemark 0"_s, u"Placemark 1"_s},
        {"no specifier", u"Точка"_s, u"Точка"_s, u"Точка"_s},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        QTemporaryDir dir;
        auto languages = std::make_shared<c::LanguageManager>(dir.path());
        m::MarkNameGenerator generator(makeFormatString(row.format, languages));
        int notified = 0;
        const auto subscription = generator.changed().subscribe([&notified] { ++notified; });
        EXPECT_EQ(generator.newName(), row.first);
        EXPECT_EQ(generator.newName(), row.second);
        EXPECT_EQ(generator.counter(), 2);
        EXPECT_EQ(notified, 2);
    }
}

TEST(MarkNameGenerator, NewNameFormatErrorLeavesCounterAndNotifications) {
    QTemporaryDir dir;
    auto languages = std::make_shared<c::LanguageManager>(dir.path());
    m::MarkNameGenerator generator(makeFormatString(u"Name %s"_s, languages));
    generator.setCounter(4);
    int notified = 0;
    const auto subscription = generator.changed().subscribe([&notified] { ++notified; });
    EXPECT_THROW(static_cast<void>(generator.newName()), std::runtime_error);
    EXPECT_EQ(generator.counter(), 4);
    EXPECT_EQ(notified, 0);
    generator.formatString()->setValue(u"Name %d"_s); // the lock was released: the generator still works
    EXPECT_EQ(generator.newName(), u"Name 4"_s);
}

// u_MarkNameGenerator.pas:72-75: a non-nil provider marks the element changed even when nothing differs;
// a nil provider does not.
TEST(MarkNameGenerator, ReadConfigNotifiesEvenWithEqualValuesButNotForNilProvider) {
    QTemporaryDir dir;
    auto languages = std::make_shared<c::LanguageManager>(dir.path());
    m::MarkNameGenerator generator(makeFormatString(u"Placemark %0:d"_s, languages));
    int notified = 0;
    const auto subscription = generator.changed().subscribe([&notified] { ++notified; });
    generator.readConfig(nullptr);
    EXPECT_EQ(notified, 0);
    generator.readConfig(wrapText("[S]\r\nCounter=0\r\n")->subItem(u"S"_s));
    EXPECT_EQ(notified, 1);
    EXPECT_EQ(generator.counter(), 0);
}

// Not in legacy: a null format element would be dereferenced by the first read or write
// (u_MarkNameGenerator.pas:65 passes it to Add). The port refuses it at construction.
TEST(MarkNameGenerator, NullFormatStringIsRejectedAtConstruction) {
    EXPECT_THROW(m::MarkNameGenerator(nullptr), std::invalid_argument);
}
