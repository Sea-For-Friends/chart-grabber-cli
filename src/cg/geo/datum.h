// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_Datum.pas, u_Datum.pas, i_DatumFactory.pas, u_DatumFactory.pas,
// Src/GeoCalc/i_DistanceCalculator.pas, u_DistanceCalculatorByGeographicLib.pas,
// i_PolygonAreaCalculator.pas, u_PolygonAreaCalculator.pas, Includes/GeographicLib.pas
//
// An ellipsoid (or sphere) plus the geodesy built on it: distances, bearings, polygon area and
// perimeter. Every geo/render/region computation that needs "how far" or "how big on the ground"
// goes through an IDatum. Immutable snapshot, no locks (docs/specs/geo.md §8.1).
//
// FEAT-GEO-003 (WGS84 + Google-sphere datums) on DatumFactory.

#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/hash_seed.h"

#include <QtGlobal>

#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace cg::geo {

class IDatum {
public:
    virtual ~IDatum() = default;

    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual int epsg() const noexcept = 0; // 0 == ad-hoc, radius-defined
    [[nodiscard]] virtual double radiusA() const noexcept = 0;
    [[nodiscard]] virtual double radiusB() const noexcept = 0;
    [[nodiscard]] virtual bool isSameDatum(const IDatum& other) const noexcept = 0;

    // --- geodesy (all in metres / degrees) -----------------------------------
    [[nodiscard]] virtual double calcDist(LonLat from, LonLat to) const = 0;
    [[nodiscard]] virtual double calcDist(LonLat from, LonLat to, double& initialBearing,
                                          double& finalBearing) const = 0;
    [[nodiscard]] virtual double calcDist(std::span<const LonLat> points,
                                          std::stop_token cancel = {}) const = 0;

    [[nodiscard]] virtual LonLat calcFinishPosition(LonLat start, double initialBearing,
                                                    double distance) const = 0;
    [[nodiscard]] virtual LonLat calcMiddlePoint(LonLat a, LonLat b) const = 0;

    [[nodiscard]] virtual double calcPolygonArea(std::span<const LonLat> points,
                                                 std::stop_token cancel = {}) const = 0;
    [[nodiscard]] virtual double calcPolygonPerimeter(std::span<const LonLat> points,
                                                      std::stop_token cancel = {}) const = 0;

    // Great-circle interpolation, pointCount intermediate samples (u_Datum.pas GetLinePoints).
    [[nodiscard]] virtual std::vector<LonLat> linePoints(LonLat from, LonLat to, int pointCount) const = 0;
};
using DatumPtr = std::shared_ptr<const IDatum>;

class DatumFactory {
public:
    explicit DatumFactory(HashSeed hash = {});

    [[nodiscard]] DatumPtr byCode(int datumEpsg) const; // nullptr if unknown
    [[nodiscard]] DatumPtr byRadius(double radiusA, double radiusB) const;

private:
    HashSeed hash_;
    DatumPtr googleSphere_; // EPSG 6055, a = b = 6378137.0
    DatumPtr wgs84_;        // EPSG 6326, a = 6378137.0, b = 6356752.3142
    DatumPtr esriSphere_;   // EPSG 53004, a = b = 6371000
};

} // namespace cg::geo
