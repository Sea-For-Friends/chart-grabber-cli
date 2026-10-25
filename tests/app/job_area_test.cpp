// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/job_area (new code; inherits MainForm/frm_Main.pas:7624-7636 and the ports of
// u_EnumDoublePointLine2Poly.pas:269-286, u_GeometryLonLatFactory.pas:1123-1150,
// u_ConfigProviderHelpers.pas:239-419). No FEAT tag (T-0188: Features none). Expected values are literals:
// the Pascal passes the selection-shadow RADIUS to the filter; the port halves the total width (Q-5), so the
// corridor of widthNm is 2 * radius = widthNm * 1852 metres wide.
#include "cg/app/job_area.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/geo/config_provider_helpers.h"
#include "cg/geo/datum.h"
#include "cg/geo/projection_set.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using cg::app::AreaKind;
using cg::app::buildArea;
using cg::app::Job;
using cg::geo::GeometryLonLatFactory;
using cg::geo::IGeometryLonLatMultiPolygon;
using cg::geo::IGeometryLonLatSinglePolygon;
using cg::geo::LonLat;

namespace {

using Pts = std::vector<LonLat>;

struct Rig {
    GeometryLonLatFactory factory;
    cg::geo::ProjectionSetPtr set =
        cg::geo::ProjectionSetFactory{cg::geo::HashSeed{},
                                      std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})}
            .byCode(3857, cg::geo::kTileSplitQuadrate256x256);
    const cg::geo::IProjection& projection() const { return *set->zoom(10); }
    QStringList warnings;
    std::expected<std::shared_ptr<const cg::geo::IGeometryLonLatPolygon>, cg::app::CliError>
    build(const Job& job) {
        return buildArea(job, factory, projection(), [this](const QString& w) { warnings.push_back(w); });
    }
};

Job bboxJob(double west, double south, double east, double north) {
    Job job;
    job.areaKind = AreaKind::BBox;
    job.bbox = {west, north, east, south}; // left, top, right, bottom
    return job;
}

Job routeJob(const QString& file, const QString& name = {}, double widthNm = 4.0) {
    Job job;
    job.areaKind = AreaKind::Route;
    job.routeFile = file;
    job.routeName = name;
    job.corridorNm = widthNm;
    return job;
}

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& content) {
    const QString path = dir.filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
    return path;
}

QByteArray gpx(const QByteArray& body) {
    return "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n" +
           body + "\n</gpx>\n";
}

QByteArray rtept(double lon, double lat) {
    return "<rtept lat=\"" + QByteArray::number(lat, 'f', 6) + "\" lon=\"" + QByteArray::number(lon, 'f', 6) +
           "\"/>";
}

QByteArray route(const QByteArray& name, const std::vector<LonLat>& pts) {
    QByteArray out = "<rte><name>" + name + "</name>";
    for (const LonLat& p : pts) {
        out += rtept(p.lon, p.lat);
    }
    return out + "</rte>";
}

std::vector<LonLat> outerOf(const std::shared_ptr<const cg::geo::IGeometryLonLatPolygon>& polygon) {
    const auto single = std::dynamic_pointer_cast<const IGeometryLonLatSinglePolygon>(polygon);
    EXPECT_TRUE(single != nullptr);
    if (!single) {
        return {};
    }
    const auto points = single->outerBorder()->points();
    return {points.begin(), points.end()};
}

// Even-odd ray cast in lon/lat (the legs are short, the polygons small).
bool inside(const std::vector<LonLat>& ring, LonLat p) {
    bool in = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        if ((ring[i].lat > p.lat) != (ring[j].lat > p.lat) &&
            p.lon < (ring[j].lon - ring[i].lon) * (p.lat - ring[i].lat) / (ring[j].lat - ring[i].lat) +
                        ring[i].lon) {
            in = !in;
        }
    }
    return in;
}

} // namespace

// ---- Proof 1: rectangle ------------------------------------------------------------------------

