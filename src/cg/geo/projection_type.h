// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ProjectionType.pas, u_ProjectionTypeBase.pas, u_ProjectionTypeGeographic.pas,
// u_ProjectionTypeMercatorOnSphere.pas, u_ProjectionTypeMercatorOnEllipsoid.pas,
// c_CoordConverter.pas
//
// lon/lat <-> "relative" (the unit square, (0,0) = north-west, (1,1) = south-east) for the three
// native formulas the app ships built in. Everything else in cg::geo is built on relative space.
// Immutable snapshot, no locks (docs/specs/geo.md §8.1).

#include "cg/core/hash.h"
#include "cg/geo/datum.h"
#include "cg/geo/geo_types.h"

#include <algorithm>
#include <memory>
#include <numbers>
#include <utility>

namespace cg::geo {

// --- EPSG constants (c_CoordConverter.pas:27-36, verbatim) --------------------
inline constexpr int kDatumEpsgGoogleSphere = 6055;
inline constexpr int kDatumEpsgWgs84 = 6326;
inline constexpr int kDatumEpsgEsriSphere = 53004; // not named in c_CoordConverter, used raw

inline constexpr int kProjEpsgGoogle = 3857;
inline constexpr int kProjEpsgGoogleOld = 3785; // deprecated alias, wrong definition
inline constexpr int kProjEpsgYandex = 3395;
inline constexpr int kProjEpsgGeographic = 4326;
inline constexpr int kProjEpsgEsriSphere = 53004;

inline constexpr int kTileSplitQuadrate256x256 = 1;

class IProjectionType {
public:
    virtual ~IProjectionType() = default;

    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
    [[nodiscard]] virtual const DatumPtr& datum() const noexcept = 0;
    [[nodiscard]] virtual int projectionEpsg() const noexcept = 0;

    // Forward / inverse. Both clamp their input first (see "Clamping" below).
    [[nodiscard]] virtual DoublePoint lonLat2Relative(LonLat p) const = 0;
    [[nodiscard]] virtual LonLat relative2LonLat(DoublePoint p) const = 0;
    [[nodiscard]] virtual DoubleRect lonLatRect2RelativeRect(const LonLatRect& r) const = 0;
    [[nodiscard]] virtual LonLatRect relativeRect2LonLatRect(const DoubleRect& r) const = 0;

    // "Metric" = the projection's own metres, as used by the map-calibration writers and the
    // scale bar. u_ProjectionTypeBase.pas:279-291,316-325.
    [[nodiscard]] virtual DoublePoint lonLat2Metr(LonLat p) const = 0;
    [[nodiscard]] virtual LonLat metr2LonLat(DoublePoint p) const = 0;

    // Clamp in place (legacy: procedure Validate*(var ...)).
    virtual void validateRelativePos(DoublePoint& p) const = 0;
    virtual void validateRelativeRect(DoubleRect& r) const = 0;
    virtual void validateLonLatPos(LonLat& p) const = 0;
    virtual void validateLonLatRect(LonLatRect& r) const = 0;

    // Test without clamping.
    [[nodiscard]] virtual bool checkRelativePos(DoublePoint p) const = 0;
    [[nodiscard]] virtual bool checkRelativeRect(const DoubleRect& r) const = 0;
    [[nodiscard]] virtual bool checkLonLatPos(LonLat p) const = 0;
    [[nodiscard]] virtual bool checkLonLatRect(const LonLatRect& r) const = 0;

    [[nodiscard]] virtual bool isSame(const IProjectionType& other) const = 0;
};
using ProjectionTypePtr = std::shared_ptr<const IProjectionType>;

// Shared clamp/check/isSame logic (u_ProjectionTypeBase.pas), an implementation detail behind the
// three concrete formulas below -- not part of the module's public surface, but declared here
// (rather than hidden in the .cpp) because the concrete classes need a complete base to derive
// from. `validLonLatRect` is `relativeRect2LonLatRect({0,0,1,1})`, computed by the *concrete*
// class's own formula before the base is constructed (u_ProjectionTypeBase.pas:106-110's
// AfterConstruction hook cannot be reproduced as a base-class virtual call in C++: the derived
// vtable is not yet live during base construction).
class IProjectionTypeBase : public IProjectionType {
public:
    [[nodiscard]] core::HashValue hash() const noexcept final { return hash_; }
    [[nodiscard]] const DatumPtr& datum() const noexcept final { return datum_; }
    [[nodiscard]] int projectionEpsg() const noexcept final { return projectionEpsg_; }

    [[nodiscard]] DoublePoint lonLat2Relative(LonLat p) const final {
        validateLonLatPosInternal(p);
        return lonLat2RelativeInternal(p);
    }
    [[nodiscard]] LonLat relative2LonLat(DoublePoint p) const final {
        validateRelativePosInternal(p);
        return relative2LonLatInternal(p);
    }
    [[nodiscard]] DoubleRect lonLatRect2RelativeRect(const LonLatRect& r) const final {
        LonLatRect vr = r;
        validateLonLatRectInternal(vr);
        const DoublePoint tl = lonLat2RelativeInternal(vr.topLeft());
        const DoublePoint br = lonLat2RelativeInternal(vr.bottomRight());
        return {tl.x(), tl.y(), br.x(), br.y()};
    }
    [[nodiscard]] LonLatRect relativeRect2LonLatRect(const DoubleRect& r) const final {
        DoubleRect vr = r;
        validateRelativeRectInternal(vr);
        const LonLat tl = relative2LonLatInternal(vr.topLeft());
        const LonLat br = relative2LonLatInternal(vr.bottomRight());
        return {tl.lon, tl.lat, br.lon, br.lat};
    }

