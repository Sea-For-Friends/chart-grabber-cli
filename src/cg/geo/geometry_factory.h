// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GeometryLonLatFactory.pas, u_GeometryLonLatFactory.pas (LonLat half,
// docs/specs/geo.md §3.5), i_/u_GeometryProjectedFactory.pas (the projected half, §3.5.1/§3.5.2,
// T-0017C2), i_/u_GeometryProjectedProvider.pas (the per-frame cache, §3.5.3, T-0017C2).
// createLineByStream/createPolygonByLine/createPolygonByLineAndFilter (T-0017B, this file's
// extension) need IPointStream/ILonLatPointFilter (point_stream.h), which T-0017A did not yet
// have -- see docs/handoffs/T-0017A.md and T-0017A-review.md F8.
//
// The LonLat factory/builders are `idioms.md` §13 purpose-(b) factories: they intern nothing, but
// they DO compute the hash and the bounding box, which the geometry objects (geometry_lonlat.h)
// then hold as immutable members. The projected factory/builders (below) are the same shape minus
// hash() (geometry_projected.h has none, §3.3): addLine/addOuter/addHole always take the caller-
// computed bounds, no compute-it-for-me overload (spec §3.5.1) -- i_GeometryProjectedFactory.pas
// declares only the one AddLine/AddOuter/AddHole signature, each with ABounds required.

#include "cg/core/hash.h"
#include "cg/core/sync.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/geometry_projected.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/point_stream.h"
#include "cg/geo/projection.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace cg::geo {

class IDatum;

// Ports IGeometryLonLatLineBuilder / TGeometryLonLatLineBuilder.
class GeometryLonLatLineBuilder {
public:
    void addLine(const LonLatRect& bounds, LonLatPointsPtr points, PointsMeta meta = {});
    void addLine(LonLatPointsPtr points, PointsMeta meta = {}); // bounds computed here
    void addLine(const IGeometryLonLatLine& line);              // single or multi

    // Degenerate-collapse rule (docs/specs/geo.md §3.5): a single line comes back as-is; two or
    // more come back as an IGeometryLonLatMultiLine; nothing added comes back nullptr.
    [[nodiscard]] GeometryLonLatPtr makeStaticAndClear();
    [[nodiscard]] GeometryLonLatPtr makeStaticCopy() const;

private:
    std::vector<GeometryLonLatSingleLinePtr> lines_;
    LonLatRect bounds_{};
    core::HashValue hash_ = core::kNoHash;
};

// Ports IGeometryLonLatPolygonBuilder / TGeometryLonLatPolygonBuilder. addHole attaches to the
// most recently added outer (addOuter or addPolygon) -- the builder is stateful, order matters.
class GeometryLonLatPolygonBuilder {
public:
    void addPolygon(const IGeometryLonLatPolygon& polygon);
    void addOuter(const LonLatRect& bounds, LonLatPointsPtr points);
    void addOuter(LonLatPointsPtr points);
    void addHole(const LonLatRect& bounds, LonLatPointsPtr points);
    void addHole(LonLatPointsPtr points);

    [[nodiscard]] GeometryLonLatPtr makeStaticAndClear();
    [[nodiscard]] GeometryLonLatPtr makeStaticCopy() const;

private:
    void addOuterContour(GeometryLonLatContourPtr outer);
    void addHoleContour(GeometryLonLatContourPtr hole);
    [[nodiscard]] GeometryLonLatSinglePolygonPtr buildPending() const;

    GeometryLonLatContourPtr pendingOuter_;
    std::vector<GeometryLonLatContourPtr> pendingHoles_;
    std::vector<GeometryLonLatSinglePolygonPtr> polygons_;
};

// FEAT-GEO-010
class GeometryLonLatFactory {
public:
    explicit GeometryLonLatFactory(HashSeed hash = {});

    [[nodiscard]] std::shared_ptr<const IGeometryLonLatPoint> createPoint(LonLat p) const;
    [[nodiscard]] std::shared_ptr<const IGeometryLonLatMultiPoint>
    createMultiPoint(std::span<const LonLat> points) const;

    [[nodiscard]] GeometryLonLatLineBuilder makeLineBuilder() const;
    [[nodiscard]] GeometryLonLatPolygonBuilder makePolygonBuilder() const;