TEST(JobArea, RectangleBoundsEqualTheBox) {
    Rig rig;
    const auto area = rig.build(bboxJob(-10.5, -20.25, 30.75, 40.125));
    ASSERT_TRUE(area.has_value());
    const auto bounds = (*area)->bounds();
    EXPECT_EQ(bounds.left, -10.5);
    EXPECT_EQ(bounds.bottom, -20.25);
    EXPECT_EQ(bounds.right, 30.75);
    EXPECT_EQ(bounds.top, 40.125);
    EXPECT_TRUE(rig.warnings.isEmpty());
}

TEST(JobArea, RectangleAcceptsTheLimitsThemselves) {
    Rig rig;
    const auto area = rig.build(bboxJob(-180, -85.0511, 180, 85.0511));
    ASSERT_TRUE(area.has_value());
    EXPECT_EQ((*area)->bounds().right, 180.0);
    EXPECT_EQ((*area)->bounds().top, 85.0511);
}

TEST(JobArea, RectangleRefusesWestNotBelowEastAndSouthNotBelowNorth) {
    const QString text =
        QStringLiteral("bounding box: west must be below east and south below north (an area "
                       "across the 180° meridian is not supported)");
    struct Case {
        double w, s, e, n;
    };
    // West equal to east, west above east, south equal to north, south above north: each alone.
    for (const Case c : {Case{10, 1, 10, 2}, Case{20, 1, 10, 2}, Case{1, 5, 2, 5}, Case{1, 6, 2, 5}}) {
        Rig rig;
        const auto area = rig.build(bboxJob(c.w, c.s, c.e, c.n));
        ASSERT_FALSE(area.has_value()) << c.w << " " << c.s << " " << c.e << " " << c.n;
        EXPECT_EQ(area.error().exitCode, 3);
        EXPECT_EQ(area.error().message, text);
    }
}

TEST(JobArea, RectangleRefusesEachValueOutsideItsRangeNamingIt) {
    struct Case {
        double w, s, e, n;
        const char* what;
        const char* value;
        const char* limit;
    };
    const Case cases[] = {
        {-181, 0, 10, 10, "west", "-181", "180"},           {0, 0, 181, 10, "east", "181", "180"},
        {0, -86, 10, 10, "south", "-86", "85.0511"},        {0, 0, 10, 86, "north", "86", "85.0511"},
        {0, 0, 10, 85.0512, "north", "85.0512", "85.0511"},
    };
    for (const Case& c : cases) {
        Rig rig;
        const auto area = rig.build(bboxJob(c.w, c.s, c.e, c.n));
        ASSERT_FALSE(area.has_value()) << c.what;
        EXPECT_EQ(area.error().exitCode, 3);
        EXPECT_EQ(area.error().message,
                  QStringLiteral("bounding box: %1 value %2 is outside +-%3")
                      .arg(QLatin1String(c.what), QLatin1String(c.value), QLatin1String(c.limit)))
            << c.what;
    }
}

// ---- Proof 2: .hlg -----------------------------------------------------------------------------

namespace {

QString writeHlg(const QTemporaryDir& dir, const QString& section, const Pts& outer,
                 const GeometryLonLatFactory& f) {
    auto root = cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());
    auto writer = root->orCreateSubItem(section);
    if (!outer.empty()) {
        cg::geo::writePolygon(*writer, std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatPolygon>(
                                           f.createPolygon(outer)));
    }
    const QString path = dir.filePath(QStringLiteral("area.hlg"));
    EXPECT_TRUE(root->file().saveAs(path).has_value());
    return path;
}

Job hlgJob(const QString& path) {
    Job job;
    job.areaKind = AreaKind::PolygonFile;
    job.polygonFile = path;
    return job;
}

} // namespace

TEST(JobArea, PolygonFileRoundTripGivesTheSamePoints) {
    Rig rig;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const Pts outer{{10.5, 20.25}, {30.5, 40.25}, {50.5, 20.75}, {30.25, 10.5}};
    const auto area = rig.build(hlgJob(writeHlg(dir, QStringLiteral("HIGHLIGHTING"), outer, rig.factory)));
    ASSERT_TRUE(area.has_value());
    EXPECT_EQ(outerOf(*area), outer);
}

