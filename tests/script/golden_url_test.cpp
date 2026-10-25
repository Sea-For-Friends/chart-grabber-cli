// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: docs/specs/script.md §11.5, docs/backlog/T-0015D-script-golden-urls-21-maps.md. One
// parameterised test over (map, tile point): loads a hand-derived golden from
// tests/fixtures/script-goldens/<map>.golden (core::IniFile, spec 2.12), runs the map's real
// tests/fixtures/zmp/sas.maps/<map>/GetUrlScript.txt through the landed UrlScript with the fake
// host and the golden's stored inputs, and compares ResultURL/RequestHead/PostData byte for byte.
//
// How each golden was produced - by reading the script and params.txt and evaluating it by hand,
// never by running this test - is written down as INI comments in the fixture file itself and
// summarised in tests/fixtures/script-goldens/README.md. Skips apply per (map, section), not per
// whole map (docs/handoffs/T-0015D-review.md finding 4b), so the sections of a map that do work
// once a defect is fixed start being asserted on their own.
//
// PD-1 (static arrays) and PD-103 (const folding) are fixed by T-0032 - see docs/handoffs/T-0032.md
// for the corpus before/after and the belgeodesy P1/P2 ErrorText correction that fixing PD-1
// uncovered (T-0015D's derivation did not account for the array's declared low bound). No
// skipDefectFor() entry remains; the next port defect a golden run turns up gets one.
//
// A golden section may declare ExpectError=1 (optionally with an ErrorText substring to match
// against the failure's message) instead of ResultURL: the test then asserts buildRequest() fails
// rather than comparing a URL (docs/handoffs/T-0015D-review.md finding 2/4a). A section that is
// not expected-error and omits the ResultURL key fails the test - it no longer silently expects
// "" (finding 4c).
#include "cg/core/ini_file.h"
#include "cg/script/url_script.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "fixture_path.h"
#include "script_host_fake.h"

using namespace cg::script;
using cg::core::IniFile;
using cg::script::test::FakeScriptHost;

