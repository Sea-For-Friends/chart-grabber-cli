// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_ConfigProviderHelpers.pas:451-631 (appearance readers/writers);
// docs/specs/vector.md §2.9, §7.2. Round trip over the real
// tests/fixtures/marks/SASPlanet.marks-sections.ini.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/config_provider_helpers.h"

#include <QFile>
#include <QStringList>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "../core/fixture_path.h"
#include "../corpus_skip.h"
#include "fake_mark_picture.h"

using namespace Qt::StringLiterals;
using namespace cg::vector;
using cg::core::ConfigProviderIni;
using cg::core::IniFile;

namespace {

const AppearanceOfMarkFactory kFactory;

std::shared_ptr<ConfigProviderIni> iniFrom(const QByteArray& text) {
    auto ini = IniFile::fromBytes(text);
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));
}

cg::core::IConfigProviderPtr sectionOf(const char* body) {
    return iniFrom(QByteArray("[S]\n") + body)->subItem(u"S");
}

class FakePicture final : public IMarkPicture {
public:
    explicit FakePicture(QString name) : name_(std::move(name)) {}
    QByteArray source() const override { return {}; }
    std::shared_ptr<const cg::render::IBitmapMarker> marker() const override { return nullptr; }
    QString name() const override { return name_; }
    Qt::AlignmentFlag textAlignment() const override { return Qt::AlignLeft; }
    Qt::AlignmentFlag textVerticalAlignment() const override { return Qt::AlignTop; }
    cg::core::HashValue hash() const noexcept override { return 1; }

private:
    QString name_;
};

using FakeList = cg::vector::test::FakeMarkPictureList;

template <class Facet> std::shared_ptr<const Facet> as(const AppearancePtr& p) {
    return std::dynamic_pointer_cast<const Facet>(p);
}

struct PointValues {
    Color32 text;
    Color32 bg;
    int font;
    int size;
    QString name;
};

PointValues pointOf(const AppearancePtr& p) {
    const auto c = as<IAppearancePointCaption>(p);
    const auto i = as<IAppearancePointIcon>(p);
    return {c->textColor(), c->textBgColor(), c->fontSize(), i->markerSize(), i->picName()};
}

AppearancePtr point(Color32 text, Color32 bg, int font, const QString& name, const MarkPicturePtr& pic,
                    int size) {
    return kFactory.createPointAppearance(text, bg, font, name, pic, size);
}

} // namespace

TEST(ReadAppearance, PointDefaultsTable) {
    const auto def = point(0x11111111U, 0x22222222U, 7, QStringLiteral("x"), nullptr, 9);
    struct Row {
        const char* body;
        bool nullConfig;
        AppearancePtr dflt;
        PointValues expected;
    };
    const Row rows[] = {
        {"", false, nullptr, {0xA6FFFFFFU, 0xA6000000U, 11, 32, {}}},            // built-ins, empty section
        {"", true, nullptr, {0xA6FFFFFFU, 0xA6000000U, 11, 32, {}}},             // built-ins, null provider
        {"", false, def, {0x11111111U, 0x22222222U, 7, 9, QStringLiteral("x")}}, // default facets
        {"", true, def, {0x11111111U, 0x22222222U, 7, 9, QStringLiteral("x")}},
        {"TextColorHex=$01020304\nShadowColorHex=$05060708\nFontSize=20\nIconSize=48\nIconName=q\n",
         false,
         def,
         {0x01020304U, 0x05060708U, 20, 48, QStringLiteral("q")}},
        {"FontSize=abc\nIconSize=\n", false, def, {0x11111111U, 0x22222222U, 7, 9, QStringLiteral("x")}},
        {"FontSize=-5\nIconSize=0\n", false, nullptr, {0xA6FFFFFFU, 0xA6000000U, -5, 0, {}}}, // no clamping
        {"TextColor=255\nShadowColorHex=$FF000001\n", false, nullptr, {0xA6FF0000U, 0xFF000001U, 11, 32, {}}},
        {"",
         false,
         kFactory.createLineAppearance(1, 2),
         {0xA6FFFFFFU, 0xA6000000U, 11, 32, {}}}, // wrong kind
    };
    for (const Row& r : rows) {
        const auto got =
            readAppearancePoint(r.nullConfig ? nullptr : sectionOf(r.body), nullptr, kFactory, r.dflt);
        ASSERT_TRUE(got) << r.body;
        const PointValues v = pointOf(got);
        EXPECT_EQ(v.text, r.expected.text) << r.body;
        EXPECT_EQ(v.bg, r.expected.bg) << r.body;
        EXPECT_EQ(v.font, r.expected.font) << r.body;
        EXPECT_EQ(v.size, r.expected.size) << r.body;
        EXPECT_EQ(v.name, r.expected.name) << r.body;
    }
}

