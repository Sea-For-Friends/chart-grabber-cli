// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_EnumDoublePoint.pas, u_EnumDoublePointAbstract.pas, i_DoublePointFilter.pas, and the
// point-stream sources/decorators of docs/specs/geo.md §3.4:
// u_EnumDoublePoint{sByArray,BySingleLine,ByLineSet,ClosePoly,FilterEqual,FilterFirstSegment,
// LonLatToMapPixel,MapPixelToLocalPixel,Line2Poly}.pas. The clipper (declared here, implemented
// in point_stream_clip.cpp) ports u_EnumDoublePointWithClip.pas + u_EnumDoublePointClipInternal.pas.
//
// Pull-based enumerator pipeline: lazy by design (§3.4 "Keep the laziness" -- several nodes emit
// *more* points than they consume, e.g. ClosePoly re-emits the first vertex, the clipper's
// outside->inside transition emits two points, the line-set flattener injects separators).
// Materialising a stage into a vector would allocate one intermediate vector per stage for one
// clipped polygon; `render` runs this per frame per layer, so do not.
//
// Every point value stays a plain DoublePoint regardless of the coordinate space it represents
// (exactly as legacy's single TDoublePoint did for lon/lat, projected and local pixel alike);
// LonLat::toPoint()/fromPoint() is the sanctioned conversion at the lon/lat boundary. The
// TypedPointStream<Tag> wrapper below is a compile-time label only (no vtable, no per-point
// tagging) so a projected stream cannot be handed to a lon/lat consumer by accident.

#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"

