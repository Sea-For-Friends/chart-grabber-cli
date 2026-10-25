// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/geometry_factory.h"

#include "cg/geo/datum.h"

#include <QByteArrayView>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cg::geo {
namespace {

// u_GeometryLonLatFactory.pas:378 -- the seed AddSinglePolygonToList starts a multi-polygon hash
// chain with, so an empty vs. non-empty hole set on the first polygon still perturbs the hash.
constexpr core::HashValue kMultiPolygonHashSeed = 0xdb1267d24f3f3f36ULL;

// CalcHashByBuffer(APoints.Points, APoints.Count * SizeOf(TDoublePoint)): one CityHash pass over
// the raw point buffer, not the scalar CalcHashByX/UpdateHashByX chain HashSeed wraps.
[[nodiscard]] core::HashValue hashPointsBuffer(std::span<const LonLat> points) noexcept {
    if (points.empty()) {
        return core::kNoHash; // u_DoublePoints.pas:98-104: an empty list has FPoints = nil, whatever the
                              // capacity
    }
    return core::hashOf(QByteArrayView(reinterpret_cast<const char*>(points.data()),
                                       static_cast<qsizetype>(points.size() * sizeof(LonLat))));
}

// UpdateHashByMeta (u_DoublePointsMetaFunc.pas): folds elevation/timestamp into a running hash
// when present. QDateTime has no stable raw layout (unlike legacy's TDateTime double), so each
// timestamp folds in as its epoch-millisecond count; hash_seed.h's contract ("equal inputs hash
// equal", not byte-identical to the legacy CityHash.dll) covers this.
void foldMetaIntoHash(core::HashValue& hash, const PointsMeta& meta) {
    if (!meta.elevation.empty()) {
        core::hashUpdate(hash,
                         QByteArrayView(reinterpret_cast<const char*>(meta.elevation.data()),
                                        static_cast<qsizetype>(meta.elevation.size() * sizeof(double))));
    }
    if (!meta.timestamp.empty()) {
        std::vector<qint64> msecs(meta.timestamp.size());
        for (std::size_t i = 0; i < meta.timestamp.size(); ++i) {
            msecs[i] = meta.timestamp[i].isValid() ? meta.timestamp[i].toMSecsSinceEpoch() : 0;
        }
        core::hashUpdate(hash, QByteArrayView(reinterpret_cast<const char*>(msecs.data()),
                                              static_cast<qsizetype>(msecs.size() * sizeof(qint64))));
    }
}

[[nodiscard]] LonLatPointsPtr copyPoints(std::span<const LonLat> points) {
    return std::make_shared<const std::vector<LonLat>>(points.begin(), points.end());
}

} // namespace

// =============================================================================================
// GeometryLonLatLineBuilder
// =============================================================================================

void GeometryLonLatLineBuilder::addLine(const LonLatRect& bounds, LonLatPointsPtr points, PointsMeta meta) {
    Q_ASSERT(points);
    core::HashValue h = hashPointsBuffer(*points);
    foldMetaIntoHash(h, meta);
    auto line =
        std::make_shared<const GeometryLonLatSingleLine>(h, bounds, std::move(points), std::move(meta));
    if (lines_.empty()) {
        hash_ = h;
        bounds_ = bounds;
    } else {
        core::hashUpdate(hash_, h);
        bounds_ = LonLatRect::united(bounds_, bounds);
    }
    lines_.push_back(std::move(line));
}

void GeometryLonLatLineBuilder::addLine(LonLatPointsPtr points, PointsMeta meta) {
    Q_ASSERT(points && !points->empty());
    const LonLatRect bounds = LonLatRect::fromPoints(points->data(), points->size());
    addLine(bounds, std::move(points), std::move(meta));
}

void GeometryLonLatLineBuilder::addLine(const IGeometryLonLatLine& line) {
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&line)) {
        addLine(single->bounds(), copyPoints(single->points()), single->meta());
    } else if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&line)) {
        for (std::size_t i = 0; i < multi->count(); ++i) {
            addLine(*multi->item(i));
        }
    } else {
        Q_ASSERT(false); // u_GeometryLonLatFactory.pas:246 Assert(False) -- unreachable IGeometryLonLatLine
    }
}

GeometryLonLatPtr GeometryLonLatLineBuilder::makeStaticAndClear() {
    if (lines_.empty()) {
        return nullptr;
    }
    GeometryLonLatPtr result = lines_.size() == 1
                                   ? GeometryLonLatPtr(lines_.front())
                                   : std::make_shared<const GeometryLonLatMultiLine>(hash_, bounds_, lines_);
    lines_.clear();
    hash_ = core::kNoHash;
    bounds_ = {};
    return result;
}

