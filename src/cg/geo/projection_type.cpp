// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/geo/projection_type.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cg::geo {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr long double kPiL = std::numbers::pi_v<long double>;

// --- Geographic (u_ProjectionTypeGeographic.pas:47-61) ------------------------
[[nodiscard]] DoublePoint geographicForward(LonLat p) noexcept {
    return {0.5 + p.lon / 360.0, 0.5 - p.lat / 360.0};
}
[[nodiscard]] LonLat geographicInverse(DoublePoint p) noexcept {
    return {(p.x() - 0.5) * 360.0, -(p.y() - 0.5) * 360.0};
}

// --- MercatorOnSphere (u_ProjectionTypeMercatorOnSphere.pas:70-101) -----------
[[nodiscard]] DoublePoint sphereForward(LonLat p) noexcept {
    const double rx = 0.5 + p.lon / 360.0;
    const double z = std::sin(p.lat * kPi / 180.0);
    double ry = 0.5 - 0.5 * std::log((1.0 + z) / (1.0 - z)) / (2.0 * kPi);
    // The two 1e-12 snaps are load-bearing (docs/specs/geo.md §2.2.2, ADR-0006 decision 1):
    // written as literals, in this order, after the formula.
    if (std::abs(ry - 0.0) <= 1e-12)
        ry = 0.0;
    if (std::abs(ry - 1.0) <= 1e-12)
        ry = 1.0;
    return {rx, ry};
}
[[nodiscard]] LonLat sphereInverse(DoublePoint p) noexcept {
    const double lon = (p.x() - 0.5) * 360.0;
    const double t = -(p.y() - 0.5) * (2.0 * kPi);
    const double lat = (2.0 * std::atan(std::exp(t)) - kPi / 2.0) * 180.0 / kPi;
    return {lon, lat};
}

// --- MercatorOnEllipsoid (u_ProjectionTypeMercatorOnEllipsoid.pas:66-115) -----
[[nodiscard]] DoublePoint ellipsoidForward(LonLat p, double e) noexcept {
    // u_ProjectionTypeMercatorOnEllipsoid.pas:71-79 declares `z: Extended` (80-bit) even though the
    // function returns TDoublePoint (64-bit fields). ADR-0006 D-11 permits `long double` only in
    // the ellipsoidal INVERSE (deviation 6), not here, and the T-0016 review (Q-B) measured why
    // that is fine: swept over the D-11 grid (lat -85..85 step 0.01), a `double`-only forward and
    // the `long double` forward produce round trips (through the same, unchanged, `long double`
    // inverse) that agree to under 1e-9 degrees -- see
    // EllipsoidForwardDoubleAgreesWithLongDoubleWithinD11Bound in projection_type_test.cpp. Plain
    // `double` is correct and sufficient.
    const double rx = 0.5 + p.lon / 360.0;
    const double z = std::sin(p.lat * kPi / 180.0);
    const double ry = 0.5 - (std::atanh(z) - e * std::atanh(e * z)) / (2.0 * kPi);
    return {rx, ry};
}
[[nodiscard]] LonLat ellipsoidInverse(DoublePoint p, double e) noexcept {
    // Precision (docs/specs/geo.md §2.2.3, ADR-0006 decision 1 / deviation 6): legacy computes this
    // in Delphi Extended (80-bit); ADR-0006 D-11 permits `long double` here, in the ellipsoidal
    // INVERSE only, so x86-64 `long double` (the same 80-bit format as Delphi Extended) keeps the
    // iteration's convergence behavior close to the Windows build's. This is a fidelity choice, not
    // a correctness requirement: the T-0016 review (Q-B) found no measurable difference in the
    // round-trip's worst-case error between `double` and `long double` here either (both
    // ~2.8370e-08 degrees over the D-11 grid).
    using Extended = long double;
    const double lon = (p.x() - 0.5) * 360.0;
    const bool southern = p.y() > 0.5;
    const Extended eLD = static_cast<Extended>(e);
    Extended yy = static_cast<Extended>(southern ? p.y() - 0.5 : 0.5 - p.y()) * (2.0L * kPiL);
    Extended zu = 2.0L * std::atan(std::exp(yy)) - kPiL / 2.0L; // first guess
    const Extended eY = std::exp(2.0L * yy);

    // The fallback the spec and the legacy source agree on: the *unsigned*, pre-loop first guess,
    // never sign-corrected -- reproduced exactly (u_ProjectionTypeMercatorOnEllipsoid.pas:98-113).
    Extended lat = zu * 180.0L / kPiL;

    for (;;) {
        const Extended zum1 = zu;
        const Extended s = std::sin(zum1);
        // std::pow, not exp(e*log(base)): they differ in the last ULP inside this loop
        // (docs/specs/geo.md §2.2.3 rule 3).
        const Extended next =
            std::asin(1.0L - (1.0L + s) * std::pow((1.0L - eLD * s) / (1.0L + eLD * s), eLD) / eY);
        if (!std::isfinite(static_cast<double>(next))) {
            break; // keep the pre-loop `lat`, un-sign-corrected -- the legacy NaN fallback.
        }
        zu = next;
        // The stop is 1e-9 on the RADIAN value, strictly less-than (§2.2.3 rule 1).
        if (std::abs(zum1 - zu) < 1e-9L) {
            lat = (southern ? -zu : zu) * 180.0L / kPiL;
            break;
        }
    }
    return {lon, static_cast<double>(lat)};
}

} // namespace

