// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/snapshot (new code, no legacy unit; docs/specs/weather-sources.md §2, §3.2-3.4, §6).
// No FEAT tag (T-0242: Features none). Fixtures: tests/fixtures/app/snapshot (real answers of the services).
#include "cg/app/app_context.h"
#include "cg/app/snapshot.h"
#include "cg/core/platform/qt_compat.h"
#include "cg/maptype/map_type_set.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <time.h>
#include <vector>

#include "../maptype/maptype_test_util.h"
#include "cli_test_util.h"

using cg::app::parseSnapshotIndex;
using cg::app::parseSnapshotTime;
using cg::app::Snapshot;
using cg::app::snapshotDefinition;
using cg::app::SnapshotDefinition;
using cg::app::snapshotFromText;
using cg::app::SnapshotIndexType;
using cg::app::SnapshotPick;
using cg::app::SnapshotTime;
using cg::app::snapshotTimeIso;
using cg::app::snapshotTimeText;
using cg::core::ErrorCode;

namespace {

QByteArray fixture(const char* name) {
    QFile file(cg::maptype::test::fixtureDir() + QStringLiteral("/app/snapshot/") +
               QString::fromLatin1(name));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << name;
    return file.readAll();
}

SnapshotDefinition def(SnapshotIndexType type, const QString& item) {
    SnapshotDefinition d;
    d.indexUrl = QStringLiteral("http://x/");
    d.type = type;
    d.item = item;
    return d;
}

SnapshotDefinition rainDef() {
    SnapshotDefinition d = def(SnapshotIndexType::JsonLast, QStringLiteral("radar.past"));
    d.versionField = QStringLiteral("path");
    return d;
}

QString isoOf(const Snapshot& s) {
    return snapshotTimeIso(s);
}

// The text of the failure; the test fails when the parse succeeded.
std::string failure(const cg::core::Result<Snapshot>& r) {
    EXPECT_FALSE(r.has_value());
    if (r.has_value()) {
        return {};
    }
    EXPECT_EQ(r.error().code(), ErrorCode::ParseError);
    return r.error().what().toStdString();
}

// The RainViewer fixture with the "past" array edited by `edit`.
QByteArray rainEdited(const std::function<void(QJsonArray&)>& edit) {
    QJsonObject root = QJsonDocument::fromJson(fixture("rainviewer-weather-maps.json")).object();
    QJsonObject radar = root.value(QStringLiteral("radar")).toObject();
    QJsonArray past = radar.value(QStringLiteral("past")).toArray();
    edit(past);
    radar.insert(QStringLiteral("past"), past);
    root.insert(QStringLiteral("radar"), radar);
    return QJsonDocument(root).toJson();
}

// Replaces the last entry of "past" with {"time": <time>, "path": "p"}.
QByteArray rainLastTime(const QJsonValue& time) {
    return rainEdited([&](QJsonArray& past) {
        past.removeLast();
        past.append(
            QJsonObject{{QStringLiteral("time"), time}, {QStringLiteral("path"), QStringLiteral("p")}});
    });
}

QByteArray wmsDoc(const QByteArray& layers) {
    return "<?xml version=\"1.0\"?><WMS_Capabilities version=\"1.3.0\"><Capability><Layer>" + layers +
           "</Layer></Capability></WMS_Capabilities>";
}

QByteArray wmtsDoc(const QByteArray& domain) {
    return "<Domains><DimensionDomain><Domain>" + domain + "</Domain></DimensionDomain></Domains>";
}

} // namespace

// ---- 1. the eight real answers

struct SampleCase {
    const char* file;
    SnapshotIndexType type;
    const char* item;
    const char* version;
    const char* iso;
    bool dateOnly;
};

TEST(SnapshotSamples, EveryFixtureGivesTheSnapshotOfItsReadme) {
    const std::vector<SampleCase> cases = {
        {"rainviewer-weather-maps.json", SnapshotIndexType::JsonLast, "radar.past", "/v2/radar/7b6bac246650",
         "2026-10-15T20:10:00Z", false},
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2026-10-15T19:50:00Z", "2026-10-15T19:50:00Z", false},
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault,
         "mtg_fd:rgb_geocolour", "2026-10-15T19:50:00Z", "2026-10-15T19:50:00Z", false},
        {"eumetview-msg_fes-h60b-capabilities.xml", SnapshotIndexType::WmsDefault, "msg_fes:h60b",
         "2026-10-15T19:45:00Z", "2026-10-15T19:45:00Z", false},
        {"dwd-radar_wn-analysis-capabilities.xml", SnapshotIndexType::WmsDefault,
         "Radar_wn-analysis_1x1km_ger", "2026-10-15T20:10:00Z", "2026-10-15T20:10:00Z", false},
        {"nowcoast-conus_base_reflectivity_mosaic-capabilities.xml", SnapshotIndexType::WmsDefault,
         "conus_base_reflectivity_mosaic", "2026-10-15T20:12:08Z", "2026-10-15T20:12:08Z", false},
        {"gibs-imerg-describedomains.xml", SnapshotIndexType::WmtsDomain, "", "2026-10-15T14:30:00Z",
         "2026-10-15T14:30:00Z", false},
        {"gibs-imerg-describedomains-all.xml", SnapshotIndexType::WmtsDomain, "", "2026-10-15T14:30:00Z",
         "2026-10-15T14:30:00Z", false},
        {"gibs-viirs-noaa20-describedomains.xml", SnapshotIndexType::WmtsDomain, "", "2026-10-15",
         "2026-10-15", true},
        // T-0283: the version is the Default text verbatim (with the milliseconds), the time its parse
        {"copernicus-waves-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault,
         "GLOBAL_ANALYSISFORECAST_WAV_001_027/cmems_mod_glo_wav_anfc_0.083deg_PT3H-i_202411/VHM0",
         "2026-10-19T12:00:00.000Z", "2026-10-19T12:00:00Z", false},
        {"copernicus-waves-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault,
         "GLOBAL_ANALYSISFORECAST_WAV_001_027/cmems_mod_glo_wav_anfc_0.083deg_PT3H-i_202411/VHM0_SW1",
         "2026-10-19T12:00:00.000Z", "2026-10-19T12:00:00Z", false},
        // the elevation dimension comes first in this layer; the time one is read
        {"copernicus-currents-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault,
         "GLOBAL_ANALYSISFORECAST_PHY_001_024/cmems_mod_glo_phy-cur_anfc_0.083deg_PT6H-i_202406/"
         "sea_water_velocity",
         "2026-10-19T12:00:00.000Z", "2026-10-19T12:00:00Z", false},
    };
    for (const SampleCase& c : cases) {
        SCOPED_TRACE(std::string(c.file) + " " + c.item);
        SnapshotDefinition d = def(c.type, QString::fromLatin1(c.item));
        if (c.type == SnapshotIndexType::JsonLast) {
            d.versionField = QStringLiteral("path");
        }
        const auto r = parseSnapshotIndex(d, fixture(c.file));
        ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
        EXPECT_EQ(r->version.toStdString(), std::string(c.version));
        EXPECT_EQ(isoOf(*r).toStdString(), std::string(c.iso));
        EXPECT_EQ(r->dateOnly, c.dateOnly);
        EXPECT_TRUE(r->time.isValid());
    }
}

// ---- 2. json-last

TEST(SnapshotJsonLast, RootThatIsNotAnObject) {
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), "[1,2]")), "not a JSON object");
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), "garbage")), "not a JSON object");
}

TEST(SnapshotJsonLast, PathProblems) {
    const QByteArray body = fixture("rainviewer-weather-maps.json");
    SnapshotDefinition d = rainDef();
    d.item = QStringLiteral("radar.nope");
    EXPECT_EQ(failure(parseSnapshotIndex(d, body)), "no array at radar.nope");
    d.item = QStringLiteral("nope.past"); // a middle step that does not exist
    EXPECT_EQ(failure(parseSnapshotIndex(d, body)), "no array at nope.past");
    d.item = QStringLiteral("version.past"); // a middle step that is a string, not an object
    EXPECT_EQ(failure(parseSnapshotIndex(d, body)), "no array at version.past");
    d.item = QStringLiteral("radar"); // the last step is an object, not an array
    EXPECT_EQ(failure(parseSnapshotIndex(d, body)), "no array at radar");
}

TEST(SnapshotJsonLast, EmptyArray) {
    const QByteArray body = rainEdited([](QJsonArray& past) { past = QJsonArray(); });
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), body)), "the array at radar.past is empty");
}

TEST(SnapshotJsonLast, LastEntryProblems) {
    const QByteArray noPath = rainEdited([](QJsonArray& past) {
        past.removeLast();
        past.append(QJsonObject{{QStringLiteral("time"), 1792095000}});
    });
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), noPath)),
              "the last entry of radar.past has no usable path");

    const QByteArray notObject = rainEdited([](QJsonArray& past) { past.append(7); });
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), notObject)),
              "the last entry of radar.past has no usable time");

    // Only the LAST entry counts: an earlier entry without a path is ignored.
    const QByteArray earlierBad =
        rainEdited([](QJsonArray& past) { past.replace(0, QJsonObject{{QStringLiteral("time"), 5}}); });
    EXPECT_TRUE(parseSnapshotIndex(rainDef(), earlierBad).has_value());
}