GeometryLonLatPtr GeometryLonLatLineBuilder::makeStaticCopy() const {
    if (lines_.empty()) {
        return nullptr;
    }
    if (lines_.size() == 1) {
        return lines_.front();
    }
    return std::make_shared<const GeometryLonLatMultiLine>(hash_, bounds_, lines_);
}

// =============================================================================================
// GeometryLonLatPolygonBuilder
// =============================================================================================

void GeometryLonLatPolygonBuilder::addOuterContour(GeometryLonLatContourPtr outer) {
    if (pendingOuter_) {
        polygons_.push_back(buildPending());
    }
    pendingOuter_ = std::move(outer);
    pendingHoles_.clear();
}

void GeometryLonLatPolygonBuilder::addHoleContour(GeometryLonLatContourPtr hole) {
    // u_GeometryLonLatFactory.pas:493-511: a hole added before any outer becomes the first outer
    // instead (a memory-reuse quirk of the FOuterDataExists flag). Reproduced per Tech Lead ruling
    // (review F2c/deviations audit: cheap to match, so match rather than record a deviation) --
    // this builder's documented contract (docs/specs/geo.md §3.5 "addHole attaches to the LAST
    // addOuter") never exercises it in acceptance tests, but a caller that does hit it now gets
    // legacy's answer instead of an assert.
    if (!pendingOuter_) {
        addOuterContour(std::move(hole));
        return;
    }
    pendingHoles_.push_back(std::move(hole));
}

void GeometryLonLatPolygonBuilder::addOuter(const LonLatRect& bounds, LonLatPointsPtr points) {
    Q_ASSERT(points);
    const core::HashValue hash = hashPointsBuffer(*points);
    addOuterContour(std::make_shared<const GeometryLonLatContour>(hash, bounds, std::move(points)));
}

void GeometryLonLatPolygonBuilder::addOuter(LonLatPointsPtr points) {
    Q_ASSERT(points && !points->empty());
    const LonLatRect bounds = LonLatRect::fromPoints(points->data(), points->size());
    addOuter(bounds, std::move(points));
}

void GeometryLonLatPolygonBuilder::addHole(const LonLatRect& bounds, LonLatPointsPtr points) {
    Q_ASSERT(points);
    const core::HashValue hash = hashPointsBuffer(*points);
    addHoleContour(std::make_shared<const GeometryLonLatContour>(hash, bounds, std::move(points)));
}

void GeometryLonLatPolygonBuilder::addHole(LonLatPointsPtr points) {
    Q_ASSERT(points && !points->empty());
    const LonLatRect bounds = LonLatRect::fromPoints(points->data(), points->size());
    addHole(bounds, std::move(points));
}

void GeometryLonLatPolygonBuilder::addPolygon(const IGeometryLonLatPolygon& polygon) {
    if (const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&polygon)) {
        addOuterContour(single->outerBorder());
        for (std::size_t i = 0; i < single->holeCount(); ++i) {
            addHoleContour(single->holeBorder(i));
        }
    } else if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&polygon)) {
        for (std::size_t i = 0; i < multi->count(); ++i) {
            addPolygon(*multi->item(i));
        }
    } else {
        Q_ASSERT(false); // u_GeometryLonLatFactory.pas:426 Assert(False)
    }
}

GeometryLonLatSinglePolygonPtr GeometryLonLatPolygonBuilder::buildPending() const {
    Q_ASSERT(pendingOuter_);
    core::HashValue hash = pendingOuter_->hash();
    for (const auto& hole : pendingHoles_) {
        core::hashUpdate(hash, hole->hash());
    }
    return std::make_shared<const GeometryLonLatSinglePolygon>(hash, pendingOuter_->bounds(), pendingOuter_,
                                                               pendingHoles_);
}

namespace {
[[nodiscard]] GeometryLonLatPtr collapsePolygons(std::vector<GeometryLonLatSinglePolygonPtr> polygons) {
    if (polygons.empty()) {
        return nullptr;
    }
    if (polygons.size() == 1) {
        return polygons.front();
    }
    LonLatRect bounds = polygons.front()->bounds();
    core::HashValue hash = kMultiPolygonHashSeed;
    core::hashUpdate(hash, polygons.front()->hash());
    for (std::size_t i = 1; i < polygons.size(); ++i) {
        bounds = LonLatRect::united(bounds, polygons[i]->bounds());
        core::hashUpdate(hash, polygons[i]->hash());
    }
    return std::make_shared<const GeometryLonLatMultiPolygon>(hash, bounds, std::move(polygons));
}
} // namespace

GeometryLonLatPtr GeometryLonLatPolygonBuilder::makeStaticAndClear() {
    if (pendingOuter_) {
        polygons_.push_back(buildPending());
        pendingOuter_.reset();
        pendingHoles_.clear();
    }
    GeometryLonLatPtr result = collapsePolygons(std::move(polygons_));
    polygons_.clear();
    return result;
}

