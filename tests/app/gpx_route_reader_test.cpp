// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/gpx_route_reader (new code; inherits Vector/u_VectorItemTreeImporterXML.pas:162-186,
// 757-857 and VectorLoaders/u_XmlVectorObjects.pas:356-362, 490-494). Expected values are literals read by
// eye from the GPX text. The closed vsagps parser has no legacy output; inline inputs are hand-written
// (see tests/fixtures/app/gpx/README.md).
// FEAT-GEO-019 (GPX import): contributes through readGpxRoutes; the row stays in held-open.md.
#include "cg/app/gpx_route_reader.h"

#include <QBuffer>
#include <QByteArray>
#include <QFile>

#include <gtest/gtest.h>

#include <cmath>

#include "../corpus_skip.h"
#include "../maptype/maptype_test_util.h"

using cg::app::GpxReadResult;
using cg::app::readGpxRoutes;
using cg::core::ErrorCode;
using cg::geo::LonLat;

namespace {

cg::core::Result<GpxReadResult> parse(const QByteArray& xml) {
    QBuffer buffer;
    buffer.setData(xml);
    EXPECT_TRUE(buffer.open(QIODevice::ReadOnly));
    return readGpxRoutes(buffer);
}

GpxReadResult parseOk(const QByteArray& xml) {
    auto r = parse(xml);
    EXPECT_TRUE(r.has_value());
    return r.has_value() ? std::move(r.value()) : GpxReadResult{};
}

QByteArray gpx11(const QByteArray& body) {
    return "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n" +
           body + "\n</gpx>\n";
}

} // namespace

