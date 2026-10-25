// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_ValueToStringConverter.pas, u_ValueToStringConverterConfig.pas.
// docs/specs/geo.md:2227-2311 (§6.5, §6.5.1). FEAT-GEO-016.
#include "cg/geo/value_to_string.h"

#include <QLocale>

#include <cmath>

namespace cg::geo {

namespace {

constexpr const char* kInvalidValue = "NaN"; // CInvalidValue, u_ValueToStringConverter.pas:85

// ValToNum, u_ValueToStringConverter.pas:87-90: `FloatToStrF(AValue, ffNumber, 15, ADigits) + ' '
// + AUnits`. ffNumber means grouped thousands and the locale's own decimal separator -- spec
// §6.5's own primitive, `QLocale().toString(value, 'f', digits)`, does both, using the *current*
// QLocale (deliberately locale-dependent, unlike coord_to_string's hard-pinned '.': see
// LocaleAffectsGroupingAndSeparator in value_to_string_test.cpp). QLocale's 'f' format rounds an
// exact decimal tie half away from zero, matching Delphi's `FormatFloat` convention -- verified
// empirically on Qt 6.11 (docs/handoffs/T-0019A2-review.md).
QString valToNum(double value, const QString& units, int digits = 2) {
    return QLocale().toString(value, 'f', digits) + QLatin1Char(' ') + units;
}

// GetSpeedStrFormat, u_ValueToStringConverter.pas:318-340: the speed unit is derived from the
// distance format, never stored on its own (§6.5.1 point 3).
SpeedStrFormat speedStrFormatFor(DistStrFormat dist) {
    switch (dist) {
    case DistStrFormat::KmAndM:
    case DistStrFormat::KmOrM:
    case DistStrFormat::SimpleKm:
    case DistStrFormat::SimpleMeter:
    case DistStrFormat::SimpleCentimeter:
        return SpeedStrFormat::Kmph;
    case DistStrFormat::SimpleMile:
    case DistStrFormat::SimpleYard:
    case DistStrFormat::SimpleFoot:
    case DistStrFormat::SimpleInch:
        return SpeedStrFormat::Mph;
    case DistStrFormat::SimpleNauticalMile:
        return SpeedStrFormat::Knot;
    }
    return SpeedStrFormat::Kmph; // unreachable for a valid DistStrFormat
}

} // namespace

ValueFormatSettings ValueFormatSettings::read(const core::IConfigProvider& src) {
    ValueFormatSettings s; // defaults mirror TValueToStringConverterConfig.Create
    // u_ValueToStringConverterConfig.pas:88-89: unchecked `TDistStrFormat(Integer)`/
    // `TAreaStrFormat(Integer)` cast -- the raw ordinal is kept as-is, in range or not.
    s.dist = static_cast<DistStrFormat>(src.readInt(u"DistFormat", static_cast<qint32>(s.dist)));
    s.area = static_cast<AreaStrFormat>(src.readInt(u"AreaShowFormat", static_cast<qint32>(s.area)));
    return s;
}

void ValueFormatSettings::write(core::IConfigWriter& dst) const {
    dst.writeInt(u"DistFormat", static_cast<qint32>(dist));
    dst.writeInt(u"AreaShowFormat", static_cast<qint32>(area));
}

ValueToStringConverter::ValueToStringConverter(ValueFormatSettings settings) : settings_(settings) {}

QString ValueToStringConverter::dataSizeConvert(double sizeInKb) const {
    // u_ValueToStringConverter.pas:214-224. No NaN guard here -- reproduce, add none (§6.5).
    if (sizeInKb > 1048576) {
        return valToNum(sizeInKb / 1048576, QStringLiteral("GB"), 1);
    }
    if (sizeInKb > 1024) {
        return valToNum(sizeInKb / 1024, QStringLiteral("MB"), 1);
    }
    return valToNum(sizeInKb, QStringLiteral("KB"), 1);
}

QString ValueToStringConverter::distConvert(double metres) const {
    if (std::isnan(metres)) {
        return QString::fromLatin1(kInvalidValue);
    }

    const double absMetres = std::abs(metres);
    QString result;

    switch (settings_.dist) {
    case DistStrFormat::KmAndM: {
        if (absMetres > 1000) {
            const double km = absMetres / 1000.0;
            const double kmWhole = std::trunc(km);
            result = valToNum(kmWhole, QStringLiteral("km"), 0) + QLatin1Char(' ') +
                     valToNum((km - kmWhole) * 1000.0, QStringLiteral("m"));
        } else {
            result = valToNum(absMetres, QStringLiteral("m"));
        }
        break;
    }
    case DistStrFormat::KmOrM:
        if (absMetres < 10000) {
            result = valToNum(absMetres, QStringLiteral("m"));
        } else {
            result = valToNum(absMetres / 1000.0, QStringLiteral("km"));
        }
        break;
    case DistStrFormat::SimpleKm:
        result = valToNum(absMetres / 1000.0, QStringLiteral("km"));
        break;
    case DistStrFormat::SimpleMeter:
        result = valToNum(absMetres, QStringLiteral("m"));
        break;
    case DistStrFormat::SimpleCentimeter:
        result = valToNum(absMetres * 100.0, QStringLiteral("cm"));
        break;
    case DistStrFormat::SimpleMile:
        result = valToNum(absMetres / 1609.344, QStringLiteral("mi"));
        break;
    case DistStrFormat::SimpleYard:
        result = valToNum(absMetres / 0.9144, QStringLiteral("yd"));
        break;
    case DistStrFormat::SimpleFoot:
        result = valToNum(absMetres / 0.3048, QStringLiteral("ft"));
        break;
    case DistStrFormat::SimpleInch:
        result = valToNum(absMetres / 0.0254, QStringLiteral("in"));
        break;
    case DistStrFormat::SimpleNauticalMile:
        result = valToNum(absMetres / 1852.0, QStringLiteral("nmi"));
        break;
    }

    // §6.5.1 point 1: Abs-then-format, then a single '-' prefixes the WHOLE string --
    // -1500 m -> "-1 km 500.00 m", never "-1 km -500.00 m".
    if (metres < 0) {
        result.prepend(QLatin1Char('-'));
    }
    return result;
}

QString ValueToStringConverter::distPerPixelConvert(double metresPerPixel) const {
    // DistPerPixelConvert, u_ValueToStringConverter.pas:285-290: no space before the slash.
    return distConvert(metresPerPixel) + QStringLiteral("/pixel");
}

QString ValueToStringConverter::areaConvert(double sqm) const {
    if (std::isnan(sqm)) {
        return QString::fromLatin1(kInvalidValue);
    }

    // §6.5.1 point 2: compares the SIGNED input, unlike distConvert.
    switch (settings_.area) {
    case AreaStrFormat::Auto:
        if (sqm <= 1000000) {
            return valToNum(sqm, QStringLiteral("m2"));
        }
        return valToNum(sqm / 1000000.0, QStringLiteral("km2"));
    case AreaStrFormat::SqM:
        if (sqm <= 100) {
            return valToNum(sqm, QStringLiteral("m2"));
        }
        return valToNum(sqm, QStringLiteral("m2"), 0);
    case AreaStrFormat::SqKm:
        if (sqm <= 1000000) {
            return valToNum(sqm / 1000000.0, QStringLiteral("km2"), 6);
        }
        return valToNum(sqm / 1000000.0, QStringLiteral("km2"));
    case AreaStrFormat::Are:
        if (sqm <= 10000) {
            return valToNum(sqm / 100.0, QStringLiteral("a"));
        }
        return valToNum(sqm / 100.0, QStringLiteral("a"), 0);
    case AreaStrFormat::Ha:
        if (sqm <= 1000000) {
            return valToNum(sqm / 10000.0, QStringLiteral("ha"), 4);
        }
        return valToNum(sqm / 10000.0, QStringLiteral("ha"));
    case AreaStrFormat::SqFoot:
        return valToNum(sqm / 0.09290304, QStringLiteral("ft2"));
    case AreaStrFormat::SqYard:
        return valToNum(sqm / 0.83612736, QStringLiteral("yd2"));
    case AreaStrFormat::SqMile:
        return valToNum(sqm / 2589988.110336, QStringLiteral("mi2"));
    case AreaStrFormat::SqNauticalMile:
        return valToNum(sqm / 3429904.0, QStringLiteral("nmi2"));
    case AreaStrFormat::SqAcre:
        return valToNum(sqm / 4046.8564224, QStringLiteral("acr"));
    }
    return QString(); // unreachable for a valid AreaStrFormat
}

QString ValueToStringConverter::speedConvert(double kmph) const {
    if (std::isnan(kmph)) {
        return QString::fromLatin1(kInvalidValue);
    }

    switch (speedStrFormatFor(settings_.dist)) {
    case SpeedStrFormat::Kmph:
        return valToNum(kmph, QStringLiteral("km/h"), 1);
    case SpeedStrFormat::Mph:
        return valToNum(kmph / 1.609344, QStringLiteral("mi/h"), 1);
    case SpeedStrFormat::Knot:
        return valToNum(kmph / 1.8520, QStringLiteral("kn"), 1);
    }
    return QString(); // unreachable
}

QString ValueToStringConverter::altitudeConvert(double metres) const {
    if (std::isnan(metres)) {
        return QString::fromLatin1(kInvalidValue);
    }
    return valToNum(metres, QStringLiteral("m"), 1);
}

} // namespace cg::geo