GeometryLonLatPtr GeometryLonLatPolygonBuilder::makeStaticCopy() const {
    std::vector<GeometryLonLatSinglePolygonPtr> polygons = polygons_;
    if (pendingOuter_) {
        polygons.push_back(buildPending());
    }
    return collapsePolygons(std::move(polygons));
}

// =============================================================================================
// GeometryLonLatFactory
// =============================================================================================

GeometryLonLatFactory::GeometryLonLatFactory(HashSeed hash) : hash_(hash) {}

std::shared_ptr<const IGeometryLonLatPoint> GeometryLonLatFactory::createPoint(LonLat p) const {
    // CalcHashByDoublePoint(APoint): a fresh CalcHashByX(lon) chained with UpdateHashByX(lat).
    return std::make_shared<const GeometryLonLatPoint>(hash_(p.lon, p.lat), p);
}

std::shared_ptr<const IGeometryLonLatMultiPoint>
GeometryLonLatFactory::createMultiPoint(std::span<const LonLat> points) const {
    Q_ASSERT(!points.empty());
    const LonLatRect bounds = LonLatRect::fromPoints(points.data(), points.size());
    const core::HashValue hash = hashPointsBuffer(points);
    return std::make_shared<const GeometryLonLatMultiPoint>(hash, bounds, copyPoints(points));
}

GeometryLonLatLineBuilder GeometryLonLatFactory::makeLineBuilder() const {
    return GeometryLonLatLineBuilder{};
}

GeometryLonLatPolygonBuilder GeometryLonLatFactory::makePolygonBuilder() const {
    return GeometryLonLatPolygonBuilder{};
}

// CreateLonLatLine (u_GeometryLonLatFactory.pas:917-968): split the flat stream on the
// PointIsEmpty (NaN) separator, one addLine per run.
GeometryLonLatPtr GeometryLonLatFactory::createLine(std::span<const LonLat> points,
                                                    const PointsMeta* meta) const {
    auto builder = makeLineBuilder();
    std::size_t start = 0;
    std::size_t len = 0;
    LonLatRect bounds{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (pointIsEmpty(points[i].toPoint())) {
            if (len > 0) {
                builder.addLine(bounds, copyPoints(points.subspan(start, len)),
                                meta ? meta->slice(start, len) : PointsMeta{});
                len = 0;
            }
        } else {
            if (len == 0) {
                start = i;
                bounds = {points[i].lon, points[i].lat, points[i].lon, points[i].lat};
            } else {
                bounds =
                    LonLatRect::united(bounds, {points[i].lon, points[i].lat, points[i].lon, points[i].lat});
            }
            ++len;
        }
    }
    if (len > 0) {
        builder.addLine(bounds, copyPoints(points.subspan(start, len)),
                        meta ? meta->slice(start, len) : PointsMeta{});
    }
    return builder.makeStaticAndClear();
}

// CreateLonLatPolygon (u_GeometryLonLatFactory.pas:1046-1082): same split, into outers.
GeometryLonLatPtr GeometryLonLatFactory::createPolygon(std::span<const LonLat> points) const {
    auto builder = makePolygonBuilder();
    std::size_t start = 0;
    std::size_t len = 0;
    LonLatRect bounds{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (pointIsEmpty(points[i].toPoint())) {
            if (len > 0) {
                builder.addOuter(bounds, copyPoints(points.subspan(start, len)));
                len = 0;
            }
        } else {
            if (len == 0) {
                start = i;
                bounds = {points[i].lon, points[i].lat, points[i].lon, points[i].lat};
            } else {
                bounds =
                    LonLatRect::united(bounds, {points[i].lon, points[i].lat, points[i].lon, points[i].lat});
            }
            ++len;
        }
    }
    if (len > 0) {
        builder.addOuter(bounds, copyPoints(points.subspan(start, len)));
    }
    return builder.makeStaticAndClear();
}

std::shared_ptr<const IGeometryLonLatSinglePolygon>
GeometryLonLatFactory::createPolygonInternal(const LonLatRect& bounds, std::vector<LonLat> points) const {
    // CreateLonLatPolygonInternal (u_GeometryLonLatFactory.pas:898-915).
    auto owned = std::make_shared<const std::vector<LonLat>>(std::move(points));
    const core::HashValue hash = hashPointsBuffer(*owned);
    auto contour = std::make_shared<const GeometryLonLatContour>(hash, bounds, owned);
    return std::make_shared<const GeometryLonLatSinglePolygon>(hash, bounds, contour,
                                                               std::vector<GeometryLonLatContourPtr>{});
}

std::shared_ptr<const IGeometryLonLatSinglePolygon>
GeometryLonLatFactory::createPolygonByRect(const LonLatRect& rect) const {
    // u_GeometryLonLatFactory.pas:1155-1170.
    std::vector<LonLat> points{
        {rect.left, rect.top}, {rect.right, rect.top}, {rect.right, rect.bottom}, {rect.left, rect.bottom}};
    return createPolygonInternal(rect, std::move(points));
}