    // Splits on the NaN separator into single-or-multi (see the builders' collapse rule above).
    [[nodiscard]] GeometryLonLatPtr createLine(std::span<const LonLat> points, const PointsMeta* meta) const;
    [[nodiscard]] GeometryLonLatPtr createPolygon(std::span<const LonLat> points) const;

    [[nodiscard]] std::shared_ptr<const IGeometryLonLatSinglePolygon>
    createPolygonByRect(const LonLatRect& rect) const;
    [[nodiscard]] std::shared_ptr<const IGeometryLonLatSinglePolygon>
    createPolygonCircleByPoint(const IDatum& datum, LonLat centre, double radiusMetres) const;
    [[nodiscard]] std::shared_ptr<const IGeometryLonLatSinglePolygon>
    createPolygonSquareByPoint(const IDatum& datum, LonLat centre, double radiusMetres) const;
    [[nodiscard]] std::shared_ptr<const IGeometryLonLatSinglePolygon>
    createPolygonSquareOnSurfaceByPoint(const IDatum& datum, LonLat centre, double radiusMetres) const;

    // CreateLonLatLineByEnum (u_GeometryLonLatFactory.pas:972-1005): drains the stream, splitting
    // on the NaN separator. Per-point meta is not read -- legacy's own CreateLonLatLineByEnum
    // calls the one-arg Next, same as this port.
    [[nodiscard]] GeometryLonLatPtr createLineByStream(LonLatStream stream,
                                                       std::vector<LonLat>& scratch) const;

    // CreateLonLatPolygonByLonLatPathAndFilter (u_GeometryLonLatFactory.pas:1123-1150): for each
    // single line in source (a bare line or a multi-line), run it through filter and split the
    // filtered stream on the NaN separator into outers.
    [[nodiscard]] GeometryLonLatPtr createPolygonByLineAndFilter(const IGeometryLonLatLine& source,
                                                                 const LonLatPointFilter& filter) const;

    // CreateLonLatPolygonByLine (u_GeometryLonLatFactory.pas:874-893): legacy selects between two
    // implementations at construction time (u_GeometryLonLatFactory.pas:23); per D-3
    // (docs/specs/geo.md §10/§3.5.2) the port always uses the streaming bisector buffer
    // (line2Poly), never the un-ported Clipper-Minkowski generator (u_LonLatPolygonGenerator.pas).
    // line2Poly needs a *live* IProjection for its pixel-space math (it derives its IDatum from
    // the projection, same as legacy's own TEnumDoublePointLine2Poly) -- the spec's illustrative
    // signature (IDatum, line, radius) has no projection and would make the datum a redundant,
    // unused parameter under D-3; this signature drops IDatum and adds IProjection instead. See
    // docs/handoffs/T-0017B.md Open questions.
    [[nodiscard]] GeometryLonLatPtr createPolygonByLine(const IGeometryLonLatLine& line, double radiusMetres,
                                                        const IProjection& projection,
                                                        std::vector<DoublePoint>& scratch) const;

private:
    [[nodiscard]] std::shared_ptr<const IGeometryLonLatSinglePolygon>
    createPolygonInternal(const LonLatRect& bounds, std::vector<LonLat> points) const;

    HashSeed hash_;
};

// =================================================================================================
// The projected factory (docs/specs/geo.md §3.5.1, T-0017C2)
// =================================================================================================

// Ports IGeometryProjectedLineBuilder / TGeometryProjectedLineBuilder
// (u_GeometryProjectedFactory.pas). Same collapse rule as GeometryLonLatLineBuilder: one line
// added comes back as itself, two or more as an IGeometryProjectedMultiLine, nothing added as
// nullptr.
class GeometryProjectedLineBuilder {
public:
    void addLine(const DoubleRect& bounds, ProjectedPointsPtr points);

    [[nodiscard]] std::shared_ptr<const IGeometryProjectedLine> makeStaticAndClear();
    [[nodiscard]] std::shared_ptr<const IGeometryProjectedLine> makeStaticCopy() const;

private:
    std::vector<GeometryProjectedSingleLinePtr> lines_;
    DoubleRect bounds_{};
};

// Ports IGeometryProjectedPolygonBuilder / TGeometryProjectedPolygonBuilder. addHole attaches to
// the most recently added outer -- the builder is stateful, order matters (same contract as
// GeometryLonLatPolygonBuilder, including the "hole before any outer" quirk below).
class GeometryProjectedPolygonBuilder {
public:
    void addOuter(const DoubleRect& bounds, ProjectedPointsPtr points);
    void addHole(const DoubleRect& bounds, ProjectedPointsPtr points);

