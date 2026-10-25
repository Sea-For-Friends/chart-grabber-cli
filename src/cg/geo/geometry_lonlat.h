// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GeometryLonLat.pas, u_GeometryLonLat.pas, u_GeometryLonLatMulti.pas,
// i_DoublePointsMeta.pas, u_DoublePointsMeta*.pas. docs/specs/geo.md §3.2.
//
// The lon/lat side of the geometry model: point / line / polygon / multi hierarchy, plus the
// per-vertex metadata sidecar. Concrete classes are built ONLY by GeometryLonLatFactory /
// GeometryLonLatLineBuilder / GeometryLonLatPolygonBuilder (geometry_factory.h): hash() and
// bounds() are computed once there and stored, never lazily (docs/specs/geo.md §3.2 point 4).

#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"

#include <QDateTime>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace cg::geo {

// --- per-vertex metadata ------------------------------------------------------
// Ports TDoublePointsMeta (t_GeoTypes.pas:61) and TDoublePointsMetaItem (:67).
// Legacy holds two nullable parallel C arrays; nil means "this line has no elevations".
// An empty vector means the same here. When non-empty a vector is the same length as
// the point vector -- assert it, do not carry a per-item "ok" flag.
struct PointsMeta {
    std::vector<double> elevation;    // metres; NaN for a point without one
    std::vector<QDateTime> timestamp; // UTC; invalid QDateTime for a point without one

    [[nodiscard]] bool hasElevation() const noexcept { return !elevation.empty(); }
    [[nodiscard]] bool hasTimestamp() const noexcept { return !timestamp.empty(); }
    [[nodiscard]] bool isEmpty() const noexcept { return elevation.empty() && timestamp.empty(); }
    // u_DoublePointsMetaFunc.pas CopyMeta(AMeta, ACount, AStartIndex).
    [[nodiscard]] PointsMeta slice(std::size_t from, std::size_t count) const;
};

using LonLatPointsPtr = std::shared_ptr<const std::vector<LonLat>>;

// --- the hierarchy --------------------------------------------------------------
// FEAT-GEO-009
class IGeometryLonLat {
public:
    virtual ~IGeometryLonLat() = default;
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual LonLatRect bounds() const noexcept = 0;
    [[nodiscard]] virtual LonLat goToPoint() const = 0; // "fly to" anchor
    [[nodiscard]] virtual bool isSameGeometry(const IGeometryLonLat& other) const = 0;
};
using GeometryLonLatPtr = std::shared_ptr<const IGeometryLonLat>;

class IGeometryLonLatPoint : public IGeometryLonLat {
public:
    [[nodiscard]] virtual LonLat point() const noexcept = 0;
};

class IGeometryLonLatMultiPoint : public IGeometryLonLat {
public:
    [[nodiscard]] virtual std::span<const LonLat> points() const noexcept = 0;
};

class IGeometryLonLatLine : public IGeometryLonLat {}; // marker

class IGeometryLonLatSingleLine : public IGeometryLonLatLine {
public:
    [[nodiscard]] virtual std::span<const LonLat> points() const noexcept = 0;
    [[nodiscard]] virtual const PointsMeta& meta() const noexcept = 0; // only here (§3.2 point 3)
};

class IGeometryLonLatMultiLine : public IGeometryLonLatLine {
public:
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;
    [[nodiscard]] virtual const std::shared_ptr<const IGeometryLonLatSingleLine>&
    item(std::size_t i) const = 0;
};

class IGeometryLonLatPolygon : public IGeometryLonLat {}; // marker

// NOTE: a Polygon (i_GeometryLonLat.pas:99) -- keep the inheritance, a bare ring satisfies a
// dynamic_pointer_cast<IGeometryLonLatPolygon>. Contours are stored OPEN: the closing vertex is
// not in points(); closure is synthesized by the WKB writer only (§3.7 rule 5). Never "normalise"
// by appending points()[0].
class IGeometryLonLatContour : public IGeometryLonLatPolygon {
public:
    [[nodiscard]] virtual std::span<const LonLat> points() const noexcept = 0;
};

class IGeometryLonLatSinglePolygon : public IGeometryLonLatPolygon {
public:
    [[nodiscard]] virtual const std::shared_ptr<const IGeometryLonLatContour>& outerBorder() const = 0;
    [[nodiscard]] virtual std::size_t holeCount() const noexcept = 0;
    [[nodiscard]] virtual const std::shared_ptr<const IGeometryLonLatContour>&
    holeBorder(std::size_t i) const = 0;
};

class IGeometryLonLatMultiPolygon : public IGeometryLonLatPolygon {
public:
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;
    [[nodiscard]] virtual const std::shared_ptr<const IGeometryLonLatSinglePolygon>&
    item(std::size_t i) const = 0;
};

// --- concrete classes (u_GeometryLonLat.pas, u_GeometryLonLatMulti.pas) ---------------------
// Built ONLY by GeometryLonLatFactory / GeometryLonLatLineBuilder / GeometryLonLatPolygonBuilder
// (geometry_factory.h), which computes hash() and bounds() once and passes them in. Exposed here
// (not file-local to geometry_lonlat.cpp) because the factory, in a different translation unit,
// constructs them directly -- the same cross-file split as i_/u_GeometryLonLat.pas and
// i_/u_GeometryLonLatFactory.pas in legacy, and the same convention as ProjectionBasic256x256
// (projection.h) being constructed by projection_set.cpp.