std::shared_ptr<const IGeometryLonLatSinglePolygon>
GeometryLonLatFactory::createPolygonCircleByPoint(const IDatum& datum, LonLat centre,
                                                  double radiusMetres) const {
    // u_GeometryLonLatFactory.pas:780-802.
    constexpr int kPointCount = 64;
    std::vector<LonLat> points;
    points.reserve(kPointCount);
    LonLatRect bounds{centre.lon, centre.lat, centre.lon, centre.lat};
    for (int i = 0; i < kPointCount; ++i) {
        const double angle = i * 360.0 / kPointCount;
        const LonLat p = datum.calcFinishPosition(centre, angle, radiusMetres);
        points.push_back(p);
        bounds = LonLatRect::united(bounds, {p.lon, p.lat, p.lon, p.lat});
    }
    return createPolygonInternal(bounds, std::move(points));
}

std::shared_ptr<const IGeometryLonLatSinglePolygon>
GeometryLonLatFactory::createPolygonSquareByPoint(const IDatum& datum, LonLat centre,
                                                  double radiusMetres) const {
    // u_GeometryLonLatFactory.pas:806-822.
    LonLatRect bounds{centre.lon, centre.lat, centre.lon, centre.lat};
    for (int i = 0; i < 4; ++i) {
        const LonLat p = datum.calcFinishPosition(centre, i * 90.0, radiusMetres);
        bounds = LonLatRect::united(bounds, {p.lon, p.lat, p.lon, p.lat});
    }
    return createPolygonByRect(bounds);
}

std::shared_ptr<const IGeometryLonLatSinglePolygon>
GeometryLonLatFactory::createPolygonSquareOnSurfaceByPoint(const IDatum& datum, LonLat centre,
                                                           double radiusMetres) const {
    // u_GeometryLonLatFactory.pas:825-862 ("Top"/"Right"/"Bottom"/"Left", 8 points).
    LonLatRect bounds{centre.lon, centre.lat, centre.lon, centre.lat};
    std::vector<LonLat> points;
    points.reserve(8);
    const auto addPoint = [&](LonLat p) {
        points.push_back(p);
        bounds = LonLatRect::united(bounds, {p.lon, p.lat, p.lon, p.lat});
    };

    const LonLat top = datum.calcFinishPosition(centre, 0.0, radiusMetres);
    addPoint(datum.calcFinishPosition(top, 270.0, radiusMetres));
    addPoint(top);
    addPoint(datum.calcFinishPosition(top, 90.0, radiusMetres));

    addPoint(datum.calcFinishPosition(centre, 90.0, radiusMetres));

    const LonLat bottom = datum.calcFinishPosition(centre, 180.0, radiusMetres);
    addPoint(datum.calcFinishPosition(bottom, 90.0, radiusMetres));
    addPoint(bottom);
    addPoint(datum.calcFinishPosition(bottom, 270.0, radiusMetres));

    addPoint(datum.calcFinishPosition(centre, 270.0, radiusMetres));

    return createPolygonInternal(bounds, std::move(points));
}

GeometryLonLatPtr GeometryLonLatFactory::createLineByStream(LonLatStream stream,
                                                            std::vector<LonLat>& scratch) const {
    auto builder = makeLineBuilder();
    scratch.clear();
    LonLatRect bounds{};
    DoublePoint p;
    while (stream.next(p)) {
        if (isSeparator(p)) {
            if (!scratch.empty()) {
                builder.addLine(bounds, copyPoints(scratch));
                scratch.clear();
            }
        } else {
            const LonLat ll = LonLat::fromPoint(p);
            if (scratch.empty()) {
                bounds = {ll.lon, ll.lat, ll.lon, ll.lat};
            } else {
                bounds = LonLatRect::united(bounds, {ll.lon, ll.lat, ll.lon, ll.lat});
            }
            scratch.push_back(ll);
        }
    }
    if (!scratch.empty()) {
        builder.addLine(bounds, copyPoints(scratch));
    }
    return builder.makeStaticAndClear();
}

namespace {

// AddPolygonsBySingleLine (u_GeometryLonLatFactory.pas:1087-1120).
void addPolygonsBySingleLine(const IGeometryLonLatSingleLine& source, const LonLatPointFilter& filter,
                             GeometryLonLatPolygonBuilder& builder, std::vector<LonLat>& scratch) {
    std::vector<DoublePoint> raw;
    const auto srcPoints = source.points();
    raw.reserve(srcPoints.size());
    for (const LonLat& p : srcPoints) {
        raw.push_back(p.toPoint());
    }
    LonLatStream stream(singleLineStream(raw, &source.meta(), false));
    LonLatStream filtered = filter(std::move(stream));

    scratch.clear();
    LonLatRect bounds{};
    DoublePoint p;
    while (filtered.next(p)) {
        if (isSeparator(p)) {
            if (!scratch.empty()) {
                builder.addOuter(bounds, copyPoints(scratch));
                scratch.clear();
            }
        } else {
            const LonLat ll = LonLat::fromPoint(p);
            if (scratch.empty()) {
                bounds = {ll.lon, ll.lat, ll.lon, ll.lat};
            } else {
                bounds = LonLatRect::united(bounds, {ll.lon, ll.lat, ll.lon, ll.lat});
            }
            scratch.push_back(ll);
        }
    }
    if (!scratch.empty()) {
        builder.addOuter(bounds, copyPoints(scratch));
    }
}

} // namespace

