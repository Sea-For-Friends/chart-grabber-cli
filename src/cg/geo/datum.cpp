// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/datum.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <geodesic/geodesic.h>
#include <limits>
#include <numbers>

namespace cg::geo {
namespace {

constexpr double kDeg2Rad = std::numbers::pi / 180.0;
constexpr double kAreaCancelThreshold = 1e-6; // u_PolygonAreaCalculator.pas cThresh
constexpr int kCancelPollInterval = 1024;     // docs/specs/geo.md §8.1

// Ports u_PolygonAreaCalculator.pas:100-277 (the GRASS/QGIS series), transcribed exactly. Not
// GeographicLib's PolygonArea and not the shoelace formula for the ellipsoid branch (§2.3.2).
class GrassAreaSeries {
public:
    GrassAreaSeries(double radiusA, double radiusB) noexcept : isEllipsoid_(radiusA != 0.0) {
        if (!isEllipsoid_) {
            return;
        }
        const double a2 = radiusA * radiusA;
        const double e2 = 1.0 - (radiusB * radiusB) / a2;
        const double e4 = e2 * e2;
        const double e6 = e4 * e2;

        twoPi_ = 2.0 * std::numbers::pi;
        ae_ = a2 * (1.0 - e2);

        qa_ = (2.0 / 3.0) * e2;
        qb_ = (3.0 / 5.0) * e4;
        qc_ = (4.0 / 7.0) * e6;

        qbarA_ = -1.0 - (2.0 / 3.0) * e2 - (3.0 / 5.0) * e4 - (4.0 / 7.0) * e6;
        qbarB_ = (2.0 / 9.0) * e2 + (2.0 / 5.0) * e4 + (4.0 / 7.0) * e6;
        qbarC_ = -(3.0 / 25.0) * e4 - (12.0 / 35.0) * e6;
        qbarD_ = (4.0 / 49.0) * e6;

        qp_ = q(std::numbers::pi / 2.0);
        e_ = std::abs(4.0 * std::numbers::pi * qp_ * ae_);
    }

    [[nodiscard]] double area(std::span<const LonLat> points, const std::stop_token& cancel) const {
        const auto n = points.size();
        if (n == 0) {
            return 0.0;
        }
        if (!isEllipsoid_) {
            return flatArea(points, cancel);
        }

        double x2 = kDeg2Rad * points[n - 1].lon;
        double y2 = kDeg2Rad * points[n - 1].lat;
        double qbar2 = qbar(y2);
        double area = 0.0;
        int count = 0;

        for (std::size_t i = 0; i < n; ++i) {
            double x1 = x2;
            double y1 = y2;
            double qbar1 = qbar2;

            x2 = kDeg2Rad * points[i].lon;
            y2 = kDeg2Rad * points[i].lat;
            qbar2 = qbar(y2);

            if (x1 > x2) {
                while (x1 - x2 > std::numbers::pi)
                    x2 += twoPi_;
            } else if (x2 > x1) {
                while (x2 - x1 > std::numbers::pi)
                    x1 += twoPi_;
            }

            const double dx = x2 - x1;
            const double dy = y2 - y1;
            if (std::abs(dy) > kAreaCancelThreshold) {
                area += dx * (qp_ - (qbar2 - qbar1) / dy);
            } else {
                area += dx * (qp_ - q((y1 + y2) / 2.0));
            }

            ++count;
            if (count % kCancelPollInterval == 0 && cancel.stop_requested()) {
                return std::numeric_limits<double>::quiet_NaN();
            }
        }

        area = std::abs(area * ae_);
        if (area > e_) {
            area = e_;
        }
        if (area > e_ / 2.0) {
            area = e_ - area;
        }
        return area;
    }

private:
    [[nodiscard]] double q(double x) const noexcept {
        const double sinx = std::sin(x);
        const double sinx2 = sinx * sinx;
        return sinx * (1.0 + sinx2 * (qa_ + sinx2 * (qb_ + sinx2 * qc_)));
    }

