// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: T-0280 (NEW tests, no production code, no FEAT id): the URL differential between a ZMP and its
// *.source.json twin. Part 1: the 24 sources of the base chart set (template-only: no script). Part 2: the
// 4 Navionics maps (a PascalScript token script against its JavaScript port). Fixtures and their origin:
// tests/fixtures/maptype/url-differential/README.md. The report is docs/status/url-differential.md.
//
// Both sides are built the way TileDownloadSubsystem builds a factory (tile_download_subsystem.cpp, the
// GetUrlScript.js / GetUrlScript.txt choice) from the SAME loader (ZmpInfoSet), so a difference is a
// difference of the twin files, not of the harness. The token service is a scripted in-process downloader
// (no socket, no network): every request the script makes is recorded and answered from a queue.
#include "cg/maptype/tile_download_request_builder_factory_js.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "maptype_test_util.h"
#include "request_builder_test_util.h"

using namespace Qt::StringLiterals;
using cg::maptype::ITileDownloadRequestBuilderFactory;
using cg::maptype::ITileDownloadRequestBuilderPtr;
using cg::maptype::IZmpInfoPtr;
using cg::maptype::TileDownloadRequestBuilderConfig;
using cg::maptype::TileDownloadRequestBuilderConfigStatic;
using cg::maptype::TileDownloadRequestBuilderFactoryJs;
using cg::maptype::test::FactoryEnv;
using cg::maptype::test::okReply;
using cg::maptype::test::ScriptedDownloader;
using cg::maptype::test::tileRequest;
using cg::maptype::test::ZmpEnv;
using cg::net::LastResponseInfo;

namespace {

const std::shared_ptr<LastResponseInfo> kNoInfo;

struct Tile {
    int x;
    int y;
    int z;
};

// A ZMP tree and a JSON tree, each loaded by ZmpInfoSet; looked up by name without the source suffix.
class Catalogue {
public:
    Catalogue(const QString& zmpRoot, const QString& jsonRoot) {
        QStringList errors;
        zmpSet_ = env_.loadAll(zmpRoot, errors);
        jsonSet_ = env_.loadAll(jsonRoot, errors);
        errors_ = errors;
        index(*zmpSet_, u".zmp"_s, zmp_);
        index(*jsonSet_, u".source.json"_s, json_);
    }
    [[nodiscard]] IZmpInfoPtr zmp(const QString& stem) const { return find(zmp_, stem); }
    [[nodiscard]] IZmpInfoPtr json(const QString& stem) const { return find(json_, stem); }
    [[nodiscard]] const QStringList& errors() const { return errors_; }
    [[nodiscard]] std::size_t zmpCount() const { return zmp_.size(); }
    [[nodiscard]] std::size_t jsonCount() const { return json_.size(); }

private:
    using Map = std::map<QString, IZmpInfoPtr>;
    void index(const cg::maptype::ZmpInfoSet& set, const QString& suffix, Map& out) const {
        for (const QUuid& guid : set.guids()) {
            const IZmpInfoPtr info = set.zmpByGuid(guid);
            QString name = info->fileName();
            EXPECT_TRUE(name.endsWith(suffix, Qt::CaseInsensitive)) << name.toStdString();
            name.chop(suffix.size());
            out[name] = info;
        }
    }
    static IZmpInfoPtr find(const Map& m, const QString& stem) {
        const auto it = m.find(stem);
        return it == m.end() ? nullptr : it->second;
    }