GeometryLonLatPtr GeometryLonLatFactory::createPolygonByLineAndFilter(const IGeometryLonLatLine& source,
                                                                      const LonLatPointFilter& filter) const {
    auto builder = makePolygonBuilder();
    std::vector<LonLat> scratch;
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&source)) {
        addPolygonsBySingleLine(*single, filter, builder, scratch);
    } else if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&source)) {
        for (std::size_t i = 0; i < multi->count(); ++i) {
            addPolygonsBySingleLine(*multi->item(i), filter, builder, scratch);
        }
    }
    return builder.makeStaticAndClear();
}

GeometryLonLatPtr GeometryLonLatFactory::createPolygonByLine(const IGeometryLonLatLine& line,
                                                             double radiusMetres,
                                                             const IProjection& projection,
                                                             std::vector<DoublePoint>& scratch) const {
    LonLatPointFilter filter = [&](LonLatStream s) {
        return LonLatStream(line2Poly(std::move(s).release(), radiusMetres, projection, scratch));
    };
    return createPolygonByLineAndFilter(line, filter);
}

// =================================================================================================
// The projected factory (u_GeometryProjectedFactory.pas, docs/specs/geo.md §3.5.1, T-0017C2)
// =================================================================================================

namespace {

// UnionProjectedRects (u_GeoFunc.pas:298-311): min(left,top), max(right,bottom) -- pixel space,
// unlike LonLatRect::united's north-up convention.
[[nodiscard]] DoubleRect unionProjectedRects(const DoubleRect& a, const DoubleRect& b) noexcept {
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
            std::max(a.bottom, b.bottom)};
}

[[nodiscard]] ProjectedPointsPtr copyProjectedPoints(const std::vector<DoublePoint>& points) {
    return std::make_shared<const std::vector<DoublePoint>>(points);
}

// EnumToPoints (u_GeometryProjectedFactory.pas:308-325): ONE pass drains the stream into
// `scratch` (cleared first) *and* accumulates the bounding box -- keep the fusion (spec §3.5.1:
// "two passes would be simpler and measurably slower on the render path"). Returns false (and
// leaves scratch empty) when the stream yields nothing, matching EnumToPoints' `Result := nil`.
[[nodiscard]] bool enumToPoints(IPointStream& stream, std::vector<DoublePoint>& scratch, DoubleRect& bounds) {
    scratch.clear();
    DoublePoint p;
    if (!stream.next(p)) {
        return false;
    }
    bounds = {p.x(), p.y(), p.x(), p.y()};
    scratch.push_back(p);
    while (stream.next(p)) {
        bounds.left = std::min(bounds.left, p.x());
        bounds.top = std::min(bounds.top, p.y());
        bounds.right = std::max(bounds.right, p.x());
        bounds.bottom = std::max(bounds.bottom, p.y());
        scratch.push_back(p);
    }
    return true;
}

// The shared body of LonLatSingleLineToBuilder (:329-350) and the outer/hole halves of
// LonLatSinglePolygonToBuilder (:438-473): project one lon/lat ring --
// TEnumDoublePointLonLatToMapPixel then TEnumProjectedPointFilterEqual -- fused with EnumToPoints.
// `closed` mirrors the source ring's own GetEnum: false for a line (TGeometryLonLatSingleLine /
// TGeometryLonLatMultiPoint), true for a polygon contour (TGeometryLonLatContour). Per-vertex
// meta is not read downstream (only the 1-arg IPointStream::next() is ever called in this
// pipeline), so it is not threaded through here, unlike legacy's GetEnum which happens to carry it
// for single lines.
[[nodiscard]] bool projectRing(const IProjection& projection, std::span<const LonLat> points, bool closed,
                               std::vector<DoublePoint>& scratch, DoubleRect& bounds) {
    std::vector<DoublePoint> raw;
    raw.reserve(points.size());
    for (const LonLat& p : points) {
        raw.push_back(p.toPoint());
    }
    PointStreamPtr stream = singleLineStream(raw, nullptr, closed);
    stream = lonLatToMapPixel(projection, std::move(stream));
    stream = filterEqual(std::move(stream));
    return enumToPoints(*stream, scratch, bounds);
}

} // namespace

