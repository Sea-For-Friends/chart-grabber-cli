// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-004, FEAT-NET-014. Ports Test/u_PascalScriptUrlTemplate_Test.pas (TestUrlTemplate,
// every check verbatim, in order) and u_PascalScriptUrlTemplate.pas; docs/specs/maptype.md 2.8, 7.1, 7.3.
#include "cg/core/config_provider_ini.h"
#include "cg/core/language_manager.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/url_template.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using cg::maptype::TileDownloadRequestBuilderConfig;
using cg::maptype::TileDownloadRequestBuilderConfigStatic;
using cg::maptype::UrlTemplate;
using cg::maptype::UrlTemplateRequest;

namespace {

cg::geo::ProjectionSetPtr projectionSet(int epsg) {
    auto datums = std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{});
    cg::geo::ProjectionSetFactory factory{cg::geo::HashSeed{}, datums};
    return factory.byCode(epsg, cg::geo::kTileSplitQuadrate256x256);
}

class UrlTemplateTest : public ::testing::Test {
protected:
    UrlTemplateTest()
        : lang(std::make_shared<cg::core::LanguageManager>(tmp.path())),
          config(std::make_shared<TileDownloadRequestBuilderConfig>(
              std::make_shared<const TileDownloadRequestBuilderConfigStatic>())),
          set(projectionSet(cg::geo::kProjEpsgGoogle)) {}

    QTemporaryDir tmp;
    std::shared_ptr<cg::core::LanguageManager> lang;
    std::shared_ptr<TileDownloadRequestBuilderConfig> config;
    cg::geo::ProjectionSetPtr set;
};

constexpr int kZoom = 18;
const QPoint kTile{200, 300};

TEST_F(UrlTemplateTest, TestUrlTemplate) {
    UrlTemplate t(lang, set, config);
    const UrlTemplateRequest request{kTile, kZoom, nullptr};

    config->setUrlBase("http://{s}.example.com/{z}/{x}/{y}.png");
    config->setServerNames("n1, n2 ");
    QString url = t.render(request);
    EXPECT_TRUE(url == u"http://n1.example.com/18/200/300.png"_s ||
                url == u"http://n2.example.com/18/200/300.png"_s)
        << url.toStdString();

    config->setUrlBase("http://{a,b,c,d}.example.com/{z}/{x}/{y}.png");
    config->setServerNames("");
    url = t.render(request);
    EXPECT_TRUE(
        url == u"http://a.example.com/18/200/300.png"_s || url == u"http://b.example.com/18/200/300.png"_s ||
        url == u"http://c.example.com/18/200/300.png"_s || url == u"http://d.example.com/18/200/300.png"_s)
        << url.toStdString();

    config->setUrlBase("http://example.com/{z-1}/{x*2}/{y/2}.png");
    config->setServerNames("");
    EXPECT_EQ(t.render(request), u"http://example.com/17/400/150.png"_s);

    config->setUrlBase("http://example.com/{z-1}/{x*2}/{y/2}.png");
    config->setServerNames("");
    EXPECT_EQ(t.render({kTile, kZoom + 1, nullptr}), u"http://example.com/18/400/150.png"_s);

    config->setUrlBase("http://example.com/{q}");
    EXPECT_EQ(t.render(request), u"http://example.com/000000000211203200"_s);

    config->setUrlBase("http://example.com/p?bbox={bbox}");
    config->setServerNames("");
    EXPECT_EQ(
        t.render(request),
        u"http://example.com/p?bbox=-20006933.53147520,19991493.25176160,-20006780.65741860,19991646.12581810"_s);

    config->setUrlBase("http://example.com/{sas_path}.png");
    EXPECT_EQ(t.render(request), u"http://example.com/z19/0/x200/0/y300.png"_s);

    config->setUrlBase("http://example.com/z{z+1}/{x/1024}/x{x}/{y/1024}/y{y}.png");
    EXPECT_EQ(t.render(request), u"http://example.com/z19/0/x200/0/y300.png"_s);
}

// {s} is chosen once per ServerNames parse (L-136), with the injected Random(N); the other tags.
TEST_F(UrlTemplateTest, ServerChoiceIsMadeOncePerParse) {
    QList<qint32> asked;
    qint32 next = 1;
    UrlTemplate t(lang, set, config, [&](qint32 n) {
        asked.push_back(n);
        return next;
    });
    config->setUrlBase("{s}/{s}");
    config->setServerNames(",a, b\t,c,,");
    const UrlTemplateRequest request{kTile, kZoom, nullptr};
    // leading separator: the first name is "" (L-139); list = "", a, b, c
    EXPECT_EQ(t.render(request), u"a/a"_s);
    EXPECT_EQ(t.render(request), u"a/a"_s);
    next = 2;
    EXPECT_EQ(t.render(request), u"a/a"_s); // config unchanged: still the first pick
    EXPECT_EQ(asked, (QList<qint32>{4}));
    config->setServerNames("a,b,c");
    EXPECT_EQ(t.render(request), u"c/c"_s);
    config->setServerNames("");
    EXPECT_EQ(t.render(request), u"c/c"_s); // an empty list keeps the old host (L-138)
}

struct EdgeRow {
    const char* tmpl;
    const char16_t* want;
};