    [[nodiscard]] std::shared_ptr<const IGeometryProjectedPolygon> makeStaticAndClear();
    [[nodiscard]] std::shared_ptr<const IGeometryProjectedPolygon> makeStaticCopy() const;

private:
    void addOuterContour(GeometryProjectedContourPtr outer);
    void addHoleContour(GeometryProjectedContourPtr hole);
    [[nodiscard]] GeometryProjectedSinglePolygonPtr buildPending() const;

    GeometryProjectedContourPtr pendingOuter_;
    std::vector<GeometryProjectedContourPtr> pendingHoles_;
    std::vector<GeometryProjectedSinglePolygonPtr> polygons_;
};

// Ports IGeometryProjectedFactory / TGeometryProjectedFactory (u_GeometryProjectedFactory.pas).
// FEAT-GEO-013
class GeometryProjectedFactory {
public:
    [[nodiscard]] GeometryProjectedLineBuilder makeLineBuilder() const;
    [[nodiscard]] GeometryProjectedPolygonBuilder makePolygonBuilder() const;

    [[nodiscard]] std::shared_ptr<const IGeometryProjectedMultiPoint>
    createMultiPoint(const DoubleRect& bounds, ProjectedPointsPtr points) const;
    [[nodiscard]] std::shared_ptr<const IGeometryProjectedMultiPoint>
    createMultiPointByLonLat(const IProjection& projection, const IGeometryLonLatMultiPoint& source,
                             std::vector<DoublePoint>& scratch) const;
    [[nodiscard]] std::shared_ptr<const IGeometryProjectedLine>
    createLineByLonLatLine(const IProjection& projection, const IGeometryLonLatLine& source,
                           std::vector<DoublePoint>& scratch) const;
    [[nodiscard]] std::shared_ptr<const IGeometryProjectedPolygon>
    createPolygonByLonLatPolygon(const IProjection& projection, const IGeometryLonLatPolygon& source,
                                 std::vector<DoublePoint>& scratch) const;
};

// Ports i_/u_GeometryProjectedProvider.pas -- docs/specs/geo.md §3.5.3. The one mutable, locked
// object in `cg::geo` (§8.1): a cache from (line-or-polygon hash, projection hash) to the
// projected geometry, computed lazily. "Per-frame" describes how it is *queried* (once per
// visible mark per frame by the renderer), not how it is rebuilt -- entries live for the life of
// the provider, subject to the capacity below.
//
// Legacy's 2Q cache (u_HashInterfaceCache2Q.pas, bounded, three internal queues) is not ported
// structure-for-structure -- Tech Lead ruling T-0017C2: no ported 2Q/HashCacheWithQueues helper
// exists yet in cg::core (checked), so this stays a plain mutex-guarded map, but it MUST be
// bounded like legacy, not the unbounded map an earlier draft of this file had. Capacity is
// legacy's total live-item count -- AFirstUseCount + AMultiUseCount + AFirstOutCount = 1000 +
// 4000 + 1000 = 6000 (u_GeometryProjectedProvider.pas:99-107) -- with a plain FIFO/clear-oldest
// eviction instead of legacy's 2Q promotion between its FirstIn/Multi/FirstOut queues. See
// docs/parity/deviations.md D-16: eviction order differs from legacy's 2Q, capacity is identical.
class GeometryProjectedProvider {
public:
    GeometryProjectedProvider() = default;

    [[nodiscard]] std::shared_ptr<const IGeometryProjectedLine>
    projectedPath(const ProjectionPtr& projection, const std::shared_ptr<const IGeometryLonLatLine>& line);
    [[nodiscard]] std::shared_ptr<const IGeometryProjectedPolygon>
    projectedPolygon(const ProjectionPtr& projection,
                     const std::shared_ptr<const IGeometryLonLatPolygon>& polygon);

private:
    // u_GeometryProjectedProvider.pas:103-106: 1000 (FirstIn) + 4000 (Multi) + 1000 (FirstOut).
    static constexpr std::size_t kCacheCapacity = 6000;

    void insertBounded(core::HashValue key, GeometryProjectedPtr value);

    GeometryProjectedFactory factory_;
    core::Mutex mutex_;
    std::unordered_map<core::HashValue, GeometryProjectedPtr> cache_;
    std::deque<core::HashValue> insertionOrder_; // FIFO eviction order; oldest at the front.
};

} // namespace cg::geo
