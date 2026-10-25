// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkTemplateConfigBase.pas, u_MarkPointTemplateConfig.pas, u_MarkLineTemplateConfig.pas,
// u_MarkPolyTemplateConfig.pas, u_MarkFactoryConfig.pas (docs/specs/marks.md section 2.3, ticket T-0098, a
// slice of the marks module). The proof is the read -> write round trip of the [MarkNewPoint|Line|Poly]
// sections and their _Name sub-sections of the real fixture. Expected values are literals.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/language_manager.h"
#include "cg/marks/mark_factory_config.h"
#include "cg/marks/mark_line_template_config.h"
#include "cg/marks/mark_point_template_config.h"
#include "cg/marks/mark_poly_template_config.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/category.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../corpus_skip.h"
#include "fixture_path.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace c = cg::core;
using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<c::ConfigProviderIni> wrapText(const QByteArray& text) {
    auto parsed = c::IniFile::fromBytes(text);
    EXPECT_TRUE(parsed.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*parsed)));
}

std::shared_ptr<c::ConfigProviderIni> loadFixture() {
    auto file = c::IniFile::fromFile(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    EXPECT_TRUE(file.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*file)));
}

// Owns the language manager (an empty folder: no translation) and the real appearance factory.
struct Env {
    QTemporaryDir dir;
    std::shared_ptr<c::LanguageManager> languages = std::make_shared<c::LanguageManager>(dir.path());
    v::AppearanceOfMarkFactoryPtr appearances = std::make_shared<v::AppearanceOfMarkFactory>();
    std::shared_ptr<m::MarkFactoryConfig> factory() const {
        return std::make_shared<m::MarkFactoryConfig>(appearances, languages);
    }
};

// The defaults of the constructors (u_MarkPointTemplateConfig.pas:90-103 and the twins).
void expectPointDefaults(const m::MarkTemplatePointPtr& t) {
    EXPECT_EQ(t->category()->name(), u""_s);
    EXPECT_EQ(t->captionAppearance()->textColor(), 0xA6FFFF00U);
    EXPECT_EQ(t->captionAppearance()->textBgColor(), 0xA6000000U);
    EXPECT_EQ(t->captionAppearance()->fontSize(), 11);
    EXPECT_EQ(t->iconAppearance()->picName(), u""_s);
    EXPECT_EQ(t->iconAppearance()->markerSize(), 32);
}
void expectLineDefaults(const m::MarkTemplateLinePtr& t) {
    EXPECT_EQ(t->category()->name(), u""_s);
    EXPECT_EQ(t->lineAppearance()->lineColor(), 0xA6FF0000U);
    EXPECT_EQ(t->lineAppearance()->lineWidth(), 2);
}
void expectPolyDefaults(const m::MarkTemplatePolyPtr& t) {
    EXPECT_EQ(t->category()->name(), u""_s);
    EXPECT_EQ(t->borderAppearance()->lineColor(), 0xA6000000U);
    EXPECT_EQ(t->borderAppearance()->lineWidth(), 2);
    EXPECT_EQ(t->fillAppearance()->fillColor(), 0x33FFFFFFU);
}

} // namespace

// u_MarkFactoryConfig.pas:66-73 (the three sub-items), u_MarkTemplateConfigBase.pas:67 (the 'Name' slot of
// the generator), the DoRead / DoWrite of the three configs: the real file is read and written back
// unchanged.
TEST(MarkTemplateConfigs, FixtureSectionsRoundTripByteIdentical) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    Env env;
    const auto root = loadFixture();
    const QByteArray original = root->file().toBytes();
    const auto factory = env.factory();
    factory->readConfig(root);
    expectPointDefaults(factory->pointTemplateConfig()->defaultTemplate());
    expectLineDefaults(factory->lineTemplateConfig()->defaultTemplate());
    expectPolyDefaults(factory->polyTemplateConfig()->defaultTemplate());
    factory->writeConfig(root);
    EXPECT_EQ(root->file().toBytes(), original);
}

