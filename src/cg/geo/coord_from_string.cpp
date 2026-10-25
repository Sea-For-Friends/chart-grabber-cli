// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_CoordFromStringParser.pas, the str2r half of u_GeoToStrFunc.pas. docs/specs/geo.md
// :2087-2164 (§6.3). FEAT-GEO-008.
//
// str2r on raw X/Y text (the UTM/Gauss-Kruger overload, u_CoordFromStringParser.pas:197-198):
// QString::toDouble trims leading/trailing whitespace and accepts "inf"/"nan" (case-insensitive,
// optionally signed); Delphi's TextToFloat (StrToFloatDef's engine, since ~Delphi XE) likewise
// trims whitespace and recognises TFormatSettings' NaNSymbol/Infinity symbols, which for the
// default English locale legacy ships with are "NAN"/"INF"/"-INF" -- the same tokens. No Delphi
// runtime is available in this repo to confirm the exact RTL version's grammar byte-for-byte (same
// limitation noted for FormatFloat's rounding-tie convention, docs/handoffs/T-0019A1.md); reusing
// str2r here (rather than a second, stricter parser) is the best-effort match, flagged in
// docs/handoffs/T-0019B2.md.
#include "cg/geo/coord_from_string.h"

#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QtGlobal>

#include <cmath>
#include <utility>

