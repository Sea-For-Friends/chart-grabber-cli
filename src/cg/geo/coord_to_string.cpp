// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_CoordToStringConverter.pas. docs/specs/geo.md:1969-2086 (§6.2). FEAT-GEO-004,
// FEAT-GEO-005 (UTM/UPS), FEAT-GEO-006 (SK-42/GSK-2011), FEAT-GEO-007 (MGRS).
#include "cg/geo/coord_to_string.h"

#include <QChar>
#include <QLatin1Char>
#include <QLatin1StringView>

#include <cmath>
#include <cstdio>
#include <utility>

namespace cg::geo {

namespace {

// Ports the family of Delphi FormatFloat masks this unit uses ("00.0000", "00.000000") for the
// DMS seconds/minutes and DDM minutes fields: `intDigits` zero-padded integer-part digits (callers
// always pass a non-negative `value` -- the sign is handled separately, see degrToStr below),
// `decimals` fixed decimal digits, '.' always the separator (u_CoordToStringConverter.pas:126 pins
// DecimalSeparator to '.' unconditionally), no thousands grouping. `%.*f` gives the same
// correctly-rounded decimal text FormatFloat does for every value these two masks actually see:
// `value` here is always itself an exact quotient of a qint64 unit count (see dmsToStr/ddmToStr),
// printed at exactly the digit count that unit count was quantized to, so the printed value is
// never within one ULP of a decimal tie -- unlike the DD/DD2 mask below, see
// formatFixedRoundHalfAwayFromZero.
QString formatFixed(double value, int intDigits, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    QString s = QString::fromLatin1(buf);
    const qsizetype dot = s.indexOf(QLatin1Char('.'));
    const qsizetype intLen = (dot < 0) ? s.size() : dot;
    const qsizetype wantIntLen = intDigits;
    if (intLen < wantIntLen) {
        s.prepend(QString(wantIntLen - intLen, QLatin1Char('0')));
    }
    return s;
}

// Ports the "0.00000000" mask used by the DD/DD2 path (ddToStr below; ports
// u_CoordToStringConverter.pas:207's `FormatFloat('0.00000000', VDegr)`). Unlike the DMS/DDM masks
// above, `value` here is an arbitrary, non-quantized double (a raw degree value), so an exact tie
// at the 9th decimal is reachable (any odd multiple of 1/512, e.g. 12.001953125 -- see
// docs/handoffs/T-0019A1-review.md finding 1). `%.*f` (snprintf, and Qt's own string-to-double
// formatting) rounds such a tie half-to-even on the binary value; Delphi's FormatFloat rounds it
// half away from zero on the decimal value (docs/specs/geo.md:2020). `intDigits`/`decimals` as
// above; `value` is always non-negative (see ddToStr).
//
// Approach: scale to an integer count of the smallest printed unit (10^-decimals) and round with
// std::round, which is itself specified to break ties away from zero -- so std::round(1200195312.5)
// gives 1200195313, matching legacy's 12.00195313 for the example above. The scaled product is
// exact here (the inputs are degrees, at most a handful of integer digits, and decimals <= 8, well
// inside a double's 53-bit mantissa) so this reproduces the exact tie rather than one perturbed by
// the scaling multiply. The rounded unit count is then formatted by direct integer division/modulo
// and zero-padding -- never fed back through a floating-point print step -- so this final
// formatting cannot reintroduce a second, different rounding decision the way printing the
// unscaled-back double through another %.*f could.
QString formatFixedRoundHalfAwayFromZero(double value, int intDigits, int decimals) {
    const auto scale = static_cast<qint64>(std::llround(std::pow(10.0, decimals)));
    const auto units = static_cast<qint64>(std::round(value * static_cast<double>(scale)));
    QString intPart = QString::number(units / scale);
    if (intPart.size() < intDigits) {
        intPart.prepend(QString(intDigits - intPart.size(), QLatin1Char('0')));
    }
    QString fracPart = QString::number(units % scale);
    if (fracPart.size() < decimals) {
        fracPart.prepend(QString(decimals - fracPart.size(), QLatin1Char('0')));
    }
    return intPart + QLatin1Char('.') + fracPart;
}

// The 1e-4-arcsecond and 1e-6-arcminute quantisation grids (u_CoordToStringConverter.pas:159,188).
// Written as qint64 products from the first operand on (clang-tidy
// bugprone-implicit-widening-of-multiplication-result): an int*int*int intermediate would still
// fit here, but the pattern matches the rest of this module's qint64 arithmetic.
constexpr qint64 kDmsUnitsPerDegree = 60LL * 60 * 10000;
constexpr qint64 kDmsUnitsPerMinute = 60LL * 10000;
constexpr qint64 kDdmUnitsPerDegree = 60LL * 1000000;

// u_CoordToStringConverter.pas:158-185 (DMS: dshCharDegrMinSec / dshSignDegrMinSec).
//
// CutZero's guard -- `Copy(Result, Length(Result)-3, 4) = '.00"'` -- reads the last 4 characters
// of the rendered string looking for a literal '.' followed by two zeros and the closing quote.
// But the seconds field is *always* rendered with 4 decimal digits (mask "00.0000"), so the '.'
// always sits 6 characters from the end, never inside that 4-character window: the guard cannot
// be true for any input (verified by construction here, and empirically over 2,000,000 random
// degree values while porting -- see docs/handoffs/T-0019A1.md). DMS CutZero is therefore a
// legacy no-op; reproduced as such rather than translating dead string-matching code (coding
// standard: write the intent, not a transliteration). Flagged as an open question in the ticket.
QString dmsToStr(double degr) {
    const double absDegr = std::abs(degr);
    auto value = static_cast<qint64>(absDegr * 60 * 60 * 10000 + 0.005);
    const qint64 deg = value / kDmsUnitsPerDegree;
    value -= deg * kDmsUnitsPerDegree;
    const qint64 minute = value / kDmsUnitsPerMinute;
    value -= minute * kDmsUnitsPerMinute;
    const double sec = static_cast<double>(value) / 10000.0;
    return QString::number(deg) + QChar(u'°') + formatFixed(static_cast<double>(minute), 2, 0) +
           QChar(u'\'') + formatFixed(sec, 2, 4) + QChar(u'"');
}

// u_CoordToStringConverter.pas:187-204 (DDM: dshCharDegrMin / dshSignDegrMin). Unlike DMS, this
// guard's fixed layout genuinely works: the apostrophe is unique in the string, so a global
// replace-all of "0'" / ".'" / "00'" only ever touches the tail -- endsWith/chop is equivalent
// (spec §6.2.2).
QString ddmToStr(double degr, bool cutZero) {
    const double absDegr = std::abs(degr);
    auto value = static_cast<qint64>(absDegr * 60 * 1000000 + 0.00005);
    const qint64 deg = value / kDdmUnitsPerDegree;
    value -= deg * kDdmUnitsPerDegree;
    const double minute = static_cast<double>(value) / 1000000.0;
    QString result = QString::number(deg) + QChar(u'°') + formatFixed(minute, 2, 6) + QChar(u'\'');
    if (cutZero) {
        while (result.endsWith(QLatin1StringView("0'"))) { // 12°34.50000' -> 12°34.5'
            result.chop(2);
            result += QChar(u'\'');
        }
        if (result.endsWith(QLatin1StringView(".'"))) { // 12°40,' -> 12°40'
            result.chop(2);
            result += QChar(u'\'');
        }
        if (result.endsWith(QLatin1StringView("00'"))) { //  12°00' -> 12°
            result.chop(3);
        }
    }
    return result;
}

// u_CoordToStringConverter.pas:206-232 (DD/DD2: dshCharDegr[2] / dshSignDegr[2]). Scan backward
// over '0', then over one '.', truncate there.
QString ddToStr(double degr, bool cutZero, bool withDegreeSign) {
    const double absDegr = std::abs(degr);
    QString result = formatFixedRoundHalfAwayFromZero(absDegr, 1, 8);
    if (cutZero) {
        qsizetype end = result.size();
        while (end > 0 && result[end - 1] == QChar(u'0')) {
            --end;
        }
        if (end > 0 && result[end - 1] == QChar(u'.')) {
            --end;
        }
        result.truncate(end);
    }
    if (withDegreeSign) {
        result += QChar(u'°');
    }
    return result;
}

// DegrToStr, u_CoordToStringConverter.pas:144-238. `degr` may be negative; only its magnitude is
// formatted here -- the sign/hemisphere letter is a separate, unspaced prefix (spec §6.2.1 intro).
QString degrToStr(GeogCoordShowFormat format, double degr, bool cutZero) {
    switch (format) {
    case GeogCoordShowFormat::CharDegrMinSec:
    case GeogCoordShowFormat::SignDegrMinSec:
        return dmsToStr(degr);
    case GeogCoordShowFormat::CharDegrMin:
    case GeogCoordShowFormat::SignDegrMin:
        return ddmToStr(degr, cutZero);
    case GeogCoordShowFormat::CharDegr:
    case GeogCoordShowFormat::SignDegr:
        return ddToStr(degr, cutZero, /*withDegreeSign=*/true);
    case GeogCoordShowFormat::CharDegr2:
    case GeogCoordShowFormat::SignDegr2:
        return ddToStr(degr, cutZero, /*withDegreeSign=*/false);
    }
    return QString(); // unreachable for a valid GeogCoordShowFormat, keeps -Wreturn-type happy
}

// GetLatitudeMarker/GetLongitudeMarker, u_CoordToStringConverter.pas:240-282. The four letters are
// hard-coded ASCII and never translated (legacy :122-125).
bool isCharFamily(GeogCoordShowFormat format) {
    switch (format) {
    case GeogCoordShowFormat::CharDegrMinSec:
    case GeogCoordShowFormat::CharDegrMin:
    case GeogCoordShowFormat::CharDegr:
    case GeogCoordShowFormat::CharDegr2:
        return true;
    default:
        return false;
    }
}

QString latitudeMarker(GeogCoordShowFormat format, double degr) {
    if (isCharFamily(format)) {
        if (degr > 0) {
            return QStringLiteral("N");
        }
        if (degr < 0) {
            return QStringLiteral("S");
        }
        return QString();
    }
    return degr < 0 ? QStringLiteral("-") : QString();
}

QString longitudeMarker(GeogCoordShowFormat format, double degr) {
    if (isCharFamily(format)) {
        if (degr > 0) {
            return QStringLiteral("E");
        }
        if (degr < 0) {
            return QStringLiteral("W");
        }
        return QString();
    }
    return degr < 0 ? QStringLiteral("-") : QString();
}

// _GeodeticCoordToStr, u_CoordToStringConverter.pas:351-359.
void geodeticCoordToStr(GeogCoordShowFormat format, LonLat lonLat, bool cutZero, bool latitudeFirst,
                        QString& lon, QString& lat) {
    lon = longitudeMarker(format, lonLat.lon) + degrToStr(format, lonLat.lon, cutZero);
    lat = latitudeMarker(format, lonLat.lat) + degrToStr(format, lonLat.lat, cutZero);
    if (latitudeFirst) {
        std::swap(lon, lat);
    }
}

// GetCoordSysTypeCaptionShort / GetUPSCoordSysTypeCaptionShort, u_CoordRepresentation.pas:94-111.
// Only the short captions are ported here (the settings dialog's long-form captions and the
// Geog/Proj/Mgrs format caption arrays stay `ui`'s, spec §6.1); GetCoordSysInfo is the one method
// in this class's own scope that consumes them. Untranslated, like every other geo caption
// (projection_set.h: "geo has no tr(); ui" wraps at the call site) -- these four SK-42/GSK-2011
// captions are gettext-wrapped in legacy, Utm/Mgrs/Wgs84 are not, but that distinction is `ui`'s
// tr() call to make, not this module's.
QString coordSysTypeCaptionShort(CoordSysType type) {
    switch (type) {
    case CoordSysType::Wgs84:
        return QStringLiteral("WGS 84");
    case CoordSysType::Utm:
        return QStringLiteral("WGS 84 / UTM");
    case CoordSysType::Mgrs:
        return QStringLiteral("WGS 84 / MGRS");
    case CoordSysType::Sk42:
        return QStringLiteral("SK-42");
    case CoordSysType::Sk42Gk:
        return QStringLiteral("SK-42 / GK");
    case CoordSysType::Gsk2011:
        return QStringLiteral("GSK-2011");
    case CoordSysType::Gsk2011Gk:
        return QStringLiteral("GSK-2011 / GK");
    }
    return QString(); // unreachable
}

// The decimal-digit count behind each ProjCoordShowFormat's FormatFloat mask
// ('0.', '0.0', '0.00', '0.000', u_CoordToStringConverter.pas:363).
int projDecimals(ProjCoordShowFormat format) {
    switch (format) {
    case ProjCoordShowFormat::RoundedToWhole:
        return 0;
    case ProjCoordShowFormat::RoundedToTenth:
        return 1;
    case ProjCoordShowFormat::RoundedToHundredths:
        return 2;
    case ProjCoordShowFormat::RoundedToThousandths:
        return 3;
    }
    return 0; // unreachable
}

// FormatFloat(CFloatFormat[FProjCoordShowFormat], value), u_CoordToStringConverter.pas:365-366. A
// mask with no digit placeholders after '.' (the RoundedToWhole '0.' mask) omits the decimal point
// from the output entirely (Delphi FormatFloat semantics) -- ProjMaskRoundedToWholeHasNoTrailingPoint
// pins this. `value` is always a UTM/Gauss-Kruger easting or northing here, which is always
// non-negative by construction (false easting/northing keeps every realistic coordinate positive),
// so formatFixedRoundHalfAwayFromZero's non-negative assumption holds; that helper (not a %.*f
// re-print) is reused for the same reason ddToStr uses it -- X/Y are arbitrary PROJ doubles, so an
// exact rounding tie at the mask's last digit is reachable. Locale-independent by construction
// (manual digit formatting, no QLocale/snprintf grouping) -- ProjHasNoThousandsSeparatorUnderAnyLocale
// pins this.
QString projCoordToStr(double value, ProjCoordShowFormat format) {
    const int decimals = projDecimals(format);
    if (decimals == 0) {
        return QString::number(static_cast<qint64>(std::llround(value)));
    }
    return formatFixedRoundHalfAwayFromZero(value, 1, decimals);
}

// _ProjectedCoordToStr, u_CoordToStringConverter.pas:361-371. `lonOut`/`latOut` are exactly the
// caller's VLonStr/VLatStr out-params (AX/AY in legacy), so the swap (Gauss-Kruger: always: UTM:
// only for the UPS fallback) happens before the lon/lat assignment, not after.
void projectedCoordToStr(double x, double y, bool swapXY, ProjCoordShowFormat format, QString& lonOut,
                         QString& latOut) {
    QString ax = projCoordToStr(x, format);
    QString ay = projCoordToStr(y, format);
    if (swapXY) {
        std::swap(ax, ay);
    }
    lonOut = ax;
    latOut = ay;
}

// %.5d, u_CoordToStringConverter.pas:375,379-380 -- 5-digit zero-padded (MGRS easting/northing are
// always in [0, 99999]).
QString mgrsDigits(int value) {
    return QString::number(value).rightJustified(5, QLatin1Char('0'));
}

// _MgrsCoordToStr, u_CoordToStringConverter.pas:373-400.
void mgrsCoordToStr(const MgrsCoord& mgrs, MgrsCoordShowFormat format, QString& zoneOut, QString& lonOut,
                    QString& latOut) {
    const QString ax = mgrsDigits(mgrs.x);
    const QString ay = mgrsDigits(mgrs.y);
    const QString vzone = (mgrs.zone == 0) ? QString() : QString::number(mgrs.zone);

    switch (format) {
    case MgrsCoordShowFormat::Splitted:
        zoneOut = vzone + mgrs.band + QLatin1Char(' ') + mgrs.digraph[0] + mgrs.digraph[1];
        lonOut = ax;
        latOut = ay;
        break;
    case MgrsCoordShowFormat::Joined:
        zoneOut = vzone + mgrs.band + mgrs.digraph[0] + mgrs.digraph[1] + ax + ay;
        lonOut.clear();
        latOut.clear();
        break;
    }
}

// _ZoneInfoToStr, u_CoordToStringConverter.pas:402-417.
QString zoneInfoToStr(int zone, bool isNorth, bool includeZone) {
    if (!includeZone) {
        return QString();
    }
    QString result;
    if (zone > 0) {
        result = QString::number(zone);
    }
    result += isNorth ? QLatin1Char('N') : QLatin1Char('S');
    return result;
}

} // namespace

CoordToStringConverter::CoordToStringConverter(const CoordRepresentationSettings& settings)
    : settings_(settings) {}

QString CoordToStringConverter::coordSysInfo(LonLat lonLat) const {
    if (settings_.coordSysInfoType == CoordSysInfoType::DontShow) {
        return QString();
    }

    QString result = coordSysTypeCaptionShort(settings_.coordSysType);

    switch (settings_.coordSysType) {
    case CoordSysType::Wgs84:
        if (settings_.coordSysInfoType == CoordSysInfoType::ShowExceptWgs84) {
            result.clear();
        }
        break;
    case CoordSysType::Utm:
        if (lonLat.lat >= 84 || lonLat.lat <= -80) {
            result = QStringLiteral("WGS 84 / UPS");
        }
        break;
    default:
        break;
    }

    return result;
}

QString CoordToStringConverter::lonLatConvert(LonLat lonLat, CoordToStringOptions options) const {
    if (settings_.isLatitudeFirst) {
        options |= CoordToStringOption::LatitudeFirst;
    }

    const CoordPartArray parts = lonLatConvertExt(lonLat, settings_.coordSysType, options);

    QString result;
    for (const QString& part : parts) {
        if (part.isEmpty()) {
            continue;
        }
        if (!result.isEmpty()) {
            result += QChar(u' ');
        }
        result += part;
    }
    return result;
}

CoordPartArray CoordToStringConverter::lonLatConvertExt(LonLat lonLat, CoordToStringOptions options) const {
    return lonLatConvertExt(lonLat, settings_.coordSysType, options);
}

CoordPartArray CoordToStringConverter::lonLatConvertExt(LonLat lonLat, CoordSysType coordSysType,
                                                        CoordToStringOptions options) const {
    const bool cutZero = options.testFlag(CoordToStringOption::CutZero);
    const bool latitudeFirst = options.testFlag(CoordToStringOption::LatitudeFirst);
    const bool includeZone = options.testFlag(CoordToStringOption::IncludeZone);

    // LonLatConvertExt, u_CoordToStringConverter.pas:345-474. Legacy's own failure sentinel for a
    // failed wgs84_to_geog/wgs84_to_proj/geodetic_wgs84_to_* call (PROJ unavailable, or the point is
    // out of range for its CRS) is the literal string "NaN" in both Lon/Lat and an empty Zone
    // (:426-428) -- every branch below starts from that sentinel and only overwrites it on success.
    QString lonStr = QStringLiteral("NaN");
    QString latStr = QStringLiteral("NaN");
    QString zoneStr;

    switch (coordSysType) {
    case CoordSysType::Wgs84: // WGS 84 / Geographic
        geodeticCoordToStr(settings_.geogFormat, lonLat, cutZero, latitudeFirst, lonStr, latStr);
        break;

    case CoordSysType::Sk42: // SK-42 / GSK-2011 (Geographic)
    case CoordSysType::Gsk2011: {
        LonLat geog = lonLat;
        if (transformer_.wgs84ToGeographic(coordSysType, geog)) {
            geodeticCoordToStr(settings_.geogFormat, geog, cutZero, latitudeFirst, lonStr, latStr);
        }
        break;
    }

    case CoordSysType::Sk42Gk: // SK-42 / GSK-2011 (Gauss-Kruger)
    case CoordSysType::Gsk2011Gk: {
        GaussKrugerCoord gauss{};
        if (transformer_.wgs84ToGaussKruger(coordSysType, lonLat, gauss)) {
            projectedCoordToStr(gauss.x, gauss.y, /*swapXY=*/true, settings_.projFormat, lonStr, latStr);
            zoneStr = zoneInfoToStr(gauss.zone, gauss.isNorth, includeZone);
        }
        break;
    }

    case CoordSysType::Utm: {
        UtmCoord utm{};
        if (CoordTransformer::wgs84ToUtm(lonLat, utm)) { // WGS 84 / UTM
            projectedCoordToStr(utm.x, utm.y, /*swapXY=*/false, settings_.projFormat, lonStr, latStr);
            zoneStr = zoneInfoToStr(utm.zone, utm.isNorth, includeZone);
        } else if (CoordTransformer::wgs84ToUps(lonLat, utm)) { // WGS 84 / UPS
            projectedCoordToStr(utm.x, utm.y, /*swapXY=*/true, settings_.projFormat, lonStr, latStr);
            zoneStr = zoneInfoToStr(utm.zone, utm.isNorth, includeZone);
        }
        break;
    }

    case CoordSysType::Mgrs: {
        MgrsCoord mgrs{};
        if (CoordTransformer::wgs84ToMgrs(lonLat, mgrs)) {
            mgrsCoordToStr(mgrs, settings_.mgrsFormat, zoneStr, lonStr, latStr);
        }
        break;
    }
    }

    return {zoneStr, lonStr, latStr};
}

} // namespace cg::geo