// A missing section reads as the constructor defaults (nil provider, :135-138 of the point unit), and the
// defaults are written under their sub-items, the generator's sub-section first (the inherited call).
TEST(MarkTemplateConfigs, AbsentSectionsGiveDefaultsAndAreWrittenWithNameSections) {
    Env env;
    const auto root = wrapText("");
    const auto factory = env.factory();
    factory->readConfig(root);
    expectPointDefaults(factory->pointTemplateConfig()->defaultTemplate());
    expectLineDefaults(factory->lineTemplateConfig()->defaultTemplate());
    expectPolyDefaults(factory->polyTemplateConfig()->defaultTemplate());
    factory->writeConfig(root);
    EXPECT_EQ(root->file().toBytes(),
              "[MarkNewPoint_Name]\r\nFormatString_=Placemark %0:d\r\nCounter=0\r\n\r\n"
              "[MarkNewPoint]\r\nCategoryName=\r\nTextColorHex=$A6FFFF00\r\nShadowColorHex=$A6000000\r\n"
              "FontSize=11\r\nIconName=\r\nIconSize=32\r\n\r\n"
              "[MarkNewLine_Name]\r\nFormatString_=Path %0:d\r\nCounter=0\r\n\r\n"
              "[MarkNewLine]\r\nCategoryName=\r\nLineColorHex=$A6FF0000\r\nLineWidth=2\r\n\r\n"
              "[MarkNewPoly_Name]\r\nFormatString_=Polygon %0:d\r\nCounter=0\r\n\r\n"
              "[MarkNewPoly]\r\nCategoryName=\r\nLineColorHex=$A6000000\r\nLineWidth=2\r\n"
              "FillColorHex=$33FFFFFF\r\n");
}

// Non-default values from a hand-made file, one row per kind; each row also proves the factory wires the
// section to the right config (the other two keep their defaults).
TEST(MarkTemplateConfigs, NonDefaultValuesAreReadPerKind) {
    struct Row {
        const char* label;
        QString section;
        QByteArray ini;
        std::function<void(m::MarkFactoryConfig&)> check;
    };
    const std::vector<Row> rows = {
        {"point", u"MarkNewPoint"_s,
         "[MarkNewPoint_Name]\r\nFormatString=Точка %0:d\r\nCounter=7\r\n\r\n"
         "[MarkNewPoint]\r\nCategoryName=Город\r\nTextColorHex=$11223344\r\nShadowColorHex=$55667788\r\n"
         "FontSize=17\r\nIconName=x.png\r\nIconSize=48\r\n",
         [](m::MarkFactoryConfig& f) {
             const auto t = f.pointTemplateConfig()->defaultTemplate();
             EXPECT_EQ(t->category()->name(), u"Город"_s);
             EXPECT_EQ(t->captionAppearance()->textColor(), 0x11223344U);
             EXPECT_EQ(t->captionAppearance()->textBgColor(), 0x55667788U);
             EXPECT_EQ(t->captionAppearance()->fontSize(), 17);
             EXPECT_EQ(t->iconAppearance()->picName(), u"x.png"_s);
             EXPECT_EQ(t->iconAppearance()->markerSize(), 48);
             EXPECT_EQ(f.pointTemplateConfig()->nameGenerator()->counter(), 7);
             EXPECT_EQ(f.pointTemplateConfig()->nameGenerator()->formatString()->value(), u"Точка %0:d"_s);
             expectLineDefaults(f.lineTemplateConfig()->defaultTemplate());
             expectPolyDefaults(f.polyTemplateConfig()->defaultTemplate());
         }},
        {"line", u"MarkNewLine"_s,
         "[MarkNewLine_Name]\r\nFormatString=Путь %0:d\r\nCounter=3\r\n\r\n"
         "[MarkNewLine]\r\nCategoryName=Пути\r\nLineColorHex=$80102030\r\nLineWidth=9\r\n",
         [](m::MarkFactoryConfig& f) {
             const auto t = f.lineTemplateConfig()->defaultTemplate();
             EXPECT_EQ(t->category()->name(), u"Пути"_s);
             EXPECT_EQ(t->lineAppearance()->lineColor(), 0x80102030U);
             EXPECT_EQ(t->lineAppearance()->lineWidth(), 9);
             EXPECT_EQ(f.lineTemplateConfig()->nameGenerator()->counter(), 3);
             expectPointDefaults(f.pointTemplateConfig()->defaultTemplate());
             expectPolyDefaults(f.polyTemplateConfig()->defaultTemplate());
         }},
        {"poly", u"MarkNewPoly"_s,
         "[MarkNewPoly_Name]\r\nFormatString=Область %0:d\r\nCounter=4\r\n\r\n"
         "[MarkNewPoly]\r\nCategoryName=Области\r\nLineColorHex=$01020304\r\nLineWidth=5\r\n"
         "FillColorHex=$0A0B0C0D\r\n",
         [](m::MarkFactoryConfig& f) {
             const auto t = f.polyTemplateConfig()->defaultTemplate();
             EXPECT_EQ(t->category()->name(), u"Области"_s);
             EXPECT_EQ(t->borderAppearance()->lineColor(), 0x01020304U);
             EXPECT_EQ(t->borderAppearance()->lineWidth(), 5);
             EXPECT_EQ(t->fillAppearance()->fillColor(), 0x0A0B0C0DU);
             EXPECT_EQ(f.polyTemplateConfig()->nameGenerator()->counter(), 4);
             expectPointDefaults(f.pointTemplateConfig()->defaultTemplate());
             expectLineDefaults(f.lineTemplateConfig()->defaultTemplate());
         }},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        Env env;
        const auto factory = env.factory();
        const auto root = wrapText(row.ini);
        factory->readConfig(root);
        row.check(*factory);
        // Complete input: writing the kind's config back to its section changes no byte.
        const QByteArray before = root->file().toBytes();
        const std::shared_ptr<c::ConfigElement> element =
            row.section == u"MarkNewPoint"_s
                ? std::shared_ptr<c::ConfigElement>(factory->pointTemplateConfig())
            : row.section == u"MarkNewLine"_s
                ? std::shared_ptr<c::ConfigElement>(factory->lineTemplateConfig())
                : std::shared_ptr<c::ConfigElement>(factory->polyTemplateConfig());
        element->writeConfig(root->orCreateSubItem(row.section));
        EXPECT_EQ(root->file().toBytes(), before);
    }
}