void GeometryProjectedLineBuilder::addLine(const DoubleRect& bounds, ProjectedPointsPtr points) {
    // TGeometryProjectedLineBuilder.AddLine (u_GeometryProjectedFactory.pas:123-145): the same
    // "first line held bare, second+ moves to a list" collapse as GeometryLonLatLineBuilder.
    Q_ASSERT(points);
    auto line = std::make_shared<const GeometryProjectedSingleLine>(bounds, std::move(points));
    if (lines_.empty()) {
        bounds_ = bounds;
    } else {
        bounds_ = unionProjectedRects(bounds_, bounds);
    }
    lines_.push_back(std::move(line));
}

std::shared_ptr<const IGeometryProjectedLine> GeometryProjectedLineBuilder::makeStaticAndClear() {
    if (lines_.empty()) {
        return nullptr;
    }
    std::shared_ptr<const IGeometryProjectedLine> result =
        lines_.size() == 1 ? std::shared_ptr<const IGeometryProjectedLine>(lines_.front())
                           : std::make_shared<const GeometryProjectedMultiLine>(bounds_, lines_);
    lines_.clear();
    bounds_ = {};
    return result;
}

std::shared_ptr<const IGeometryProjectedLine> GeometryProjectedLineBuilder::makeStaticCopy() const {
    if (lines_.empty()) {
        return nullptr;
    }
    if (lines_.size() == 1) {
        return lines_.front();
    }
    return std::make_shared<const GeometryProjectedMultiLine>(bounds_, lines_);
}

void GeometryProjectedPolygonBuilder::addOuterContour(GeometryProjectedContourPtr outer) {
    if (pendingOuter_) {
        polygons_.push_back(buildPending());
    }
    pendingOuter_ = std::move(outer);
    pendingHoles_.clear();
}

void GeometryProjectedPolygonBuilder::addHoleContour(GeometryProjectedContourPtr hole) {
    // TGeometryProjectedPolygonBuilder.AddHole (u_GeometryProjectedFactory.pas:204-222): a hole
    // added before any outer becomes the first outer instead -- same FPoints-reuse quirk as
    // GeometryLonLatPolygonBuilder::addHoleContour (u_GeometryLonLatFactory.pas:493-511), kept
    // for the same reason (cheap to match, never hit by this ticket's acceptance tests).
    if (!pendingOuter_) {
        addOuterContour(std::move(hole));
        return;
    }
    pendingHoles_.push_back(std::move(hole));
}

void GeometryProjectedPolygonBuilder::addOuter(const DoubleRect& bounds, ProjectedPointsPtr points) {
    Q_ASSERT(points);
    addOuterContour(std::make_shared<const GeometryProjectedContour>(bounds, std::move(points)));
}

void GeometryProjectedPolygonBuilder::addHole(const DoubleRect& bounds, ProjectedPointsPtr points) {
    Q_ASSERT(points);
    addHoleContour(std::make_shared<const GeometryProjectedContour>(bounds, std::move(points)));
}

GeometryProjectedSinglePolygonPtr GeometryProjectedPolygonBuilder::buildPending() const {
    Q_ASSERT(pendingOuter_);
    // Single-polygon bounds is the OUTER ring's own MBR only, never unioned with its holes --
    // TGeometryProjectedPolygonBuilder.MakeCurrentSinglePolygon (:248-269) passes FPolygonBounds
    // (set only by AddOuter/the first AddHole) straight through, matching the R01-R14 fixture's
    // documented contract (tests/geo/geometry_projected_test.cpp).
    return std::make_shared<const GeometryProjectedSinglePolygon>(pendingOuter_->bounds(), pendingOuter_,
                                                                  pendingHoles_);
}

namespace {
[[nodiscard]] std::shared_ptr<const IGeometryProjectedPolygon>
collapseProjectedPolygons(std::vector<GeometryProjectedSinglePolygonPtr> polygons) {
    if (polygons.empty()) {
        return nullptr;
    }
    if (polygons.size() == 1) {
        return polygons.front();
    }
    DoubleRect bounds = polygons.front()->bounds();
    for (std::size_t i = 1; i < polygons.size(); ++i) {
        bounds = unionProjectedRects(bounds, polygons[i]->bounds());
    }
    return std::make_shared<const GeometryProjectedMultiPolygon>(bounds, std::move(polygons));
}
} // namespace

std::shared_ptr<const IGeometryProjectedPolygon> GeometryProjectedPolygonBuilder::makeStaticAndClear() {
    if (pendingOuter_) {
        polygons_.push_back(buildPending());
        pendingOuter_.reset();
        pendingHoles_.clear();
    }
    auto result = collapseProjectedPolygons(std::move(polygons_));
    polygons_.clear();
    return result;
}

