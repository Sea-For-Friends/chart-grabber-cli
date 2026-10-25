// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GeometryProjected.pas, u_GeometryProjected.pas, u_GeometryProjectedMulti.pas.
// docs/specs/geo.md §3.3.
//
// The projected mirror of the lon/lat hierarchy (geometry_lonlat.h): same shape, with four
// deliberate asymmetries (all legacy, §3.3): no `Point` singular type (only MultiPoint), no
// hash(), no isSame*, no meta; bounds() is a plain DoubleRect; and the polygon interface (not a
// free function) carries the spatial predicates -- IsPointInPolygon, IsPointOnBorder,
// CheckRectIntersection, IsRectIntersectPolygon/Border, CalcArea.
//
// Concrete classes are built ONLY by GeometryProjectedFactory / GeometryProjectedPolygonBuilder
// (T-0017C2, out of scope here): bounds() is computed once there (ProjectedMBRByPoints) and
// passed in, never lazily -- same convention as geometry_lonlat.h.

#include "cg/geo/geo_types.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace cg::geo {

// FEAT-GEO-013
class IGeometryProjected {
public:
    virtual ~IGeometryProjected() = default;
    [[nodiscard]] virtual DoubleRect bounds() const noexcept = 0;
};
using GeometryProjectedPtr = std::shared_ptr<const IGeometryProjected>;

class IGeometryProjectedMultiPoint : public IGeometryProjected {
public:
    [[nodiscard]] virtual std::span<const DoublePoint> points() const noexcept = 0;
};

class IGeometryProjectedLine : public IGeometryProjected {
public:
    [[nodiscard]] virtual bool isPointOnPath(DoublePoint p, double dist) const = 0;
    [[nodiscard]] virtual bool isRectIntersectPath(const DoubleRect& r) const = 0;
};

class IGeometryProjectedSingleLine : public IGeometryProjectedLine {
public:
    [[nodiscard]] virtual std::span<const DoublePoint> points() const noexcept = 0;
};
using GeometryProjectedSingleLinePtr = std::shared_ptr<const IGeometryProjectedSingleLine>;

class IGeometryProjectedMultiLine : public IGeometryProjectedLine {
public:
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;
    [[nodiscard]] virtual const GeometryProjectedSingleLinePtr& item(std::size_t i) const = 0;
};

class IGeometryProjectedPolygon : public IGeometryProjected {
public:
    [[nodiscard]] virtual bool isPointInPolygon(DoublePoint p) const = 0;
    [[nodiscard]] virtual bool isPointOnBorder(DoublePoint p, double dist) const = 0;
    [[nodiscard]] virtual RectWithPolygonIntersection checkRectIntersection(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual bool isRectIntersectPolygon(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual bool isRectIntersectBorder(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual double calcArea() const = 0;
};

class IGeometryProjectedContour : public IGeometryProjectedPolygon {
public:
    [[nodiscard]] virtual std::span<const DoublePoint> points() const noexcept = 0;
};
using GeometryProjectedContourPtr = std::shared_ptr<const IGeometryProjectedContour>;

class IGeometryProjectedSinglePolygon : public IGeometryProjectedPolygon {
public:
    [[nodiscard]] virtual const GeometryProjectedContourPtr& outerBorder() const = 0;
    [[nodiscard]] virtual std::size_t holeCount() const noexcept = 0;
    [[nodiscard]] virtual const GeometryProjectedContourPtr& holeBorder(std::size_t i) const = 0;
};
using GeometryProjectedSinglePolygonPtr = std::shared_ptr<const IGeometryProjectedSinglePolygon>;

class IGeometryProjectedMultiPolygon : public IGeometryProjectedPolygon {
public:
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;
    [[nodiscard]] virtual const GeometryProjectedSinglePolygonPtr& item(std::size_t i) const = 0;
};

// --- concrete classes (u_GeometryProjected.pas, u_GeometryProjectedMulti.pas) ----------------
// Exposed here (not file-local to geometry_projected.cpp) for the same reason as their LonLat
// counterparts: GeometryProjectedFactory, in a different translation unit, constructs them
// directly.

using ProjectedPointsPtr = std::shared_ptr<const std::vector<DoublePoint>>;

class GeometryProjectedMultiPoint final : public IGeometryProjectedMultiPoint {
public:
    GeometryProjectedMultiPoint(const DoubleRect& bounds, ProjectedPointsPtr points) noexcept;

    [[nodiscard]] DoubleRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] std::span<const DoublePoint> points() const noexcept override { return *points_; }

private:
    DoubleRect bounds_;
    ProjectedPointsPtr points_;
};

class GeometryProjectedSingleLine final : public IGeometryProjectedSingleLine {
public:
    GeometryProjectedSingleLine(const DoubleRect& bounds, ProjectedPointsPtr points) noexcept;

    [[nodiscard]] DoubleRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] std::span<const DoublePoint> points() const noexcept override { return *points_; }
    [[nodiscard]] bool isPointOnPath(DoublePoint p, double dist) const override;
    [[nodiscard]] bool isRectIntersectPath(const DoubleRect& r) const override;

private:
    DoubleRect bounds_;
    ProjectedPointsPtr points_;
};

class GeometryProjectedMultiLine final : public IGeometryProjectedMultiLine {
public:
    GeometryProjectedMultiLine(const DoubleRect& bounds,
                               std::vector<GeometryProjectedSingleLinePtr> items) noexcept;