TEST(ReadAppearance, LineAndPolygonDefaultsTable) {
    const auto line = kFactory.createLineAppearance(0x11223344U, 5);
    const auto poly = kFactory.createPolygonAppearance(0x55667788U, 6, 0x99AABBCCU);
    struct Row {
        const char* body;
        bool nullConfig;
        AppearancePtr dflt;
        Color32 color;
        int width;
    };
    const Row lineRows[] = {
        {"", false, nullptr, 0xFFFFFFFFU, 1},
        {"", true, line, 0x11223344U, 5},
        {"LineColorHex=$A6FF0000\nLineWidth=2\n", false, line, 0xA6FF0000U, 2},
        {"LineWidth=x\n", false, line, 0x11223344U, 5},
        {"", false, poly, 0xFFFFFFFFU, 1}, // a polygon is not a line: built-ins
    };
    for (const Row& r : lineRows) {
        const auto got = readAppearanceLine(r.nullConfig ? nullptr : sectionOf(r.body), kFactory, r.dflt);
        ASSERT_TRUE(got);
        EXPECT_EQ(as<IAppearanceLine>(got)->lineColor(), r.color) << r.body;
        EXPECT_EQ(as<IAppearanceLine>(got)->lineWidth(), r.width) << r.body;
    }
    struct PRow {
        const char* body;
        bool nullConfig;
        AppearancePtr dflt;
        Color32 color;
        int width;
        Color32 fill;
    };
    const PRow polyRows[] = {
        {"", false, nullptr, 0xFFFFFFFFU, 1, 0x33FFFFFFU},
        {"", true, poly, 0x55667788U, 6, 0x99AABBCCU},
        {"LineColorHex=$A6000000\nLineWidth=2\nFillColorHex=$33FFFFFF\n", false, poly, 0xA6000000U, 2,
         0x33FFFFFFU},
        {"FillColor=0\nFillColorAlfa=10\n", false, nullptr, 0xFFFFFFFFU, 1, 0x0A000000U},
        {"", false, line, 0xFFFFFFFFU, 1, 0x33FFFFFFU},
    };
    for (const PRow& r : polyRows) {
        const auto got = readAppearancePolygon(r.nullConfig ? nullptr : sectionOf(r.body), kFactory, r.dflt);
        ASSERT_TRUE(got);
        EXPECT_EQ(as<IAppearancePolygonBorder>(got)->lineColor(), r.color) << r.body;
        EXPECT_EQ(as<IAppearancePolygonBorder>(got)->lineWidth(), r.width) << r.body;
        EXPECT_EQ(as<IAppearancePolygonFill>(got)->fillColor(), r.fill) << r.body;
    }
}