    [[nodiscard]] DoublePoint lonLat2Metr(LonLat p) const final {
        // u_ProjectionTypeBase.pas:279-291
        validateLonLatPosInternal(p);
        const DoublePoint relative = lonLat2RelativeInternal(p);
        const double mul = 2.0 * std::numbers::pi * datum_->radiusA();
        return {(relative.x() - 0.5) * mul, (0.5 - relative.y()) * mul};
    }
    [[nodiscard]] LonLat metr2LonLat(DoublePoint p) const final {
        // u_ProjectionTypeBase.pas:316-325 -- the legacy sign defect, reproduced (§13 Q-3):
        // `x * mul - 0.5` is NOT the inverse of `(relative.x - 0.5) * mul` above (that would be
        // `x * mul + 0.5`). Metr2LonLatIsNotTheInverse pins this exactly.
        const double mul = 1.0 / (2.0 * std::numbers::pi * datum_->radiusA());
        const DoublePoint relative{p.x() * mul - 0.5, 0.5 - p.y() * mul};
        return relative2LonLatInternal(relative);
    }

    void validateRelativePos(DoublePoint& p) const final {
        p.setX(std::clamp(p.x(), 0.0, 1.0));
        p.setY(std::clamp(p.y(), 0.0, 1.0));
    }
    void validateRelativeRect(DoubleRect& r) const final {
        r.left = std::clamp(r.left, 0.0, 1.0);
        r.top = std::clamp(r.top, 0.0, 1.0);
        r.right = std::clamp(r.right, 0.0, 1.0);
        r.bottom = std::clamp(r.bottom, 0.0, 1.0);
    }
    // NOT final: u_ProjectionTypeGeographic.pas:80-89 overrides these two with a hardcoded
    // +/-180 lon / +/-90 lat clamp (CMaxLatitude), while leaving check*() and the *internal*
    // clamp used by lonLat2Relative/lonLatRect2RelativeRect/lonLat2Metr (below) on the generic
    // validLonLatRect_ derived from the formula itself. That split is a real legacy quirk
    // (validateLonLatPos clamps to +/-90, but lonLat2Relative's own internal clamp does not),
    // reproduced here exactly rather than "fixed" -- see GeographicClampsToNinety in the tests.
    void validateLonLatPos(LonLat& p) const override {
        p.lon = std::clamp(p.lon, validLonLatRect_.left, validLonLatRect_.right);
        p.lat = std::clamp(p.lat, validLonLatRect_.bottom, validLonLatRect_.top);
    }
    void validateLonLatRect(LonLatRect& r) const override {
        r.left = std::clamp(r.left, validLonLatRect_.left, validLonLatRect_.right);
        r.bottom = std::clamp(r.bottom, validLonLatRect_.bottom, validLonLatRect_.top);
        r.right = std::clamp(r.right, validLonLatRect_.left, validLonLatRect_.right);
        r.top = std::clamp(r.top, validLonLatRect_.bottom, validLonLatRect_.top);
    }

    [[nodiscard]] bool checkRelativePos(DoublePoint p) const final {
        return p.x() >= 0.0 && p.x() <= 1.0 && p.y() >= 0.0 && p.y() <= 1.0;
    }
    [[nodiscard]] bool checkRelativeRect(const DoubleRect& r) const final {
        return r.left >= 0.0 && r.left <= 1.0 && r.top >= 0.0 && r.top <= 1.0 && r.right >= 0.0 &&
               r.right <= 1.0 && r.bottom >= 0.0 && r.bottom <= 1.0;
    }
    [[nodiscard]] bool checkLonLatPos(LonLat p) const final {
        return p.lon >= validLonLatRect_.left && p.lon <= validLonLatRect_.right &&
               p.lat >= validLonLatRect_.bottom && p.lat <= validLonLatRect_.top;
    }
    [[nodiscard]] bool checkLonLatRect(const LonLatRect& r) const final {
        return r.left >= validLonLatRect_.left && r.left <= validLonLatRect_.right &&
               r.bottom >= validLonLatRect_.bottom && r.bottom <= validLonLatRect_.top &&
               r.right >= validLonLatRect_.left && r.right <= validLonLatRect_.right &&
               r.top >= validLonLatRect_.bottom && r.top <= validLonLatRect_.top;
    }

