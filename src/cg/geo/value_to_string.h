// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ValueToStringConverter.pas, u_ValueToStringConverter.pas,
// i_ValueToStringConverterConfig.pas, u_ValueToStringConverterConfig.pas, t_CommonTypes.pas:39-74,
// the unit strings in u_ResStrings.pas:330-360. docs/specs/geo.md:2227-2311 (§6.5, §6.5.1).
// FEAT-GEO-016.
//
// Turns raw doubles (metres, sq. metres, km/h, kB) into the exact display strings legacy showed:
// unit thresholds, per-threshold decimal counts, and the distance-format-derives-the-speed-unit
// rule (§6.5 point 3). `geo` has no tr(): every unit suffix below is the literal ASCII legacy
// source string; `ui` wraps them in tr() at the call site, same split as coord_to_string.h.

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"

#include <QString>

#include <cstdint>

namespace cg::geo {

// t_CommonTypes.pas:39-49. Persisted id == ordinal (u_ValueToStringConverterConfig.pas:88, an
// unchecked `TDistStrFormat(Integer)` cast in legacy); the port keeps the same unchecked cast on
// read (strict 1:1) -- an out-of-range ordinal round-trips unchanged, exactly as legacy stores it.
enum class DistStrFormat : std::uint8_t {
    KmAndM,
    KmOrM,
    SimpleKm,
    SimpleMeter,
    SimpleCentimeter,
    SimpleMile,
    SimpleYard,
    SimpleFoot,
    SimpleInch,
    SimpleNauticalMile,
};

// t_CommonTypes.pas:51-61. Persisted id == ordinal.
enum class AreaStrFormat : std::uint8_t {
    Auto,
    SqM,
    SqKm,
    Are,
    Ha,
    SqFoot,
    SqYard,
    SqMile,
    SqNauticalMile,
    SqAcre,
};

// t_CommonTypes.pas:63-67. Never persisted directly -- always derived from DistStrFormat
// (GetSpeedStrFormat, u_ValueToStringConverter.pas:318-340).
enum class SpeedStrFormat : std::uint8_t { Kmph, Mph, Knot };

// The value half of IValueToStringConverterConfig/IValueToStringConverterConfigStatic
// (u_ValueToStringConverterConfig.pas:34-71). The ConfigDataElement wrapper -- locking, change
// notification -- is `ui`'s, same split as CoordRepresentationSettings.
struct ValueFormatSettings {
    DistStrFormat dist = DistStrFormat::KmAndM; // section [ValueFormats], key "DistFormat"
    AreaStrFormat area = AreaStrFormat::Auto;   // section [ValueFormats], key "AreaShowFormat"

    [[nodiscard]] bool operator==(const ValueFormatSettings&) const = default;

    // Ports TValueToStringConverterConfig.DoReadConfig/.DoWriteConfig
    // (u_ValueToStringConverterConfig.pas:90-101), section `[ValueFormats]`
    // (u_GlobalConfig.pas:302). The caller passes that section's own provider, exactly as legacy's
    // DoReadConfig/DoWriteConfig do.
    [[nodiscard]] static ValueFormatSettings read(const core::IConfigProvider& src);
    void write(core::IConfigWriter& dst) const;
};

// Ports TValueToStringConverter (u_ValueToStringConverter.pas). Not an interface: one
// implementation; IValueToStringConverterChangeable is `ui`'s.
class ValueToStringConverter {
public:
    explicit ValueToStringConverter(ValueFormatSettings settings);

    // DataSizeConvert, u_ValueToStringConverter.pas:214-224. The one conversion with no NaN guard
    // (spec §6.5 closing note) -- reproduce, add none.
    [[nodiscard]] QString dataSizeConvert(double sizeInKb) const;

    // DistConvert, u_ValueToStringConverter.pas:226-283. Formats Abs(metres) and prepends a single
    // '-' to the whole result when the input was negative (§6.5.1 point 1) -- never per-part.
    [[nodiscard]] QString distConvert(double metres) const;

    // DistPerPixelConvert, u_ValueToStringConverter.pas:285-290: distConvert(v) + "/pixel", no
    // space before the slash.
    [[nodiscard]] QString distPerPixelConvert(double metresPerPixel) const;

    // AreaConvert, u_ValueToStringConverter.pas:128-212. Compares the SIGNED input, unlike
    // distConvert (§6.5.1 point 2) -- a negative area (never produced by GeoCalc today, but not
    // guarded against) takes the "small" branch of every threshold.
    [[nodiscard]] QString areaConvert(double sqm) const;

    // SpeedConvert, u_ValueToStringConverter.pas:342-357. The unit is derived from `dist`, never
    // stored (§6.5.1 point 3).
    [[nodiscard]] QString speedConvert(double kmph) const;

    // AltitudeConvert, u_ValueToStringConverter.pas:150-157. Always metres, 1 decimal, regardless
    // of `dist`.
    [[nodiscard]] QString altitudeConvert(double metres) const;

private:
    ValueFormatSettings settings_;
};

} // namespace cg::geo