    [[nodiscard]] DoubleRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] std::size_t count() const noexcept override { return items_.size(); }
    [[nodiscard]] const GeometryProjectedSingleLinePtr& item(std::size_t i) const override {
        return items_[i];
    }
    [[nodiscard]] bool isPointOnPath(DoublePoint p, double dist) const override;
    [[nodiscard]] bool isRectIntersectPath(const DoubleRect& r) const override;

private:
    DoubleRect bounds_;
    std::vector<GeometryProjectedSingleLinePtr> items_;
};

// A contour's ring is implicitly closed (§3.3.1: CalcArea/IsPointInPolygon/IsPointOnBorder all
// treat it as a closed loop by wrapping from the last point back to the first), but
// TGeometryProjectedBase.Create(AClosed=True, ...) still trims a caller-supplied literal repeat
// of the first point, exactly like GeometryLonLatContour (geometry_lonlat.h) does.
class GeometryProjectedContour final : public IGeometryProjectedContour {
public:
    GeometryProjectedContour(const DoubleRect& bounds, ProjectedPointsPtr points) noexcept;

    [[nodiscard]] DoubleRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] std::span<const DoublePoint> points() const noexcept override {
        return {points_->data(), count_};
    }
    [[nodiscard]] bool isPointInPolygon(DoublePoint p) const override;
    [[nodiscard]] bool isPointOnBorder(DoublePoint p, double dist) const override;
    [[nodiscard]] RectWithPolygonIntersection checkRectIntersection(const DoubleRect& r) const override;
    [[nodiscard]] bool isRectIntersectPolygon(const DoubleRect& r) const override;
    [[nodiscard]] bool isRectIntersectBorder(const DoubleRect& r) const override;
    [[nodiscard]] double calcArea() const override;

private:
    DoubleRect bounds_;
    ProjectedPointsPtr points_;
    std::size_t count_;
};

// Ports both TGeometryProjectedPolygon (no holes, self-as-contour) and
// TGeometryProjectedPolygonWithHoles: one class with an explicit outerBorder_ member covers both
// -- an empty holes_ is exactly the "no holes" case (every method below degenerates to a direct
// call on outer_ when holes_ is empty), same simplification as GeometryLonLatSinglePolygon.
class GeometryProjectedSinglePolygon final : public IGeometryProjectedSinglePolygon {
public:
    GeometryProjectedSinglePolygon(const DoubleRect& bounds, GeometryProjectedContourPtr outer,
                                   std::vector<GeometryProjectedContourPtr> holes) noexcept;

    [[nodiscard]] DoubleRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] const GeometryProjectedContourPtr& outerBorder() const override { return outer_; }
    [[nodiscard]] std::size_t holeCount() const noexcept override { return holes_.size(); }
    [[nodiscard]] const GeometryProjectedContourPtr& holeBorder(std::size_t i) const override {
        return holes_[i];
    }
    [[nodiscard]] bool isPointInPolygon(DoublePoint p) const override;
    [[nodiscard]] bool isPointOnBorder(DoublePoint p, double dist) const override;
    [[nodiscard]] RectWithPolygonIntersection checkRectIntersection(const DoubleRect& r) const override;
    [[nodiscard]] bool isRectIntersectPolygon(const DoubleRect& r) const override;
    [[nodiscard]] bool isRectIntersectBorder(const DoubleRect& r) const override;
    [[nodiscard]] double calcArea() const override;

private:
    DoubleRect bounds_;
    GeometryProjectedContourPtr outer_;
    std::vector<GeometryProjectedContourPtr> holes_;
};

class GeometryProjectedMultiPolygon final : public IGeometryProjectedMultiPolygon {
public:
    GeometryProjectedMultiPolygon(const DoubleRect& bounds,
                                  std::vector<GeometryProjectedSinglePolygonPtr> items) noexcept;

    [[nodiscard]] DoubleRect bounds() const noexcept override { return bounds_; }
    [[nodiscard]] std::size_t count() const noexcept override { return items_.size(); }
    [[nodiscard]] const GeometryProjectedSinglePolygonPtr& item(std::size_t i) const override {
        return items_[i];
    }
    [[nodiscard]] bool isPointInPolygon(DoublePoint p) const override;
    [[nodiscard]] bool isPointOnBorder(DoublePoint p, double dist) const override;
    [[nodiscard]] RectWithPolygonIntersection checkRectIntersection(const DoubleRect& r) const override;
    [[nodiscard]] bool isRectIntersectPolygon(const DoubleRect& r) const override;
    [[nodiscard]] bool isRectIntersectBorder(const DoubleRect& r) const override;
    [[nodiscard]] double calcArea() const override;

private:
    DoubleRect bounds_;
    std::vector<GeometryProjectedSinglePolygonPtr> items_;
};

} // namespace cg::geo