// SetDefaultTemplate (u_MarkPointTemplateConfig.pas:167-182 and the twins): a nil value is ignored, a value
// that IsSame as the current one is not stored and not notified, a different one is stored and notified once.
template <class Config, class TplPtr>
void checkSetDefault(Config& config, const TplPtr& sameAsDefault, const TplPtr& different) {
    int notified = 0;
    const auto subscription = config.changed().subscribe([&notified] { ++notified; });
    const TplPtr original = config.defaultTemplate();

    config.setDefaultTemplate(nullptr);
    EXPECT_EQ(config.defaultTemplate(), original);
    EXPECT_EQ(notified, 0);

    config.setDefaultTemplate(sameAsDefault);
    EXPECT_EQ(config.defaultTemplate(), original); // the new object is not stored
    EXPECT_EQ(notified, 0);

    config.setDefaultTemplate(different);
    EXPECT_EQ(config.defaultTemplate(), different);
    EXPECT_EQ(notified, 1);
}

TEST(MarkTemplateConfigs, SetDefaultTemplateStoresOnlyAChange) {
    Env env;
    const auto factory = env.factory();
    const v::IAppearanceOfMarkFactory& a = *env.appearances;
    const auto cat = std::make_shared<const v::Category>(u"Город"_s);

    auto point = factory->pointTemplateConfig();
    checkSetDefault(*point, point->createTemplate(point->defaultTemplate()->appearance(), nullptr),
                    point->createTemplate(
                        a.createPointAppearance(0xFF000001U, 0xFF000002U, 12, u"b.png"_s, nullptr, 33), cat));
    auto line = factory->lineTemplateConfig();
    checkSetDefault(*line, line->createTemplate(line->defaultTemplate()->appearance(), nullptr),
                    line->createTemplate(a.createLineAppearance(0xFF000003U, 4), nullptr));
    auto poly = factory->polyTemplateConfig();
    checkSetDefault(*poly, poly->createTemplate(poly->defaultTemplate()->appearance(), nullptr),
                    poly->createTemplate(a.createPolygonAppearance(0xFF000004U, 5, 0xFF000005U), nullptr));
}

