// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). See gpx_route_reader.h.
#include "cg/app/gpx_route_reader.h"

#include <QXmlStreamReader>

#include <cmath>

namespace cg::app {

geo::GeometryLonLatPtr GpxRoute::toLine(const geo::GeometryLonLatFactory& factory) const {
    std::vector<geo::LonLat> joined;
    for (const auto& part : parts) {
        if (!joined.empty()) {
            joined.push_back(geo::LonLat::fromPoint(geo::emptyDoublePoint()));
        }
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return factory.createLine(joined, nullptr);
}

namespace {

// GetPointForGPX (u_VectorItemTreeImporterXML.pas:162-186): the point is used only when its
// position is valid; the closed parser's PositionOK is replaced by "lat and lon parse and are in range".
bool readPoint(const QXmlStreamAttributes& attributes, geo::LonLat& out) {
    bool latOk = false;
    bool lonOk = false;
    const double lat = attributes.value(QStringLiteral("lat")).toDouble(&latOk);
    const double lon = attributes.value(QStringLiteral("lon")).toDouble(&lonOk);
    if (!latOk || !lonOk || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90.0 ||
        std::abs(lon) > 180.0) {
        return false;
    }
    out = {lon, lat};
    return true;
}

class Reader {
public:
    explicit Reader(QIODevice& device) : xml_(&device) {}

    core::Result<GpxReadResult> run();

private:
    void onStart();
    void onEnd();
    void onPoint();
    void closePart();
    void closeRoute();

    QXmlStreamReader xml_;
    GpxReadResult result_;
    std::vector<GpxRoute> tracks_;
    GpxRoute current_;
    std::vector<geo::LonLat> part_;
    bool inRoute_ = false;
    bool inPart_ = false;
    int depth_ = 0;      // open elements
    int routeDepth_ = 0; // depth_ of the open rte / trk element
};

core::Result<GpxReadResult> Reader::run() {
    while (!xml_.atEnd()) {
        switch (xml_.readNext()) {
        case QXmlStreamReader::StartElement:
            onStart();
            break;
        case QXmlStreamReader::EndElement:
            onEnd();
            break;
        default:
            break;
        }
    }
    if (xml_.hasError()) {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("GPX line %1: %2").arg(xml_.lineNumber()).arg(xml_.errorString()));
    }
    for (auto& track : tracks_) {
        result_.routes.push_back(std::move(track));
    }
    return std::move(result_);
}

void Reader::onStart() {
    const QStringView name = xml_.name();
    if (name == u"metadata" || name == u"wpt" ||
        name == u"extensions") { // c_GPX_Skipped :469-470; wpt :838-855
        xml_.skipCurrentElement();
        return;
    }
    ++depth_;
    if (!inRoute_ && (name == u"rte" || name == u"trk")) { // :769-772, :808-812
        inRoute_ = true;
        routeDepth_ = depth_;
        current_ = {};
        current_.isTrack = (name == u"trk");
        if (!current_.isTrack) {
            inPart_ = true; // OpenTrackSegment on rte open :815
            part_.clear();
        }
    } else if (inRoute_ && current_.isTrack && !inPart_ && depth_ == routeDepth_ + 1 &&
               name == u"trkseg") { // :797-801
        inPart_ = true;
        part_.clear();
    } else if (inRoute_ && (name == u"trkpt" || name == u"rtept")) { // :786-796, :827-837
        onPoint();
    } else if (inRoute_ && depth_ == routeDepth_ + 1 && name == u"name") {
        current_.name = xml_.readElementText(QXmlStreamReader::SkipChildElements);
        --depth_; // readElementText consumed the end element
    }
}

void Reader::onPoint() {
    // A trkpt outside a trkseg is ignored (see the handoff: legacy keeps it in an aggregator).
    geo::LonLat point;
    if (inPart_) {
        if (readPoint(xml_.attributes(), point)) {
            part_.push_back(point);
        } else {
            ++result_.skippedPoints;
        }
    }
    xml_.skipCurrentElement();
    --depth_;
}

void Reader::onEnd() {
    const QStringView name = xml_.name();
    if (inRoute_ && depth_ == routeDepth_ + 1 && name == u"trkseg" && current_.isTrack) {
        closePart();
    } else if (inRoute_ && depth_ == routeDepth_ && (name == u"rte" || name == u"trk")) {
        if (!current_.isTrack) {
            closePart();
        }
        closeRoute();
    }
    --depth_;
}

// CloseTrackSegment (u_XmlVectorObjects.pas:356-362): a part with no point adds nothing.
void Reader::closePart() {
    if (!inPart_) {
        return;
    }
    inPart_ = false;
    if (part_.size() == 1) {
        ++result_.droppedParts;
    } else if (part_.size() >= 2) {
        current_.parts.push_back(std::move(part_));
    }
    part_.clear();
}

void Reader::closeRoute() {
    inRoute_ = false;
    if (current_.parts.empty()) {
        return;
    }
    if (current_.isTrack) {
        tracks_.push_back(std::move(current_));
    } else {
        result_.routes.push_back(std::move(current_));
    }
}

} // namespace

core::Result<GpxReadResult> readGpxRoutes(QIODevice& device) {
    return Reader(device).run();
}

} // namespace cg::app