TEST(SnapshotJsonLast, TimeMember) {
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), rainLastTime(-5))),
              "the last entry of radar.past has no usable time");
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), rainLastTime(0))),
              "the last entry of radar.past has no usable time");
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), rainLastTime(1.5))),
              "the last entry of radar.past has no usable time");
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), rainLastTime(true))),
              "the last entry of radar.past has no usable time");
    EXPECT_EQ(failure(parseSnapshotIndex(rainDef(), rainLastTime(QStringLiteral("bad")))),
              "bad is not a time");

    // A number is Unix seconds; a string is the §3.3 form.
    const auto num = parseSnapshotIndex(rainDef(), rainLastTime(86400));
    ASSERT_TRUE(num.has_value());
    EXPECT_EQ(isoOf(*num).toStdString(), "1970-01-02T00:00:00Z");
    EXPECT_FALSE(num->dateOnly);
    const auto str = parseSnapshotIndex(rainDef(), rainLastTime(QStringLiteral("2026-10-15T21:50:00+02:00")));
    ASSERT_TRUE(str.has_value());
    EXPECT_EQ(isoOf(*str).toStdString(), "2026-10-15T19:50:00Z");
    const auto day = parseSnapshotIndex(rainDef(), rainLastTime(QStringLiteral("2026-10-15")));
    ASSERT_TRUE(day.has_value());
    EXPECT_TRUE(day->dateOnly);
}

TEST(SnapshotJsonLast, VersionMember) {
    // No version field: the version is the time member; a number becomes its decimal text.
    SnapshotDefinition d = def(SnapshotIndexType::JsonLast, QStringLiteral("radar.past"));
    const auto byTime = parseSnapshotIndex(d, fixture("rainviewer-weather-maps.json"));
    ASSERT_TRUE(byTime.has_value());
    EXPECT_EQ(byTime->version.toStdString(), "1792095000");
    EXPECT_EQ(isoOf(*byTime).toStdString(), "2026-10-15T20:10:00Z");

    // Own time and version fields.
    d.timeField = QStringLiteral("t");
    d.versionField = QStringLiteral("v");
    const QByteArray body = R"({"a":{"b":[{"t":10,"v":"old"},{"t":86400,"v":"new"}]}})";
    d.item = QStringLiteral("a.b");
    const auto custom = parseSnapshotIndex(d, body);
    ASSERT_TRUE(custom.has_value());
    EXPECT_EQ(custom->version.toStdString(), "new");
    EXPECT_EQ(isoOf(*custom).toStdString(), "1970-01-02T00:00:00Z");

    // A version that is neither a string nor an integral number.
    d.versionField = QStringLiteral("f");
    const QByteArray fractional = R"({"a":{"b":[{"t":86400,"f":1.5}]}})";
    EXPECT_EQ(failure(parseSnapshotIndex(d, fractional)), "the last entry of a.b has no usable f");
    // A version that §3.3 refuses.
    const QByteArray empty = R"({"a":{"b":[{"t":86400,"f":""}]}})";
    EXPECT_EQ(failure(parseSnapshotIndex(d, empty)), "the version \"\" cannot be used");
}

// ---- 3. wms-default

TEST(SnapshotWms, NameOfAStyleIsNotALayerName) {
    // "weather_radar_base_reflectivity" is only the Name of a Style in the nowCOAST file.
    const auto d = def(SnapshotIndexType::WmsDefault, QStringLiteral("weather_radar_base_reflectivity"));
    EXPECT_EQ(
        failure(parseSnapshotIndex(d, fixture("nowcoast-conus_base_reflectivity_mosaic-capabilities.xml"))),
        "no layer weather_radar_base_reflectivity");
}

TEST(SnapshotWms, LayerWithoutDefaultTime) {
    QByteArray body = fixture("eumetview-mtg_fd-rgb_geocolour-capabilities.xml");
    ASSERT_TRUE(body.contains(" default=\"2026-10-15T19:50:00Z\""));
    body.replace(" default=\"2026-10-15T19:50:00Z\"", "");
    EXPECT_EQ(failure(parseSnapshotIndex(def(SnapshotIndexType::WmsDefault, QStringLiteral("rgb_geocolour")),
                                         body)),
              "layer rgb_geocolour has no default time");
}

TEST(SnapshotWms, NotACapabilitiesDocument) {
    const auto d = def(SnapshotIndexType::WmsDefault, QStringLiteral("rgb_geocolour"));
    const std::string garbage = failure(parseSnapshotIndex(d, "not xml"));
    EXPECT_TRUE(garbage.starts_with("not a WMS 1.3.0 capabilities document (")) << garbage;
    EXPECT_TRUE(garbage.ends_with(")")) << garbage;

    QByteArray body = fixture("eumetview-mtg_fd-rgb_geocolour-capabilities.xml");
    body.replace("WMS_Capabilities", "WMT_MS_Capabilities");
    EXPECT_EQ(failure(parseSnapshotIndex(d, body)), "not a WMS 1.3.0 capabilities document");

    // A truncated document: the layer is not reached, the XML error is named.
    const QByteArray whole = fixture("eumetview-mtg_fd-rgb_geocolour-capabilities.xml");
    const std::string truncated = failure(parseSnapshotIndex(
        def(SnapshotIndexType::WmsDefault, QStringLiteral("absent")), whole.left(whole.size() / 2)));
    EXPECT_TRUE(truncated.starts_with("not a WMS 1.3.0 capabilities document (")) << truncated;
}

TEST(SnapshotWms, LayerRules) {
    const auto wms = [](const QByteArray& layers, const QString& item) {
        return parseSnapshotIndex(def(SnapshotIndexType::WmsDefault, item), wmsDoc(layers));
    };
    // First matching layer in document order.
    const auto first = wms("<Layer><Name>w</Name><Dimension name=\"time\" default=\"2026-01-01\"/></Layer>"
                           "<Layer><Name>w</Name><Dimension name=\"time\" default=\"2026-02-02\"/></Layer>",
                           QStringLiteral("w"));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->version.toStdString(), "2026-01-01");
    EXPECT_TRUE(first->dateOnly);

    // The dimension name is case-insensitive; the default is trimmed and is the version verbatim.
    const auto upper =
        wms("<Layer><Name> w </Name><Dimension name=\"TIME\" default=\" 2026-03-03T01:02:03Z \"/></Layer>",
            QStringLiteral("w"));
    ASSERT_TRUE(upper.has_value());
    EXPECT_EQ(upper->version.toStdString(), "2026-03-03T01:02:03Z");
    EXPECT_EQ(isoOf(*upper).toStdString(), "2026-03-03T01:02:03Z");

    // Only a dimension named "time" counts.
    EXPECT_EQ(
        failure(wms("<Layer><Name>w</Name><Dimension name=\"elevation\" default=\"2026-01-01\"/></Layer>",
                    QStringLiteral("w"))),
        "layer w has no default time");
    // A time dimension of a child layer is not the parent's; one of the parent is not inherited.
    EXPECT_EQ(failure(wms("<Layer><Name>p</Name><Layer><Name>c</Name>"
                          "<Dimension name=\"time\" default=\"2026-01-01\"/></Layer></Layer>",
                          QStringLiteral("p"))),
              "layer p has no default time");
    EXPECT_EQ(failure(wms("<Layer><Name>p</Name><Dimension name=\"time\" default=\"2026-01-01\"/>"
                          "<Layer><Name>c</Name></Layer></Layer>",
                          QStringLiteral("c"))),
              "layer c has no default time");
    // Only DIRECT children of a Layer count (depth d+1): a Name or a Dimension one level deeper is not the
    // layer's.
    EXPECT_EQ(failure(wms("<Layer><Style><Name>s</Name></Style><Name>w</Name>"
                          "<Dimension name=\"time\" default=\"2026-01-01\"/></Layer>",
                          QStringLiteral("s"))),
              "no layer s");
    EXPECT_EQ(failure(wms("<Layer><Name>w</Name><Extension><Dimension name=\"time\" default=\"2026-01-01\"/>"
                          "</Extension></Layer>",
                          QStringLiteral("w"))),
              "layer w has no default time");
    // A layer without its own Name does not take the Name of a Style; the next layer still matches.
    const auto afterNameless =
        wms("<Layer><Style><Name>w</Name></Style></Layer>"
            "<Layer><Name>w</Name><Dimension name=\"time\" default=\"2026-04-04\"/></Layer>",
            QStringLiteral("w"));
    ASSERT_TRUE(afterNameless.has_value());
    EXPECT_EQ(afterNameless->version.toStdString(), "2026-04-04");
    // The first Dimension named time wins; the first Name wins.
    const auto twice =
        wms("<Layer><Name>w</Name><Name>z</Name><Dimension name=\"time\" default=\"2026-05-05\"/>"
            "<Dimension name=\"time\" default=\"2026-06-06\"/></Layer>",
            QStringLiteral("w"));
    ASSERT_TRUE(twice.has_value());
    EXPECT_EQ(twice->version.toStdString(), "2026-05-05");
    EXPECT_EQ(failure(wms("<Layer><Name>w</Name><Name>z</Name></Layer>", QStringLiteral("z"))), "no layer z");
    // A default that is not a time.
    EXPECT_EQ(failure(wms("<Layer><Name>w</Name><Dimension name=\"time\" default=\"current\"/></Layer>",
                          QStringLiteral("w"))),
              "current is not a time");

    // The workspace prefix: removed from whichever side has one; not when both have one and differ.
    const QByteArray plain =
        "<Layer><Name>h60b</Name><Dimension name=\"time\" default=\"2026-01-01\"/></Layer>";
    const QByteArray prefixed =
        "<Layer><Name>msg_fes:h60b</Name><Dimension name=\"time\" default=\"2026-01-01\"/></Layer>";
    EXPECT_TRUE(wms(plain, QStringLiteral("msg_fes:h60b")).has_value());
    EXPECT_TRUE(wms(prefixed, QStringLiteral("h60b")).has_value());
    EXPECT_EQ(failure(wms(prefixed, QStringLiteral("other:h60b"))), "no layer other:h60b");
    EXPECT_EQ(failure(wms(plain, QStringLiteral("h60"))), "no layer h60");
}

// ---- 4. wmts-domain