    ZmpEnv env_;
    std::unique_ptr<cg::maptype::ZmpInfoSet> zmpSet_;
    std::unique_ptr<cg::maptype::ZmpInfoSet> jsonSet_;
    QStringList errors_;
    Map zmp_;
    Map json_;
};

QString urlDir(const char* part) {
    return cg::maptype::test::fixtureDir() + u"/maptype/url-differential/"_s + QString::fromLatin1(part);
}

// One request builder made from one source the way the subsystem makes it. `servers` replaces the config's
// ServerNames (the `{s}` pick is random per builder; one value makes the two sides comparable).
struct Side {
    Side(const IZmpInfoPtr& zmp, const QByteArray& servers) {
        auto statics = std::make_shared<TileDownloadRequestBuilderConfigStatic>(
            *zmp->tileDownloadRequestBuilderConfig());
        if (!servers.isEmpty()) {
            statics->serverNames = servers;
        }
        env.projectionSet = zmp->projectionSet();
        auto config = std::make_shared<TileDownloadRequestBuilderConfig>(statics);
        const QByteArray js = zmp->dataProvider()->binary(u"GetUrlScript.js"_s);
        usesJs = !js.isEmpty();
        if (usesJs) {
            factory = std::make_shared<TileDownloadRequestBuilderFactoryJs>(
                zmp->fileName(), js, config, env.downloaderConfig, env.projectionSet, env.checker, env.lang);
        } else {
            factory =
                env.factory(zmp->dataProvider()->binary(u"GetUrlScript.txt"_s), config, zmp->fileName());
        }
        builder = factory->buildRequestBuilder(downloader);
    }