TEST(JobArea, PolygonFileWithoutTheSectionIsRefused) {
    Rig rig;
    QTemporaryDir dir;
    const QString path = writeHlg(dir, QStringLiteral("OTHER"), {{1, 2}, {3, 4}, {5, 2}}, rig.factory);
    const auto area = rig.build(hlgJob(path));
    ASSERT_FALSE(area.has_value());
    EXPECT_EQ(area.error().exitCode, 3);
    EXPECT_EQ(area.error().message, QStringLiteral("no [HIGHLIGHTING] section in %1").arg(path));
}

TEST(JobArea, PolygonFileWithAnEmptySectionIsRefused) {
    Rig rig;
    QTemporaryDir dir;
    const QString path = writeFile(dir, QStringLiteral("empty.hlg"), "[HIGHLIGHTING]\nSomething=1\n");
    const auto area = rig.build(hlgJob(path));
    ASSERT_FALSE(area.has_value());
    EXPECT_EQ(area.error().exitCode, 3);
    EXPECT_EQ(area.error().message, QStringLiteral("no polygon in %1").arg(path));
}

// ---- Proof 3: corridor width -------------------------------------------------------------------

namespace {

// The corridor of a two-point leg is a quadrilateral with flat ends (frm_Main.pas:7624-7636 gives line2Poly a
// radius). Its width across the middle of the leg: the geodesic distance between the midpoints of the two
// long sides. The long side of a start corner is the edge to the NEARER end corner.
double widthAcrossTheMiddle(const std::vector<LonLat>& ring, LonLat from, LonLat to) {
    const cg::geo::DatumFactory datums{cg::geo::HashSeed{}};
    const auto wgs84 = datums.byCode(6326);
    std::vector<LonLat> start;
    std::vector<LonLat> end;
    for (const LonLat& corner : ring) {
        (wgs84->calcDist(corner, from) < wgs84->calcDist(corner, to) ? start : end).push_back(corner);
    }
    EXPECT_EQ(start.size(), 2u);
    EXPECT_EQ(end.size(), 2u);
    if (start.size() != 2 || end.size() != 2) {
        return 0;
    }
    const auto nearer = [&](LonLat s) {
        return wgs84->calcDist(s, end[0]) < wgs84->calcDist(s, end[1]) ? end[0] : end[1];
    };
    const auto middle = [](LonLat a, LonLat b) { return LonLat{(a.lon + b.lon) / 2, (a.lat + b.lat) / 2}; };
    return wgs84->calcDist(middle(start[0], nearer(start[0])), middle(start[1], nearer(start[1])));
}

struct Leg {
    const char* what;
    LonLat from, to;
};

} // namespace

TEST(JobArea, CorridorIsTheTotalWidthAcrossTheMiddleOfTheLeg) {
    // Meridian, equator-parallel and diagonal legs at 0, 45 and 60 degrees of latitude.
    const Leg legs[] = {
        {"meridian 0", {10, 0}, {10, 0.3}},      {"parallel 0", {10, -0.1}, {10.3, -0.1}},
        {"diagonal 0", {10, 0}, {10.2, 0.2}},    {"meridian 45", {10, 45}, {10, 45.3}},
        {"parallel 45", {10, 45}, {10.4, 45}},   {"diagonal 45", {10, 45}, {10.3, 45.2}},
        {"meridian 60", {10, 60}, {10, 60.3}},   {"parallel 60", {10, 60}, {10.6, 60}},
        {"diagonal 60", {10, 60}, {10.4, 60.2}},
    };
    constexpr double widthNm = 4.0;
    for (const Leg& leg : legs) {
        Rig rig;
        QTemporaryDir dir;
        const QString file = writeFile(dir, QStringLiteral("r.gpx"), gpx(route("r", {leg.from, leg.to})));
        const auto area = rig.build(routeJob(file, {}, widthNm));
        ASSERT_TRUE(area.has_value()) << leg.what;
        const auto ring = outerOf(*area);
        ASSERT_EQ(ring.size(), 4u) << leg.what;
        const double width = widthAcrossTheMiddle(ring, leg.from, leg.to);
        // TOTAL width: 4 nm = 7408 m (a radius of 4 nm would give 14816 m).
        EXPECT_NEAR(width, 7408.0, 74.08) << leg.what << " measured " << width;
        // The leg lies inside, one step in from each flat end (the ends themselves are on the border).
        const auto along = [&](double t) {
            return LonLat{leg.from.lon + t * (leg.to.lon - leg.from.lon),
                          leg.from.lat + t * (leg.to.lat - leg.from.lat)};
        };
        for (const double t : {0.01, 0.25, 0.5, 0.75, 0.99}) {
            EXPECT_TRUE(inside(ring, along(t))) << leg.what << " t=" << t;
        }
    }
}