TEST(SnapshotWmts, Domains) {
    const auto wmts = [](const QByteArray& body) {
        return parseSnapshotIndex(def(SnapshotIndexType::WmtsDomain, {}), body);
    };
    EXPECT_EQ(failure(wmts(wmtsDoc(""))), "no time domain");
    EXPECT_EQ(failure(wmts(wmtsDoc("a/b"))), "no time domain");
    EXPECT_EQ(failure(wmts("<Domains><SpaceDomain/></Domains>")), "no time domain");
    EXPECT_EQ(failure(wmts("<Foo><Domain>2026-10-15</Domain></Foo>")), "not a DescribeDomains document");
    EXPECT_EQ(failure(wmts("not xml")), "not a DescribeDomains document");

    const auto instant = wmts(wmtsDoc("2026-10-15T00:00:00Z"));
    ASSERT_TRUE(instant.has_value());
    EXPECT_EQ(instant->version.toStdString(), "2026-10-15T00:00:00Z");
    EXPECT_EQ(isoOf(*instant).toStdString(), "2026-10-15T00:00:00Z");
    EXPECT_FALSE(instant->dateOnly);

    // Three parts: the END (the middle one); the last non-empty comma part wins.
    const auto list = wmts(wmtsDoc(" 2020-01-01/2020-01-02/P1D, 2026-10-01/2026-10-14/P1D ,"));
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->version.toStdString(), "2026-10-14");
    EXPECT_TRUE(list->dateOnly);

    // The first Domain element is the one read.
    const auto two = wmts("<Domains><Domain>2026-01-01</Domain><Domain>2026-02-02</Domain></Domains>");
    ASSERT_TRUE(two.has_value());
    EXPECT_EQ(two->version.toStdString(), "2026-01-01");

    // A value of the right shape that is not a time.
    EXPECT_EQ(failure(wmts(wmtsDoc("x/current/P1D"))), "current is not a time");
}

// ---- 4b. wmts-default (T-0283)

namespace {

constexpr const char* kWavesLayer =
    "GLOBAL_ANALYSISFORECAST_WAV_001_027/cmems_mod_glo_wav_anfc_0.083deg_PT3H-i_202411/VHM0";
constexpr const char* kCurrentsLayer = "GLOBAL_ANALYSISFORECAST_PHY_001_024/"
                                       "cmems_mod_glo_phy-cur_anfc_0.083deg_PT6H-i_202406/sea_water_velocity";

// A capabilities document around `layers`; the elements are in the namespaces of the real one.
QByteArray capsDoc(const QByteArray& layers) {
    return "<?xml version=\"1.0\"?><Capabilities xmlns=\"http://www.opengis.net/wmts/1.0\" "
           "xmlns:ows=\"http://www.opengis.net/ows/1.1\"><Contents>" +
           layers + "</Contents></Capabilities>";
}

QByteArray capsLayer(const QByteArray& id, const QByteArray& dimensions) {
    return "<Layer><ows:Identifier>" + id + "</ows:Identifier>" + dimensions + "</Layer>";
}

QByteArray timeDim(const QByteArray& def, const QByteArray& values = {}, const QByteArray& id = "time") {
    return "<Dimension><ows:Identifier>" + id + "</ows:Identifier><Default>" + def + "</Default>" + values +
           "</Dimension>";
}

SnapshotDefinition timed(const QString& item, const char* time, const char* now = "2026-10-19T10:20:00Z") {
    SnapshotDefinition d = def(SnapshotIndexType::WmtsDefault, item);
    d.sourceName = QStringLiteral("Waves");
    d.now = QDateTime::fromString(QString::fromLatin1(now), Qt::ISODate);
    if (time != nullptr) {
        const auto parsed = parseSnapshotTime(QString::fromLatin1(time));
        EXPECT_TRUE(parsed.has_value()) << time;
        d.time = parsed.value_or(SnapshotTime{});
    }
    return d;
}

// The refusal of a --time: its code is Unsupported and the text is complete.
std::string timeRefusal(const cg::core::Result<Snapshot>& r) {
    EXPECT_FALSE(r.has_value());
    if (r.has_value()) {
        return {};
    }
    EXPECT_EQ(r.error().code(), ErrorCode::Unsupported);
    return r.error().what().toStdString();
}

} // namespace

TEST(SnapshotWmtsDefault, LayerRules) {
    const auto wmts = [](const QByteArray& layers, const QString& item) {
        return parseSnapshotIndex(def(SnapshotIndexType::WmtsDefault, item), capsDoc(layers));
    };
    // The identifier of a style, deeper in the layer, is not the layer's name.
    EXPECT_EQ(failure(wmts("<Layer><Style><ows:Identifier>s</ows:Identifier></Style><ows:Identifier>w"
                           "</ows:Identifier>" +
                               timeDim("2026-01-01T00:00:00Z") + "</Layer>",
                           QStringLiteral("s"))),
              "no layer s");
    // The name and the default are trimmed; the dimension id is ASCII case-insensitive; the default is
    // kept verbatim apart from the trim.
    const auto trimmed =
        wmts(capsLayer(" w ", timeDim(" 2026-03-03T01:02:03.500Z ", {}, " TIME ")), QStringLiteral("w"));
    ASSERT_TRUE(trimmed.has_value());
    EXPECT_EQ(trimmed->version.toStdString(), "2026-03-03T01:02:03.500Z");
    // A dimension of another name gives no default; the time one after it does.
    EXPECT_EQ(
        failure(wmts(capsLayer("w", timeDim("2026-01-01T00:00:00Z", {}, "elevation")), QStringLiteral("w"))),
        "layer w has no default time");
    const auto second = wmts(capsLayer("w", timeDim("5", {}, "elevation") + timeDim("2026-04-04T00:00:00Z")),
                             QStringLiteral("w"));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->version.toStdString(), "2026-04-04T00:00:00Z");
    // A Dimension that is not a direct child of the layer is ignored, as is one in the nested layer's parent.
    EXPECT_EQ(failure(wmts("<Layer><ows:Identifier>w</ows:Identifier><Extension>" +
                               timeDim("2026-01-01T00:00:00Z") + "</Extension></Layer>",
                           QStringLiteral("w"))),
              "layer w has no default time");
    // Layers nest; a time dimension is not inherited, the child has its own.
    const QByteArray nested = "<Layer><ows:Identifier>p</ows:Identifier>" + timeDim("2026-02-02T00:00:00Z") +
                              capsLayer("c", {}) + capsLayer("d", timeDim("2026-05-05T00:00:00Z")) +
                              "</Layer>";
    EXPECT_EQ(failure(wmts(nested, QStringLiteral("c"))), "layer c has no default time");
    const auto child = wmts(nested, QStringLiteral("d"));
    ASSERT_TRUE(child.has_value());
    EXPECT_EQ(child->version.toStdString(), "2026-05-05T00:00:00Z");
    const auto parent = wmts(nested, QStringLiteral("p"));
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->version.toStdString(), "2026-02-02T00:00:00Z");
    // An empty default is no default; the first layer of the name is the one; a prefix is no match.
    EXPECT_EQ(failure(wmts(capsLayer("w", timeDim("  ")), QStringLiteral("w"))),
              "layer w has no default time");
    const auto firstOfTwo = wmts(capsLayer("w", timeDim("2026-06-06T00:00:00Z")) +
                                     capsLayer("w", timeDim("2026-07-07T00:00:00Z")),
                                 QStringLiteral("w"));
    ASSERT_TRUE(firstOfTwo.has_value());
    EXPECT_EQ(firstOfTwo->version.toStdString(), "2026-06-06T00:00:00Z");
    EXPECT_EQ(failure(wmts(capsLayer("wave", timeDim("2026-06-06T00:00:00Z")), QStringLiteral("w"))),
              "no layer w");
    // A default that is no time: the text of snapshotFromText.
    EXPECT_EQ(failure(wmts(capsLayer("w", timeDim("current")), QStringLiteral("w"))),
              "current is not a time");
    EXPECT_EQ(failure(wmts({}, QStringLiteral("w"))), "no layer w");
}

TEST(SnapshotWmtsDefault, NotACapabilitiesDocument) {
    const auto d = def(SnapshotIndexType::WmtsDefault, QStringLiteral("w"));
    // A wrong root has no XML error to add; a text that is no XML names the reader's error.
    EXPECT_EQ(failure(parseSnapshotIndex(d, "<WMS_Capabilities/>")), "not a WMTS capabilities document");
    EXPECT_EQ(failure(parseSnapshotIndex(d, "")),
              "not a WMTS capabilities document (Premature end of document.)");
    EXPECT_EQ(failure(parseSnapshotIndex(d, "{\"json\": 1}")),
              "not a WMTS capabilities document (Start tag expected.)");
    // A document broken after the layer was found still gives it (as wms-default); one without the layer
    // names the XML error.
    const QByteArray whole = capsDoc(capsLayer("w", timeDim("2026-01-01T00:00:00Z")));
    EXPECT_TRUE(parseSnapshotIndex(d, whole.left(whole.size() - 20)).has_value());
    const std::string brokenOther = failure(parseSnapshotIndex(
        def(SnapshotIndexType::WmtsDefault, QStringLiteral("absent")), whole.left(whole.size() - 20)));
    EXPECT_TRUE(brokenOther.starts_with("not a WMTS capabilities document (")) << brokenOther;
}

struct TimeCase {
    const char* file;
    const char* layer;
    const char* time; // nullptr = absent
    const char* now;
    const char* expected; // the version and the ISO time; nullptr = refused
};