TEST(ReadAppearance, PictureRule) {
    const auto picA = std::make_shared<FakePicture>(QStringLiteral("a"));
    struct Row {
        const char* body;
        bool withList;
        MarkPicturePtr defaultPic;
        QString defaultName;
        QString expectedName;
        bool expectPic;
        int lookups;
    };
    const Row rows[] = {
        {"IconName=a\n", false, nullptr, {}, "a", false, 0},    // no list: no picture, name kept
        {"IconName=a\n", true, nullptr, {}, "a", true, 1},      // found
        {"IconName=zzz\n", true, nullptr, {}, "zzz", false, 1}, // not found: null pic, name kept
        {"", true, nullptr, {}, "", false, 1},                  // empty name is looked up too
        {"", true, picA, "a", "a", true, 0},                    // default pic matches: no lookup
        {"IconName=b\n", true, picA, "a", "b", true, 1},        // name differs: looked up
        {"IconName=A\n", true, picA, "a", "A", false, 1},       // exact compare, "A" is unknown
        {"IconName=zzz\n", false, picA, "a", "zzz", false, 0},  // differs and no list: pic dropped
    };
    for (const Row& r : rows) {
        FakeList list;
        const auto def = point(1, 2, 3, r.defaultName, r.defaultPic, 4);
        const auto got = readAppearancePoint(
            sectionOf(r.body),
            r.withList ? MarkPictureListPtr(std::shared_ptr<const FakeList>(&list, [](auto*) {})) : nullptr,
            kFactory, def);
        const auto icon = as<IAppearancePointIcon>(got);
        EXPECT_EQ(icon->picName(), r.expectedName) << r.body;
        EXPECT_EQ(icon->pic() != nullptr, r.expectPic) << r.body;
        EXPECT_EQ(list.lookups, r.lookups) << r.body;
    }
}

TEST(WriteAppearance, NullAndForeignWriteNothing) {
    const auto root = iniFrom("");
    const auto sec = root->orCreateSubItem(u"S");
    writeAppearancePoint(sec, nullptr);
    writeAppearancePoint(sec, kFactory.createLineAppearance(1, 2));
    writeAppearanceLine(sec, nullptr);
    writeAppearanceLine(sec, kFactory.createPolygonAppearance(1, 2, 3));
    writeAppearancePolygon(sec, kFactory.createLineAppearance(1, 2));
    writeAppearancePolygon(nullptr,
                           kFactory.createPolygonAppearance(1, 2, 3)); // defensive; unreachable in legacy
    EXPECT_FALSE(root->file().toBytes().contains("LineColor"));
    EXPECT_FALSE(root->file().toBytes().contains("TextColor"));
}

// The proof: read each appearance section of the real file, write it to a fresh writer, compare TEXT lines.
TEST(AppearanceConfig, RoundTripOverRealMarksSections) {
    CG_SKIP_IF_NO_FIXTURE(cgFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    QFile f(cgFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray original = f.readAll();
    const auto source = iniFrom(original);
    const auto target = iniFrom("");
    struct Row {
        const char* section;
        QStringList keys; // what legacy writes, in order
    };
    const Row rows[] = {
        {"MarkNewPoint", {"TextColorHex", "ShadowColorHex", "FontSize", "IconName", "IconSize"}},
        {"MarkNewLine", {"LineColorHex", "LineWidth"}},
        {"MarkNewPoly", {"LineColorHex", "LineWidth", "FillColorHex"}},
    };
    for (const Row& r : rows) {
        const QString name = QString::fromLatin1(r.section);
        const auto in = source->subItem(name);
        const auto out = target->orCreateSubItem(name);
        if (name == u"MarkNewPoint") {
            writeAppearancePoint(out, readAppearancePoint(in, nullptr, kFactory, nullptr));
        } else if (name == u"MarkNewLine") {
            writeAppearanceLine(out, readAppearanceLine(in, kFactory, nullptr));
        } else {
            writeAppearancePolygon(out, readAppearancePolygon(in, kFactory, nullptr));
        }
        QStringList expected;
        QStringList produced;
        for (const QString& key : r.keys) {
            expected << key + u'=' + source->file().readString(name, key, u"<absent>"_s);
        }
        for (const QString& key : target->file().keyNames(name)) {
            produced << key + u'=' + target->file().readString(name, key, QString());
        }
        EXPECT_EQ(produced, expected) << r.section;
        // The original lines themselves, verbatim.
        for (const QString& line : expected) {
            EXPECT_TRUE(original.contains(line.toLatin1() + "\r\n")) << qPrintable(line);
        }
    }
}