namespace {

// Test-only pass-through projection. Map 19 (Maps.by.zmp)'s golden (spec table row 19) needs an
// *identity* DefProjConverter so RoundEx sees the lon/lat inputs directly - distinct from
// script_host_fake.h's FakeProjConverter (an x1000 scale built for a different ticket's method-
// dispatch tests, script.md §11.1/§11.2), so it stays local to this file rather than growing the
// shared fake.
class IdentityProjConverter : public IProjConverter {
public:
    [[nodiscard]] DoublePoint lonLat2XY(DoublePoint p) const override { return p; }
    [[nodiscard]] DoublePoint xy2LonLat(DoublePoint p) const override { return p; }
};

// §11.5.1: every lon/lat/metre input is stored as a C99-ish "raw bits" hex literal
// (0x + 16 hex digits = the IEEE-754 double's bit pattern, big-endian order as written) so the
// golden is exact and never touches cg::geo.
double hexBitsToDouble(const QString& s) {
    QByteArray hex = s.toUtf8();
    if (hex.startsWith("0x") || hex.startsWith("0X")) {
        hex = hex.mid(2);
    }
    bool ok = false;
    const quint64 bits = hex.toULongLong(&ok, 16);
    Q_ASSERT(ok);
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

QByteArray readRawBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// The 21 maps of spec §11.5.3, path relative to both tests/fixtures/zmp/sas.maps/ (the corpus
// script) and tests/fixtures/script-goldens/ (the golden, same path + ".golden").
const QStringList& allMaps() {
    static const QStringList maps = {
        QStringLiteral("ESRI/ArcGIS.Imagery.zmp"),
        QStringLiteral("_osm/Gps_tiles.zmp"),
        QStringLiteral("Here/here.com.map.zmp"),
        QStringLiteral("Genshtab_and_others/topo_marshruty.zmp"),
        QStringLiteral("_water/river_kuban.zmp"),
        QStringLiteral("_broken_maps/Others/Geoportal/geoportal.md.cadastre.zmp"),
        QStringLiteral("_broken_maps/_low_quality_/vi-tel.ru.zmp"),
        QStringLiteral("Google/GoogleTransit.zmp"),
        QStringLiteral("Visicom.zmp"),
        QStringLiteral("_broken_maps/Local/Ukraine/mapservices.com.ua.map.zmp"),
        QStringLiteral("Bing/Bing.map.zmp"),
        QStringLiteral("Tatar.ru.zmp"),
        QStringLiteral("_other/Water_resources.zmp"),
        QStringLiteral("_broken_maps/Local/Belarus/belgeodesy.zmp"),
        QStringLiteral("_broken_maps/Others/Geoportal/RoscosmGeo.zmp"),
        QStringLiteral("Wikimapia/Wiki.zmp"),
        QStringLiteral("Lantmateriet.se.zmp"),
        QStringLiteral("_broken_maps/Rosreestr/rosreestr_cadastr.zmp"),
        QStringLiteral("Maps.by.zmp"),
        QStringLiteral("megafonpro.zmp"),
        QStringLiteral("_historic/Other/zoomify_usage_example.zmp"),
    };
    return maps;
}

// Skip reason for one (map, section) pair, or empty if the section is asserted. Applied per
// section (finding 4b), not per map. PD-1 (belgeodesy/megafonpro) and PD-103 (zoomify) are fixed
// by T-0032 - every section of every map is asserted now; kept as a function (not deleted) so the
// next port defect a golden run turns up has somewhere to go, same as corpus_run_test.cpp's table.
QString skipDefectFor(const QString& map, const QString& section) {
    (void)map;
    (void)section;
    return QString();
}

struct GoldenCase {
    QString map;
    QString section;
    QString skipDefect;
    bool expectError = false;
};

std::vector<GoldenCase> buildCases() {
    std::vector<GoldenCase> cases;
    const QString goldensDir = QDir(cgScriptFixturesDir()).filePath(QStringLiteral("script-goldens"));
    for (const QString& map : allMaps()) {
        const QString goldenPath = QDir(goldensDir).filePath(map + QStringLiteral(".golden"));
        auto ini = IniFile::fromFile(goldenPath);
        if (!ini.has_value()) {
            ADD_FAILURE() << "cannot load " << goldenPath.toStdString();
            continue;
        }
        for (const QString& section : ini->sectionNames()) {
            const QString skip = skipDefectFor(map, section);
            const bool expectError = ini->readBool(section, QStringLiteral("ExpectError"), false);
            cases.push_back(GoldenCase{map, section, skip, expectError});
        }
    }
    return cases;
}

std::string caseName(const ::testing::TestParamInfo<GoldenCase>& info) {
    QString safe = info.param.map;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    safe.replace(QLatin1Char('.'), QLatin1Char('_'));
    safe.replace(QLatin1Char('-'), QLatin1Char('_'));
    return (safe + QStringLiteral("_") + info.param.section).toStdString();
}

} // namespace

class GoldenUrlTest : public ::testing::TestWithParam<GoldenCase> {};

TEST_P(GoldenUrlTest, MatchesHandDerivedGolden) {
    CG_SKIP_IF_NO_CORPUS();
    const GoldenCase& c = GetParam();
    if (!c.skipDefect.isEmpty()) {
        GTEST_SKIP() << c.skipDefect.toStdString() << ": " << c.map.toStdString() << " ["
                     << c.section.toStdString()
                     << "] is blocked by a landed port defect (docs/handoffs/T-0015D.md), not "
                        "fixed here (0 production LOC).";
    }

    const QString fixturesDir = cgScriptFixturesDir();
    const QString scriptPath =
        QDir(fixturesDir)
            .filePath(QStringLiteral("zmp/sas.maps/") + c.map + QStringLiteral("/GetUrlScript.txt"));
    const QByteArray source = readRawBytes(scriptPath);
    ASSERT_FALSE(source.isEmpty()) << scriptPath.toStdString();

    const QString goldenPath =
        QDir(fixturesDir).filePath(QStringLiteral("script-goldens/") + c.map + QStringLiteral(".golden"));
    auto iniR = IniFile::fromFile(goldenPath);
    ASSERT_TRUE(iniR.has_value()) << goldenPath.toStdString();
    const IniFile& ini = *iniR;

    ScriptVars vars;
    vars.getX = ini.readInt(c.section, QStringLiteral("GetX"), 0);
    vars.getY = ini.readInt(c.section, QStringLiteral("GetY"), 0);
    vars.getZ = ini.readInt(c.section, QStringLiteral("GetZ"), 1);
    vars.getUrlBase = ini.readString(c.section, QStringLiteral("GetURLBase")).toUtf8();
    vars.version = ini.readString(c.section, QStringLiteral("Version")).toUtf8();
    vars.lang = ini.readString(c.section, QStringLiteral("Lang")).toUtf8();
    vars.requestHead = ini.readString(c.section, QStringLiteral("RequestHead")).toUtf8();
    vars.getLlon = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetLLon")));
    vars.getTLat = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetTLat")));
    vars.getRLon = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetRLon")));
    vars.getBLat = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetBLat")));
    vars.getLMetr = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetLMetr")));
    vars.getTMetr = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetTMetr")));
    vars.getRMetr = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetRMetr")));
    vars.getBMetr = hexBitsToDouble(ini.readString(c.section, QStringLiteral("GetBMetr")));
    if (!ini.readBool(c.section, QStringLiteral("DefProjConverterNil"), false)) {
        vars.defProjConverter = std::make_shared<const IdentityProjConverter>();
    }

    FakeScriptHost host;
    auto script = UrlScript::create(source, c.map, host);
    ASSERT_TRUE(script.has_value()) << script.error().what().toStdString();
    auto result = (*script)->buildRequest(vars);

    if (c.expectError) {
        ASSERT_FALSE(result.has_value()) << c.map.toStdString() << " [" << c.section.toStdString()
                                         << "] expected a script error but buildRequest() succeeded with url="
                                         << result->url.toStdString();
        if (ini.hasKey(c.section, QStringLiteral("ErrorText"))) {
            const QString expected = ini.readString(c.section, QStringLiteral("ErrorText"));
            EXPECT_TRUE(result.error().what().contains(expected))
                << c.map.toStdString() << " [" << c.section.toStdString() << "] error was \""
                << result.error().what().toStdString() << "\", expected it to contain \""
                << expected.toStdString() << "\"";
        }
        return;
    }

    ASSERT_TRUE(result.has_value()) << result.error().what().toStdString();
    ASSERT_TRUE(ini.hasKey(c.section, QStringLiteral("ResultURL")))
        << c.map.toStdString() << " [" << c.section.toStdString() << "] has no ResultURL key";

    EXPECT_EQ(result->url, ini.readString(c.section, QStringLiteral("ResultURL")).toUtf8())
        << c.map.toStdString() << " [" << c.section.toStdString() << "]";
    EXPECT_EQ(result->requestHead, ini.readString(c.section, QStringLiteral("RequestHead")).toUtf8())
        << c.map.toStdString() << " [" << c.section.toStdString() << "]";
    EXPECT_EQ(result->postData, ini.readString(c.section, QStringLiteral("PostData")).toUtf8())
        << c.map.toStdString() << " [" << c.section.toStdString() << "]";
}

INSTANTIATE_TEST_SUITE_P(Maps, GoldenUrlTest, ::testing::ValuesIn(buildCases()), caseName);