TEST(SnapshotWmtsDefault, TimeOfTheRealDocuments) {
    // Waves: start 2022-11-01T03:00:00Z, end 2026-10-29T00:00:00Z, step 3 h. Currents: start
    // 2022-06-01T00:00:00Z, end 2026-10-29T00:00:00Z, step 6 h.
    const std::vector<TimeCase> cases = {
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, nullptr, "2026-10-19T10:20:00Z",
         "2026-10-19T12:00:00.000Z"},
        // +24h: 2026-10-20T10:20 floored to the 3 h grid
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "+24h", "2026-10-19T10:20:00Z",
         "2026-10-20T09:00:00Z"},
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "+0h", "2026-10-19T10:20:00Z",
         "2026-10-19T09:00:00Z"},
        // exactly on the grid stays
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "+3h", "2026-10-19T09:00:00Z",
         "2026-10-19T12:00:00Z"},
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "2026-10-20T12:00:00Z",
         "2026-10-19T10:20:00Z", "2026-10-20T12:00:00Z"},
        // the first and the last instant of the range
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "2022-11-01T03:00:00Z",
         "2026-10-19T10:20:00Z", "2022-11-01T03:00:00Z"},
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "2026-10-29T00:00:00Z",
         "2026-10-19T10:20:00Z", "2026-10-29T00:00:00Z"},
        // refused: off the grid, after the end, before the start, +N h beyond the end
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "2026-10-20T12:30:00Z",
         "2026-10-19T10:20:00Z", nullptr},
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "2026-10-29T03:00:00Z",
         "2026-10-19T10:20:00Z", nullptr},
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "2022-11-01T00:00:00Z",
         "2026-10-19T10:20:00Z", nullptr},
        {"copernicus-waves-wmts-capabilities.xml", kWavesLayer, "+9999h", "2026-10-19T10:20:00Z", nullptr},
        // currents: 6 h step, the elevation dimension in front
        {"copernicus-currents-wmts-capabilities.xml", kCurrentsLayer, "+1h", "2026-10-19T10:20:00Z",
         "2026-10-19T06:00:00Z"},
        {"copernicus-currents-wmts-capabilities.xml", kCurrentsLayer, "2026-10-19T12:00:00Z",
         "2026-10-19T10:20:00Z", "2026-10-19T12:00:00Z"},
        {"copernicus-currents-wmts-capabilities.xml", kCurrentsLayer, "2026-10-19T09:00:00Z",
         "2026-10-19T10:20:00Z", nullptr},
    };
    for (const TimeCase& c : cases) {
        SCOPED_TRACE(std::string(c.file) + " " + (c.time != nullptr ? c.time : "latest") + " now " + c.now);
        const auto r =
            parseSnapshotIndex(timed(QString::fromLatin1(c.layer), c.time, c.now), fixture(c.file));
        if (c.expected == nullptr) {
            EXPECT_FALSE(r.has_value());
            continue;
        }
        ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
        EXPECT_EQ(r->version.toStdString(), std::string(c.expected));
        EXPECT_FALSE(r->dateOnly);
        // the time is the instant of the version
        EXPECT_EQ(isoOf(*r).toStdString(),
                  QString::fromLatin1(c.expected).left(19).append(u'Z').toStdString());
    }
}

TEST(SnapshotWmtsDefault, TimeRefusalTexts) {
    const QByteArray waves = fixture("copernicus-waves-wmts-capabilities.xml");
    const QString layer = QString::fromLatin1(kWavesLayer);
    EXPECT_EQ(timeRefusal(parseSnapshotIndex(timed(layer, "2026-10-20T12:30:00Z"), waves)),
              "--time 2026-10-20T12:30:00Z is not a time of \"Waves\" (range 2022-11-01T03:00:00Z.."
              "2026-10-29T00:00:00Z, step 3 h)");
    EXPECT_EQ(timeRefusal(parseSnapshotIndex(timed(layer, "+9999h"), waves)),
              "--time +9999h is not a time of \"Waves\" (range 2022-11-01T03:00:00Z.."
              "2026-10-29T00:00:00Z, step 3 h)");
    const QByteArray currents = fixture("copernicus-currents-wmts-capabilities.xml");
    EXPECT_EQ(timeRefusal(parseSnapshotIndex(
                  timed(QString::fromLatin1(kCurrentsLayer), "2026-10-19T09:00:00Z"), currents)),
              "--time 2026-10-19T09:00:00Z is not a time of \"Waves\" (range 2022-06-01T00:00:00Z.."
              "2026-10-29T00:00:00Z, step 6 h)");
}

TEST(SnapshotWmtsDefault, RangeForms) {
    const auto doc = [](const QByteArray& values) {
        return capsDoc(capsLayer("w", timeDim("2026-01-01T01:00:00Z", values)));
    };
    const auto run = [&](const QByteArray& values, const char* time) {
        return parseSnapshotIndex(timed(QStringLiteral("w"), time), doc(values));
    };
    // The grid is counted from the start of the range, not from midnight.
    const QByteArray offset = "<Value>2026-01-01T01:00:00Z/2026-01-03T01:00:00Z/PT3H</Value>";
    EXPECT_TRUE(run(offset, "2026-01-01T04:00:00Z").has_value());
    EXPECT_NE(timeRefusal(run(offset, "2026-01-01T03:00:00Z")).find("step 3 h"), std::string::npos);
    // The first Value with the start/end/step form; a plain value before it is skipped; P1D is 24 h.
    const auto listed =
        run("<Value>2026-01-01T01:00:00Z</Value><Value>2026-01-01T01:00:00Z/2026-01-09T01:00:00Z/P1D</Value>",
            "2026-01-03T01:00:00Z");
    ASSERT_TRUE(listed.has_value());
    EXPECT_EQ(listed->version.toStdString(), "2026-01-03T01:00:00Z");
    // A step that is not whole hours is told in seconds.
    EXPECT_NE(timeRefusal(run("<Value>2026-01-01T01:00:00Z/2026-01-03T01:00:00Z/PT5400S</Value>",
                              "2026-01-01T01:00:01Z"))
                  .find("step 5400 s"),
              std::string::npos);
    // No usable range: no Value, a list of instants, a step that is not a duration or is zero, a bad date.
    for (const char* values : {"", "<Value>2026-01-01T01:00:00Z</Value>",
                               "<Value>2026-01-01T01:00:00Z/2026-01-03T01:00:00Z/3h</Value>",
                               "<Value>2026-01-01T01:00:00Z/2026-01-03T01:00:00Z/PT0S</Value>",
                               "<Value>soon/2026-01-03T01:00:00Z/PT3H</Value>"}) {
        SCOPED_TRACE(values);
        const auto r = run(values, "+1h");
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code(), ErrorCode::Unsupported);
        EXPECT_EQ(r.error().what().toStdString(), "--time: \"Waves\" has no time range to choose from");
    }
    // Without --time the range is not needed.
    EXPECT_TRUE(parseSnapshotIndex(timed(QStringLiteral("w"), nullptr), doc("")).has_value());
}

TEST(SnapshotWmtsDefault, TimeTextForms) {
    const auto ok = parseSnapshotTime(QStringLiteral("2026-10-18T12:00:00Z"));
    ASSERT_TRUE(ok.has_value());
    EXPECT_FALSE(ok->relative);
    EXPECT_EQ(ok->instant.toString(Qt::ISODate).toStdString(), "2026-10-18T12:00:00Z");
    const auto rel = parseSnapshotTime(QStringLiteral("+24h"));
    ASSERT_TRUE(rel.has_value());
    EXPECT_TRUE(rel->relative);
    EXPECT_EQ(rel->hours, 24);
    EXPECT_EQ(parseSnapshotTime(QStringLiteral("+0h"))->hours, 0);
    EXPECT_EQ(parseSnapshotTime(QStringLiteral("+99999h"))->hours, 99999);
    for (const char* text :
         {"", "24h", "+24", "+h", "-3h", "+1.5h", "+24H", "+24 h", "+100000h", "2026-10-18T12:00:00",
          "2026-10-18", "2026-10-18T12:00Z", "2026-13-18T12:00:00Z", "2026-10-18T25:00:00Z",
          "2026-10-18T12:00:00+02:00", "2026-10-18T12:00:00.5Z", "latest"}) {
        SCOPED_TRACE(text);
        const auto r = parseSnapshotTime(QString::fromLatin1(text));
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().toStdString(),
                  std::string("\"") + text + "\" is not a time such as 2026-10-18T12:00:00Z or +24h");
    }
    // Full-width digits are not digits here.
    EXPECT_FALSE(parseSnapshotTime(QString::fromUtf8("+\xEF\xBC\x92\xEF\xBC\x94h")).has_value());
}

// ---- 4c. SnapshotPick=nearest-now and none-refetch (T-0337)

namespace {

// `item` of `file` with the clock `now` and the pick nearest-now.
cg::core::Result<Snapshot> nearest(const char* file, SnapshotIndexType type, const QString& item,
                                   const char* now) {
    SnapshotDefinition d = def(type, item);
    d.pick = SnapshotPick::NearestNow;
    d.now = QDateTime::fromString(QString::fromLatin1(now), Qt::ISODate);
    return parseSnapshotIndex(d, fixture(file));
}

cg::core::Result<Snapshot> nearestIn(const QByteArray& layers, const char* now) {
    SnapshotDefinition d = def(SnapshotIndexType::WmtsDefault, QStringLiteral("w"));
    d.pick = SnapshotPick::NearestNow;
    d.now = QDateTime::fromString(QString::fromLatin1(now), Qt::ISODate);
    return parseSnapshotIndex(d, capsDoc(layers));
}

} // namespace