class GeometryLonLatPoint final : public IGeometryLonLatPoint {
public:
    GeometryLonLatPoint(core::HashValue hash, LonLat point) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override { return point_; }
    [[nodiscard]] LonLat point() const noexcept override { return point_; }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLat point_;
    LonLatRect bounds_;
};

class GeometryLonLatMultiPoint final : public IGeometryLonLatMultiPoint {
public:
    GeometryLonLatMultiPoint(core::HashValue hash, const LonLatRect& bounds, LonLatPointsPtr points) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override;
    [[nodiscard]] std::span<const LonLat> points() const noexcept override { return *points_; }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLatRect bounds_;
    LonLatPointsPtr points_;
};

class GeometryLonLatSingleLine final : public IGeometryLonLatSingleLine {
public:
    GeometryLonLatSingleLine(core::HashValue hash, const LonLatRect& bounds, LonLatPointsPtr points,
                             PointsMeta meta) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override;
    [[nodiscard]] std::span<const LonLat> points() const noexcept override { return *points_; }
    [[nodiscard]] const PointsMeta& meta() const noexcept override { return meta_; }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLatRect bounds_;
    LonLatPointsPtr points_;
    PointsMeta meta_;
};

// A contour's ring is stored OPEN (§3.2 point 2): points() never includes a closing vertex the
// caller did not supply. But u_GeometryLonLat.pas:TGeometryLonLatBase.Create(AClosed=True, ...)
// auto-trims the OPPOSITE case: when the input already ends with a literal repeat of its first
// point (a caller-supplied closing vertex), the logical count drops by one so the ring is still
// open. hash() is computed by the caller over the UNTRIMMED buffer (legacy does the same --
// CalcHashByBuffer runs before TGeometryLonLatContour.Create, using the original count), so
// points_ keeps the untrimmed buffer and count_ alone tracks the trim.
class GeometryLonLatContour final : public IGeometryLonLatContour {
public:
    GeometryLonLatContour(core::HashValue hash, const LonLatRect& bounds, LonLatPointsPtr points) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override { return bounds_.center(); }
    [[nodiscard]] std::span<const LonLat> points() const noexcept override {
        return {points_->data(), count_};
    }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLatRect bounds_;
    LonLatPointsPtr points_;
    std::size_t count_;
};
using GeometryLonLatContourPtr = std::shared_ptr<const IGeometryLonLatContour>;

// Ports both TGeometryLonLatSinglePolygon (no holes, self-as-contour) and
// TGeometryLonLatSinglePolygonWithHoles: a single class with an explicit outerBorder_ member
// covers both -- an empty holes_ is exactly the "no holes" case, with no runtime-observable
// difference (same hash()/bounds()/isSameGeometry() shape).
class GeometryLonLatSinglePolygon final : public IGeometryLonLatSinglePolygon {
public:
    GeometryLonLatSinglePolygon(core::HashValue hash, const LonLatRect& bounds,
                                GeometryLonLatContourPtr outer,
                                std::vector<GeometryLonLatContourPtr> holes) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override { return outer_->goToPoint(); }
    [[nodiscard]] const GeometryLonLatContourPtr& outerBorder() const override { return outer_; }
    [[nodiscard]] std::size_t holeCount() const noexcept override { return holes_.size(); }
    [[nodiscard]] const GeometryLonLatContourPtr& holeBorder(std::size_t i) const override {
        return holes_[i];
    }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLatRect bounds_;
    GeometryLonLatContourPtr outer_;
    std::vector<GeometryLonLatContourPtr> holes_;
};
using GeometryLonLatSinglePolygonPtr = std::shared_ptr<const IGeometryLonLatSinglePolygon>;

using GeometryLonLatSingleLinePtr = std::shared_ptr<const IGeometryLonLatSingleLine>;

class GeometryLonLatMultiLine final : public IGeometryLonLatMultiLine {
public:
    GeometryLonLatMultiLine(core::HashValue hash, const LonLatRect& bounds,
                            std::vector<GeometryLonLatSingleLinePtr> items) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override { return items_.front()->goToPoint(); }
    [[nodiscard]] std::size_t count() const noexcept override { return items_.size(); }
    [[nodiscard]] const GeometryLonLatSingleLinePtr& item(std::size_t i) const override { return items_[i]; }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLatRect bounds_;
    std::vector<GeometryLonLatSingleLinePtr> items_;
};

class GeometryLonLatMultiPolygon final : public IGeometryLonLatMultiPolygon {
public:
    GeometryLonLatMultiPolygon(core::HashValue hash, const LonLatRect& bounds,
                               std::vector<GeometryLonLatSinglePolygonPtr> items) noexcept;

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] LonLatRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] LonLat goToPoint() const override { return bounds_.center(); }
    [[nodiscard]] std::size_t count() const noexcept override { return items_.size(); }
    [[nodiscard]] const GeometryLonLatSinglePolygonPtr& item(std::size_t i) const override {
        return items_[i];
    }
    [[nodiscard]] bool isSameGeometry(const IGeometryLonLat& other) const override;

private:
    core::HashValue hash_;
    LonLatRect bounds_;
    std::vector<GeometryLonLatSinglePolygonPtr> items_;
};

} // namespace cg::geo