// ---- Proof 4: legs and parts -------------------------------------------------------------------

TEST(JobArea, ThreeLegRouteGivesOneSinglePolygonContainingEveryRoutePoint) {
    Rig rig;
    QTemporaryDir dir;
    const std::vector<LonLat> pts{{10, 45}, {10.3, 45.1}, {10.5, 45.4}, {10.9, 45.5}};
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), gpx(route("r", pts)));
    const auto area = rig.build(routeJob(file, {}, 2.0));
    ASSERT_TRUE(area.has_value());
    const auto ring = outerOf(*area); // dynamic_cast to a single polygon: one polygon, not a multi-polygon
    ASSERT_GE(ring.size(), 6u);
    EXPECT_TRUE(inside(ring, pts[1]));
    EXPECT_TRUE(inside(ring, pts[2]));
    EXPECT_TRUE(inside(ring, {10.001, 45.0003})); // just in from the first end
    EXPECT_TRUE(inside(ring, {10.899, 45.4997})); // just in from the last end
    EXPECT_FALSE(inside(ring, {10.3, 45.4}));     // between the legs, far from both
    EXPECT_TRUE(rig.warnings.isEmpty());
}

TEST(JobArea, TrackWithTwoSegmentsGivesATwoPartPolygonAndTheWarning) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(
        dir, QStringLiteral("t.gpx"),
        gpx("<trk><name>T</name>"
            "<trkseg><trkpt lat=\"45\" lon=\"10\"/><trkpt lat=\"45\" lon=\"10.2\"/></trkseg>"
            "<trkseg><trkpt lat=\"46\" lon=\"12\"/><trkpt lat=\"46\" lon=\"12.2\"/></trkseg></trk>"));
    const auto area = rig.build(routeJob(file));
    ASSERT_TRUE(area.has_value());
    const auto multi = std::dynamic_pointer_cast<const IGeometryLonLatMultiPolygon>(*area);
    ASSERT_TRUE(multi != nullptr);
    EXPECT_EQ(multi->count(), 2u);
    EXPECT_EQ(rig.warnings, (QStringList{QStringLiteral(
                                "the route has 2 parts; a resumed session covers the first part only "
                                "(legacy .sls format)")}));
}

// ---- Proof 5: route choice ---------------------------------------------------------------------

namespace {

QByteArray twoRoutesAndATrack() {
    return gpx(route("Альфа", {{10, 45}, {10.2, 45}}) + route("beta", {{20, 50}, {20.2, 50}}) +
               "<trk><name>trk</name><trkseg><trkpt lat=\"30\" lon=\"30\"/><trkpt lat=\"30\" "
               "lon=\"30.2\"/></trkseg></trk>");
}

double centreLon(const std::vector<LonLat>& ring) {
    double sum = 0;
    for (const LonLat& p : ring) {
        sum += p.lon;
    }
    return sum / static_cast<double>(ring.size());
}

} // namespace