TEST(SnapshotNearestNow, RangeOfTheRealDocuments) {
    struct Case {
        const char* file;
        SnapshotIndexType type;
        const char* item;
        const char* now;
        const char* version; // empty = a refusal
    };
    const std::vector<Case> cases = {
        // WMS range 2024-09-23T00:00:00Z/2026-10-15T19:50:00Z/PT10M, default 19:50
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2026-10-15T12:34:00Z", "2026-10-15T12:30:00Z"},
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2026-10-15T12:35:00Z", "2026-10-15T12:30:00Z"}, // a tie goes to the earlier frame
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2026-10-15T12:35:01Z", "2026-10-15T12:40:00Z"},
        // after the end: the last frame
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2026-11-01T00:00:00Z", "2026-10-15T19:50:00Z"},
        // before the start, within one step of it: the first frame; further back: a refusal
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2024-09-22T23:55:00Z", "2024-09-23T00:00:00Z"},
        {"eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault, "rgb_geocolour",
         "2024-09-22T23:49:00Z", ""},
        // the workspace-prefixed name and PT15M
        {"eumetview-msg_fes-h60b-capabilities.xml", SnapshotIndexType::WmsDefault, "msg_fes:h60b",
         "2026-10-15T12:08:00Z", "2026-10-15T12:15:00Z"},
        // nested layer, PT5M
        {"dwd-radar_wn-analysis-capabilities.xml", SnapshotIndexType::WmsDefault,
         "Radar_wn-analysis_1x1km_ger", "2026-10-14T03:02:29Z", "2026-10-14T03:00:00Z"},
        // WMTS range 2022-11-01T03:00:00Z/2026-10-29T00:00:00Z/PT10800S; the default is 12:00
        {"copernicus-waves-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault, kWavesLayer,
         "2026-10-19T10:20:00Z", "2026-10-19T09:00:00Z"},
        {"copernicus-waves-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault, kWavesLayer,
         "2026-10-19T10:30:01Z", "2026-10-19T12:00:00Z"},
        // a grid line (03:00) is nearer than the one before it (00:00)
        {"copernicus-waves-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault, kWavesLayer,
         "2026-10-19T02:00:00Z", "2026-10-19T03:00:00Z"},
        // PT21600S from 2022-06-01T00:00:00Z; the elevation dimension's numbers are no frames
        {"copernicus-currents-wmts-capabilities.xml", SnapshotIndexType::WmtsDefault, kCurrentsLayer,
         "2026-10-19T10:20:00Z", "2026-10-19T12:00:00Z"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(std::string(c.file) + " " + c.item + " now " + c.now);
        const auto r = nearest(c.file, c.type, QString::fromLatin1(c.item), c.now);
        if (std::string(c.version).empty()) {
            EXPECT_TRUE(failure(r).starts_with("layer rgb_geocolour has no time up to now + one step"));
            continue;
        }
        ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
        EXPECT_EQ(r->version.toStdString(), std::string(c.version));
        EXPECT_EQ(isoOf(*r).toStdString(), std::string(c.version));
        EXPECT_FALSE(r->dateOnly);
    }
    // the refusal text in full
    EXPECT_EQ(
        failure(nearest("eumetview-mtg_fd-rgb_geocolour-capabilities.xml", SnapshotIndexType::WmsDefault,
                        QStringLiteral("rgb_geocolour"), "2024-09-22T23:49:00Z")),
        "layer rgb_geocolour has no time up to now + one step (the nearest is 2024-09-23T00:00:00Z)");
}

TEST(SnapshotNearestNow, ListOfTheRealDocumentKeepsTheServersText) {
    // nowcoast: 2026-10-15T12:04:11.000Z,2026-10-15T12:08:15.000Z,2026-10-15T12:12:12.000Z, ... 20:12:08.000Z
    const auto wms = [](const char* now) {
        return nearest("nowcoast-conus_base_reflectivity_mosaic-capabilities.xml",
                       SnapshotIndexType::WmsDefault, QStringLiteral("conus_base_reflectivity_mosaic"), now);
    };
    const auto before = wms("2026-10-15T12:10:00Z"); // 105 s after 12:08:15, 132 s before 12:12:12
    ASSERT_TRUE(before.has_value()) << before.error().what().toStdString();
    EXPECT_EQ(before->version.toStdString(), "2026-10-15T12:08:15.000Z");
    EXPECT_EQ(isoOf(*before).toStdString(), "2026-10-15T12:08:15Z");
    const auto ahead = wms("2026-10-15T12:11:00Z"); // the nearest frame lies 72 s after now
    ASSERT_TRUE(ahead.has_value()) << ahead.error().what().toStdString();
    EXPECT_EQ(ahead->version.toStdString(), "2026-10-15T12:12:12.000Z");
    const auto late = wms("2026-10-16T00:00:00Z");
    ASSERT_TRUE(late.has_value()) << late.error().what().toStdString();
    EXPECT_EQ(late->version.toStdString(), "2026-10-15T20:12:08.000Z");
    // before the first frame (12:04:11) by more than its step, the 244 s gap to the next listed frame
    // (T-0352)
    EXPECT_EQ(failure(wms("2026-10-15T12:00:00Z")),
              "layer conus_base_reflectivity_mosaic has no time up to now + one step (the nearest is "
              "2026-10-15T12:04:11Z)");
}

TEST(SnapshotNearestNow, WmtsValueForms) {
    const char* const now = "2026-10-19T10:20:00Z";
    // several Values: instants and a range together; the nearest of all wins (ties: the earlier)
    const auto mixed = nearestIn(
        capsLayer(
            "w",
            timeDim("2026-01-01T00:00:00Z",
                    "<Value>2026-10-19T10:00:00Z</Value><Value>2026-10-18T00:00:00Z/2026-10-19T00:00:00Z/"
                    "PT6H</Value><Value>2026-10-19T11:00:00Z</Value>")),
        now);
    ASSERT_TRUE(mixed.has_value()) << mixed.error().what().toStdString();
    EXPECT_EQ(mixed->version.toStdString(), "2026-10-19T10:00:00Z");
    // a comma-separated list in one Value
    const auto list =
        nearestIn(capsLayer("w", timeDim("2026-01-01T00:00:00Z",
                                         "<Value>2026-10-19T08:00:00Z, 2026-10-19T10:30:00Z</Value>")),
                  now);
    ASSERT_TRUE(list.has_value()) << list.error().what().toStdString();
    EXPECT_EQ(list->version.toStdString(), "2026-10-19T10:30:00Z");
    // the range of a Value with a zero step or reversed ends is skipped
    const auto skipped =
        nearestIn(capsLayer("w", timeDim("2026-01-01T00:00:00Z",
                                         "<Value>2026-10-19T00:00:00Z/2026-10-20T00:00:00Z/PT0S</Value>"
                                         "<Value>2026-10-20T00:00:00Z/2026-10-19T00:00:00Z/PT1H</Value>"
                                         "<Value>2026-10-19T09:00:00Z</Value>")),
                  now);
    ASSERT_TRUE(skipped.has_value()) << skipped.error().what().toStdString();
    EXPECT_EQ(skipped->version.toStdString(), "2026-10-19T09:00:00Z");
    // two listed instants at the same distance (10:10 and 10:30 around 10:20): the earlier one, given later
    const auto tie = nearestIn(
        capsLayer("w", timeDim("2026-01-01T00:00:00Z",
                               "<Value>2026-10-19T10:30:00Z</Value><Value>2026-10-19T10:10:00Z</Value>")),
        now);
    ASSERT_TRUE(tie.has_value()) << tie.error().what().toStdString();
    EXPECT_EQ(tie->version.toStdString(), "2026-10-19T10:10:00Z");
    // a date-only instant keeps its text and is flagged
    const auto dateOnly = nearestIn(capsLayer("w", timeDim("2026-01-01", "<Value>2026-10-19</Value>")), now);
    ASSERT_TRUE(dateOnly.has_value()) << dateOnly.error().what().toStdString();
    EXPECT_EQ(dateOnly->version.toStdString(), "2026-10-19");
    EXPECT_TRUE(dateOnly->dateOnly);
}

TEST(SnapshotNearestNow, UnreadableDimensionIsRefused) {
    const char* const now = "2026-10-19T10:20:00Z";
    EXPECT_EQ(failure(nearestIn(capsLayer("w", timeDim("2026-01-01T00:00:00Z")), now)),
              "layer w has no time values to choose from");
    EXPECT_EQ(
        failure(nearestIn(capsLayer("w", timeDim("2026-01-01T00:00:00Z", "<Value>current</Value>")), now)),
        "layer w has no time values to choose from");
    // only a frame after now + its step (a single listed instant has step 0)
    EXPECT_EQ(
        failure(nearestIn(
            capsLayer("w", timeDim("2026-01-01T00:00:00Z", "<Value>2026-10-19T10:21:00Z</Value>")), now)),
        "layer w has no time up to now + one step (the nearest is 2026-10-19T10:21:00Z)");
    // WMS: a time dimension without text
    const QByteArray wms =
        wmsDoc("<Name>x</Name><Dimension name=\"time\" default=\"2026-10-15T19:45:00Z\"/>");
    SnapshotDefinition d = def(SnapshotIndexType::WmsDefault, QStringLiteral("x"));
    d.pick = SnapshotPick::NearestNow;
    d.now = QDateTime::fromString(QStringLiteral("2026-10-15T20:00:00Z"), Qt::ISODate);
    EXPECT_EQ(failure(parseSnapshotIndex(d, wms)), "layer x has no time values to choose from");
    // and without the pick the default time of the same layer is taken
    d.pick = SnapshotPick::Default;
    const auto plain = parseSnapshotIndex(d, wms);
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->version.toStdString(), "2026-10-15T19:45:00Z");
}