namespace cg::geo {

namespace {

bool isAsciiDigit(QChar c) {
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

bool isAllowedScanChar(QChar c) {
    return isAsciiDigit(c) || c == QLatin1Char('-') || c == QLatin1Char('+') || c == QLatin1Char('.') ||
           c == QLatin1Char(',') || c == QLatin1Char(' ');
}

// Legacy SysUtils.UpperCase converts only the 7-bit ASCII letters a..z; QString::toUpper() follows
// full Unicode and would also rewrite non-ASCII letters (e.g. lowercase Cyrillic, German ß), which
// changes the grammar (see docs/handoffs/T-0019B1-review.md finding 1). Reused at every UpperCase
// call site (Edit2Digit x2, str_to_mgrs).
QString toUpperAscii(const QString& s) {
    QString result = s;
    for (QChar& c : result) {
        if (c >= QLatin1Char('a') && c <= QLatin1Char('z')) {
            c = QChar(c.unicode() - (u'a' - u'A'));
        }
    }
    return result;
}

// str2r, u_GeoToStrFunc.pas:78-99. A token reaching here only ever contains characters from
// edit2Digit's own allowed set ([0-9+-,. ]), by construction -- everything else was already
// rewritten to a space and split away before str2r is called. Decimal separator is ',' if the
// token contains one, else '.'; a token with *both* (e.g. "1,234.5") therefore still has a stray
// '.' left over after the ',' -> '.' rewrite, which fails to parse -> 0, exactly like legacy's
// StrToFloatDef with DecimalSeparator := ',' choking on the other symbol. Parse failure never
// throws, it yields 0 (legacy: StrToFloatDef(..., 0, ...)).
double str2r(const QString& token) {
    if (token.isEmpty()) {
        return 0.0;
    }
    QString canonical = token;
    if (token.contains(QLatin1Char(','))) {
        canonical.replace(QLatin1Char(','), QLatin1Char('.'));
    }
    // else: '.' already is the decimal separator, or there is no separator in the token at all --
    // legacy's locale-default fallback (str2r's third branch) can only matter when the token
    // contains that locale separator character, which (given the allowed-char set above) is only
    // ever ',' or '.' -- both already handled.
    bool ok = false;
    const double value = canonical.toDouble(&ok);
    return ok ? value : 0.0;
}

// Edit2Digit, u_CoordFromStringParser.pas:75-150 (the geographic scanner, §6.3.1). `i` mirrors the
// Pascal source's own 1-based, mutating-string index exactly (including the i=0 access the first
// branch's `dec(i)` can produce for a leading invalid character) via a sentinel out-of-range
// QChar that can never equal a digit, space or comma -- matching what that out-of-bounds Delphi
// string access reads in practice (the UnicodeString header, never literally a space).
bool edit2Digit(const QString& input, bool isLat, double& result) {
    result = 0.0;

    QString text = toUpperAscii(input);
    text.replace(QLatin1Char('S'), QLatin1Char('-'));
    text.replace(QLatin1Char('W'), QLatin1Char('-'));
    text.replace(QLatin1Char('N'), QLatin1Char('+'));
    text.replace(QLatin1Char('E'), QLatin1Char('+'));
    text.replace(QChar(0x042E), QLatin1Char('-')); // Cyrillic Ю
    text.replace(QChar(0x0417), QLatin1Char('-')); // Cyrillic З
    text.replace(QChar(0x0412), QLatin1Char('+')); // Cyrillic В
    text.replace(QChar(0x0421), QLatin1Char('+')); // Cyrillic С

    const bool minus = text.contains(QLatin1Char('-'));

    constexpr QChar kOutOfRange(u'￿');
    auto charAt = [&](qsizetype i) -> QChar {
        if (i < 1 || i > text.size()) {
            return kOutOfRange;
        }
        return text.at(i - 1);
    };

    qsizetype i = 1;
    while (i <= text.size()) {
        if (!isAllowedScanChar(charAt(i))) {
            text[i - 1] = QLatin1Char(' ');
            --i;
        }

        const QChar ci = charAt(i);
        const QChar ciNext = charAt(i + 1);
        const QChar ciPrev = charAt(i - 1);
        const qsizetype len = text.size();
        const bool squeeze = (i == 1 && ci == QLatin1Char(' ')) || (i == len && ci == QLatin1Char(' ')) ||
                             (i < len - 1 && ci == QLatin1Char(' ') && ciNext == QLatin1Char(' ')) ||
                             (i > 1 && ci == QLatin1Char(' ') && !isAsciiDigit(ciPrev)) ||
                             (i < len - 1 && ci == QLatin1Char(',') && ciNext == QLatin1Char(' '));
        if (squeeze) {
            text.remove(i - 1, 1);
            --i;
        }
        ++i;
    }

    bool ok = true;
    qint64 divisor = 1;
    for (;;) {
        const qsizetype spacePos = text.indexOf(QLatin1Char(' '));
        QString token;
        if (spacePos < 0) {
            token = text;
        } else {
            token = text.left(spacePos);
            text.remove(0, spacePos + 1);
        }
        const double component = str2r(token);

        if ((divisor > 1 && std::abs(component) > 60.0) ||
            (divisor == 1 && isLat && std::abs(component) > 90.0) ||
            (divisor == 1 && !isLat && std::abs(component) > 180.0)) {
            ok = false;
        }

        if (result < 0) {
            result -= component / static_cast<double>(divisor);
        } else {
            result += component / static_cast<double>(divisor);
        }
        if (minus && result > 0) {
            result = -result;
        }

        divisor *= 60;
        if (spacePos < 0 || divisor > 3600 || !ok) {
            break;
        }
    }

    return ok;
}

// _GetZoneParts, u_CoordFromStringParser.pas:212-238 (§6.3.2). Spaces stripped, upper-cased,
// leading digits are the zone number, the next character is the (unvalidated -- range-checked by
// the caller) zone letter.
void zoneParts(const QString& zone, int& zoneNumber, QChar& zoneLetter, bool& zoneNumberOverflow) {
    QString text = zone;
    text.remove(QLatin1Char(' '));
    text = toUpperAscii(text);

    zoneNumberOverflow = false;
    qsizetype i = 0;
    while (i < text.size() && isAsciiDigit(text.at(i))) {
        ++i;
    }

    zoneNumber = 0;
    if (i > 0) {
        bool digitsOk = false;
        zoneNumber = text.left(i).toInt(&digitsOk);
        // D-25 (docs/parity/deviations.md): legacy's StrToInt raises on overflow (unhandled --
        // TryStrToCoord(AX,AY,AZone) has no try/except, unlike Edit2Digit); the port fails cleanly
        // instead of crashing. No valid zone (1..60 / 0..60) is anywhere near this range, so no
        // acceptance/rejection outcome changes for any zone digit string that isn't already
        // pathological.
        zoneNumberOverflow = !digitsOk;
    }

    zoneLetter = (i < text.size()) ? text.at(i) : QChar();
}

} // namespace

CoordFromStringParser::CoordFromStringParser(const CoordRepresentationSettings& settings)
    : settings_(settings) {}

bool CoordFromStringParser::tryStrToCoord(const QString& lon, const QString& lat, LonLat& out) const {
    if (!edit2Digit(lon, /*isLat=*/false, out.lon)) {
        return false;
    }
    if (!edit2Digit(lat, /*isLat=*/true, out.lat)) {
        return false;
    }

    switch (settings_.coordSysType) {
    case CoordSysType::Wgs84:
        return true;
    case CoordSysType::Sk42:
    case CoordSysType::Gsk2011:
        // u_CoordFromStringParser.pas:196-198.
        return transformer_.geographicToWgs84(settings_.coordSysType, out);
    default:
        // legacy: Assert(False) -- not a valid coordSysType for this overload
        // (u_CoordFromStringParser.pas:199-202).
        Q_ASSERT(false);
        return false;
    }
}

bool CoordFromStringParser::tryStrToCoord(const QString& x, const QString& y, const QString& zone,
                                          LonLat& out) const {
    double xValue = str2r(x);
    double yValue = str2r(y);

    int zoneNumber = 0;
    QChar zoneLetter;
    bool zoneNumberOverflow = false;
    zoneParts(zone, zoneNumber, zoneLetter, zoneNumberOverflow);
    if (zoneNumberOverflow) {
        return false;
    }
    const bool hasNorS = zoneLetter == QLatin1Char('N') || zoneLetter == QLatin1Char('S');

    switch (settings_.coordSysType) {
    case CoordSysType::Sk42Gk:
    case CoordSysType::Gsk2011Gk: {
        if (zoneNumber < 1 || zoneNumber > 60 || !hasNorS) {
            return false;
        }
        std::swap(xValue, yValue);
        // u_CoordFromStringParser.pas:266-280.
        const GaussKrugerCoord coord{zoneNumber, zoneLetter == QLatin1Char('N'), xValue, yValue};
        return transformer_.gaussKrugerToWgs84(settings_.coordSysType, coord, out);
    }
    case CoordSysType::Utm: {
        if (zoneNumber < 0 || zoneNumber > 60 || !hasNorS) {
            return false;
        }
        if (zoneNumber == 0) {
            std::swap(xValue, yValue);
        }
        // u_CoordFromStringParser.pas:282-297.
        const UtmCoord coord{zoneNumber, QChar(), zoneLetter == QLatin1Char('N'), xValue, yValue};
        return CoordTransformer::utmToWgs84(coord, out);
    }
    default:
        // legacy: Assert(False) -- not a valid coordSysType for this overload
        // (u_CoordFromStringParser.pas:299-301).
        Q_ASSERT(false);
        return false;
    }
}

bool CoordFromStringParser::tryStrToCoord(const QString& s, LonLat& out) const {
    if (settings_.coordSysType != CoordSysType::Mgrs) {
        // legacy: Assert(False) -- not a valid coordSysType for this overload
        // (u_CoordFromStringParser.pas:317-319).
        Q_ASSERT(false);
        return false;
    }

    // str_to_mgrs, Proj4.UTM.pas:368-432 (§6.3.3).
    QString text = toUpperAscii(s);
    text.remove(QLatin1Char(' '));
    const qsizetype len = text.size();
    if (len < 13) {
        return false;
    }

    qsizetype i = 0;
    while (i < len && isAsciiDigit(text.at(i))) {
        ++i;
    }
    if (i > 2) {
        return false;
    }

    int zoneNumber = 0;
    if (i > 0) {
        zoneNumber = text.left(i).toInt();
        if (zoneNumber < 1 || zoneNumber > 60) {
            return false;
        }
    }

    const QChar band = text.at(i);
    const QChar digraph0 = text.at(i + 1);
    const QChar digraph1 = text.at(i + 2);

    for (qsizetype p = i + 3; p < len; ++p) {
        if (!isAsciiDigit(text.at(p))) {
            return false;
        }
    }

    const qsizetype prefixLen = i + 3;
    const qsizetype digitsLen = len - prefixLen;
    // Legacy splits the remaining digits with integer division (floor for easting, the remainder --
    // ceil -- for northing): `Copy(VStr, 1+I, (VLen-I) div 2)` then `Copy(VStr, 1+I)` (rest of
    // string). Its final `Result := I = VLen` is a tautology given the digit-scan loop above
    // already succeeded, so an ODD digit tail is *not* rejected in legacy despite what §6.3.3's
    // prose says -- legacy wins; see docs/handoffs/T-0019B1.md "legacy-vs-spec conflicts".
    const qsizetype eastingLen = digitsLen / 2;
    const QString eastingStr = text.mid(prefixLen, eastingLen);
    const QString northingStr = text.mid(prefixLen + eastingLen);
    bool eastingOk = false;
    bool northingOk = false;
    const int easting = eastingStr.toInt(&eastingOk);
    const int northing = northingStr.toInt(&northingOk);
    // D-25 (docs/parity/deviations.md): legacy's StrToInt raises on overflow, unhandled here either
    // (Proj4.UTM.pas:419,423); the port fails cleanly instead of converting a garbage 0 (B1 review
    // finding, "For slice B2").
    if (!eastingOk || !northingOk) {
        return false;
    }

    // u_CoordFromStringParser.pas:311-316.
    const MgrsCoord coord{zoneNumber, band, {digraph0, digraph1}, easting, northing};
    return CoordTransformer::mgrsToWgs84(coord, out);
}

} // namespace cg::geo