std::shared_ptr<const IGeometryProjectedPolygon> GeometryProjectedPolygonBuilder::makeStaticCopy() const {
    std::vector<GeometryProjectedSinglePolygonPtr> polygons = polygons_;
    if (pendingOuter_) {
        polygons.push_back(buildPending());
    }
    return collapseProjectedPolygons(std::move(polygons));
}

GeometryProjectedLineBuilder GeometryProjectedFactory::makeLineBuilder() const {
    return GeometryProjectedLineBuilder{};
}

GeometryProjectedPolygonBuilder GeometryProjectedFactory::makePolygonBuilder() const {
    return GeometryProjectedPolygonBuilder{};
}

std::shared_ptr<const IGeometryProjectedMultiPoint>
GeometryProjectedFactory::createMultiPoint(const DoubleRect& bounds, ProjectedPointsPtr points) const {
    // TGeometryProjectedFactory.CreateMultiPoint (:352-362): a thin wrapper, no projection work.
    Q_ASSERT(points);
    return std::make_shared<const GeometryProjectedMultiPoint>(bounds, std::move(points));
}

std::shared_ptr<const IGeometryProjectedMultiPoint>
GeometryProjectedFactory::createMultiPointByLonLat(const IProjection& projection,
                                                   const IGeometryLonLatMultiPoint& source,
                                                   std::vector<DoublePoint>& scratch) const {
    // CreateMultiPointByLonLat (:364-395): source.GetEnum is TEnumDoublePointBySingleLonLatLine
    // with AClosed=False, same shape as a single line's (u_GeometryLonLat.pas:224-227).
    DoubleRect bounds{};
    if (!projectRing(projection, source.points(), /*closed=*/false, scratch, bounds)) {
        return nullptr;
    }
    return std::make_shared<const GeometryProjectedMultiPoint>(bounds, copyProjectedPoints(scratch));
}

std::shared_ptr<const IGeometryProjectedLine>
GeometryProjectedFactory::createLineByLonLatLine(const IProjection& projection,
                                                 const IGeometryLonLatLine& source,
                                                 std::vector<DoublePoint>& scratch) const {
    // CreateProjectedLineByLonLatPath (:397-426): one AddLine per single line, in source order;
    // a degenerate sub-line (projectRing finds nothing, e.g. every point invalid) is silently
    // skipped, same as legacy's `if Assigned(VPoints)` guard in LonLatSingleLineToBuilder.
    auto builder = makeLineBuilder();
    DoubleRect bounds{};
    if (const auto* single = dynamic_cast<const IGeometryLonLatSingleLine*>(&source)) {
        if (projectRing(projection, single->points(), /*closed=*/false, scratch, bounds)) {
            builder.addLine(bounds, copyProjectedPoints(scratch));
        }
    } else if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiLine*>(&source)) {
        for (std::size_t i = 0; i < multi->count(); ++i) {
            if (projectRing(projection, multi->item(i)->points(), /*closed=*/false, scratch, bounds)) {
                builder.addLine(bounds, copyProjectedPoints(scratch));
            }
        }
    } else {
        Q_ASSERT(
            false); // u_GeometryProjectedFactory.pas:423 Assert(False) -- unreachable IGeometryLonLatLine
    }
    return builder.makeStaticAndClear();
}

std::shared_ptr<const IGeometryProjectedPolygon>
GeometryProjectedFactory::createPolygonByLonLatPolygon(const IProjection& projection,
                                                       const IGeometryLonLatPolygon& source,
                                                       std::vector<DoublePoint>& scratch) const {
    // CreateProjectedPolygonByLonLatPolygon (:474-503) / LonLatSinglePolygonToBuilder (:438-473):
    // outer first, then each hole, all through the same closed=True ring projection; a hole that
    // degenerates (projectRing finds nothing) is skipped, the outer is not (matching legacy's
    // `if Assigned(VPoints)` guard, which for the outer gates the whole AddOuter/hole-loop, so a
    // degenerate outer silently contributes nothing to this single polygon).
    auto builder = makePolygonBuilder();
    DoubleRect bounds{};
    const auto projectOne = [&](const IGeometryLonLatSinglePolygon& poly) {
        if (!projectRing(projection, poly.outerBorder()->points(), /*closed=*/true, scratch, bounds)) {
            return;
        }
        builder.addOuter(bounds, copyProjectedPoints(scratch));
        for (std::size_t i = 0; i < poly.holeCount(); ++i) {
            if (projectRing(projection, poly.holeBorder(i)->points(), /*closed=*/true, scratch, bounds)) {
                builder.addHole(bounds, copyProjectedPoints(scratch));
            }
        }
    };
    if (const auto* single = dynamic_cast<const IGeometryLonLatSinglePolygon*>(&source)) {
        projectOne(*single);
    } else if (const auto* multi = dynamic_cast<const IGeometryLonLatMultiPolygon*>(&source)) {
        for (std::size_t i = 0; i < multi->count(); ++i) {
            projectOne(*multi->item(i));
        }
    } else {
        Q_ASSERT(false); // u_GeometryProjectedFactory.pas:497 Assert(False)
    }
    return builder.makeStaticAndClear();
}