    [[nodiscard]] double qbar(double x) const noexcept {
        const double cosx = std::cos(x);
        const double cosx2 = cosx * cosx;
        return cosx * (qbarA_ + cosx2 * (qbarB_ + cosx2 * (qbarC_ + cosx2 * qbarD_)));
    }

    // u_PolygonAreaCalculator.pas:247-277, the "not ellipsoid" (radiusA == 0) branch: plain
    // shoelace with wrap-around indexing, |.| at the end.
    [[nodiscard]] static double flatArea(std::span<const LonLat> points,
                                         const std::stop_token& cancel) noexcept {
        const auto n = points.size();
        double area = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const LonLat& p0 = points[i];
            const LonLat& p1 = points[(i + 1) % n];
            area += p0.lon * p1.lat - p1.lon * p0.lat;
            ++count;
            if (count % kCancelPollInterval == 0 && cancel.stop_requested()) {
                return std::numeric_limits<double>::quiet_NaN();
            }
        }
        return std::abs(area / 2.0);
    }

    bool isEllipsoid_ = false;
    double qa_ = 0, qb_ = 0, qc_ = 0;
    double qbarA_ = 0, qbarB_ = 0, qbarC_ = 0, qbarD_ = 0;
    double twoPi_ = 0, ae_ = 0, qp_ = 0, e_ = 0;
};

class Datum final : public IDatum {
public:
    Datum(core::HashValue hash, int epsg, double radiusA, double radiusB)
        : hash_(hash), epsg_(epsg), radiusA_(radiusA), radiusB_(radiusB), area_(radiusA, radiusB) {
        // u_DistanceCalculatorByGeographicLib.pas:78: flattening from the two radii, not a table.
        geod_init(&geod_, radiusA_, radiusA_ != 0.0 ? (radiusA_ - radiusB_) / radiusA_ : 0.0);
    }

    [[nodiscard]] core::HashValue hash() const noexcept override { return hash_; }
    [[nodiscard]] int epsg() const noexcept override { return epsg_; }
    [[nodiscard]] double radiusA() const noexcept override { return radiusA_; }
    [[nodiscard]] double radiusB() const noexcept override { return radiusB_; }

    [[nodiscard]] bool isSameDatum(const IDatum& other) const noexcept override {
        // u_Datum.pas:179-194
        if (this == &other)
            return true;
        if (hash_ != core::kNoHash && other.hash() != core::kNoHash && hash_ != other.hash())
            return false;
        if (other.epsg() != 0 || epsg_ != 0) {
            return epsg_ == other.epsg();
        }
        return radiusA_ == other.radiusA() && radiusB_ == other.radiusB();
    }

    [[nodiscard]] double calcDist(LonLat from, LonLat to) const override {
        double s12 = 0.0;
        geod_inverse(&geod_, from.lat, from.lon, to.lat, to.lon, &s12, nullptr, nullptr);
        return s12;
    }

    [[nodiscard]] double calcDist(LonLat from, LonLat to, double& initialBearing,
                                  double& finalBearing) const override {
        double s12 = 0.0;
        geod_inverse(&geod_, from.lat, from.lon, to.lat, to.lon, &s12, &initialBearing, &finalBearing);
        return s12;
    }

    [[nodiscard]] double calcDist(std::span<const LonLat> points, std::stop_token cancel) const override {
        // u_Datum.pas:245-270: open sum over consecutive points, no closing segment.
        double total = 0.0;
        int count = 0;
        for (std::size_t i = 1; i < points.size(); ++i) {
            double s12 = 0.0;
            geod_inverse(&geod_, points[i - 1].lat, points[i - 1].lon, points[i].lat, points[i].lon, &s12,
                         nullptr, nullptr);
            total += s12;
            ++count;
            if (count % kCancelPollInterval == 0 && cancel.stop_requested()) {
                return std::numeric_limits<double>::quiet_NaN();
            }
        }
        return total;
    }

    [[nodiscard]] LonLat calcFinishPosition(LonLat start, double initialBearing,
                                            double distance) const override {
        double lat2 = 0.0, lon2 = 0.0;
        geod_direct(&geod_, start.lat, start.lon, initialBearing, distance, &lat2, &lon2, nullptr);
        return {lon2, lat2};
    }