    FactoryEnv env;
    std::shared_ptr<ScriptedDownloader> downloader = std::make_shared<ScriptedDownloader>();
    std::shared_ptr<ITileDownloadRequestBuilderFactory> factory;
    ITileDownloadRequestBuilderPtr builder;
    bool usesJs = false;
};

// What a builder answered for one tile: "no request", an error text, or the request's fields.
struct Answer {
    std::string kind; // "none", "error", "request"
    std::string url;
    std::string header;
    std::string post;
    int method = 0;
    bool operator==(const Answer&) const = default;
};

std::ostream& operator<<(std::ostream& os, const Answer& a) {
    return os << "{" << a.kind << " url=" << a.url << " header=[" << a.header << "] post=[" << a.post
              << "] method=" << a.method << "}";
}

Answer ask(Side& side, const Tile& t) {
    Answer a;
    try {
        const auto request = side.builder->buildRequest(tileRequest(t.x, t.y, t.z), kNoInfo, {});
        if (!request) {
            a.kind = "none";
            return a;
        }
        a.kind = "request";
        a.url = request->url.toStdString();
        a.header = request->requestHeader.toStdString();
        a.post = request->postData.toStdString();
        a.method = static_cast<int>(request->method);
    } catch (const std::exception& e) {
        a.kind = "error";
        a.url = e.what();
    }
    return a;
}

// The fixed grid: zooms 0-18, the four golden points of tests/script/golden_url_test.cpp
// (tests/fixtures/script-goldens/README.md "The four tile points"), and per zoom the first, the middle and
// the last tile of the grid.
std::vector<Tile> grid() {
    std::vector<Tile> tiles = {{0, 0, 1}, {1, 1, 2}, {1234, 5678, 14}, {131071, 87654, 18}};
    for (int z = 0; z <= 18; ++z) {
        const int last = (1 << z) - 1;
        tiles.push_back({0, 0, z});
        tiles.push_back({(1 << z) / 2, last / 3, z});
        tiles.push_back({last, last, z});
    }
    return tiles;
}

// --- part 1: the base set
// ------------------------------------------------------------------------------------

// The 24 sources of the plugin's data/sources at commit acdf739 (group/name, no suffix).
const std::vector<std::string>& baseStems() {
    static const std::vector<std::string> stems = {
        "coast-and-land/OpenTopoMap",
        "depth/EMODnet-Bathymetry-Europe",
        "depth/EMODnet-Bathymetry-World",
        "depth/GEBCO-Bathymetry",
        "depth/NOAA-BlueTopo-US",
        "nautical-charts/Finland-Sea-Chart-Traficom",
        "nautical-charts/NOAA-Chart-US",
        "nautical-charts/Norway-Sea-Chart-Kartverket",
        "ocean/NASA-GIBS-Sea-Ice",
        "ocean/NASA-GIBS-Sea-Surface-Temperature",
        "satellite/Esri-World-Imagery",
        "satellite/Sentinel2-Cloudless-2025",
        "satellite/USGS-Imagery-USA",
        "seamarks-and-traffic/EMODnet-Vessel-Density-Sailing",
        "seamarks-and-traffic/EMODnet-Vessel-Density",
        "seamarks-and-traffic/OpenSeaMap-Seamarks",
        "weather-now/DWD-Radar-Germany",
        "weather-now/EUMETView-GeoColour",
        "weather-now/EUMETView-H60B-Precipitation",
        "weather-now/NASA-GIBS-IMERG-Precipitation",
        "weather-now/NASA-GIBS-VIIRS-TrueColor",
        "weather-now/NOAA-nowCOAST-Clouds-Infrared",
        "weather-now/NOAA-nowCOAST-Radar-CONUS",
        "weather-now/RainViewer-Radar",
    };
    return stems;
}

const Catalogue& baseCatalogue() {
    static const Catalogue c(urlDir("base-zmp"), urlDir("base-json"));
    return c;
}

TEST(UrlDifferentialBase, BothTreesLoadWithoutErrorsAndHoldTheTwentyFourSources) {
    const Catalogue& c = baseCatalogue();
    EXPECT_TRUE(c.errors().isEmpty()) << c.errors().join(u"; "_s).toStdString();
    EXPECT_EQ(c.zmpCount(), 24U);
    EXPECT_EQ(c.jsonCount(), 24U);
    EXPECT_EQ(baseStems().size(), 24U);
}

// T-0346: the [PARAMS] NavigationWarning text of a source, the key the CLI's notice reads
// (app/job_checks.cpp).
QString navigationWarningOf(const IZmpInfoPtr& info) {
    return info->dataProvider()
        ->subItem(u"params.txt")
        ->subItem(u"PARAMS")
        ->readString(u"NavigationWarning", {});
}

TEST(UrlDifferentialBase, SevenSourcesCarryNavigationWarningOnBothSides) {
    const Catalogue& c = baseCatalogue();
    int zmpCount = 0;
    int jsonCount = 0;
    for (const std::string& stem : baseStems()) {
        const QString name = QString::fromStdString(stem);
        zmpCount += navigationWarningOf(c.zmp(name)) == u"1"_s ? 1 : 0;
        jsonCount += navigationWarningOf(c.json(name)) == u"1"_s ? 1 : 0;
    }
    EXPECT_EQ(zmpCount, 7);
    EXPECT_EQ(jsonCount, 7);
}

class UrlDifferentialBase : public ::testing::TestWithParam<std::string> {};

TEST_P(UrlDifferentialBase, ZmpAndJsonGiveTheSameConfigAndTheSameRequestOnTheGrid) {
    const Catalogue& c = baseCatalogue();
    const IZmpInfoPtr zmp = c.zmp(QString::fromStdString(GetParam()));
    const IZmpInfoPtr json = c.json(QString::fromStdString(GetParam()));
    ASSERT_NE(zmp, nullptr);
    ASSERT_NE(json, nullptr);

    EXPECT_EQ(zmp->guid(), json->guid());
    EXPECT_EQ(zmp->isLayer(), json->isLayer());
    EXPECT_EQ(zmp->version(), json->version());
    const auto zc = zmp->tileDownloadRequestBuilderConfig();
    const auto jc = json->tileDownloadRequestBuilderConfig();
    EXPECT_EQ(zc->urlBase, jc->urlBase);
    EXPECT_EQ(zc->serverNames, jc->serverNames);
    EXPECT_EQ(zc->requestHeader, jc->requestHeader);
    EXPECT_EQ(zc->isUseDownloader, jc->isUseDownloader);
    EXPECT_EQ(navigationWarningOf(zmp), navigationWarningOf(json));

    Side zmpSide(zmp, {});
    Side jsonSide(json, {});
    ASSERT_NE(zmpSide.builder, nullptr);
    ASSERT_NE(jsonSide.builder, nullptr);
    EXPECT_FALSE(zmpSide.usesJs);
    EXPECT_FALSE(jsonSide.usesJs); // none of the 24 has a script: both use the template-only builder
    for (const Tile& t : grid()) {
        const Answer fromZmp = ask(zmpSide, t);
        const Answer fromJson = ask(jsonSide, t);
        EXPECT_EQ(fromZmp.kind, "request") << t.x << "," << t.y << "," << t.z;
        EXPECT_EQ(fromZmp, fromJson) << "tile " << t.x << "," << t.y << " z" << t.z;
    }
    EXPECT_TRUE(zmpSide.downloader->requests.empty());
    EXPECT_TRUE(jsonSide.downloader->requests.empty());
}

INSTANTIATE_TEST_SUITE_P(BaseSet, UrlDifferentialBase, ::testing::ValuesIn(baseStems()),
                         [](const ::testing::TestParamInfo<std::string>& pi) {
                             std::string name = pi.param;
                             for (char& ch : name) {
                                 ch = (std::isalnum(static_cast<unsigned char>(ch)) != 0) ? ch : '_';
                             }
                             return name;
                         });

// Both sides could agree on a wrong URL; these are written from the params.txt templates and the tile
// numbering ({z} 0-based, row order of each template), not read from the class under test.
struct Golden {
    QString stem;
    Tile tile;
    const char* url;
};

TEST(UrlDifferentialBase, GoldenUrlsOfTheJsonSide) {
    const Catalogue& c = baseCatalogue();
    const std::vector<Golden> goldens = {
        {u"coast-and-land/OpenTopoMap"_s,
         {1234, 5678, 14},
         "https://a.tile.opentopomap.org/14/1234/5678.png"},
        {u"satellite/Esri-World-Imagery"_s,
         {131071, 87654, 18},
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/18/87654/131071"},
        {u"nautical-charts/Norway-Sea-Chart-Kartverket"_s,
         {1, 1, 2},
         "https://cache.kartverket.no/v1/wmts/1.0.0/sjokartraster/default/webmercator/2/1/1.png"},
        {u"ocean/NASA-GIBS-Sea-Ice"_s,
         {3, 2, 5},
         "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GHRSST_L4_MUR_Sea_Ice_Concentration/default//"
         "GoogleMapsCompatible_Level7/5/2/3.png"},
        {u"weather-now/RainViewer-Radar"_s,
         {1234, 5678, 14},
         "https://tilecache.rainviewer.com/256/14/1234/5678/2/1_1.png"},
        {u"nautical-charts/Finland-Sea-Chart-Traficom"_s,
         {1234, 5678, 14},
         "https://julkinen.traficom.fi/rasteripalvelu/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER="
         "Traficom:Merikarttasarjat%20public&STYLE=&TILEMATRIXSET=WGS84_Pseudo-Mercator&TILEMATRIX="
         "WGS84_Pseudo-Mercator:14&TILEROW=5678&TILECOL=1234&FORMAT=image/png"},
    };
    for (const Golden& g : goldens) {
        for (const IZmpInfoPtr& zmp : {c.zmp(g.stem), c.json(g.stem)}) {
            ASSERT_NE(zmp, nullptr) << g.stem.toStdString();
            Side side(zmp, {});
            const Answer a = ask(side, g.tile);
            EXPECT_EQ(a.url, g.url) << g.stem.toStdString() << " from " << zmp->fileName().toStdString();
        }
    }
}

// {bbox} is metres of EPSG:3857 with 8 decimals (RoundEx(.., 8), u_PascalScriptUrlTemplate.pas:286-318); the
// last digits depend on the float path (tile -> lon/lat -> metres), so the values written here (computed
// independently: 40075016.685578488 / 2^z per tile from -20037508.342789244) are compared to 1e-6.
TEST(UrlDifferentialBase, BboxOfTheWmsSourcesIsTheTileInMetresWithEightDecimals) {
    const Catalogue& c = baseCatalogue();
    struct Case {
        Tile tile;
        double box[4]; // west, south, east, north
    };
    const Case cases[] = {
        {{1234, 5678, 14}, {-17019162.969864205, 6146760.066580733, -17016716.984959077, 6149206.051485859}},
        {{1, 1, 2}, {-10018754.171394622, 0.0, 0.0, 10018754.171394622}},
    };
    for (const Case& cs : cases) {
        for (const IZmpInfoPtr& zmp :
             {c.zmp(u"depth/GEBCO-Bathymetry"_s), c.json(u"depth/GEBCO-Bathymetry"_s)}) {
            ASSERT_NE(zmp, nullptr);
            Side side(zmp, {});
            const std::string url = ask(side, cs.tile).url;
            const std::string key = "BBOX=";
            const std::size_t from = url.find(key) + key.size();
            const std::string numbers = url.substr(from, url.find('&', from) - from);
            QStringList parts = QString::fromStdString(numbers).split(u',');
            ASSERT_EQ(parts.size(), 4) << url;
            for (int i = 0; i < 4; ++i) {
                EXPECT_NEAR(parts[i].toDouble(), cs.box[i], 1e-6) << url;
                EXPECT_EQ(parts[i].size() - parts[i].indexOf(u'.') - 1, 8) << parts[i].toStdString();
            }
        }
    }
}

// --- part 2: Navionics ------------------------------------------------------------------------------------

const Catalogue& navionicsCatalogue() {
    static const Catalogue c(urlDir("navionics-zmp"), urlDir("navionics-json"));
    return c;
}

constexpr auto kTokenUrl = "https://maps.garmin.com/marine/api/getNavionicsTokens";
constexpr auto kTokenHeader = "Origin: https://maps.garmin.com\r\nReferer: https://maps.garmin.com/";

struct NavMap {
    QString stem;
    const char* query; // the script's cReqParams
};

const std::vector<NavMap>& navMaps() {
    static const std::vector<NavMap> maps = {
        {u"navionics"_s, "transparent=false&ugc=true&layer=0&du=1&sd=2&sa=false"},
        {u"navionics.layer"_s, "transparent=true&ugc=true&layer=0&du=1&sd=2&sa=false"},
        {u"navionics.SonarChart"_s, "transparent=false&ugc=true&layer=1&du=1&sd=2&sa=false"},
        {u"navionics.SonarChart.layer"_s, "transparent=true&ugc=true&layer=1&du=1&sd=2&sa=false"},
    };
    return maps;
}

// A token answer and what legacy's two regular expressions (greedy, '.' matches a line break, see the README
// of the fixtures) take out of it. `token` empty = the script gives no tile.
struct TokenBody {
    const char* name;
    QByteArray body;
    std::string token;
    std::string bearer;
};

std::vector<TokenBody> tokenBodies() {
    return {
        {"plain", R"({"access_token":"BEA1","configuration_token":"CFG1"})", "CFG1", "BEA1"},
        // greedy: the first pattern runs to the LAST quote of the whole answer
        {"config-first", R"({"configuration_token":"CFG2","access_token":"BEA2","expires_in":600})",
         R"(CFG2","access_token":"BEA2","expires_in)", "BEA2"},
        {"no-bearer", R"({"configuration_token":"CFG3"})", "CFG3", ""},
        {"no-token", R"({"error":"denied"})", "", ""},
        // '.' crosses the line break in TRegExpr (ModifierS), so the capture spans the lines
        {"multi-line", "{\"configuration_token\":\"CFG4\",\n\"access_token\":\"BEA4\",\n\"ttl\":1}",
         "CFG4\",\n\"access_token\":\"BEA4\",\n\"ttl", "BEA4"},
        // the subject ends at the first NUL (script.md R15): the junk after it is never matched
        {"nul-cut", QByteArray("{\"access_token\":\"BEA5\",\"configuration_token\":\"CFG5\"}\0\"zz\"", 54),
         "CFG5", "BEA5"},
    };
}

std::string expectedUrl(const NavMap& m, const Tile& t, const std::string& token) {
    return "https://tile3.navionics.com/viewer/api/v1/tile/" + std::to_string(t.z) + "/" +
           std::to_string(t.x) + "/" + std::to_string(t.y) + "?config=" + token + "&" + m.query;
}

class UrlDifferentialNavionics : public ::testing::TestWithParam<NavMap> {};

TEST(UrlDifferentialNavionics, BothTreesLoadWithoutErrorsAndHoldTheFourMaps) {
    CG_SKIP_IF_NO_FIXTURE(urlDir("navionics-zmp"));
    const Catalogue& c = navionicsCatalogue();
    EXPECT_TRUE(c.errors().isEmpty()) << c.errors().join(u"; "_s).toStdString();
    EXPECT_EQ(c.zmpCount(), 4U);
    EXPECT_EQ(c.jsonCount(), 4U);
}

TEST_P(UrlDifferentialNavionics, ConfigsAreEqualAndOnlyTheJsonSideRunsJavaScript) {
    CG_SKIP_IF_NO_FIXTURE(urlDir("navionics-zmp"));
    const Catalogue& c = navionicsCatalogue();
    const IZmpInfoPtr zmp = c.zmp(GetParam().stem);
    const IZmpInfoPtr json = c.json(GetParam().stem);
    ASSERT_NE(zmp, nullptr);
    ASSERT_NE(json, nullptr);
    EXPECT_EQ(zmp->guid(), json->guid());
    EXPECT_EQ(zmp->isLayer(), json->isLayer());
    const auto zc = zmp->tileDownloadRequestBuilderConfig();
    const auto jc = json->tileDownloadRequestBuilderConfig();
    EXPECT_EQ(zc->urlBase, "https://tile{s}.navionics.com/viewer/api/v1/tile/{z}/{x}/{y}");
    EXPECT_EQ(zc->urlBase, jc->urlBase);
    EXPECT_EQ(zc->serverNames, "1,2,3,4,5");
    EXPECT_EQ(zc->serverNames, jc->serverNames);
    EXPECT_EQ(zc->requestHeader, jc->requestHeader);
    EXPECT_TRUE(zc->isUseDownloader);
    EXPECT_TRUE(jc->isUseDownloader);

    Side zmpSide(zmp, {});
    Side jsonSide(json, {});
    ASSERT_NE(zmpSide.builder, nullptr);
    ASSERT_NE(jsonSide.builder, nullptr);
    EXPECT_FALSE(zmpSide.usesJs);
    EXPECT_TRUE(jsonSide.usesJs);
}

TEST_P(UrlDifferentialNavionics, TheServerOfTheUrlIsOneOfTheFiveOnBothSides) {
    CG_SKIP_IF_NO_FIXTURE(urlDir("navionics-zmp"));
    const Catalogue& c = navionicsCatalogue();
    for (const IZmpInfoPtr& zmp : {c.zmp(GetParam().stem), c.json(GetParam().stem)}) {
        ASSERT_NE(zmp, nullptr);
        Side side(zmp, {});
        side.downloader->replies.push_back(
            okReply(200, {}, R"({"access_token":"B","configuration_token":"C"})"));
        const Answer a = ask(side, {1, 2, 3});
        ASSERT_EQ(a.kind, "request") << zmp->fileName().toStdString();
        const std::string head = "https://tile";
        const std::string tail =
            ".navionics.com/viewer/api/v1/tile/3/1/2?config=C&" + std::string(GetParam().query);
        ASSERT_EQ(a.url.size(), head.size() + 1 + tail.size()) << a.url;
        EXPECT_EQ(a.url.substr(0, head.size()), head);
        EXPECT_NE(std::string("12345").find(a.url[head.size()]), std::string::npos) << a.url;
        EXPECT_EQ(a.url.substr(head.size() + 1), tail);
    }
}

TEST_P(UrlDifferentialNavionics, ForEveryTokenAnswerJsonAndZmpGiveTheSameRequestOnTheGrid) {
    CG_SKIP_IF_NO_FIXTURE(urlDir("navionics-zmp"));
    const NavMap& map = GetParam();
    const Catalogue& c = navionicsCatalogue();
    const std::vector<Tile> tiles = {{0, 0, 1}, {1, 1, 2}, {1234, 5678, 14}, {131071, 87654, 18}, {0, 0, 0}};
    for (const TokenBody& body : tokenBodies()) {
        SCOPED_TRACE(std::string(body.name) + " / " + map.stem.toStdString());
        Side zmpSide(c.zmp(map.stem), "3");
        Side jsonSide(c.json(map.stem), "3");
        zmpSide.downloader->replies.push_back(okReply(200, {}, body.body));
        jsonSide.downloader->replies.push_back(okReply(200, {}, body.body));
        for (const Tile& t : tiles) {
            const Answer fromZmp = ask(zmpSide, t);
            const Answer fromJson = ask(jsonSide, t);
            EXPECT_EQ(fromZmp, fromJson) << "tile " << t.x << "," << t.y << " z" << t.z;
            // the literal, from the Pascal script: URL, then the three header lines in the script's order
            if (body.token.empty()) {
                EXPECT_EQ(fromZmp.kind, "none");
            } else {
                Answer expected;
                expected.kind = "request";
                expected.method = static_cast<int>(cg::net::Method::Get);
                expected.url = expectedUrl(map, t, body.token);
                expected.header = "Referer: https://maps.garmin.com/\r\nOrigin: https://maps.garmin.com\r\n"
                                  "Authorization: Bearer " +
                                  body.bearer;
                EXPECT_EQ(fromZmp, expected);
            }
        }
        // a 200 answer is kept for 300 s even when it holds no token: one request for the whole grid
        for (Side* side : {&zmpSide, &jsonSide}) {
            const std::size_t asked = side->downloader->requests.size();
            EXPECT_EQ(asked, 1U) << (side->usesJs ? "json" : "zmp");
            for (const auto& r : side->downloader->requests) {
                EXPECT_EQ(r.url, kTokenUrl);
                EXPECT_EQ(r.requestHeader, kTokenHeader);
            }
        }
    }
}

TEST_P(UrlDifferentialNavionics, ATokenServiceFailureGivesNoTileOnBothSidesAndTheNextTileAsksAgain) {
    CG_SKIP_IF_NO_FIXTURE(urlDir("navionics-zmp"));
    const NavMap& map = GetParam();
    const Catalogue& c = navionicsCatalogue();
    for (const IZmpInfoPtr& zmp : {c.zmp(map.stem), c.json(map.stem)}) {
        Side side(zmp, "3");
        // 503 (a body that would parse is ignored), then a good answer
        side.downloader->replies.push_back(
            okReply(503, {}, R"({"access_token":"X","configuration_token":"Y"})"));
        side.downloader->replies.push_back(
            okReply(200, {}, R"({"access_token":"B","configuration_token":"C"})"));
        EXPECT_EQ(ask(side, {1, 1, 2}).kind, "none") << zmp->fileName().toStdString();
        const Answer second = ask(side, {1, 1, 2});
        EXPECT_EQ(second.url, expectedUrl(map, {1, 1, 2}, "C")) << zmp->fileName().toStdString();
        EXPECT_EQ(second.header, "Referer: https://maps.garmin.com/\r\nOrigin: "
                                 "https://maps.garmin.com\r\nAuthorization: Bearer B");
        EXPECT_EQ(side.downloader->requests.size(), 2U);
    }
}

INSTANTIATE_TEST_SUITE_P(FourMaps, UrlDifferentialNavionics, ::testing::ValuesIn(navMaps()),
                         [](const ::testing::TestParamInfo<NavMap>& pi) {
                             std::string name = pi.param.stem.toStdString();
                             for (char& ch : name) {
                                 ch = (std::isalnum(static_cast<unsigned char>(ch)) != 0) ? ch : '_';
                             }
                             return name;
                         });

} // namespace
