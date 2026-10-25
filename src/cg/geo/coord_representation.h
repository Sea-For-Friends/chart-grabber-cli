// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: t_CoordRepresentation.pas (enums), u_CoordRepresentation.pas (persisted-id mapping only --
// the caption arrays are `ui`'s business, docs/specs/geo.md §6.1, except that their array order is
// the settings-dialog combo index and therefore part of the saved-settings contract),
// i_CoordRepresentationConfig.pas, u_CoordRepresentationConfig.pas (the value half only -- the
// ConfigDataElement wrapper with locking/change-notification is `ui`'s, spec §6.1).
// docs/specs/geo.md:1896-1968. FEAT-GEO-004.

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"

#include <cstdint>

namespace cg::geo {

// Underlying type is std::uint8_t (clang-tidy performance-enum-size, see docs/handoffs/T-0011A.md
// for the precedent) -- the legacy ordinals persisted in the INI are unaffected: every read/write
// still goes through an explicit int cast (coordSysTypeToInt() & co.).

// t_CoordRepresentation.pas:27-36
enum class GeogCoordShowFormat : std::uint8_t {
    CharDegrMinSec,
    CharDegrMin,
    CharDegr,
    CharDegr2,
    SignDegrMinSec,
    SignDegrMin,
    SignDegr,
    SignDegr2,
};

// t_CoordRepresentation.pas:38-43
enum class ProjCoordShowFormat : std::uint8_t {
    RoundedToWhole,
    RoundedToTenth,
    RoundedToHundredths,
    RoundedToThousandths,
};

// t_CoordRepresentation.pas:45-48
enum class MgrsCoordShowFormat : std::uint8_t {
    Splitted,
    Joined,
};

// t_CoordRepresentation.pas:50-60
enum class CoordSysType : std::uint8_t {
    Wgs84,     // WGS 84 / Geographic
    Utm,       // WGS 84 / UTM zones
    Mgrs,      // WGS 84 / MGRS
    Sk42,      // SK-42 / Geographic
    Sk42Gk,    // SK-42 / Gauss-Kruger zones
    Gsk2011,   // GSK-2011 / Geographic
    Gsk2011Gk, // GSK-2011 / Gauss-Kruger zones
};

// t_CoordRepresentation.pas:62-66
enum class CoordSysInfoType : std::uint8_t {
    DontShow = 0,
    ShowExceptWgs84 = 1,
    ShowForAll = 2,
};

// --- persistence: the ids on disk are NOT the ordinals -------------------------------------------
// u_CoordRepresentation.pas:76-78,113-129. Unknown id -> the first enumerator, matching legacy's
// IntegerToCoordSysType/IntegerToGeogCoordShowFormat fallback (spec §6.1 table).
[[nodiscard]] int coordSysTypeToInt(CoordSysType v) noexcept;
[[nodiscard]] CoordSysType intToCoordSysType(int v) noexcept;

// u_CoordRepresentation.pas:170-191
[[nodiscard]] int geogCoordShowFormatToInt(GeogCoordShowFormat v) noexcept;
[[nodiscard]] GeogCoordShowFormat intToGeogCoordShowFormat(int v) noexcept;

// u_CoordRepresentation.pas:193-223. Persisted id == ordinal.
[[nodiscard]] int projCoordShowFormatToInt(ProjCoordShowFormat v) noexcept;
[[nodiscard]] ProjCoordShowFormat intToProjCoordShowFormat(int v) noexcept;

// MgrsCoordShowFormat/CoordSysInfoType are read/written through an *unchecked* `TFoo(Integer)` cast
// in legacy (u_CoordRepresentationConfig.pas:142,144 -- an out-of-range ordinal from a hand-edited
// INI produces an invalid enum value and hits the `default:`/`else Assert(False)` branch
// downstream). The port validates the range on read instead and falls back to the first enumerator,
// like the other two mappings -- "the port validates all four ... a strict improvement with no
// effect on any valid file" (spec §6.1, §10 D-7).
[[nodiscard]] int mgrsCoordShowFormatToInt(MgrsCoordShowFormat v) noexcept; // == ordinal
[[nodiscard]] MgrsCoordShowFormat intToMgrsCoordShowFormat(int v) noexcept;
[[nodiscard]] int coordSysInfoTypeToInt(CoordSysInfoType v) noexcept; // == ordinal
[[nodiscard]] CoordSysInfoType intToCoordSysInfoType(int v) noexcept;

// The value half of ICoordRepresentationConfig / ICoordRepresentationConfigStatic
// (u_CoordRepresentationConfig.pas:34-97). The ConfigDataElement wrapper -- locking, change
// notification, the Changeable/Static split -- is `ui`'s (spec §6.1); this struct is the plain
// snapshot every consumer (coord_to_string, the settings dialog) actually needs.
struct CoordRepresentationSettings {
    bool isLatitudeFirst = true;
    GeogCoordShowFormat geogFormat = GeogCoordShowFormat::CharDegrMinSec;
    ProjCoordShowFormat projFormat = ProjCoordShowFormat::RoundedToWhole;
    MgrsCoordShowFormat mgrsFormat = MgrsCoordShowFormat::Splitted;
    CoordSysType coordSysType = CoordSysType::Wgs84;
    CoordSysInfoType coordSysInfoType = CoordSysInfoType::ShowExceptWgs84;

    [[nodiscard]] bool operator==(const CoordRepresentationSettings&) const = default;

    // Ports TCoordRepresentationConfig.DoReadConfig/.DoWriteConfig, u_CoordRepresentationConfig.pas
    // :133-160. Section `[CoordFormats]`, registered in u_GlobalConfig.pas:298-302 -- the caller
    // passes that section's own provider, exactly as legacy's DoReadConfig/DoWriteConfig do.
    // Keys, exactly (note the legacy spelling "DegrisShowFormat" -- it is in every user's INI):
    // FirstLat, DegrisShowFormat, ProjCoordShowFormat, MgrsCoordShowFormat, CoordSysType,
    // CoordSysInfoType.
    [[nodiscard]] static CoordRepresentationSettings read(const core::IConfigProvider& src);
    void write(core::IConfigWriter& dst) const;
};

} // namespace cg::geo