    [[nodiscard]] LonLat calcMiddlePoint(LonLat a, LonLat b) const override {
        // u_Datum.pas:289-319: full inverse for distance+bearing, then direct at half the distance.
        double s12 = 0.0, azi1 = 0.0, azi2 = 0.0;
        geod_inverse(&geod_, a.lat, a.lon, b.lat, b.lon, &s12, &azi1, &azi2);
        double lat2 = 0.0, lon2 = 0.0;
        geod_direct(&geod_, a.lat, a.lon, azi1, s12 / 2.0, &lat2, &lon2, nullptr);
        return {lon2, lat2};
    }

    [[nodiscard]] double calcPolygonArea(std::span<const LonLat> points,
                                         std::stop_token cancel) const override {
        return area_.area(points, cancel);
    }

    [[nodiscard]] double calcPolygonPerimeter(std::span<const LonLat> points,
                                              std::stop_token cancel) const override {
        // u_Datum.pas:206-217: open sum, plus the closing segment last->first, but ONLY when
        // there are more than two points (that guard is legacy's, reproduced exactly).
        double result = calcDist(points, cancel);
        if (!std::isnan(result) && points.size() > 2) {
            result += calcDist(points.back(), points.front());
        }
        return result;
    }

    [[nodiscard]] std::vector<LonLat> linePoints(LonLat from, LonLat to, int pointCount) const override {
        // u_Datum.pas:321-366. The SAME initial bearing (computed once, from -> to) is reused at
        // every step; only the *base point* advances (LinePointsReusesInitialBearing, §11.2).
        Q_ASSERT(pointCount >= 1);
        double distFull = 0.0, initialBearing = 0.0, finalBearing = 0.0;
        geod_inverse(&geod_, from.lat, from.lon, to.lat, to.lon, &distFull, &initialBearing, &finalBearing);
        const double distPart = distFull / (pointCount + 1);

        std::vector<LonLat> result;
        result.reserve(static_cast<std::size_t>(pointCount));
        LonLat current = from;
        for (int i = 0; i < pointCount; ++i) {
            double lat2 = 0.0, lon2 = 0.0;
            geod_direct(&geod_, current.lat, current.lon, initialBearing, distPart, &lat2, &lon2, nullptr);
            current = {lon2, lat2};
            result.push_back(current);
        }
        return result;
    }

private:
    core::HashValue hash_;
    int epsg_;
    double radiusA_;
    double radiusB_;
    geod_geodesic geod_{};
    GrassAreaSeries area_;
};

} // namespace

DatumFactory::DatumFactory(HashSeed hash)
    : hash_(hash),
      googleSphere_(std::make_shared<const Datum>(hash_(6378137.0, 6378137.0), 6055, 6378137.0, 6378137.0)),
      wgs84_(std::make_shared<const Datum>(hash_(6378137.0, 6356752.3142), 6326, 6378137.0, 6356752.3142)),
      esriSphere_(std::make_shared<const Datum>(hash_(6371000.0, 6371000.0), 53004, 6371000.0, 6371000.0)) {}

DatumPtr DatumFactory::byCode(int datumEpsg) const {
    switch (datumEpsg) {
    case 6055:
        return googleSphere_;
    case 6326:
        return wgs84_;
    case 53004:
        return esriSphere_;
    default:
        return nullptr;
    }
}

DatumPtr DatumFactory::byRadius(double radiusA, double radiusB) const {
    // u_DatumFactory.pas:103-124, including the asymmetry: the second test can override the
    // first, and there is deliberately no branch that resolves to 53004 (§2.3.3).
    int epsg = 0;
    if (std::abs(radiusA - 6378137.0) < 1.0 && std::abs(radiusB - 6378137.0) < 1.0) {
        epsg = 6055;
    }
    if (std::abs(radiusA - 6378137.0) < 1.0 && std::abs(radiusB - 6356752.0) < 1.0) {
        epsg = 6326;
    }
    if (epsg > 0) {
        return byCode(epsg);
    }
    return std::make_shared<const Datum>(hash_(radiusA, radiusB), 0, radiusA, radiusB);
}

} // namespace cg::geo