TEST(JobArea, RouteNamePicksTheNamedRoute) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), twoRoutesAndATrack());
    const auto area = rig.build(routeJob(file, QStringLiteral("beta")));
    ASSERT_TRUE(area.has_value());
    EXPECT_NEAR(centreLon(outerOf(*area)), 20.1, 0.01);
    const auto track = rig.build(routeJob(file, QStringLiteral("trk")));
    ASSERT_TRUE(track.has_value());
    EXPECT_NEAR(centreLon(outerOf(*track)), 30.1, 0.01);
}

TEST(JobArea, RouteNameMatchesNonAsciiExactly) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), twoRoutesAndATrack());
    const auto exact = rig.build(routeJob(file, QStringLiteral("Альфа")));
    ASSERT_TRUE(exact.has_value());
    EXPECT_NEAR(centreLon(outerOf(*exact)), 10.1, 0.01);
}

TEST(JobArea, RouteNameIsCaseSensitive) {
    // Documented rule (ticket: "exact, case-sensitive match on name").
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), twoRoutesAndATrack());
    for (const QString& wrong : {QStringLiteral("BETA"), QStringLiteral("альфа")}) {
        const auto area = rig.build(routeJob(file, wrong));
        ASSERT_FALSE(area.has_value()) << wrong.toStdString();
        EXPECT_EQ(area.error().exitCode, 3);
    }
}

TEST(JobArea, UnknownRouteNameListsTheNamesFound) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), twoRoutesAndATrack());
    const auto area = rig.build(routeJob(file, QStringLiteral("gamma")));
    ASSERT_FALSE(area.has_value());
    EXPECT_EQ(area.error().exitCode, 3);
    EXPECT_EQ(area.error().message,
              QStringLiteral("no route named \"gamma\" in %1; found: Альфа, beta, trk").arg(file));
}

TEST(JobArea, WithoutANameTheFirstRouteIsTakenBeforeAnyTrack) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), twoRoutesAndATrack());
    const auto area = rig.build(routeJob(file));
    ASSERT_TRUE(area.has_value());
    EXPECT_NEAR(centreLon(outerOf(*area)), 10.1, 0.01);
}

TEST(JobArea, WithoutANameAFileWithOnlyTracksTakesTheFirstTrack) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(
        dir, QStringLiteral("t.gpx"),
        gpx("<trk><name>one</name><trkseg><trkpt lat=\"40\" lon=\"5\"/><trkpt lat=\"40\" lon=\"5.2\"/>"
            "</trkseg></trk><trk><name>two</name><trkseg><trkpt lat=\"41\" lon=\"6\"/>"
            "<trkpt lat=\"41\" lon=\"6.2\"/></trkseg></trk>"));
    const auto area = rig.build(routeJob(file));
    ASSERT_TRUE(area.has_value());
    EXPECT_NEAR(centreLon(outerOf(*area)), 5.1, 0.01);
}

// ---- Proof 6: skipped points, and the other refusals -------------------------------------------

TEST(JobArea, SkippedPointsGiveExactlyOneWarningWithBothCounts) {
    Rig rig;
    QTemporaryDir dir;
    const QString file =
        writeFile(dir, QStringLiteral("r.gpx"),
                  gpx("<rte><name>r</name><rtept lat=\"45\" lon=\"10\"/><rtept lat=\"95\" lon=\"10\"/>"
                      "<rtept lat=\"x\" lon=\"10\"/><rtept lat=\"45\" lon=\"10.2\"/></rte>"
                      "<trk><name>t</name><trkseg><trkpt lat=\"1\" lon=\"1\"/></trkseg></trk>"));
    const auto area = rig.build(routeJob(file));
    ASSERT_TRUE(area.has_value());
    EXPECT_EQ(rig.warnings, (QStringList{QStringLiteral("%1: 2 point(s) with an invalid position skipped, 1 "
                                                        "part(s) with a single point dropped")
                                             .arg(file)}));
}