// =================================================================================================
// The per-frame cache (u_GeometryProjectedProvider.pas, docs/specs/geo.md §3.5.3, T-0017C2)
// =================================================================================================

namespace {

// u_GeometryProjectedProvider.pas:86-87. Below this many pixels in BOTH dimensions, the geometry
// is too small to matter on screen; the cached answer is nullptr, same as a real cache miss.
constexpr double kMinProjectedLineSize = 1.0;
constexpr double kMinProjectedPolygonSize = 10.0;

// GetProjectedPath/GetProjectedPolygon's literal seed constants (:164-199) -- two fixed starting
// values so a path and a polygon built from geometry that happens to hash the same never collide
// in the one shared cache map (legacy: one THashInterfaceCache2Q for both).
constexpr core::HashValue kProjectedPathHashSeed = 0xfbcb5f3e1bef5742ULL;
constexpr core::HashValue kProjectedPolygonHashSeed = 0x5af2a0463bf6e921ULL;

} // namespace

// Tech Lead ruling T-0017C2 (docs/parity/deviations.md D-16): bounded FIFO/clear-oldest eviction,
// legacy's total capacity (kCacheCapacity). Caller holds no lock; this takes the write lock alone.
void GeometryProjectedProvider::insertBounded(core::HashValue key, GeometryProjectedPtr value) {
    core::WriteLock lock(mutex_);
    if (cache_.contains(key)) {
        cache_[key] = std::move(value);
        return;
    }
    if (cache_.size() >= kCacheCapacity && !insertionOrder_.empty()) {
        cache_.erase(insertionOrder_.front());
        insertionOrder_.pop_front();
    }
    cache_.emplace(key, std::move(value));
    insertionOrder_.push_back(key);
}

std::shared_ptr<const IGeometryProjectedLine>
GeometryProjectedProvider::projectedPath(const ProjectionPtr& projection,
                                         const std::shared_ptr<const IGeometryLonLatLine>& line) {
    Q_ASSERT(projection && line);
    core::HashValue key = kProjectedPathHashSeed;
    core::hashUpdate(key, line->hash());
    core::hashUpdate(key, projection->hash());

    {
        core::ReadLock lock(mutex_);
        if (const auto it = cache_.find(key); it != cache_.end()) {
            return std::static_pointer_cast<const IGeometryProjectedLine>(it->second);
        }
    }

    // TGeometryProjectedProvider.CreateByKey (u_GeometryProjectedProvider.pas:110-146): validate
    // the LonLat bounds for this projection's type, then test the *projected* extent -- computed
    // outside the lock, per the Tech Lead's threading table (§8.1).
    LonLatRect bounds = line->bounds();
    projection->projectionType()->validateLonLatRect(bounds);
    const DoubleRect pixelRect = projection->lonLatRect2PixelRectFloat(bounds);
    std::shared_ptr<const IGeometryProjectedLine> result;
    if (std::abs(pixelRect.left - pixelRect.right) > kMinProjectedLineSize ||
        std::abs(pixelRect.top - pixelRect.bottom) > kMinProjectedLineSize) {
        std::vector<DoublePoint> scratch;
        result = factory_.createLineByLonLatLine(*projection, *line, scratch);
    }

    insertBounded(key, result);
    return result;
}

std::shared_ptr<const IGeometryProjectedPolygon>
GeometryProjectedProvider::projectedPolygon(const ProjectionPtr& projection,
                                            const std::shared_ptr<const IGeometryLonLatPolygon>& polygon) {
    Q_ASSERT(projection && polygon);
    core::HashValue key = kProjectedPolygonHashSeed;
    core::hashUpdate(key, polygon->hash());
    core::hashUpdate(key, projection->hash());

    {
        core::ReadLock lock(mutex_);
        if (const auto it = cache_.find(key); it != cache_.end()) {
            return std::static_pointer_cast<const IGeometryProjectedPolygon>(it->second);
        }
    }

    LonLatRect bounds = polygon->bounds();
    projection->projectionType()->validateLonLatRect(bounds);
    const DoubleRect pixelRect = projection->lonLatRect2PixelRectFloat(bounds);
    std::shared_ptr<const IGeometryProjectedPolygon> result;
    if (std::abs(pixelRect.left - pixelRect.right) > kMinProjectedPolygonSize ||
        std::abs(pixelRect.top - pixelRect.bottom) > kMinProjectedPolygonSize) {
        std::vector<DoublePoint> scratch;
        result = factory_.createPolygonByLonLatPolygon(*projection, *polygon, scratch);
    }

    insertBounded(key, result);
    return result;
}

} // namespace cg::geo