// T-0352: the first listed instant takes the gap to the next one as its step (as a range does), so a list
// whose instants all lie in the future is accepted when the nearest is within that gap of now; a single
// listed instant keeps step 0.
TEST(SnapshotNearestNow, FirstListedInstantTakesTheGapToTheNext) {
    const char* const now = "2026-10-19T10:20:00Z";
    const auto r =
        nearestIn(capsLayer("w", timeDim("2026-01-01T00:00:00Z",
                                         "<Value>2026-10-19T10:30:00Z,2026-10-19T10:50:00Z</Value>")),
                  now);
    ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
    EXPECT_EQ(r->version.toStdString(), "2026-10-19T10:30:00Z"); // 10 min ahead, the gap is 20 min
    // beyond the gap it is refused
    EXPECT_EQ(
        failure(nearestIn(capsLayer("w", timeDim("2026-01-01T00:00:00Z",
                                                 "<Value>2026-10-19T10:50:00Z,2026-10-19T10:55:00Z</Value>")),
                          now)),
        "layer w has no time up to now + one step (the nearest is 2026-10-19T10:50:00Z)");
}

TEST(SnapshotNearestNow, ExplicitTimeWinsOverThePick) {
    SnapshotDefinition d = timed(kWavesLayer, "2026-10-20T12:00:00Z");
    d.pick = SnapshotPick::NearestNow;
    const auto r = parseSnapshotIndex(d, fixture("copernicus-waves-wmts-capabilities.xml"));
    ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
    EXPECT_EQ(r->version.toStdString(), "2026-10-20T12:00:00Z");
}

TEST(SnapshotNearestNow, DefaultPickStaysTheLayerDefault) {
    SnapshotDefinition d = def(SnapshotIndexType::WmsDefault, QStringLiteral("rgb_geocolour"));
    d.now = QDateTime::fromString(QStringLiteral("2026-10-15T12:34:00Z"), Qt::ISODate);
    const auto r = parseSnapshotIndex(d, fixture("eumetview-mtg_fd-rgb_geocolour-capabilities.xml"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version.toStdString(), "2026-10-15T19:50:00Z");
}

TEST(SnapshotNoneRefetch, TheInstantOfTheResolveIsTheSnapshot) {
    SnapshotDefinition d = def(SnapshotIndexType::NoneRefetch, {});
    d.now = QDateTime::fromString(QStringLiteral("2026-10-22T08:09:10.789Z"), Qt::ISODateWithMs);
    ASSERT_TRUE(d.now.isValid());
    const auto r = parseSnapshotIndex(d, {});
    ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
    EXPECT_EQ(r->version.toStdString(), "2026-10-22T08:09:10Z");
    EXPECT_EQ(isoOf(*r).toStdString(), "2026-10-22T08:09:10Z");
    EXPECT_FALSE(r->dateOnly);
    // two instants a second apart are two versions (the cache never serves the older picture)
    d.now = d.now.addSecs(1);
    const auto next = parseSnapshotIndex(d, {});
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->version.toStdString(), "2026-10-22T08:09:11Z");
    // no clock given: the real one
    d.now = QDateTime();
    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    const auto live = parseSnapshotIndex(d, {});
    const QDateTime after = QDateTime::currentDateTimeUtc().addSecs(1);
    ASSERT_TRUE(live.has_value());
    EXPECT_GE(live->time, before);
    EXPECT_LE(live->time, after);
}

// ---- 5. snapshotFromText

TEST(SnapshotText, TimeForms) {
    struct Case {
        const char* text;
        const char* iso;
        bool dateOnly;
    };
    const std::vector<Case> cases = {
        {"2026-10-15", "2026-10-15", true},
        {"2026-10-15T19:50:00Z", "2026-10-15T19:50:00Z", false},
        {"2026-10-15T19:50:00.000Z", "2026-10-15T19:50:00Z", false},
        {"2026-10-15T21:50:00+02:00", "2026-10-15T19:50:00Z", false},
        {"2026-10-15T19:50:00-03:30", "2026-10-15T23:20:00Z", false},
        {"2026-10-15T19:50:00", "2026-10-15T19:50:00Z", false},
        {" 2026-10-15T19:50:00Z ", "2026-10-15T19:50:00Z", false},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.text);
        const auto r = snapshotFromText(QStringLiteral("v1"), QString::fromLatin1(c.text));
        ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
        EXPECT_EQ(isoOf(*r).toStdString(), std::string(c.iso));
        EXPECT_EQ(r->dateOnly, c.dateOnly);
        EXPECT_EQ(r->version.toStdString(), "v1");
        EXPECT_EQ(r->time.timeSpec(), Qt::UTC);
    }
}

// A zone-less time is UTC whatever the host zone is (a local DST gap must not move it).
TEST(SnapshotText, ZonelessTimeInDstGap) {
    const QByteArray oldTz = qgetenv("TZ");
    const bool hadTz = qEnvironmentVariableIsSet("TZ");
    qputenv("TZ", "Europe/Berlin");
    tzset();
    const auto r = snapshotFromText(QStringLiteral("v1"), QStringLiteral("2026-03-29T02:30:00"));
    if (hadTz) {
        qputenv("TZ", oldTz);
    } else {
        qunsetenv("TZ");
    }
    tzset();
    ASSERT_TRUE(r.has_value()) << r.error().what().toStdString();
    EXPECT_EQ(isoOf(*r).toStdString(), "2026-03-29T02:30:00Z");
}

TEST(SnapshotText, NotATime) {
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("v"), QStringLiteral("current"))),
              "current is not a time");
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("v"), QStringLiteral("2026-13-45"))),
              "2026-13-45 is not a time");
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("v"), QString())), " is not a time");
    // the Z retry for a zoneless DST-gap time must not make a bad zone valid
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("v"), QStringLiteral("2026-10-15T19:50:00+99:00"))),
              "2026-10-15T19:50:00+99:00 is not a time");
}

TEST(SnapshotText, VersionRules) {
    const QString t = QStringLiteral("2026-10-15");
    EXPECT_EQ(failure(snapshotFromText(QString(), t)), "the version \"\" cannot be used");
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("   "), t)), "the version \"\" cannot be used");
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("a\nb"), t)), "the version \"a\nb\" cannot be used");
    EXPECT_EQ(failure(snapshotFromText(QStringLiteral("a\tb"), t)), "the version \"a\tb\" cannot be used");
    const QString long201(201, QLatin1Char('a'));
    EXPECT_FALSE(snapshotFromText(long201, t).has_value());
    // The limits: 200 characters pass; the version is trimmed; / : . stay.
    const QString long200(200, QLatin1Char('a'));
    const auto ok = snapshotFromText(long200, t);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->version.size(), 200);
    const auto path = snapshotFromText(QStringLiteral("  /v2/radar:1.x  "), t);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->version.toStdString(), "/v2/radar:1.x");
    // U+0020 is the first allowed character.
    EXPECT_TRUE(snapshotFromText(QStringLiteral("a b"), t).has_value());
    // A bad time is reported before a bad version.
    EXPECT_EQ(failure(snapshotFromText(QString(), QStringLiteral("x"))), "x is not a time");
}

// ---- 6. formatters

TEST(SnapshotFormat, TextAndIso) {
    Snapshot s;
    s.time = cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10, 0));
    EXPECT_EQ(snapshotTimeText(s).toStdString(), "2026-10-15 20:10 UTC");
    EXPECT_EQ(snapshotTimeIso(s).toStdString(), "2026-10-15T20:10:00Z");

    // Seconds are shown in the ISO form only; a non-UTC value is converted.
    s.time = QDateTime(QDate(2026, 10, 15), QTime(22, 10, 7), QTimeZone(7200));
    EXPECT_EQ(snapshotTimeText(s).toStdString(), "2026-10-15 20:10 UTC");
    EXPECT_EQ(snapshotTimeIso(s).toStdString(), "2026-10-15T20:10:07Z");

    s.dateOnly = true;
    s.time = cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(0, 0));
    EXPECT_EQ(snapshotTimeText(s).toStdString(), "2026-10-15");
    EXPECT_EQ(snapshotTimeIso(s).toStdString(), "2026-10-15");

    const Snapshot unknown;
    EXPECT_EQ(snapshotTimeText(unknown).toStdString(), "unknown");
    EXPECT_TRUE(snapshotTimeIso(unknown).isEmpty());
    Snapshot unknownDay;
    unknownDay.dateOnly = true;
    EXPECT_EQ(snapshotTimeText(unknownDay).toStdString(), "unknown");
    EXPECT_TRUE(snapshotTimeIso(unknownDay).isEmpty());
}

// ---- 7, 8. the keys of params.txt, read from loaded maps