    [[nodiscard]] bool isSame(const IProjectionType& other) const final {
        // u_ProjectionTypeBase.pas:260-277
        if (this == &other)
            return true;
        if (hash_ != core::kNoHash && other.hash() != core::kNoHash && hash_ != other.hash())
            return false;
        if (!datum_->isSameDatum(*other.datum()))
            return false;
        return projectionEpsg_ == other.projectionEpsg();
    }

protected:
    IProjectionTypeBase(core::HashValue hash, DatumPtr datum, int projectionEpsg, LonLatRect validLonLatRect)
        : hash_(hash), datum_(std::move(datum)), projectionEpsg_(projectionEpsg),
          validLonLatRect_(validLonLatRect) {}

    // The two formulas every concrete class supplies. Called on ALREADY-clamped input.
    [[nodiscard]] virtual DoublePoint lonLat2RelativeInternal(LonLat p) const noexcept = 0;
    [[nodiscard]] virtual LonLat relative2LonLatInternal(DoublePoint p) const noexcept = 0;

private:
    // "Internal" clamp (u_ProjectionTypeBase.pas:368-388): a private, NON-virtual method on the
    // base, always clamping against validLonLatRect_/[0,1] directly. It does NOT go through the
    // (overridable) public Validate* above -- that is precisely why GeographicProjectionType's
    // override of the public validateLonLatPos/Rect has no effect on lonLat2Relative,
    // lonLatRect2RelativeRect or lonLat2Metr/metr2LonLat (they all call these Internal helpers).
    // Legacy asserts here in a debug build and clamps silently in release; the port never
    // Q_ASSERT()s (docs/specs/geo.md §2.5.3) -- real maps do reach these paths through rounding.
    void validateLonLatPosInternal(LonLat& p) const noexcept {
        p.lon = std::clamp(p.lon, validLonLatRect_.left, validLonLatRect_.right);
        p.lat = std::clamp(p.lat, validLonLatRect_.bottom, validLonLatRect_.top);
    }
    void validateRelativePosInternal(DoublePoint& p) const noexcept {
        p.setX(std::clamp(p.x(), 0.0, 1.0));
        p.setY(std::clamp(p.y(), 0.0, 1.0));
    }
    void validateLonLatRectInternal(LonLatRect& r) const noexcept {
        r.left = std::clamp(r.left, validLonLatRect_.left, validLonLatRect_.right);
        r.bottom = std::clamp(r.bottom, validLonLatRect_.bottom, validLonLatRect_.top);
        r.right = std::clamp(r.right, validLonLatRect_.left, validLonLatRect_.right);
        r.top = std::clamp(r.top, validLonLatRect_.bottom, validLonLatRect_.top);
    }
    void validateRelativeRectInternal(DoubleRect& r) const noexcept {
        r.left = std::clamp(r.left, 0.0, 1.0);
        r.top = std::clamp(r.top, 0.0, 1.0);
        r.right = std::clamp(r.right, 0.0, 1.0);
        r.bottom = std::clamp(r.bottom, 0.0, 1.0);
    }

    core::HashValue hash_;
    DatumPtr datum_;
    int projectionEpsg_;
    LonLatRect validLonLatRect_;
};

// --- 2.2.1 GeographicProjectionType -- EPSG 4326 -------------------------------
// u_ProjectionTypeGeographic.pas:39-54,56-71. CMaxLatitude = 90 (line 38).
class GeographicProjectionType final : public IProjectionTypeBase {
public:
    GeographicProjectionType(core::HashValue hash, DatumPtr datum, int projectionEpsg);

    // u_ProjectionTypeGeographic.pas:80-89: CMaxLatitude = 90, independent of validLonLatRect_.
    void validateLonLatPos(LonLat& p) const override;
    void validateLonLatRect(LonLatRect& r) const override;

protected:
    [[nodiscard]] DoublePoint lonLat2RelativeInternal(LonLat p) const noexcept override;
    [[nodiscard]] LonLat relative2LonLatInternal(DoublePoint p) const noexcept override;
};

// --- 2.2.2 MercatorSphereProjectionType -- EPSG 3857 (alias 3785) / ESRI 53004 -
// u_ProjectionTypeMercatorOnSphere.pas. Serves EPSG 3857 (datum a=b=6378137) and 53004
// (datum a=b=6371000); only the datum and the reported EPSG differ.
class MercatorSphereProjectionType final : public IProjectionTypeBase {
public:
    MercatorSphereProjectionType(core::HashValue hash, DatumPtr datum, int projectionEpsg);

protected:
    [[nodiscard]] DoublePoint lonLat2RelativeInternal(LonLat p) const noexcept override;
    [[nodiscard]] LonLat relative2LonLatInternal(DoublePoint p) const noexcept override;
};

// --- 2.2.3 MercatorEllipsoidProjectionType -- EPSG 3395 ------------------------
// u_ProjectionTypeMercatorOnEllipsoid.pas. Eccentricity e = sqrt(a*a - b*b) / a, from the datum.
class MercatorEllipsoidProjectionType final : public IProjectionTypeBase {
public:
    MercatorEllipsoidProjectionType(core::HashValue hash, DatumPtr datum, int projectionEpsg);

protected:
    [[nodiscard]] DoublePoint lonLat2RelativeInternal(LonLat p) const noexcept override;
    [[nodiscard]] LonLat relative2LonLatInternal(DoublePoint p) const noexcept override;

private:
    double eccentricity_;
};

} // namespace cg::geo