// 7.3 parse edge cases (hand-derived from the Pascal), tile 200/300 zoom 18.
TEST_F(UrlTemplateTest, ParseEdgeCases) {
    const EdgeRow rows[] = {
        {"{}", u"{}"},           // the empty tag is the text entry: literal
        {"a{foo}b", u"a{foo}b"}, // unknown tag: literal, name case kept
        {"{{x}}", u"{{x}}"},     // tag "{x" is unknown
        {"{{z-1}}", u"}"},       // tag "{z-1" is math, fails, renders ""
        {"{x", u"{x"},           // unmatched {
        {"x}{y", u"x}{y"},       // } before { is text
        {"{X}{Y}{Z}", u"200300"
                      "18"}, // case-insensitive
        {"{-y}", u"261843"}, // (1 shl 18) - 1 - 300 is a name, not arithmetic
        {"{foo-bar}", u""},  // unknown tag with an operator renders "" (L-142)
        {"{z + 1}", u"18"},  // trailing text after the value of z (L-137)
        {"{x*2abc}-{X*2}", u"400-400"},
        {"{sas_path}", u"z19/0/x200/0/y300"},
        {"{lang}|{ver}", u"en|"}, // no language file: ""; no version: ""
        {"", u""},
        {"}", u"}"},
    };
    UrlTemplate t(lang, set, config);
    for (const EdgeRow& r : rows) {
        SCOPED_TRACE(r.tmpl);
        config->setUrlBase(r.tmpl);
        EXPECT_EQ(t.render({kTile, kZoom, nullptr}), QString::fromUtf16(r.want));
    }
}

TEST_F(UrlTemplateTest, QuadkeyBboxTimestampVersionLanguage) {
    UrlTemplate t(lang, set, config);
    config->setUrlBase("[{q}]");
    EXPECT_EQ(t.render({{0, 0}, 0, nullptr}), u"[]"_s); // zoom 0: ""
    EXPECT_EQ(t.render({{1, 1}, 1, nullptr}), u"[3]"_s);
    EXPECT_EQ(t.render({{2, 1}, 2, nullptr}), u"[12]"_s);
    config->setUrlBase("{ver}");
    const cg::storage::MapVersionInfo ver(u"v 7"_s, 1);
    EXPECT_EQ(t.render({kTile, kZoom, &ver}), u"v 7"_s);
    config->setUrlBase("{timestamp}");
    bool ok = false;
    const qint64 ts = t.render({kTile, kZoom, nullptr}).toLongLong(&ok);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(static_cast<double>(ts), static_cast<double>(QDateTime::currentSecsSinceEpoch()), 5.0);
    config->setUrlBase("{lang}");
    EXPECT_EQ(t.render({kTile, kZoom, nullptr}), lang->currentLanguageCode());
    // Geographic type = square 360x360 pyramid (u_ProjectionTypeGeographic.pas:59-60: lon = (x-0.5)*360,
    // lat = -(y-0.5)*360), used unconverted (u_PascalScriptUrlTemplate.pas:286-316), Left,Bottom,Right,Top.
    // Zoom 1 tile (0,0): lon -180..0, lat 180..0.
    UrlTemplate geo(lang, projectionSet(cg::geo::kProjEpsgGeographic), config);
    config->setUrlBase("{bbox}");
    EXPECT_EQ(geo.render({{0, 0}, 1, nullptr}).toStdString(),
              "-180.00000000,0.00000000,0.00000000,180.00000000");
    // Zoom 3 tile (4,1): x 0.5..0.625 -> lon 0..45; y 0.125..0.25 -> lat 135..90
    EXPECT_EQ(geo.render({{4, 1}, 3, nullptr}).toStdString(),
              "0.00000000,90.00000000,45.00000000,135.00000000");
}

// templateToUrl renders the given text for the stored request and leaves it cached (L-143).
TEST_F(UrlTemplateTest, TemplateToUrl) {
    UrlTemplate t(lang, set, config);
    config->setUrlBase("U{x}");
    EXPECT_EQ(t.render({kTile, kZoom, nullptr}), u"U200"_s);
    EXPECT_EQ(t.templateToUrl("S{y}"), u"S300"_s);
    EXPECT_EQ(t.render({kTile, kZoom, nullptr}), u"S300"_s); // config unchanged: URLBase is not re-parsed
    config->setUrlBase("V{x}");
    EXPECT_EQ(t.render({kTile, kZoom, nullptr}), u"V200"_s);
}

// Corpus: every DefURLBase of the script-less stock ZMPs (164) renders with no unknown-tag literal.
TEST_F(UrlTemplateTest, CorpusScriptlessDefUrlBaseRendersWithoutLeftoverTags) {
    CG_SKIP_IF_NO_CORPUS();
    int rendered = 0;
    QStringList leftover;
    QDirIterator it(cg::maptype::test::fixtureDir() + u"/zmp/sas.maps"_s, {u"params.txt"_s}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString dir = QFileInfo(path).absolutePath();
        if (QFileInfo::exists(dir + u"/GetUrlScript.txt"_s)) {
            continue;
        }
        const auto ini = cg::core::ConfigProviderIni::open(path);
        ASSERT_TRUE(ini.has_value()) << path.toStdString();
        const auto p = (*ini)->subItem(u"PARAMS");
        ASSERT_NE(p, nullptr);
        const QString base = p->readString(u"DefURLBase", QString());
        auto cfg = std::make_shared<TileDownloadRequestBuilderConfig>(
            std::make_shared<const TileDownloadRequestBuilderConfigStatic>(
                TileDownloadRequestBuilderConfigStatic{
                    base.toUtf8(), p->readString(u"ServerNames", QString()).toUtf8(), {}, false, {}}));
        UrlTemplate t(lang, set, cfg);
        const QString url = t.render({kTile, 10, nullptr});
        ++rendered;
        if (url.contains(u'{') || url.contains(u'}')) {
            leftover.push_back(QFileInfo(dir).fileName());
        }
    }
    EXPECT_EQ(rendered, 164);
    EXPECT_EQ(leftover, QStringList{});
}

} // namespace