TEST(GpxRouteReader, OpenCpnFileRouteNamePointsAndExtensionsSkipped) {
    CG_SKIP_IF_NO_FIXTURE(cg::maptype::test::fixtureDir() + QStringLiteral("/app/gpx/opencpn_foo.gpx"));
    QFile file(cg::maptype::test::fixtureDir() + QStringLiteral("/app/gpx/opencpn_foo.gpx"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    auto r = readGpxRoutes(file);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->routes.size(), 1u);
    const auto& route = r->routes[0];
    EXPECT_EQ(route.name, QStringLiteral("foo")); // not "001": the rtept names are not the route's
    EXPECT_FALSE(route.isTrack);
    ASSERT_EQ(route.parts.size(), 1u);
    ASSERT_EQ(route.parts[0].size(), 13u);
    EXPECT_EQ(route.parts[0].front(), (LonLat{22.230250298, 65.547729337}));
    EXPECT_EQ(route.parts[0][5], (LonLat{22.333000414, 65.547162239}));
    EXPECT_EQ(route.parts[0].back(), (LonLat{22.333685415, 65.530900184}));
    EXPECT_EQ(r->skippedPoints, 0);
    EXPECT_EQ(r->droppedParts, 0);
}

TEST(GpxRouteReader, TrackWithTwoSegmentsIsOneTrackWithTwoParts) {
    const auto r =
        parseOk(gpx11("<trk><name>T1</name>"
                      "<trkseg><trkpt lat=\"10\" lon=\"20\"/><trkpt lat=\"11\" lon=\"21\"/></trkseg>"
                      "<trkseg><trkpt lat=\"30\" lon=\"40\"/><trkpt lat=\"31\" lon=\"41\"/>"
                      "<trkpt lat=\"32\" lon=\"42\"/></trkseg></trk>"));
    ASSERT_EQ(r.routes.size(), 1u);
    const auto& track = r.routes[0];
    EXPECT_TRUE(track.isTrack);
    EXPECT_EQ(track.name, QStringLiteral("T1"));
    ASSERT_EQ(track.parts.size(), 2u);
    EXPECT_EQ(track.parts[0].size(), 2u);
    EXPECT_EQ(track.parts[1].size(), 3u);
    EXPECT_EQ(track.parts[1][2], (LonLat{42.0, 32.0}));

    const cg::geo::GeometryLonLatFactory factory;
    const auto line = track.toLine(factory);
    const auto multi = std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatMultiLine>(line);
    ASSERT_TRUE(multi);
    ASSERT_EQ(multi->count(), 2u);
    ASSERT_EQ(multi->item(0)->points().size(), 2u);
    ASSERT_EQ(multi->item(1)->points().size(), 3u);
    EXPECT_EQ(multi->item(0)->points()[1], (LonLat{21.0, 11.0}));
    EXPECT_EQ(multi->item(1)->points()[0], (LonLat{40.0, 30.0}));
}

TEST(GpxRouteReader, SinglePartToLineIsSingleLine) {
    const auto r = parseOk(gpx11("<rte><rtept lat=\"1\" lon=\"2\"/><rtept lat=\"3\" lon=\"4\"/></rte>"));
    ASSERT_EQ(r.routes.size(), 1u);
    const cg::geo::GeometryLonLatFactory factory;
    const auto single =
        std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatSingleLine>(r.routes[0].toLine(factory));
    ASSERT_TRUE(single);
    ASSERT_EQ(single->points().size(), 2u);
    EXPECT_EQ(single->points()[1], (LonLat{4.0, 3.0}));
}

TEST(GpxRouteReader, Gpx10NamespaceAndPrefixedNamespaceAreRead) {
    const auto old =
        parseOk("<?xml version=\"1.0\"?>\n<gpx version=\"1.0\" xmlns=\"http://www.topografix.com/GPX/1/0\">"
                "<rte><name>old</name><rtept lat=\"5\" lon=\"6\"/><rtept lat=\"7\" lon=\"8\"/></rte></gpx>");
    ASSERT_EQ(old.routes.size(), 1u);
    EXPECT_EQ(old.routes[0].name, QStringLiteral("old"));
    EXPECT_EQ(old.routes[0].parts[0].size(), 2u);

    const auto prefixed =
        parseOk("<g:gpx xmlns:g=\"http://www.topografix.com/GPX/1/1\">"
                "<g:rte><g:name>pre</g:name><g:rtept lat=\"5\" lon=\"6\"/>"
                "<g:rtept lat=\"7\" lon=\"8\"/></g:rte>"
                "<g:trk><g:trkseg><g:trkpt lat=\"1\" lon=\"2\"/><g:trkpt lat=\"3\" lon=\"4\"/>"
                "</g:trkseg></g:trk></g:gpx>");
    ASSERT_EQ(prefixed.routes.size(), 2u);
    EXPECT_EQ(prefixed.routes[0].name, QStringLiteral("pre"));
    EXPECT_FALSE(prefixed.routes[0].isTrack);
    EXPECT_TRUE(prefixed.routes[1].isTrack);
}

TEST(GpxRouteReader, InvalidPointsAreSkippedAndCountedTheRestKept) {
    const auto r = parseOk(gpx11("<rte>"
                                 "<rtept lon=\"1\"/>" // no lat
                                 "<rtept lat=\"10\" lon=\"11\"/>"
                                 "<rtept lat=\"abc\" lon=\"2\"/>" // not a number
                                 "<rtept lat=\"12\" lon=\"13\"/>"
                                 "<rtept lat=\"91\" lon=\"3\"/>" // out of range
                                 "<rtept lat=\"14\" lon=\"15\"/>"
                                 "</rte>"));
    EXPECT_EQ(r.skippedPoints, 3);
    ASSERT_EQ(r.routes.size(), 1u);
    ASSERT_EQ(r.routes[0].parts[0].size(), 3u);
    EXPECT_EQ(r.routes[0].parts[0][0], (LonLat{11.0, 10.0}));
    EXPECT_EQ(r.routes[0].parts[0][1], (LonLat{13.0, 12.0}));
    EXPECT_EQ(r.routes[0].parts[0][2], (LonLat{15.0, 14.0}));
}

TEST(GpxRouteReader, RangeBoundsAreInclusiveAndLongitudeIsChecked) {
    const auto r = parseOk(gpx11("<rte>"
                                 "<rtept lat=\"90\" lon=\"180\"/>"
                                 "<rtept lat=\"-90\" lon=\"-180\"/>"
                                 "<rtept lat=\"0\" lon=\"180.5\"/>" // lon out of range
                                 "<rtept lat=\"-90.5\" lon=\"0\"/>" // lat out of range
                                 "</rte>"));
    EXPECT_EQ(r.skippedPoints, 2);
    ASSERT_EQ(r.routes.size(), 1u);
    ASSERT_EQ(r.routes[0].parts[0].size(), 2u);
    EXPECT_EQ(r.routes[0].parts[0][0], (LonLat{180.0, 90.0}));
    EXPECT_EQ(r.routes[0].parts[0][1], (LonLat{-180.0, -90.0}));
}

TEST(GpxRouteReader, OnePointRouteIsOmittedAndCounted) {
    const auto r = parseOk(gpx11("<rte><name>lone</name><rtept lat=\"1\" lon=\"2\"/></rte>"));
    EXPECT_TRUE(r.routes.empty());
    EXPECT_EQ(r.droppedParts, 1);
    EXPECT_EQ(r.skippedPoints, 0);
}

TEST(GpxRouteReader, OnePointSegmentIsDroppedOtherSegmentKeptEmptySegmentCountsNothing) {
    const auto r = parseOk(gpx11("<trk>"
                                 "<trkseg><trkpt lat=\"1\" lon=\"2\"/></trkseg>" // one point: dropped
                                 "<trkseg></trkseg>"                             // empty: nothing
                                 "<trkseg><trkpt lat=\"3\" lon=\"4\"/><trkpt lat=\"5\" lon=\"6\"/></trkseg>"
                                 "</trk>"));
    EXPECT_EQ(r.droppedParts, 1);
    ASSERT_EQ(r.routes.size(), 1u);
    ASSERT_EQ(r.routes[0].parts.size(), 1u);
    EXPECT_EQ(r.routes[0].parts[0][0], (LonLat{4.0, 3.0}));
}

TEST(GpxRouteReader, RouteWithOnlyAnEmptySegmentOrNoPointsIsOmittedWithoutCount) {
    const auto r = parseOk(gpx11("<trk><trkseg/></trk><rte/>"));
    EXPECT_TRUE(r.routes.empty());
    EXPECT_EQ(r.droppedParts, 0);
    EXPECT_EQ(r.skippedPoints, 0);
}

TEST(GpxRouteReader, PointNameAndExtensionNameDoNotReplaceTheRouteName) {
    const auto r = parseOk(gpx11("<rte><name>route</name>"
                                 "<extensions><name>ext</name></extensions>"
                                 "<rtept lat=\"1\" lon=\"2\"><name>point</name></rtept>"
                                 "<rtept lat=\"3\" lon=\"4\"><name>point2</name></rtept></rte>"
                                 "<trk><trkseg><trkpt lat=\"1\" lon=\"2\"><name>tp</name></trkpt>"
                                 "<trkpt lat=\"3\" lon=\"4\"/></trkseg><name>after</name></trk>"));
    ASSERT_EQ(r.routes.size(), 2u);
    EXPECT_EQ(r.routes[0].name, QStringLiteral("route"));
    EXPECT_EQ(r.routes[1].name, QStringLiteral("after")); // a direct child after the segment still counts
}

TEST(GpxRouteReader, NameInsideAnUnknownContainerOfTheRouteIsNotTheRouteName) {
    const auto r = parseOk(gpx11("<rte><name>route</name><link><name>deep</name></link>"
                                 "<rtept lat=\"1\" lon=\"2\"/><rtept lat=\"3\" lon=\"4\"/></rte>"));
    ASSERT_EQ(r.routes.size(), 1u);
    EXPECT_EQ(r.routes[0].name, QStringLiteral("route"));
}

TEST(GpxRouteReader, WaypointsAndMetadataAreSkipped) {
    const auto r = parseOk(gpx11("<metadata><name>meta</name><bounds minlat=\"0\"/></metadata>"
                                 "<wpt lat=\"50\" lon=\"50\"><name>w</name></wpt>"
                                 "<wpt lat=\"abc\" lon=\"50\"/>"
                                 "<rte><rtept lat=\"1\" lon=\"2\"/><rtept lat=\"3\" lon=\"4\"/></rte>"));
    ASSERT_EQ(r.routes.size(), 1u);
    EXPECT_TRUE(r.routes[0].name.isEmpty());
    EXPECT_EQ(r.routes[0].parts[0].size(), 2u);
    EXPECT_EQ(r.skippedPoints, 0); // the broken wpt is not a route point
}

TEST(GpxRouteReader, TruncatedXmlGivesParseErrorWithLineNumber) {
    const QByteArray xml = "<?xml version=\"1.0\"?>\n<gpx>\n<rte>\n<rtept lat=\"1\" lon=\"2\"";
    const auto r = parse(xml);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::ParseError);
    EXPECT_TRUE(r.error().what().startsWith(QStringLiteral("GPX line 4:"))) << r.error().what().toStdString();
}

