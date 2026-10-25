// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/coord_converter_simple_by_projection_set.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace cg::maptype {

namespace {

geo::LonLat toLonLat(script::DoublePoint p) {
    return {p.x, p.y};
}

script::DoublePoint fromDoublePoint(geo::DoublePoint p) {
    return {p.x(), p.y()};
}

script::DoublePoint fromLonLat(geo::LonLat p) {
    return {p.lon, p.lat};
}

script::Point fromPoint(geo::PointI p) {
    return {p.x(), p.y()};
}

geo::PointI toPoint(script::Point p) {
    return {p.x, p.y};
}

// Zoom arguments above 23 address pixel positions of zoom - 8 (:103-109, :139-145).
constexpr quint8 kMaxTileZoom = 23;
constexpr unsigned kPixelZoomShift = 8;

} // namespace

CoordConverterSimpleByProjectionSet::CoordConverterSimpleByProjectionSet(geo::ProjectionSetPtr projectionSet)
    : projectionSet_(std::move(projectionSet)) {
    if (projectionSet_ == nullptr) {
        throw std::invalid_argument("CoordConverterSimpleByProjectionSet: null projection set");
    }
    projectionType_ = projectionSet_->zoom(0)->projectionType(); // :86
}

const geo::IProjection& CoordConverterSimpleByProjectionSet::projection(unsigned index) const {
    if (index >= projectionSet_->zoomCount()) {
        throw std::out_of_range("List index out of bounds (" + std::to_string(index) + ")");
    }
    return *projectionSet_->zoom(static_cast<quint8>(index));
}

script::DoublePoint CoordConverterSimpleByProjectionSet::lonLat2Metr(script::DoublePoint ll) const {
    return fromDoublePoint(projectionType_->lonLat2Metr(toLonLat(ll))); // :89-94
}

script::Point CoordConverterSimpleByProjectionSet::lonLat2Pos(script::DoublePoint ll, quint8 zoom) const {
    if (zoom > kMaxTileZoom) { // :103-105
        const geo::DoublePoint p = projection(zoom - kPixelZoomShift).lonLat2PixelPosFloat(toLonLat(ll));
        return fromPoint(geo::roundPoint(p, geo::PointRounding::ToTopLeft));
    }
    const geo::DoublePoint p = projection(zoom).lonLat2TilePosFloat(toLonLat(ll)); // :107-108
    return fromPoint(geo::roundPoint(p, geo::PointRounding::ToTopLeft));
}

script::DoublePoint CoordConverterSimpleByProjectionSet::metr2LonLat(script::DoublePoint mm) const {
    return fromLonLat(projectionType_->metr2LonLat({mm.x, mm.y})); // :112-117
}

qint32 CoordConverterSimpleByProjectionSet::pixelsAtZoom(quint8 zoom) const {
    if (!projectionSet_->checkZoom(zoom)) { // :119-130
        return 0;
    }
    const geo::RectI r = projection(zoom).pixelRect();
    return r.right - r.left;
}

script::DoublePoint CoordConverterSimpleByProjectionSet::pos2LonLat(script::Point xy, quint8 zoom) const {
    if (zoom > kMaxTileZoom) { // :139-141
        return fromLonLat(projection(zoom - kPixelZoomShift).pixelPos2LonLat(toPoint(xy)));
    }
    return fromLonLat(projection(zoom).tilePos2LonLat(toPoint(xy))); // :143-144
}

script::Point CoordConverterSimpleByProjectionSet::tilePos2PixelPos(script::Point xy, quint8 zoom) const {
    if (projectionSet_->checkZoom(zoom)) { // :153-155; otherwise legacy leaves Result unassigned
        return fromPoint(projection(zoom).tilePos2PixelPos(toPoint(xy)));
    }
    return {};
}

script::Rect CoordConverterSimpleByProjectionSet::tilePos2PixelRect(script::Point xy, quint8 zoom) const {
    if (projectionSet_->checkZoom(zoom)) { // :163-165; otherwise legacy leaves Result unassigned
        const geo::RectI r = projection(zoom).tilePos2PixelRect(toPoint(xy));
        return {r.left, r.top, r.right, r.bottom};
    }
    return {};
}

qint32 CoordConverterSimpleByProjectionSet::tilesAtZoom(quint8 zoom) const {
    if (!projectionSet_->checkZoom(zoom)) { // :168-179
        return 0;
    }
    const geo::RectI r = projection(zoom).tileRect();
    return r.right - r.left;
}

} // namespace cg::maptype