TEST(JobArea, ADroppedSinglePointPartAloneGivesTheWarningWithZeroSkipped) {
    Rig rig;
    QTemporaryDir dir;
    const QString file =
        writeFile(dir, QStringLiteral("r.gpx"),
                  gpx(route("r", {{10, 45}, {10.2, 45}}) +
                      "<trk><name>t</name><trkseg><trkpt lat=\"1\" lon=\"1\"/></trkseg></trk>"));
    ASSERT_TRUE(rig.build(routeJob(file)).has_value());
    EXPECT_EQ(rig.warnings, (QStringList{QStringLiteral("%1: 0 point(s) with an invalid position skipped, 1 "
                                                        "part(s) with a single point dropped")
                                             .arg(file)}));
}

TEST(JobArea, ACleanRouteGivesNoWarning) {
    Rig rig;
    QTemporaryDir dir;
    const QString file = writeFile(dir, QStringLiteral("r.gpx"), gpx(route("r", {{10, 45}, {10.2, 45}})));
    ASSERT_TRUE(rig.build(routeJob(file)).has_value());
    EXPECT_TRUE(rig.warnings.isEmpty());
}

TEST(JobArea, RouteFileThatIsMissingOrNotXmlOrWithoutRoutesIsRefused) {
    Rig rig;
    QTemporaryDir dir;
    const QString missing = dir.filePath(QStringLiteral("none.gpx"));
    const auto noFile = rig.build(routeJob(missing));
    ASSERT_FALSE(noFile.has_value());
    EXPECT_EQ(noFile.error().exitCode, 3);
    EXPECT_TRUE(noFile.error().message.startsWith(missing + QStringLiteral(": ")));

    const auto broken = rig.build(routeJob(writeFile(dir, QStringLiteral("b.gpx"), "<gpx><rte>")));
    ASSERT_FALSE(broken.has_value());
    EXPECT_EQ(broken.error().exitCode, 2); // a parse error is a usage error, not an area error

    const QString emptyFile = writeFile(dir, QStringLiteral("e.gpx"), gpx("<wpt lat=\"1\" lon=\"1\"/>"));
    const auto none = rig.build(routeJob(emptyFile));
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().exitCode, 3);
    EXPECT_EQ(none.error().message, QStringLiteral("no route or track in %1").arg(emptyFile));
}

TEST(JobArea, RouteWithFewerThanTwoPointsIsRefused) {
    // The reader drops a one-point part and an empty route, so no route is left in the file. The "fewer than
    // two points" refusal of buildArea cannot be reached from a file the reader accepts; it has no test.
    Rig rig;
    QTemporaryDir dir;
    for (const QByteArray& body : {route("r", {{10, 45}}), QByteArray("<rte><name>r</name></rte>")}) {
        const QString file = writeFile(dir, QStringLiteral("r.gpx"), gpx(body));
        const auto area = rig.build(routeJob(file));
        ASSERT_FALSE(area.has_value());
        EXPECT_EQ(area.error().exitCode, 3);
        EXPECT_EQ(area.error().message, QStringLiteral("no route or track in %1").arg(file));
    }
}

TEST(JobArea, RouteSpanningMoreThan180DegreesIsRefusedButExactly180Passes) {
    Rig rig;
    QTemporaryDir dir;
    const QString exact =
        writeFile(dir, QStringLiteral("a.gpx"), gpx(route("r", {{-90, 10}, {0, 10}, {90, 10}})));
    EXPECT_TRUE(rig.build(routeJob(exact)).has_value());
    const QString over =
        writeFile(dir, QStringLiteral("b.gpx"), gpx(route("r", {{-90.5, 10}, {0, 10}, {90, 10}})));
    const auto area = rig.build(routeJob(over));
    ASSERT_FALSE(area.has_value());
    EXPECT_EQ(area.error().exitCode, 3);
    EXPECT_EQ(area.error().message,
              QStringLiteral("the route in %1 spans more than 180° of longitude (an area across the 180° "
                             "meridian is not supported)")
                  .arg(over));
}