namespace {

struct KeyCase {
    const char* name;
    QByteArray extra; // [PARAMS] lines, each ending in CRLF
};

QByteArray guidOf(int n) {
    return QByteArray("{AB0000") + QByteArray::number(n + 0x10, 16).toUpper() + "-0000-0000-0000-0000000000" +
           QByteArray::number(n + 0x10, 16).toUpper() + "}";
}

const std::vector<KeyCase>& keyCases() {
    static const std::vector<KeyCase> cases = {
        /* 0 */ {"static", ""},
        /* 1 */
        {"jsonFull", "SnapshotIndex=https://api.example.org/maps.json\r\nSnapshotIndexType=json-last\r\n"
                     "SnapshotItem=radar.past\r\nSnapshotTimeField=t\r\nSnapshotVersionField=path\r\n"},
        /* 2 */
        {"jsonDefaults", "SnapshotIndex=HTTP://api.example.org/maps.json\r\nSnapshotIndexType=JSON-Last\r\n"
                         "SnapshotItem=radar.past\r\n"},
        /* 3 */
        {"typeXml", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=xml\r\nSnapshotItem=i\r\n"},
        /* 4 */ {"typeMissing", "SnapshotIndex=https://a.example.org/x\r\nSnapshotItem=i\r\n"},
        /* 5 */ {"wmsNoItem", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wms-default\r\n"},
        /* 6 */ {"jsonNoItem", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=json-last\r\n"},
        /* 7 */
        {"wmtsWithItem", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wmts-domain\r\n"
                         "SnapshotItem=i\r\n"},
        /* 8 */
        {"wmtsOk", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wmts-domain\r\n"
                   "SnapshotTimeField=ignored\r\n"},
        /* 9 */ {"ftp", "SnapshotIndex=ftp://x/y\r\nSnapshotIndexType=json-last\r\nSnapshotItem=i\r\n"},
        /* 10 */
        {"keysWithoutIndex", "SnapshotIndexType=json-last\r\nSnapshotItem=i\r\nSnapshotVersionField=v\r\n"},
        /* 11 */
        {"badUrl",
         "SnapshotIndex=https://exa mple.org/x\r\nSnapshotIndexType=json-last\r\nSnapshotItem=i\r\n"},
        /* 12 */
        {"wmsOk", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wms-default\r\n"
                  "SnapshotItem=mtg_fd:rgb_geocolour\r\n"},
        /* 13 */ {"zoom7", "MaxZoom=7\r\n"},
        /* 14 */ {"zoom0", "MaxZoom=0\r\n"},
        /* 15 */ {"zoom23", "MaxZoom=23\r\n"},
        /* 16 */ {"zoom24", "MaxZoom=24\r\n"},
        /* 17 */ {"zoomMinus1", "MaxZoom=-1\r\n"},
        /* 18 */ {"zoomX", "MaxZoom=x\r\n"},
        /* 19 */ {"zoomEmpty", "MaxZoom=\r\n"},
        /* 20 */ {"zoomFraction", "MaxZoom=7.5\r\n"},
        /* 21 */
        {"wmtsDefaultOk", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=WMTS-Default\r\n"
                          "SnapshotItem=P/D/VHM0\r\n"},
        /* 22 */
        {"wmtsDefaultNoItem", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wmts-default\r\n"},
        /* 23 */
        {"pickWms",
         "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wms-default\r\nSnapshotItem=i\r\n"
         "SnapshotPick=Nearest-Now\r\n"},
        /* 24 */
        {"pickDefault", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wms-default\r\n"
                        "SnapshotItem=i\r\nSnapshotPick=DEFAULT\r\n"},
        /* 25 */
        {"pickBad", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wms-default\r\n"
                    "SnapshotItem=i\r\nSnapshotPick=latest\r\n"},
        /* 26 */
        {"pickJson", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=json-last\r\n"
                     "SnapshotItem=i\r\nSnapshotPick=nearest-now\r\n"},
        /* 27 */
        {"noneOk", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=None-Refetch\r\n"},
        /* 28 */
        {"noneWithItem", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=none-refetch\r\n"
                         "SnapshotItem=i\r\n"},
        /* 29 */
        {"pickWmts", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=wmts-default\r\n"
                     "SnapshotItem=i\r\nSnapshotPick=nearest-now\r\n"},
        /* 30 */
        {"pickNone", "SnapshotIndex=https://a.example.org/x\r\nSnapshotIndexType=none-refetch\r\n"
                     "SnapshotPick=nearest-now\r\n"},
    };
    return cases;
}

class SnapshotKeysTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        cg::maptype::test::ensureGuiApp();
        root_ = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(root_->isValid());
        const QString maps = root_->filePath(QStringLiteral("Maps"));
        ASSERT_TRUE(QDir().mkpath(maps));
        int n = 0;
        for (const KeyCase& c : keyCases()) {
            cg::app::test::writeZmp(maps, {.folder = QStringLiteral("m%1.zmp").arg(n),
                                           .guid = guidOf(n),
                                           .name = QByteArray("Map ") + c.name,
                                           .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png",
                                           .extra = "CacheType=9\r\n" + c.extra});
            ++n;
        }
        cg::app::AppOptions o;
        o.arguments = {QStringLiteral("--datadir=") + root_->filePath(QStringLiteral("data"))};
        o.mapsDirs = {maps};
        o.warn = [](const QString&) {};
        auto ctx = cg::app::AppContext::create(o);
        ASSERT_TRUE(ctx.has_value());
        app_ = std::move(*ctx);
        ASSERT_TRUE(app_->loadMaps().has_value());
        ASSERT_EQ(app_->fullMapsSet()->count(), static_cast<int>(keyCases().size()));
    }
    static void TearDownTestSuite() {
        app_.reset();
        root_.reset();
    }

    [[nodiscard]] static cg::maptype::IMapTypePtr map(int n) {
        auto m = app_->fullMapsSet()->mapTypeByGuid(QUuid::fromString(QString::fromLatin1(guidOf(n))));
        EXPECT_TRUE(m != nullptr) << n;
        return m;
    }
    // The failure text of snapshotDefinition for case n.
    static std::string definitionFailure(int n) {
        const auto r = snapshotDefinition(*map(n));
        EXPECT_FALSE(r.has_value()) << keyCases()[static_cast<size_t>(n)].name;
        if (r.has_value()) {
            return {};
        }
        EXPECT_EQ(r.error().code(), ErrorCode::ParseError);
        return r.error().what().toStdString();
    }
    static std::string zoomFailure(int n) {
        const auto r = cg::app::maxZoom(*map(n));
        EXPECT_FALSE(r.has_value()) << keyCases()[static_cast<size_t>(n)].name;
        if (r.has_value()) {
            return {};
        }
        EXPECT_EQ(r.error().code(), ErrorCode::ParseError);
        return r.error().what().toStdString();
    }

    static std::unique_ptr<QTemporaryDir> root_;
    static std::unique_ptr<cg::app::AppContext> app_;
};
std::unique_ptr<QTemporaryDir> SnapshotKeysTest::root_;
std::unique_ptr<cg::app::AppContext> SnapshotKeysTest::app_;

} // namespace

TEST_F(SnapshotKeysTest, StaticSourceHasNoDefinition) {
    for (const int n : {0, 10, 13}) { // no key; Snapshot* keys without SnapshotIndex; only MaxZoom
        const auto r = snapshotDefinition(*map(n));
        ASSERT_TRUE(r.has_value()) << n;
        EXPECT_FALSE(r->has_value()) << n;
    }
}

TEST_F(SnapshotKeysTest, FullJsonDefinition) {
    const auto r = snapshotDefinition(*map(1));
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ((*r)->indexUrl.toStdString(), "https://api.example.org/maps.json");
    EXPECT_EQ((*r)->type, SnapshotIndexType::JsonLast);
    EXPECT_EQ((*r)->item.toStdString(), "radar.past");
    EXPECT_EQ((*r)->timeField.toStdString(), "t");
    EXPECT_EQ((*r)->versionField.toStdString(), "path");
}

TEST_F(SnapshotKeysTest, DefaultsAndCaseInsensitiveSpellings) {
    const auto r = snapshotDefinition(*map(2));
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ((*r)->indexUrl.toStdString(), "HTTP://api.example.org/maps.json");
    EXPECT_EQ((*r)->type, SnapshotIndexType::JsonLast);
    EXPECT_EQ((*r)->timeField.toStdString(), "time");
    EXPECT_TRUE((*r)->versionField.isEmpty());

    const auto wms = snapshotDefinition(*map(12));
    ASSERT_TRUE(wms.has_value() && wms->has_value());
    EXPECT_EQ((*wms)->type, SnapshotIndexType::WmsDefault);
    EXPECT_EQ((*wms)->item.toStdString(), "mtg_fd:rgb_geocolour");

    const auto wmts = snapshotDefinition(*map(8));
    ASSERT_TRUE(wmts.has_value() && wmts->has_value());
    EXPECT_EQ((*wmts)->type, SnapshotIndexType::WmtsDomain);
    EXPECT_TRUE((*wmts)->item.isEmpty());
}

TEST_F(SnapshotKeysTest, WmtsDefaultDefinition) {
    const auto r = snapshotDefinition(*map(21));
    ASSERT_TRUE(r.has_value() && r->has_value());
    EXPECT_EQ((*r)->type, SnapshotIndexType::WmtsDefault);
    EXPECT_EQ((*r)->item.toStdString(), "P/D/VHM0");
    EXPECT_FALSE((*r)->time.has_value());
    EXPECT_EQ(definitionFailure(22), "SnapshotItem is required for wmts-default");
}

TEST_F(SnapshotKeysTest, SnapshotPickAndNoneRefetch) {
    const auto pick = [&](int n) {
        const auto r = snapshotDefinition(*map(n));
        EXPECT_TRUE(r.has_value() && r->has_value()) << n;
        return r.has_value() && r->has_value() ? (*r)->pick : SnapshotPick::Default;
    };
    EXPECT_EQ(pick(23), SnapshotPick::NearestNow); // case-insensitive
    EXPECT_EQ(pick(29), SnapshotPick::NearestNow);
    EXPECT_EQ(pick(24), SnapshotPick::Default);
    EXPECT_EQ(pick(12), SnapshotPick::Default); // no key
    EXPECT_EQ(definitionFailure(25), "SnapshotPick=latest is not default or nearest-now");
    EXPECT_EQ(definitionFailure(26), "SnapshotPick=nearest-now needs wms-default or wmts-default");
    EXPECT_EQ(definitionFailure(30), "SnapshotPick=nearest-now needs wms-default or wmts-default");
    const auto none = snapshotDefinition(*map(27));
    ASSERT_TRUE(none.has_value() && none->has_value());
    EXPECT_EQ((*none)->type, SnapshotIndexType::NoneRefetch);
    EXPECT_TRUE((*none)->item.isEmpty());
    EXPECT_EQ(definitionFailure(28), "SnapshotItem must be empty for none-refetch");
}

TEST_F(SnapshotKeysTest, InvalidDefinitions) {
    EXPECT_EQ(
        definitionFailure(3),
        "SnapshotIndexType=xml is not json-last, wms-default, wmts-default, wmts-domain or none-refetch");
    EXPECT_EQ(definitionFailure(4), "SnapshotIndexType is missing");
    EXPECT_EQ(definitionFailure(5), "SnapshotItem is required for wms-default");
    EXPECT_EQ(definitionFailure(6), "SnapshotItem is required for json-last");
    EXPECT_EQ(definitionFailure(7), "SnapshotItem must be empty for wmts-domain");
    EXPECT_EQ(definitionFailure(9), "SnapshotIndex=ftp://x/y is not an http(s) URL");
    EXPECT_EQ(definitionFailure(11), "SnapshotIndex=https://exa mple.org/x is not an http(s) URL");
}

TEST_F(SnapshotKeysTest, MaxZoomValues) {
    const auto absent = cg::app::maxZoom(*map(0));
    ASSERT_TRUE(absent.has_value());
    EXPECT_FALSE(absent->has_value());
    const auto empty = cg::app::maxZoom(*map(19));
    ASSERT_TRUE(empty.has_value());
    EXPECT_FALSE(empty->has_value());
    const std::vector<std::pair<int, int>> good = {{13, 7}, {14, 0}, {15, 23}};
    for (const auto& [n, zoom] : good) {
        const auto r = cg::app::maxZoom(*map(n));
        ASSERT_TRUE(r.has_value()) << n;
        ASSERT_TRUE(r->has_value()) << n;
        EXPECT_EQ(static_cast<int>(**r), zoom) << n;
    }
    EXPECT_EQ(zoomFailure(16), "MaxZoom=24 is not a zoom from 0 to 23");
    EXPECT_EQ(zoomFailure(17), "MaxZoom=-1 is not a zoom from 0 to 23");
    EXPECT_EQ(zoomFailure(18), "MaxZoom=x is not a zoom from 0 to 23");
    EXPECT_EQ(zoomFailure(20), "MaxZoom=7.5 is not a zoom from 0 to 23");
}

// T-0249: every {year} is replaced, nothing else.
TEST(SnapshotWithYear, ReplacesEveryPlaceholder) {
    EXPECT_EQ(cg::app::withYear(QStringLiteral("Meteosat data {year}, {year}"), 2026),
              QStringLiteral("Meteosat data 2026, 2026"));
    EXPECT_EQ(cg::app::withYear(QStringLiteral("no placeholder {Year} {yea}"), 2026),
              QStringLiteral("no placeholder {Year} {yea}"));
}

// ---- 4d. The frame before the chosen one (T-0356): `previous` and `newest`

namespace {

SnapshotDefinition wmtsNow(const char* now, bool previous, SnapshotPick pick = SnapshotPick::NearestNow) {
    SnapshotDefinition d = def(SnapshotIndexType::WmtsDefault, QStringLiteral("w"));
    d.pick = pick;
    d.previous = previous;
    d.now = QDateTime::fromString(QString::fromLatin1(now), Qt::ISODate);
    return d;
}

} // namespace

TEST(SnapshotPreviousFrame, JsonLastTakesTheSecondLastEntryAndTheLastIsTheNewest) {
    const QByteArray body = fixture("rainviewer-weather-maps.json");
    const auto last = parseSnapshotIndex(rainDef(), body);
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->version.toStdString(), "/v2/radar/7b6bac246650");
    EXPECT_TRUE(last->newest);

    SnapshotDefinition before = rainDef();
    before.previous = true;
    const auto prev = parseSnapshotIndex(before, body);
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(prev->version.toStdString(), "/v2/radar/28db240facca");
    EXPECT_EQ(isoOf(*prev).toStdString(), "2026-10-15T20:00:00Z");
    EXPECT_FALSE(prev->newest);

    // one entry: no frame before it
    const QByteArray single = rainEdited([](QJsonArray& past) {
        while (past.size() > 1) {
            past.removeFirst();
        }
    });
    EXPECT_EQ(failure(parseSnapshotIndex(before, single)),
              "the array at radar.past has no entry before the last");
    // the entry before the last is read by the rules of the last one
    const QByteArray broken = rainEdited([](QJsonArray& past) { past[past.size() - 2] = QJsonValue(5); });
    EXPECT_EQ(failure(parseSnapshotIndex(before, broken)), "the last entry of radar.past has no usable time");
}

TEST(SnapshotPreviousFrame, WmsDefaultRangeOfTheRealDocument) {
    // default 2026-10-15T19:45:00Z is the end of a PT15M range: the newest; the frame before is 19:30
    SnapshotDefinition d = def(SnapshotIndexType::WmsDefault, QStringLiteral("msg_fes:h60b"));
    const QByteArray body = fixture("eumetview-msg_fes-h60b-capabilities.xml");
    const auto newest = parseSnapshotIndex(d, body);
    ASSERT_TRUE(newest.has_value());
    EXPECT_TRUE(newest->newest);
    EXPECT_EQ(newest->version.toStdString(), "2026-10-15T19:45:00Z");
    d.previous = true;
    const auto prev = parseSnapshotIndex(d, body);
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(prev->version.toStdString(), "2026-10-15T19:30:00Z");
    EXPECT_EQ(isoOf(*prev).toStdString(), "2026-10-15T19:30:00Z");
    EXPECT_FALSE(prev->newest);
}

TEST(SnapshotPreviousFrame, WmtsNearestNowAmongRangesAndListedInstants) {
    struct Case {
        const char* name;
        const char* values;
        const char* now;
        const char* version; // the chosen frame
        bool newest;
        const char* previous; // its version; empty = none
    };
    const std::vector<Case> cases{
        {"the end of a range is the newest", "<Value>2026-10-19T00:00:00Z/2026-10-19T12:00:00Z/PT3H</Value>",
         "2026-10-19T11:00:00Z", "2026-10-19T12:00:00Z", true, "2026-10-19T09:00:00Z"},
        {"a later frame of the range exists", "<Value>2026-10-19T00:00:00Z/2026-10-19T12:00:00Z/PT3H</Value>",
         "2026-10-19T10:20:00Z", "2026-10-19T09:00:00Z", false, "2026-10-19T06:00:00Z"},
        {"listed instants: the previous keeps the server's text",
         "<Value>2026-10-19T10:30:00Z</Value><Value>2026-10-19T08:00:00.000Z</Value>", "2026-10-19T10:20:00Z",
         "2026-10-19T10:30:00Z", true, "2026-10-19T08:00:00.000Z"},
        {"a listed instant against a range: the range gives the previous",
         "<Value>2026-10-19T10:00:00Z</Value><Value>2026-10-18T00:00:00Z/2026-10-19T12:00:00Z/PT3H</Value>",
         "2026-10-19T10:20:00Z", "2026-10-19T10:00:00Z", false, "2026-10-19T09:00:00Z"},
        {"the first frame of a range has none before it",
         "<Value>2026-10-19T10:00:00Z/2026-10-19T10:00:00Z/PT1H</Value>", "2026-10-19T10:20:00Z",
         "2026-10-19T10:00:00Z", true, ""},
        {"a single instant", "<Value>2026-10-19T10:30:00Z</Value>", "2026-10-19T10:40:00Z",
         "2026-10-19T10:30:00Z", true, ""},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        const QByteArray doc = capsDoc(capsLayer("w", timeDim("2026-01-01T00:00:00Z", c.values)));
        const auto chosen = parseSnapshotIndex(wmtsNow(c.now, false), doc);
        ASSERT_TRUE(chosen.has_value()) << chosen.error().what().toStdString();
        EXPECT_EQ(chosen->version.toStdString(), c.version);
        EXPECT_EQ(chosen->newest, c.newest);
        const auto prev = parseSnapshotIndex(wmtsNow(c.now, true), doc);
        if (std::string(c.previous).empty()) {
            EXPECT_EQ(failure(prev), std::string("layer w has no time before ") + c.version);
            continue;
        }
        ASSERT_TRUE(prev.has_value()) << prev.error().what().toStdString();
        EXPECT_EQ(prev->version.toStdString(), c.previous);
        EXPECT_FALSE(prev->newest);
    }
}

// The default pick: the layer's default time is the newest when no listed frame is later; --time and a
// source without a list never claim it.
TEST(SnapshotPreviousFrame, WmtsDefaultPickAndTimeOption) {
    const auto doc = [](const char* def, const char* values) {
        return capsDoc(capsLayer("w", timeDim(def, values)));
    };
    const char* const values = "<Value>2026-10-19T09:00:00Z</Value><Value>2026-10-19T10:00:00Z</Value>";
    const auto top = parseSnapshotIndex(wmtsNow("2026-10-19T10:20:00Z", false, SnapshotPick::Default),
                                        doc("2026-10-19T10:00:00Z", values));
    ASSERT_TRUE(top.has_value());
    EXPECT_TRUE(top->newest);
    const auto prev = parseSnapshotIndex(wmtsNow("2026-10-19T10:20:00Z", true, SnapshotPick::Default),
                                         doc("2026-10-19T10:00:00Z", values));
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(prev->version.toStdString(), "2026-10-19T09:00:00Z");
    const auto notTop = parseSnapshotIndex(wmtsNow("2026-10-19T10:20:00Z", false, SnapshotPick::Default),
                                           doc("2026-10-19T09:00:00Z", values));
    ASSERT_TRUE(notTop.has_value());
    EXPECT_FALSE(notTop->newest);
    EXPECT_FALSE(parseSnapshotIndex(wmtsNow("2026-10-19T10:20:00Z", false, SnapshotPick::Default),
                                    doc("2026-10-19T10:00:00Z", ""))
                     ->newest); // no list of frames
    // --time names the frame: it is never the one to step back from
    SnapshotDefinition timedDef = timed(QStringLiteral("w"), "2026-10-19T10:00:00Z");
    const auto asked =
        parseSnapshotIndex(timedDef, doc("2026-10-19T10:00:00Z",
                                         "<Value>2026-10-19T09:00:00Z/2026-10-19T12:00:00Z/PT1H</Value>"));
    ASSERT_TRUE(asked.has_value());
    EXPECT_FALSE(asked->newest);
}
