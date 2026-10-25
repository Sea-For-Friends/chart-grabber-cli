// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/geometry_lonlat.h"

#include <QtGlobal>

#include <algorithm>

namespace cg::geo {

PointsMeta PointsMeta::slice(std::size_t from, std::size_t count) const {
    // u_DoublePointsMetaFunc.pas CopyMeta(AMeta, ACount, AStartIndex).
    PointsMeta result;
    if (!elevation.empty()) {
        result.elevation.assign(elevation.begin() + static_cast<std::ptrdiff_t>(from),
                                elevation.begin() + static_cast<std::ptrdiff_t>(from + count));
    }
    if (!timestamp.empty()) {
        result.timestamp.assign(timestamp.begin() + static_cast<std::ptrdiff_t>(from),
                                timestamp.begin() + static_cast<std::ptrdiff_t>(from + count));
    }
    return result;
}

namespace {

// The shape every u_GeometryLonLat*.pas IsSameGeometry shares: identity, an early mismatch on a
// hash both sides have computed, a type-dispatching downcast, then a structural body. Six classes
// use this (point/multipoint/line/contour/multiline/multipolygon), so it earns being a helper
// rather than six hand copies (CHARTER.md §6 "Lean": no abstraction without two users).
template <class Interface, class Body>
bool sameGeometryImpl(core::HashValue selfHash, const IGeometryLonLat* self, const IGeometryLonLat& other,
                      Body&& body) {
    if (self == &other) {
        return true;
    }
    if (selfHash != core::kNoHash) {
        const core::HashValue otherHash = other.hash();
        if (otherHash != core::kNoHash && selfHash != otherHash) {
            return false;
        }
    }
    const auto* o = dynamic_cast<const Interface*>(&other);
    if (o == nullptr) {
        return false;
    }
    return body(*o);
}

} // namespace

// --- GeometryLonLatPoint --------------------------------------------------------------------

GeometryLonLatPoint::GeometryLonLatPoint(core::HashValue hash, LonLat point) noexcept
    : hash_(hash), point_(point), bounds_{point.lon, point.lat, point.lon, point.lat} {}

bool GeometryLonLatPoint::isSameGeometry(const IGeometryLonLat& other) const {
    return sameGeometryImpl<IGeometryLonLatPoint>(
        hash_, this, other, [&](const IGeometryLonLatPoint& o) { return point_ == o.point(); });
}

// --- GeometryLonLatMultiPoint ----------------------------------------------------------------

GeometryLonLatMultiPoint::GeometryLonLatMultiPoint(core::HashValue hash, const LonLatRect& bounds,
                                                   LonLatPointsPtr points) noexcept
    : hash_(hash), bounds_(bounds), points_(std::move(points)) {
    Q_ASSERT(points_ && !points_->empty());
}

LonLat GeometryLonLatMultiPoint::goToPoint() const {
    return points_->empty() ? LonLat::fromPoint(emptyDoublePoint()) : points_->front();
}

bool GeometryLonLatMultiPoint::isSameGeometry(const IGeometryLonLat& other) const {
    return sameGeometryImpl<IGeometryLonLatMultiPoint>(
        hash_, this, other, [&](const IGeometryLonLatMultiPoint& o) {
            const auto op = o.points();
            if (points_->size() != op.size()) {
                return false;
            }
            if (hash_ != core::kNoHash && o.hash() != core::kNoHash) {
                return true; // sameGeometryImpl already ruled out a mismatch
            }
            if (!(bounds_ == o.bounds())) {
                return false;
            }
            return std::equal(points_->begin(), points_->end(), op.begin());
        });
}

// --- GeometryLonLatSingleLine ----------------------------------------------------------------

GeometryLonLatSingleLine::GeometryLonLatSingleLine(core::HashValue hash, const LonLatRect& bounds,
                                                   LonLatPointsPtr points, PointsMeta meta) noexcept
    : hash_(hash), bounds_(bounds), points_(std::move(points)), meta_(std::move(meta)) {
    Q_ASSERT(points_ && !points_->empty());
}

LonLat GeometryLonLatSingleLine::goToPoint() const {
    return points_->empty() ? LonLat::fromPoint(emptyDoublePoint()) : points_->front();
}

bool GeometryLonLatSingleLine::isSameGeometry(const IGeometryLonLat& other) const {
    return sameGeometryImpl<IGeometryLonLatSingleLine>(
        hash_, this, other, [&](const IGeometryLonLatSingleLine& o) {
            const auto op = o.points();
            if (points_->size() != op.size()) {
                return false;
            }
            if (hash_ != core::kNoHash && o.hash() != core::kNoHash) {
                return true;
            }
            if (!(bounds_ == o.bounds())) {
                return false;
            }
            if (!std::equal(points_->begin(), points_->end(), op.begin())) {
                return false;
            }
            // u_DoublePointsMetaFunc.pas IsSameMeta: both empty is equal, both non-empty compares
            // values, size mismatch (one empty, one not) is unequal -- exactly what vector== gives.
            return meta_.elevation == o.meta().elevation && meta_.timestamp == o.meta().timestamp;
        });
}

// --- GeometryLonLatContour -------------------------------------------------------------------

GeometryLonLatContour::GeometryLonLatContour(core::HashValue hash, const LonLatRect& bounds,
                                             LonLatPointsPtr points) noexcept
    : hash_(hash), bounds_(bounds), points_(std::move(points)), count_(points_ ? points_->size() : 0) {
    Q_ASSERT(points_ && !points_->empty());
    if (count_ > 1 && (*points_)[0] == (*points_)[count_ - 1]) {
        --count_; // pre-closed input: drop the caller-supplied duplicate of the first point
    }
}

bool GeometryLonLatContour::isSameGeometry(const IGeometryLonLat& other) const {
    return sameGeometryImpl<IGeometryLonLatContour>(hash_, this, other, [&](const IGeometryLonLatContour& o) {
        const auto pts = points();
        const auto op = o.points();
        if (pts.size() != op.size()) {
            return false;
        }
        if (hash_ != core::kNoHash && o.hash() != core::kNoHash) {
            return true;
        }
        if (!(bounds_ == o.bounds())) {
            return false;
        }
        return std::equal(pts.begin(), pts.end(), op.begin());
    });
}

// --- GeometryLonLatSinglePolygon -------------------------------------------------------------

GeometryLonLatSinglePolygon::GeometryLonLatSinglePolygon(core::HashValue hash, const LonLatRect& bounds,
                                                         GeometryLonLatContourPtr outer,
                                                         std::vector<GeometryLonLatContourPtr> holes) noexcept
    : hash_(hash), bounds_(bounds), outer_(std::move(outer)), holes_(std::move(holes)) {
    Q_ASSERT(outer_);
}

bool GeometryLonLatSinglePolygon::isSameGeometry(const IGeometryLonLat& other) const {
    // u_GeometryLonLat.pas TGeometryLonLatSinglePolygonWithHoles.IsSame, folded into the shared
    // dispatcher: hash-or-(hole count + outer + each hole, structurally).
    return sameGeometryImpl<IGeometryLonLatSinglePolygon>(
        hash_, this, other, [&](const IGeometryLonLatSinglePolygon& o) {
            if (holes_.size() != o.holeCount()) {
                return false;
            }
            if (hash_ != core::kNoHash && o.hash() != core::kNoHash) {
                return true;
            }
            if (!outer_->isSameGeometry(*o.outerBorder())) {
                return false;
            }
            for (std::size_t i = 0; i < holes_.size(); ++i) {
                if (!holes_[i]->isSameGeometry(*o.holeBorder(i))) {
                    return false;
                }
            }
            return true;
        });
}

// --- GeometryLonLatMultiLine -----------------------------------------------------------------

GeometryLonLatMultiLine::GeometryLonLatMultiLine(core::HashValue hash, const LonLatRect& bounds,
                                                 std::vector<GeometryLonLatSingleLinePtr> items) noexcept
    : hash_(hash), bounds_(bounds), items_(std::move(items)) {
    Q_ASSERT(items_.size() > 1);
}

bool GeometryLonLatMultiLine::isSameGeometry(const IGeometryLonLat& other) const {
    return sameGeometryImpl<IGeometryLonLatMultiLine>(
        hash_, this, other, [&](const IGeometryLonLatMultiLine& o) {
            if (items_.size() != o.count()) {
                return false;
            }
            if (hash_ != core::kNoHash && o.hash() != core::kNoHash) {
                return true;
            }
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (!items_[i]->isSameGeometry(*o.item(i))) {
                    return false;
                }
            }
            return true;
        });
}

// --- GeometryLonLatMultiPolygon --------------------------------------------------------------

GeometryLonLatMultiPolygon::GeometryLonLatMultiPolygon(
    core::HashValue hash, const LonLatRect& bounds,
    std::vector<GeometryLonLatSinglePolygonPtr> items) noexcept
    : hash_(hash), bounds_(bounds), items_(std::move(items)) {
    Q_ASSERT(items_.size() > 1);
}

bool GeometryLonLatMultiPolygon::isSameGeometry(const IGeometryLonLat& other) const {
    return sameGeometryImpl<IGeometryLonLatMultiPolygon>(
        hash_, this, other, [&](const IGeometryLonLatMultiPolygon& o) {
            if (items_.size() != o.count()) {
                return false;
            }
            if (hash_ != core::kNoHash && o.hash() != core::kNoHash) {
                return true;
            }
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (!items_[i]->isSameGeometry(*o.item(i))) {
                    return false;
                }
            }
            return true;
        });
}

} // namespace cg::geo