#include <QDateTime>

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace cg::geo {

class IProjection;
class ILocalCoordConverter;

// The NaN separator protocol. Legacy streams CEmptyDoublePoint (u_GeoFunc.pas:32) *as a value*
// to mark a sub-line/contour break, and every decorator has an explicit branch for it. The port
// keeps the protocol (it crosses into WKB meta indexing and into the factories), but names it,
// so the branches are greppable. The test is legacy PointIsEmpty (u_GeoFunc.pas:550-552): X or Y NaN.
[[nodiscard]] inline bool isSeparator(DoublePoint p) noexcept {
    return pointIsEmpty(p);
}

// Ports TDoublePointsMetaItem (t_GeoTypes.pas:67), the per-point payload of the two-arg next().
struct PointMetaItem {
    bool hasElevation = false;
    bool hasTimestamp = false;
    double elevation = 0.0;
    QDateTime timestamp;
};

// Ports IEnumDoublePoint (i_EnumDoublePoint.pas).
class IPointStream {
public:
    virtual ~IPointStream() = default;
    [[nodiscard]] virtual bool next(DoublePoint& out) = 0;
    // Default fills a blank meta item (u_EnumDoublePointAbstract.pas:18) and delegates to the
    // one-arg next(); only singleLineStream overrides it with real per-vertex data.
    [[nodiscard]] virtual bool next(DoublePoint& out, PointMetaItem& meta);
};
using PointStreamPtr = std::unique_ptr<IPointStream>;

// The three legacy phantom interfaces (IEnumLonLatPoint/IEnumProjectedPoint/IEnumLocalPoint)
// become move-only wrapper types with no vtable of their own.
template <class Tag> class TypedPointStream {
public:
    TypedPointStream() = default;
    explicit TypedPointStream(PointStreamPtr s) : s_(std::move(s)) {}
    [[nodiscard]] bool next(DoublePoint& out) { return s_->next(out); }
    [[nodiscard]] bool next(DoublePoint& out, PointMetaItem& m) { return s_->next(out, m); }
    [[nodiscard]] IPointStream& raw() noexcept { return *s_; }
    // Not in the spec's illustrative code block, but required to bridge a typed stream back to
    // the untyped decorators (closePoly/line2Poly/...) that every LonLatPointFilter etc. must
    // compose internally -- see GeometryLonLatFactory::createPolygonByLine.
    [[nodiscard]] PointStreamPtr release() && { return std::move(s_); }

private:
    PointStreamPtr s_;
};
struct LonLatTag {};
struct ProjectedTag {};
struct LocalTag {};
using LonLatStream = TypedPointStream<LonLatTag>;
using ProjectedStream = TypedPointStream<ProjectedTag>;
using LocalStream = TypedPointStream<LocalTag>;

// i_DoublePointFilter.pas's five typed interfaces (the untyped IDoublePointFilter base has no
// C++ counterpart -- nothing in this module needs it at the type level).
using LonLatPointFilter = std::function<LonLatStream(LonLatStream)>;
using LonLatPointConverter = std::function<ProjectedStream(LonLatStream)>;
using ProjectedPointFilter = std::function<ProjectedStream(ProjectedStream)>;
using ProjectedPointConverter = std::function<LocalStream(ProjectedStream)>;
using LocalPointFilter = std::function<LocalStream(LocalStream)>;

// --- sources (§3.4.1) --------------------------------------------------------------------------
// All non-owning: the caller keeps the backing storage (and, for lineSetStream, the
// IGeometryLonLatMultiLine itself) alive for the stream's lifetime -- same contract as legacy's
// raw PDoublePointArray plus an owner reference held only to pin lifetime.

// TEnumDoublePointsByArray: a plain walk.
[[nodiscard]] PointStreamPtr pointsStream(std::span<const DoublePoint> points);

// TEnumDoublePointBySingleLineBase: walk; when closed, re-emit points[0] after the last vertex
// (this is where implicit contour closure lives). meta may be null; points must not be empty
// (legacy: Assert(ACount > 0, 'No points')).
[[nodiscard]] PointStreamPtr singleLineStream(std::span<const DoublePoint> points, const PointsMeta* meta,
                                              bool closed);

// TEnumDoublePointByLineSetBase / TEnumLonLatPointByPath: flatten sub-lines, injecting one
// separator *between* them (never before the first, never after the last).
[[nodiscard]] PointStreamPtr lineSetStream(const IGeometryLonLatMultiLine& lineSet);

// --- decorators (§3.4.1) -----------------------------------------------------------------------

// TEnumDoublePointClosePoly: at every break and at EOF, if the run had > 1 point and last != first,
// emit the first point again, then the break.
[[nodiscard]] PointStreamPtr closePoly(PointStreamPtr source);

// TEnumDoublePointFilterEqual: Chebyshev decimation -- drop a point unless |dx| > 1 or |dy| > 1
// against the last *emitted* point; also collapses consecutive breaks.
[[nodiscard]] PointStreamPtr filterEqual(PointStreamPtr source);

// TEnumDoublePointFilterFirstSegment: skip leading breaks, emit the first non-empty run, stop at
// the next break.
[[nodiscard]] PointStreamPtr filterFirstSegment(PointStreamPtr source);

// TEnumDoublePointLonLatToMapPixel: per point, projectionType->validateLonLatPos() then
// projection->lonLat2PixelPosFloat(); separators pass through untouched.
[[nodiscard]] PointStreamPtr lonLatToMapPixel(const IProjection& projection, PointStreamPtr lonLatSource);

// TEnumDoublePointMapPixelToLocalPixel / ...Simple: via ILocalCoordConverter, or the fast
// translate-by-(-topLeft) path.
[[nodiscard]] PointStreamPtr mapPixelToLocalPixel(const ILocalCoordConverter& converter,
                                                  PointStreamPtr source);
[[nodiscard]] PointStreamPtr mapPixelToLocalPixelSimple(DoublePoint topLeft, PointStreamPtr source);

// TEnumDoublePointLine2Poly: buffer a polyline into a polygon of half-width radiusMetres, in
// projected space, using angle bisectors; emits the forward side live, then the reversed
// scratch buffer for the return side. scratch is a reusable buffer (u_DoublePointsAggregator's
// role); it is cleared internally when a new line starts.
[[nodiscard]] PointStreamPtr line2Poly(PointStreamPtr lonLatSource, double radiusMetres,
                                       const IProjection& projection, std::vector<DoublePoint>& scratch);

// --- the clipper (point_stream_clip.cpp) --------------------------------------------------------

// The half-plane primitives (u_EnumDoublePointClipInternal.pas), exposed individually because
// clipByRect composes them and the legacy DUnit suite (§11.1 item 7) pins the left-border
// primitive directly.
[[nodiscard]] PointStreamPtr clipByLeftBorder(double x, PointStreamPtr source);
[[nodiscard]] PointStreamPtr clipByRightBorder(double x, PointStreamPtr source);
[[nodiscard]] PointStreamPtr clipByTopBorder(double y, PointStreamPtr source);
[[nodiscard]] PointStreamPtr clipByBottomBorder(double y, PointStreamPtr source);

// u_EnumDoublePointWithClip.pas: order-sensitive, the two branches differ (§3.4.1) --
// closed:  source -> closePoly -> clipBottom -> closePoly -> clipRight -> closePoly
//                  -> clipTop   -> closePoly -> clipLeft  -> closePoly        (9 stages)
// open:    source -> clipBottom -> clipTop -> clipLeft -> clipRight          (4 stages, B,T,L,R)
[[nodiscard]] PointStreamPtr clipByRect(bool closed, const DoubleRect& rect, PointStreamPtr source);

} // namespace cg::geo
