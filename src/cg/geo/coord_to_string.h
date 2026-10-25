// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_CoordToStringConverter.pas, u_CoordToStringConverter.pas. docs/specs/geo.md:1969-2086
// (§6.2). FEAT-GEO-004 (WGS84 display), FEAT-GEO-005/006/007 (UTM/MGRS/SK-42/GSK-2011 -- see the
// note on lonLatConvertExt below).
//
// Non-WGS84 display (T-0019B2) goes through a CoordTransformer member, exactly as legacy's
// TCoordToStringConverter holds its own FTransformer (u_CoordToStringConverter.pas's uses clause).
// When a transform fails -- PROJ unavailable, or the point is out of range for its CRS -- this
// class falls back to legacy's own failure sentinel (spec §6.2.4: the literal "NaN" in Lon/Lat, an
// empty Zone).

#include "cg/geo/coord_representation.h"
#include "cg/geo/coord_transformer.h"
#include "cg/geo/geo_types.h"

#include <QFlags>
#include <QString>

#include <array>
#include <cstdint>

namespace cg::geo {

// i_CoordToStringConverter.pas:32-38. Underlying type is std::uint8_t (clang-tidy
// performance-enum-size); three bits are plenty for a QFlags enum.
enum class CoordToStringOption : std::uint8_t {
    CutZero = 1u << 0,
    IncludeZone = 1u << 1,
    LatitudeFirst = 1u << 2,
};
using CoordToStringOptions = QFlags<CoordToStringOption>;

// i_CoordToStringConverter.pas:40-44. Do not change the order -- it is the TCoordPartArray index.
enum class CoordPart : std::uint8_t { Zone = 0, Lon = 1, Lat = 2 };
using CoordPartArray = std::array<QString, 3>;

// Ports TCoordToStringConverter (u_CoordToStringConverter.pas). Not an interface: one
// implementation; the legacy ICoordToStringConverterChangeable wrapper is `ui`'s (spec §6.2).
class CoordToStringConverter {
public:
    explicit CoordToStringConverter(const CoordRepresentationSettings& settings);

    // GetCoordSysInfo, u_CoordToStringConverter.pas:284-308.
    [[nodiscard]] QString coordSysInfo(LonLat lonLat) const;

    // LonLatConvert, u_CoordToStringConverter.pas:310-335. The only method that applies
    // isLatitudeFirst (added to `options` before delegating) -- the two lonLatConvertExt overloads
    // do not, so a dialog filling separate Lat/Lon boxes gets untransposed parts. Preserve that
    // asymmetry, `fr_LonLat` depends on it (spec §6.2.4).
    [[nodiscard]] QString lonLatConvert(LonLat lonLat, CoordToStringOptions options = {}) const;

    // LonLatConvertExt, 1-arg overload: u_CoordToStringConverter.pas:337-343 (dispatches on the
    // settings' own coordSysType).
    [[nodiscard]] CoordPartArray lonLatConvertExt(LonLat lonLat, CoordToStringOptions options = {}) const;

    // LonLatConvertExt, 2-arg overload: u_CoordToStringConverter.pas:345-474. `options` has no
    // default (unlike legacy's `[]`, never relied on by any in-scope caller --
    // docs/handoffs/T-0019A1-review.md finding 3): a default here would make
    // `lonLatConvertExt(lonLat, {})` genuinely ambiguous against the 1-arg overload's own
    // `options = {}` (a scalar value-initialization outranks the class-type default constructor in
    // overload resolution), so `{}` would silently pick this overload with `coordSysType`
    // defaulted to Wgs84 instead of dispatching on the converter's own configured system. Without
    // the default, `f(ll, {})` resolves unambiguously to the 1-arg overload.
    [[nodiscard]] CoordPartArray lonLatConvertExt(LonLat lonLat, CoordSysType coordSysType,
                                                  CoordToStringOptions options) const;

private:
    CoordRepresentationSettings settings_;
    CoordTransformer transformer_;
};

} // namespace cg::geo
