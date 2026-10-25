// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/geometry_projected.h"

#include <QtGlobal>

#include <cmath>
#include <utility>

namespace cg::geo {

namespace {

// u_GeoFunc.pas:541-548/528-539 (IsIntersecProjectedRect / IntersecProjectedRect). Projected
// space is half-open [left,right) x [top,bottom) with Top <= Bottom (u_ProjectionBasic256x256.pas
// pixel convention), same as DoubleRect / isPointInRect in geo_types.h.
bool isIntersecProjectedRect(const DoubleRect& r1, const DoubleRect& r2) noexcept {
    return r1.left <= r2.right && r1.right >= r2.left && r1.top <= r2.bottom && r1.bottom >= r2.top;
}

bool intersecProjectedRect(DoubleRect& out, const DoubleRect& r1, const DoubleRect& r2) noexcept {
    out = r1;
    if (r2.left > out.left)
        out.left = r2.left;
    if (r2.top > out.top)
        out.top = r2.top;
    if (r2.right < out.right)
        out.right = r2.right;
    if (r2.bottom < out.bottom)
        out.bottom = r2.bottom;
    const bool empty = out.right <= out.left || out.bottom <= out.top;
    if (empty) {
        out = DoubleRect{};
    }
    return !empty;
}

// TGeometryProjectedBase.Create (u_GeometryProjected.pas:146-161): a closed ring whose caller
// already repeated the first point as a literal last point has that duplicate trimmed off, same
// rule as GeometryLonLatContour's constructor.
std::size_t trimClosingDuplicate(const std::vector<DoublePoint>& points) noexcept {
    std::size_t count = points.size();
    if (count > 1 && points[0] == points[count - 1]) {
        --count;
    }
    return count;
}

// u_GeometryProjected.pas:208-252 (TGeometryProjectedLine.IsPointOnPath) / :402-446
// (TGeometryProjectedContour.IsPointOnBorder): identical perpendicular-projection
// distance-to-segment test, shared by the open-line and closed-contour variants -- only the loop
// bounds differ (the contour also walks the closing edge).
bool pointNearSegment(DoublePoint p, DoublePoint prev, DoublePoint cur, double distSqr) noexcept {
    const DoublePoint w{p.x() - prev.x(), p.y() - prev.y()};
    const DoublePoint v{cur.x() - prev.x(), cur.y() - prev.y()};
    const double c1 = w.x() * v.x() + w.y() * v.y();
    if (c1 <= 0) {
        return false;
    }
    const double c2 = v.x() * v.x() + v.y() * v.y();
    if (c2 <= c1) {
        return false;
    }
    const double b = c1 / c2;
    const double dx = w.x() - b * v.x();
    const double dy = w.y() - b * v.y();
    return dx * dx + dy * dy < distSqr;
}

// Whether one polygon/path edge (prev -> cur) crosses one of ARect's four sides within that
// side's span, plus (Top threshold only) whether the crossing toggles the ray-parity of ARect's
// top-left corner. u_GeometryProjected.pas has this identical 8-way Top/Bottom/Left/Right
// threshold walk in four places: TGeometryProjectedLine.IsRectIntersectPath (:254-341),
// TGeometryProjectedContour.CheckRectIntersection (:448-557), .IsRectIntersectBorder (:559-649)
// and .IsRectIntersectPolygon (:651-756). CheckRectIntersection/IsRectIntersectPolygon fuse a
// point-in-polygon ray test for ARect.TopLeft into the same walk (the `if ARect.Left > VIntersect
// then VRectIn := not VRectIn` on the Top sub-check only -- Bottom/Left/Right never toggle it);
// IsRectIntersectBorder/IsRectIntersectPath only need `bordersRect`.
struct EdgeRectCross {
    bool bordersRect = false;
    bool flipsTopLeftParity = false;
};

EdgeRectCross edgeRectCross(DoublePoint prev, DoublePoint cur, const DoubleRect& rect) noexcept {
    EdgeRectCross result;
    const double dx = cur.x() - prev.x();
    const double dy = cur.y() - prev.y();
    if (dy < 0) {
        if (cur.y() <= rect.top && rect.top < prev.y()) {
            const double vi = dx * (rect.top - prev.y()) / dy + prev.x();
            if (rect.left <= vi && vi < rect.right) {
                result.bordersRect = true;
                return result;
            }
            if (rect.left > vi) {
                result.flipsTopLeftParity = true;
            }
        }
        if (cur.y() <= rect.bottom && rect.bottom < prev.y()) {
            const double vi = dx * (rect.bottom - prev.y()) / dy + prev.x();
            if (rect.left <= vi && vi < rect.right) {
                result.bordersRect = true;
                return result;
            }
        }
    } else if (dy > 0) {
        if (cur.y() > rect.top && rect.top >= prev.y()) {
            const double vi = dx * (rect.top - prev.y()) / dy + prev.x();
            if (rect.left <= vi && vi < rect.right) {
                result.bordersRect = true;
                return result;
            }
            if (rect.left > vi) {
                result.flipsTopLeftParity = true;
            }
        }
        if (cur.y() > rect.bottom && rect.bottom >= prev.y()) {
            const double vi = dx * (rect.bottom - prev.y()) / dy + prev.x();
            if (rect.left <= vi && vi < rect.right) {
                result.bordersRect = true;
                return result;
            }
        }
    }
    if (dx < 0) {
        if (cur.x() <= rect.left && rect.left < prev.x()) {
            const double vi = dy * (rect.left - prev.x()) / dx + prev.y();
            if (rect.top <= vi && vi < rect.bottom) {
                result.bordersRect = true;
                return result;
            }
        }
        if (cur.x() <= rect.right && rect.right < prev.x()) {
            const double vi = dy * (rect.right - prev.x()) / dx + prev.y();
            if (rect.top <= vi && vi < rect.bottom) {
                result.bordersRect = true;
                return result;
            }
        }
    } else if (dx > 0) {
        if (cur.x() > rect.left && rect.left >= prev.x()) {
            const double vi = dy * (rect.left - prev.x()) / dx + prev.y();
            if (rect.top <= vi && vi < rect.bottom) {
                result.bordersRect = true;
                return result;
            }
        }
        if (cur.x() > rect.right && rect.right >= prev.x()) {
            const double vi = dy * (rect.right - prev.x()) / dx + prev.y();
            if (rect.top <= vi && vi < rect.bottom) {
                result.bordersRect = true;
                return result;
            }
        }
    }
    return result;
}

} // namespace

// --- GeometryProjectedMultiPoint --------------------------------------------------------------

GeometryProjectedMultiPoint::GeometryProjectedMultiPoint(const DoubleRect& bounds,
                                                         ProjectedPointsPtr points) noexcept
    : bounds_(bounds), points_(std::move(points)) {
    Q_ASSERT(points_ && !points_->empty());
}

// --- GeometryProjectedSingleLine ---------------------------------------------------------------

GeometryProjectedSingleLine::GeometryProjectedSingleLine(const DoubleRect& bounds,
                                                         ProjectedPointsPtr points) noexcept
    : bounds_(bounds), points_(std::move(points)) {
    Q_ASSERT(points_ && !points_->empty());
}

bool GeometryProjectedSingleLine::isPointOnPath(DoublePoint p, double dist) const {
    const auto& pts = *points_;
    const std::size_t count = pts.size();
    if (count <= 1) {
        return false;
    }
    const double distSqr = dist * dist;
    DoublePoint prev = pts[0];
    for (std::size_t i = 1; i < count; ++i) {
        const DoublePoint cur = pts[i];
        if (pointNearSegment(p, prev, cur, distSqr)) {
            return true;
        }
        prev = cur;
    }
    return false;
}

bool GeometryProjectedSingleLine::isRectIntersectPath(const DoubleRect& r) const {
    if (!isIntersecProjectedRect(bounds_, r)) {
        return false;
    }
    const auto& pts = *points_;
    const std::size_t count = pts.size();
    if (count > 1) {
        DoublePoint prev = pts[0];
        for (std::size_t i = 1; i < count; ++i) {
            const DoublePoint cur = pts[i];
            if (edgeRectCross(prev, cur, r).bordersRect) {
                return true;
            }
            prev = cur;
        }
    }
    return false;
}

// --- GeometryProjectedMultiLine ----------------------------------------------------------------

GeometryProjectedMultiLine::GeometryProjectedMultiLine(
    const DoubleRect& bounds, std::vector<GeometryProjectedSingleLinePtr> items) noexcept
    : bounds_(bounds), items_(std::move(items)) {
    Q_ASSERT(!items_.empty());
}

bool GeometryProjectedMultiLine::isPointOnPath(DoublePoint p, double dist) const {
    for (const auto& item : items_) {
        if (item->isPointOnPath(p, dist)) {
            return true;
        }
    }
    return false;
}

bool GeometryProjectedMultiLine::isRectIntersectPath(const DoubleRect& r) const {
    if (!isIntersecProjectedRect(r, bounds_)) {
        return false;
    }
    for (const auto& item : items_) {
        if (item->isRectIntersectPath(r)) {
            return true;
        }
    }
    return false;
}

// --- GeometryProjectedContour ------------------------------------------------------------------

GeometryProjectedContour::GeometryProjectedContour(const DoubleRect& bounds,
                                                   ProjectedPointsPtr points) noexcept
    : bounds_(bounds), points_(std::move(points)), count_(points_ ? trimClosingDuplicate(*points_) : 0) {
    Q_ASSERT(points_ && !points_->empty());
}

double GeometryProjectedContour::calcArea() const {
    // §3.3.1: shoelace over the implicitly closed ring, starting from the LAST point, using the
    // legacy (x1+x2)*(y1-y2) form -- not x1*y2-x2*y1. They agree algebraically, not bit-for-bit;
    // keep the legacy form (u_GeometryProjected.pas:353-371).
    double result = 0.0;
    if (count_ > 1) {
        const auto& pts = *points_;
        DoublePoint prev = pts[count_ - 1];
        for (std::size_t i = 0; i < count_; ++i) {
            const DoublePoint cur = pts[i];
            result += (prev.x() + cur.x()) * (prev.y() - cur.y());
            prev = cur;
        }
        result = std::abs(result) / 2.0;
    }
    return result;
}

bool GeometryProjectedContour::isPointInPolygon(DoublePoint p) const {
    // §3.3.1: crossing number, half-open rule (<= on one side, < on the other) and a strict > on
    // X -- this is what puts a point on a shared edge in exactly one of two adjacent polygons.
    // Do not substitute a different point-in-polygon test (u_GeometryProjected.pas:378-400).
    bool result = false;
    if (count_ > 1) {
        const auto& pts = *points_;
        DoublePoint prev = pts[count_ - 1];
        for (std::size_t i = 0; i < count_; ++i) {
            const DoublePoint cur = pts[i];
            if (((cur.y() <= p.y() && p.y() < prev.y()) || (prev.y() <= p.y() && p.y() < cur.y())) &&
                (p.x() > (prev.x() - cur.x()) * (p.y() - cur.y()) / (prev.y() - cur.y()) + cur.x())) {
                result = !result;
            }
            prev = cur;
        }
    }
    return result;
}

bool GeometryProjectedContour::isPointOnBorder(DoublePoint p, double dist) const {
    if (count_ <= 1) {
        return false;
    }
    const auto& pts = *points_;
    const double distSqr = dist * dist;
    DoublePoint prev = pts[count_ - 1];
    for (std::size_t i = 0; i < count_; ++i) {
        const DoublePoint cur = pts[i];
        if (pointNearSegment(p, prev, cur, distSqr)) {
            return true;
        }
        prev = cur;
    }
    return false;
}

RectWithPolygonIntersection GeometryProjectedContour::checkRectIntersection(const DoubleRect& r) const {
    // §3.3.1 / u_GeometryProjected.pas:448-557: the 4-valued result. NoIntersect if the bounds
    // don't even overlap; PolygonInRect if the rect fully contains bounds(); otherwise a fused
    // border-crossing + point[0]-in-rect edge walk decides between IntersectPartial,
    // RectInPolygon and NoIntersect.
    DoubleRect intersectRect{};
    if (!intersecProjectedRect(intersectRect, bounds_, r)) {
        return RectWithPolygonIntersection::NoIntersect;
    }
    if (intersectRect == bounds_) {
        return RectWithPolygonIntersection::PolygonInRect;
    }
    const auto& pts = *points_;
    bool rectIn = isPointInRect(pts[0], r);
    if (count_ > 1) {
        DoublePoint prev = pts[count_ - 1];
        for (std::size_t i = 0; i < count_; ++i) {
            const DoublePoint cur = pts[i];
            const EdgeRectCross ec = edgeRectCross(prev, cur, r);
            if (ec.bordersRect) {
                return RectWithPolygonIntersection::IntersectPartial;
            }
            if (ec.flipsTopLeftParity) {
                rectIn = !rectIn;
            }
            prev = cur;
        }
    }
    return rectIn ? RectWithPolygonIntersection::RectInPolygon : RectWithPolygonIntersection::NoIntersect;
}

bool GeometryProjectedContour::isRectIntersectPolygon(const DoubleRect& r) const {
    if (!isIntersecProjectedRect(bounds_, r)) {
        return false;
    }
    const auto& pts = *points_;
    if (isPointInRect(pts[0], r)) {
        return true;
    }
    if (count_ <= 1) {
        return false;
    }
    bool rectIn = false;
    DoublePoint prev = pts[count_ - 1];
    for (std::size_t i = 0; i < count_; ++i) {
        const DoublePoint cur = pts[i];
        const EdgeRectCross ec = edgeRectCross(prev, cur, r);
        if (ec.bordersRect) {
            return true;
        }
        if (ec.flipsTopLeftParity) {
            rectIn = !rectIn;
        }
        prev = cur;
    }
    return rectIn;
}

bool GeometryProjectedContour::isRectIntersectBorder(const DoubleRect& r) const {
    if (!isIntersecProjectedRect(bounds_, r)) {
        return false;
    }
    if (count_ > 1) {
        const auto& pts = *points_;
        DoublePoint prev = pts[count_ - 1];
        for (std::size_t i = 0; i < count_; ++i) {
            const DoublePoint cur = pts[i];
            if (edgeRectCross(prev, cur, r).bordersRect) {
                return true;
            }
            prev = cur;
        }
    }
    return false;
}

// --- GeometryProjectedSinglePolygon ------------------------------------------------------------

GeometryProjectedSinglePolygon::GeometryProjectedSinglePolygon(
    const DoubleRect& bounds, GeometryProjectedContourPtr outer,
    std::vector<GeometryProjectedContourPtr> holes) noexcept
    : bounds_(bounds), outer_(std::move(outer)), holes_(std::move(holes)) {
    Q_ASSERT(outer_);
}

double GeometryProjectedSinglePolygon::calcArea() const {
    // TGeometryProjectedPolygonWithHoles.CalcArea (u_GeometryProjected.pas:803-813): outer area
    // minus each hole's. With an empty holes_ this is exactly outer_->calcArea(), the same value
    // legacy's no-holes TGeometryProjectedPolygon (which IS its own outer contour) returns.
    double result = outer_->calcArea();
    for (const auto& hole : holes_) {
        result -= hole->calcArea();
    }
    return result;
}

bool GeometryProjectedSinglePolygon::isPointInPolygon(DoublePoint p) const {
    bool result = false;
    if (outer_->isPointInPolygon(p)) {
        result = true;
        for (const auto& hole : holes_) {
            if (hole->isPointInPolygon(p)) {
                result = false;
                break;
            }
        }
    }
    return result;
}

bool GeometryProjectedSinglePolygon::isPointOnBorder(DoublePoint p, double dist) const {
    if (outer_->isPointOnBorder(p, dist)) {
        return true;
    }
    for (const auto& hole : holes_) {
        if (hole->isPointOnBorder(p, dist)) {
            return true;
        }
    }
    return false;
}

RectWithPolygonIntersection GeometryProjectedSinglePolygon::checkRectIntersection(const DoubleRect& r) const {
    // TGeometryProjectedPolygonWithHoles.CheckRectIntersection (:879-901): only refines the outer
    // result when it is RectInPolygon -- a hole that also fully contains the rect downgrades to
    // NoIntersect, any other non-NoIntersect hole result downgrades to IntersectPartial.
    RectWithPolygonIntersection result = outer_->checkRectIntersection(r);
    if (result == RectWithPolygonIntersection::RectInPolygon) {
        for (const auto& hole : holes_) {
            const RectWithPolygonIntersection holeResult = hole->checkRectIntersection(r);
            if (holeResult == RectWithPolygonIntersection::RectInPolygon) {
                result = RectWithPolygonIntersection::NoIntersect;
                break;
            }
            if (holeResult != RectWithPolygonIntersection::NoIntersect) {
                result = RectWithPolygonIntersection::IntersectPartial;
                break;
            }
        }
    }
    return result;
}

bool GeometryProjectedSinglePolygon::isRectIntersectBorder(const DoubleRect& r) const {
    if (outer_->isRectIntersectBorder(r)) {
        return true;
    }
    for (const auto& hole : holes_) {
        if (hole->isRectIntersectBorder(r)) {
            return true;
        }
    }
    return false;
}

bool GeometryProjectedSinglePolygon::isRectIntersectPolygon(const DoubleRect& r) const {
    // TGeometryProjectedPolygonWithHoles.IsRectIntersectPolygon (:924-944): NOT a plain XOR of
    // outer/hole results. A hole only vetoes when the rect's top-left corner lies inside that
    // hole AND the rect doesn't cross the hole's border (i.e. the rect is fully swallowed by the
    // hole) -- keep this exact rule, do not "simplify" to bounds subtraction.
    bool result = false;
    if (outer_->isRectIntersectPolygon(r)) {
        result = true;
        for (const auto& hole : holes_) {
            if (hole->isPointInPolygon(r.topLeft())) {
                if (!hole->isRectIntersectBorder(r)) {
                    result = false;
                    break;
                }
            }
        }
    }
    return result;
}

// --- GeometryProjectedMultiPolygon -------------------------------------------------------------

GeometryProjectedMultiPolygon::GeometryProjectedMultiPolygon(
    const DoubleRect& bounds, std::vector<GeometryProjectedSinglePolygonPtr> items) noexcept
    : bounds_(bounds), items_(std::move(items)) {
    Q_ASSERT(!items_.empty());
}

double GeometryProjectedMultiPolygon::calcArea() const {
    double result = 0.0;
    for (const auto& item : items_) {
        result += item->calcArea();
    }
    return result;
}

bool GeometryProjectedMultiPolygon::isPointInPolygon(DoublePoint p) const {
    for (const auto& item : items_) {
        if (item->isPointInPolygon(p)) {
            return true;
        }
    }
    return false;
}

bool GeometryProjectedMultiPolygon::isPointOnBorder(DoublePoint p, double dist) const {
    for (const auto& item : items_) {
        if (item->isPointOnBorder(p, dist)) {
            return true;
        }
    }
    return false;
}

RectWithPolygonIntersection GeometryProjectedMultiPolygon::checkRectIntersection(const DoubleRect& r) const {
    // TGeometryProjectedMultiPolygon.CheckRectIntersection (u_GeometryProjectedMulti.pas:199-229):
    // a real legacy quirk -- `Result` is REASSIGNED each iteration (not accumulated with OR), so
    // if every item comes back NoIntersect, the *last* item's result survives, not a merged
    // "no part intersects" answer. Keep it as-is.
    DoubleRect intersectRect{};
    if (!intersecProjectedRect(intersectRect, bounds_, r)) {
        return RectWithPolygonIntersection::NoIntersect;
    }
    if (intersectRect == bounds_) {
        return RectWithPolygonIntersection::PolygonInRect;
    }
    RectWithPolygonIntersection result = RectWithPolygonIntersection::NoIntersect;
    for (const auto& item : items_) {
        result = item->checkRectIntersection(r);
        if (result == RectWithPolygonIntersection::IntersectPartial) {
            break;
        }
        if (result == RectWithPolygonIntersection::PolygonInRect) {
            result = RectWithPolygonIntersection::IntersectPartial;
            break;
        }
        if (result == RectWithPolygonIntersection::RectInPolygon) {
            break;
        }
        // NoIntersect: fall through to the next item, `result` staying at this item's NoIntersect
        // until either a later item overwrites it or the loop ends.
    }
    return result;
}

bool GeometryProjectedMultiPolygon::isRectIntersectBorder(const DoubleRect& r) const {
    if (!isIntersecProjectedRect(r, bounds_)) {
        return false;
    }
    for (const auto& item : items_) {
        if (item->isRectIntersectBorder(r)) {
            return true;
        }
    }
    return false;
}

bool GeometryProjectedMultiPolygon::isRectIntersectPolygon(const DoubleRect& r) const {
    if (!isIntersecProjectedRect(r, bounds_)) {
        return false;
    }
    for (const auto& item : items_) {
        if (item->isRectIntersectPolygon(r)) {
            return true;
        }
    }
    return false;
}

} // namespace cg::geo