GeographicProjectionType::GeographicProjectionType(core::HashValue hash, DatumPtr datum, int projectionEpsg)
    : IProjectionTypeBase(hash, std::move(datum), projectionEpsg,
                          {geographicInverse({0.0, 0.0}).lon, geographicInverse({0.0, 0.0}).lat,
                           geographicInverse({1.0, 1.0}).lon, geographicInverse({1.0, 1.0}).lat}) {}

DoublePoint GeographicProjectionType::lonLat2RelativeInternal(LonLat p) const noexcept {
    return geographicForward(p);
}
LonLat GeographicProjectionType::relative2LonLatInternal(DoublePoint p) const noexcept {
    return geographicInverse(p);
}

namespace {
constexpr double kMaxLatitude = 90.0; // u_ProjectionTypeGeographic.pas:43
[[nodiscard]] double clampGeographicLon(double lon) noexcept {
    return std::clamp(lon, -180.0, 180.0);
}
[[nodiscard]] double clampGeographicLat(double lat) noexcept {
    return std::clamp(lat, -kMaxLatitude, kMaxLatitude);
}
} // namespace

void GeographicProjectionType::validateLonLatPos(LonLat& p) const {
    p.lon = clampGeographicLon(p.lon);
    p.lat = clampGeographicLat(p.lat);
}
void GeographicProjectionType::validateLonLatRect(LonLatRect& r) const {
    r.left = clampGeographicLon(r.left);
    r.top = clampGeographicLat(r.top);
    r.right = clampGeographicLon(r.right);
    r.bottom = clampGeographicLat(r.bottom);
}

MercatorSphereProjectionType::MercatorSphereProjectionType(core::HashValue hash, DatumPtr datum,
                                                           int projectionEpsg)
    : IProjectionTypeBase(hash, std::move(datum), projectionEpsg,
                          {sphereInverse({0.0, 0.0}).lon, sphereInverse({0.0, 0.0}).lat,
                           sphereInverse({1.0, 1.0}).lon, sphereInverse({1.0, 1.0}).lat}) {
    // u_ProjectionTypeMercatorOnSphere.pas:64: Assert(Abs(a - b) < 1) on its datum.
    Q_ASSERT(std::abs(this->datum()->radiusA() - this->datum()->radiusB()) < 1.0);
}

DoublePoint MercatorSphereProjectionType::lonLat2RelativeInternal(LonLat p) const noexcept {
    return sphereForward(p);
}
LonLat MercatorSphereProjectionType::relative2LonLatInternal(DoublePoint p) const noexcept {
    return sphereInverse(p);
}

namespace {
[[nodiscard]] double eccentricityOf(const IDatum& datum) noexcept {
    const double a = datum.radiusA();
    const double b = datum.radiusB();
    return std::sqrt(a * a - b * b) / a;
}
} // namespace

MercatorEllipsoidProjectionType::MercatorEllipsoidProjectionType(core::HashValue hash, DatumPtr datum,
                                                                 int projectionEpsg)
    : IProjectionTypeBase(hash, datum, projectionEpsg,
                          [&] {
                              const double e = eccentricityOf(*datum);
                              const LonLat tl = ellipsoidInverse({0.0, 0.0}, e);
                              const LonLat br = ellipsoidInverse({1.0, 1.0}, e);
                              return LonLatRect{tl.lon, tl.lat, br.lon, br.lat};
                          }()),
      eccentricity_(eccentricityOf(*this->datum())) {}

DoublePoint MercatorEllipsoidProjectionType::lonLat2RelativeInternal(LonLat p) const noexcept {
    return ellipsoidForward(p, eccentricity_);
}
LonLat MercatorEllipsoidProjectionType::relative2LonLatInternal(DoublePoint p) const noexcept {
    return ellipsoidInverse(p, eccentricity_);
}

} // namespace cg::geo
