// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/sml/geometry_to_stream_sml.h"

#include "cg/marks/sml/geometry_point_sml.h"

#include <cstring>
#include <optional>

namespace cg::marks {

namespace {

using geo::LonLat;

void appendRecord(QByteArray& out, const std::uint8_t* x, const std::uint8_t* y) {
    GeometryPointSml rec{}; // Reserved stays 0
    std::memcpy(rec.x, x, kExtended80Size);
    std::memcpy(rec.y, y, kExtended80Size);
    out.append(reinterpret_cast<const char*>(&rec), static_cast<qsizetype>(sizeof rec));
}

// DoubleToSmlPoint (:94-99) and the inline widening of the vertex loops.
void appendVertex(QByteArray& out, const LonLat& p) {
    std::uint8_t x[kExtended80Size];
    std::uint8_t y[kExtended80Size];
    doubleToExtended80(p.lon, x);
    doubleToExtended80(p.lat, y);
    appendRecord(out, x, y);
}

void appendEmpty(QByteArray& out) { // CEmptySmlPoint (:88-92)
    appendRecord(out, kExtended80NaN, kExtended80NaN);
}

void saveSingleLine(const geo::IGeometryLonLatSingleLine& line, QByteArray& out) { // :158-174
    for (const LonLat& p : line.points()) {
        appendVertex(out, p);
    }
}

void saveLine(const geo::IGeometryLonLatLine& line, QByteArray& out) { // :136-156
    if (const auto* single = dynamic_cast<const geo::IGeometryLonLatSingleLine*>(&line)) {
        saveSingleLine(*single, out);
    } else if (const auto* multi = dynamic_cast<const geo::IGeometryLonLatMultiLine*>(&line)) {
        if (multi->count() >
            0) { // legacy: Assert(Count > 0), then Item[0] (:184-186); see the D row of T-0109
            saveSingleLine(*multi->item(0), out);
            for (std::size_t i = 1; i < multi->count(); ++i) {
                appendEmpty(out);
                saveSingleLine(*multi->item(i), out);
            }
        }
    }
    if (!out.isEmpty()) {
        appendEmpty(out);
    }
}

// SaveContour (:218-241): the legacy contour enumerator yields the vertices and then the first one again
// (u_EnumDoublePointBySingleLine.pas:141-147, closed and count > 1). Returns the first vertex.
std::optional<LonLat> saveContour(const geo::IGeometryLonLatContour& contour, QByteArray& out) {
    const auto pts = contour.points();
    if (pts.empty()) {
        return std::nullopt;
    }
    for (const LonLat& p : pts) {
        appendVertex(out, p);
    }
    if (pts.size() > 1) {
        appendVertex(out, pts.front());
    }
    return pts.front();
}

// SaveSinglePolygon (:243-266).
std::optional<LonLat> saveSinglePolygon(const geo::IGeometryLonLatSinglePolygon& polygon, QByteArray& out) {
    const std::optional<LonLat> first = saveContour(*polygon.outerBorder(), out);
    for (std::size_t i = 0; i < polygon.holeCount(); ++i) {
        appendRecord(out, kExtended80NaN, kExtended80MinusOne);
        saveContour(*polygon.holeBorder(i), out);
    }
    return first;
}

} // namespace

QByteArray saveGeometrySml(const geo::IGeometryLonLat* geometry) {
    QByteArray out;
    if (geometry == nullptr) {
        return out;
    }
    if (const auto* point = dynamic_cast<const geo::IGeometryLonLatPoint*>(geometry)) {
        appendVertex(out, point->point());
    } else if (const auto* line = dynamic_cast<const geo::IGeometryLonLatLine*>(geometry)) {
        saveLine(*line, out);
    } else if (const auto* polygon = dynamic_cast<const geo::IGeometryLonLatPolygon*>(geometry)) {
        // SavePolygon (:193-216)
        std::optional<LonLat> first;
        if (const auto* single = dynamic_cast<const geo::IGeometryLonLatSinglePolygon*>(polygon)) {
            first = saveSinglePolygon(*single, out);
        } else if (const auto* multi = dynamic_cast<const geo::IGeometryLonLatMultiPolygon*>(polygon)) {
            if (multi->count() > 0) { // :279-284
                first = saveSinglePolygon(*multi->item(0), out);
                for (std::size_t i = 1; i < multi->count(); ++i) {
                    appendEmpty(out);
                    saveSinglePolygon(*multi->item(i), out);
                }
            }
        }
        // :210 -- the closing record. Legacy reads an uninitialised first vertex when no contour had a
        // vertex; the port writes no closing record then (handoff D row).
        if (!out.isEmpty() && first) {
            appendVertex(out, *first);
        }
    }
    return out;
}

} // namespace cg::marks