// DoReadConfig (u_MarkPointTemplateConfig.pas:126-146): the new template is offered to SetDefaultTemplate, so
// reading equal values notifies nothing, a real change notifies once, and a nil provider changes nothing (and
// does not hang on the non-recursive element lock).
TEST(MarkTemplateConfigs, ReadConfigNotifiesOnlyForAChangedDefault) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    Env env;
    const auto factory = env.factory();
    const auto point = factory->pointTemplateConfig();
    int notified = 0;
    const auto subscription = point->changed().subscribe([&notified] { ++notified; });

    point->readConfig(nullptr);
    EXPECT_EQ(notified, 0);
    point->readConfig(loadFixture()->subItem(u"MarkNewPoint"_s));
    EXPECT_EQ(notified, 0);
    point->readConfig(wrapText("[S]\r\nFontSize=19\r\n")->subItem(u"S"_s));
    EXPECT_EQ(notified, 1);
    EXPECT_EQ(point->defaultTemplate()->captionAppearance()->fontSize(), 19);
    EXPECT_EQ(point->defaultTemplate()->captionAppearance()->textColor(),
              0xA6FFFF00U); // kept from the default
}

// u_MarkFactoryConfig.pas:67 adds the kind config with the flags on (its change notifies the factory config
// once), u_MarkTemplateConfigBase.pas:67 adds the generator with the flags off (newName notifies neither).
TEST(MarkTemplateConfigs, KindConfigChangeNotifiesFactoryButNameGeneratorDoesNot) {
    Env env;
    const auto factory = env.factory();
    const auto point = factory->pointTemplateConfig();
    int kind = 0;
    int whole = 0;
    const auto s1 = point->changed().subscribe([&kind] { ++kind; });
    const auto s2 = factory->changed().subscribe([&whole] { ++whole; });

    point->setDefaultTemplate(point->createTemplate(
        env.appearances->createPointAppearance(0xFF000001U, 0xFF000002U, 12, u"b.png"_s, nullptr, 33),
        nullptr));
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(whole, 1);

    EXPECT_EQ(point->nameGenerator()->newName(), u"Placemark 0"_s);
    EXPECT_EQ(kind, 1);
    EXPECT_EQ(whole, 1);
}

// CreateTemplate (u_MarkPointTemplateConfig.pas:106-124): a nil category becomes the empty-named one, a given
// one is kept; every template shares the config's name generator (u_MarkTemplateConfigBase.pas:62-67,
// GetNameGenerator :70-73).
TEST(MarkTemplateConfigs, CreateTemplateUsesEmptyCategoryAndTheSharedNameGenerator) {
    Env env;
    const auto factory = env.factory();
    const auto point = factory->pointTemplateConfig();
    const auto app = point->defaultTemplate()->appearance();

    const auto plain = point->createTemplate(app, nullptr);
    ASSERT_NE(plain->category(), nullptr);
    EXPECT_EQ(plain->category()->name(), u""_s);
    EXPECT_EQ(point->createTemplate(app, std::make_shared<const v::Category>(u"Город"_s))->category()->name(),
              u"Город"_s);

    EXPECT_EQ(plain->newName(), u"Placemark 0"_s);
    EXPECT_EQ(point->defaultTemplate()->newName(), u"Placemark 1"_s);
    EXPECT_EQ(point->nameGenerator()->counter(), 2);
    EXPECT_EQ(factory->lineTemplateConfig()->defaultTemplate()->newName(), u"Path 0"_s);
    EXPECT_EQ(factory->polyTemplateConfig()->defaultTemplate()->newName(), u"Polygon 0"_s);
    EXPECT_EQ(point->nameGenerator()->counter(), 2); // the generators are separate
}

// Not in legacy: a nil appearance factory is an access violation in the legacy constructors (a nil language
// manager: not traced) (u_MarkPointTemplateConfig.pas:91, outside any handler); the port refuses them.
TEST(MarkTemplateConfigs, NilArgumentsAreRefusedAtConstruction) {
    Env env;
    EXPECT_THROW(m::MarkFactoryConfig(nullptr, env.languages), std::invalid_argument);
    EXPECT_THROW(m::MarkFactoryConfig(env.appearances, nullptr), std::invalid_argument);
    EXPECT_THROW(m::MarkPointTemplateConfig(nullptr, env.languages), std::invalid_argument);
    EXPECT_THROW(m::MarkLineTemplateConfig(env.appearances, nullptr), std::invalid_argument);
    EXPECT_THROW(m::MarkPolyTemplateConfig(nullptr, env.languages), std::invalid_argument);
}