TEST(GpxRouteReader, MismatchedTagGivesParseErrorAtItsLine) {
    const QByteArray xml = "<gpx>\n<rte>\n<rtept lat=\"1\" lon=\"2\"/>\n</trk>\n</gpx>\n";
    const auto r = parse(xml);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::ParseError);
    EXPECT_TRUE(r.error().what().startsWith(QStringLiteral("GPX line 4:"))) << r.error().what().toStdString();
}

TEST(GpxRouteReader, RoutesComeBeforeTracksWhateverTheFileOrder) {
    const auto r = parseOk(gpx11(
        "<trk><name>t1</name><trkseg><trkpt lat=\"1\" lon=\"2\"/><trkpt lat=\"3\" lon=\"4\"/></trkseg></trk>"
        "<rte><name>r1</name><rtept lat=\"1\" lon=\"2\"/><rtept lat=\"3\" lon=\"4\"/></rte>"
        "<trk><name>t2</name><trkseg><trkpt lat=\"1\" lon=\"2\"/><trkpt lat=\"3\" lon=\"4\"/></trkseg></trk>"
        "<rte><name>r2</name><rtept lat=\"1\" lon=\"2\"/><rtept lat=\"3\" lon=\"4\"/></rte>"));
    ASSERT_EQ(r.routes.size(), 4u);
    EXPECT_EQ(r.routes[0].name, QStringLiteral("r1"));
    EXPECT_EQ(r.routes[1].name, QStringLiteral("r2"));
    EXPECT_EQ(r.routes[2].name, QStringLiteral("t1"));
    EXPECT_EQ(r.routes[3].name, QStringLiteral("t2"));
    EXPECT_FALSE(r.routes[1].isTrack);
    EXPECT_TRUE(r.routes[2].isTrack);
}
