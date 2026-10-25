// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-008; docs/specs/geo.md:2087-2164 (§6.3). No legacy DUnit test exists for the
// parser (only u_CoordToStringConverter_Test.pas, ported for coord_to_string in T-0019A1) -- per
// the "port, don't multiply" test policy this file is one table-driven acceptance test and one
// table-driven rejection test, hand-derived by replaying u_CoordFromStringParser.pas's Edit2Digit
// scanner (and, for the zone/MGRS rows, _GetZoneParts / str_to_mgrs) by hand.
#include "cg/geo/coord_from_string.h"

#include <gtest/gtest.h>

#include <vector>

using cg::geo::CoordFromStringParser;
using cg::geo::CoordRepresentationSettings;
using cg::geo::CoordSysType;
using cg::geo::LonLat;

namespace {

CoordRepresentationSettings settingsFor(CoordSysType type) {
    CoordRepresentationSettings s;
    s.coordSysType = type;
    return s;
}

} // namespace

// --- accepted geographic formats (Wgs84), one row per format family (§6.3.1) --------------------
//
// Values are exact fractions (e.g. 30.0/60.0) rather than pre-rounded decimal literals so the
// expectation matches the parser's own arithmetic bit-for-bit; EXPECT_NEAR's epsilon only guards
// against summation-order rounding noise.
TEST(CoordFromString, AcceptedGeographicFormats) {
    const CoordFromStringParser parser(settingsFor(CoordSysType::Wgs84));

    struct Row {
        const char* name;
        QString lon;
        QString lat;
        double expectedLon;
        double expectedLat;
    };
    const std::vector<Row> rows = {
        // The §6.3.1 trap, pinned deliberately: str2r("") == 0, no component loop runs at all.
        {"EmptyStringParsesAsZeroAndSucceeds", QStringLiteral(""), QStringLiteral(""), 0.0, 0.0},
        // Decimal degrees; also exercises '.' as the decimal separator.
        {"SingleNumberIsDecimalDegrees/DotDecimalSeparator", QStringLiteral("37.1234"),
         QStringLiteral("55.6789"), 37.1234, 55.6789},
        // '°' is not in the allowed-char set, so it becomes a component separator (DDM).
        {"DegreeSignActsAsSeparator", QStringLiteral(u"37°30"), QStringLiteral(u"51°45"), 37.0 + 30.0 / 60.0,
         51.0 + 45.0 / 60.0},
        // '\'' and '"' are likewise rewritten to spaces (DMS).
        {"QuoteAndDoubleQuoteActAsSeparators", QStringLiteral(u"37°30'15\""), QStringLiteral(u"55°15'30\""),
         37.0 + 30.0 / 60.0 + 15.0 / 3600.0, 55.0 + 15.0 / 60.0 + 30.0 / 3600.0},
        {"CommaDecimalSeparator", QStringLiteral("12,25"), QStringLiteral("45,75"), 12.25, 45.75},
        // Prefix hemisphere letters glue directly onto the front of the numeric token, so the
        // resulting token ("-37.5") parses as a signed number on its own.
        {"HemisphereLetterAsPrefix", QStringLiteral("W37.5"), QStringLiteral("N55.5"), -37.5, 55.5},
        // Suffix hemisphere letters only work when a separator symbol (here "'") isolates the sign
        // into its own (subsequently ignored, §6.3.1 point 6) trailing component -- the running
        // total was already forced negative by the point-8 sign-flip after the first component. A
        // *bare* decimal-degree suffix ("37.5W", no separator) is a different, broken case: the
        // '-' glues onto the end of "37.5", the combined token fails to parse and silently
        // degrades to 0 (see BothSeparatorsYieldZero below for the same str2r failure mode).
        {"HemisphereLetterAsSuffix", QStringLiteral(u"37°30'W"), QStringLiteral(u"55°15'N"), -37.5,
         55.0 + 15.0 / 60.0},
        // Cyrillic hemisphere letters (spec §6.3.1 point 2): B (U+0412) -> '+' east, Ю (U+042E) ->
        // '-' south/west's counterpart. Upper-casing is ASCII-only (matches legacy
        // SysUtils.UpperCase), so this only works for the already-uppercase Cyrillic letters --
        // lowercase ю/з/в/с are NOT rewritten to signs (see the two rows below).
        {"CyrillicHemisphereLetters", QStringLiteral(u"В12.5"), QStringLiteral(u"Ю34.5"), 12.5, -34.5},
        // Lowercase Cyrillic Ю (U+044E) is not one of the four uppercase sign letters and ASCII-only
        // upper-casing does not touch it, so it falls into the "not allowed" branch like any other
        // stray character and acts as a separator that gets squeezed away -- legacy gives 55.5 here
        // (review finding 1; QString::toUpper() would instead rewrite it to '-', giving "55.5-",
        // which str2r cannot parse -> 0).
        {"LowercaseCyrillicYuIsNotASignLetter", QStringLiteral("0"), QStringLiteral(u"55.5ю"), 0.0, 55.5},
        // Same for lowercase З (U+0437): it is not the uppercase '-' letter, so it is squeezed away
        // as a leading separator and the number parses on its own, positive -- legacy gives +37.5
        // (QString::toUpper() would instead rewrite it to '-', giving -37.5).
        {"LowercaseCyrillicZeIsNotASignLetter", QStringLiteral(u"з37.5"), QStringLiteral("0"), 37.5, 0.0},
        // A 4th space-separated component is silently ignored (the loop stops after seconds).
        {"FourthComponentIsIgnored", QStringLiteral("10 20 30 40"), QStringLiteral("0"),
         10.0 + 20.0 / 60.0 + 30.0 / 3600.0, 0.0},
        // str2r picks ',' as the decimal separator because it appears first; the leftover '.' then
        // makes the whole token fail to parse -> 0 (u_GeoToStrFunc.pas:78-99, "port it literally").
        {"BothSeparatorsYieldZero", QStringLiteral("1,234.5"), QStringLiteral("0"), 0.0, 0.0},
        // A stray letter (not a hemisphere letter) becomes a space like any other invalid
        // character, so "12X34" reads as DDM 12 deg 34' -- garbage generally still returns true.
        {"StrayLetterActsAsSeparator", QStringLiteral("12X34"), QStringLiteral("0"), 12.0 + 34.0 / 60.0, 0.0},
    };

    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        LonLat out{};
        ASSERT_TRUE(parser.tryStrToCoord(row.lon, row.lat, out));
        EXPECT_NEAR(out.lon, row.expectedLon, 1e-9);
        EXPECT_NEAR(out.lat, row.expectedLat, 1e-9);
    }
}

// --- rejections: out of range, ambiguous hemisphere, empty/too-short (§6.3.1-§6.3.3) -------------
TEST(CoordFromString, RejectedInputs) {
    {
        const CoordFromStringParser parser(settingsFor(CoordSysType::Wgs84));
        struct Row {
            const char* name;
            QString lon;
            QString lat;
        };
        // A degrees/minutes/seconds range violation on either coordinate fails the whole call; the
        // >60 check applies identically to minutes and seconds (both have divisor > 1).
        const std::vector<Row> rows = {
            {"MinutesAboveSixtyFail", QStringLiteral(u"37°75'"), QStringLiteral("0")},
            {"LatitudeAboveNinetyFails", QStringLiteral("0"), QStringLiteral("95")},
            {"LongitudeAboveOneEightyFails", QStringLiteral("185"), QStringLiteral("0")},
        };
        for (const Row& row : rows) {
            SCOPED_TRACE(row.name);
            LonLat out{};
            EXPECT_FALSE(parser.tryStrToCoord(row.lon, row.lat, out));
        }
    }

    // UTM / Gauss-Kruger / MGRS: the text grammar and range checks are validated here (§6.3.2,
    // §6.3.3), but the projected/grid -> WGS84 transform itself is T-0019B2 (coord_transformer.h,
    // not yet ported), so every one of these overloads returns false unconditionally in this
    // slice, on top of any grammar rejection -- both reasons are observably "false" until B2
    // lands. These rows pin the zone/length validation branches themselves (they must not throw or
    // mis-parse), not a same-vs-different-reason distinction the return value can't expose yet.
    {
        // D-25 (a)/(b)/(d), reliability audit 2026-09-28 (L-24): zone-parsing paths where legacy
        // raises an unhandled exception (overflow) or truncates an AnsiChar (Cyrillic byte
        // aliasing) -- the port must fail cleanly (false, no throw) on all of them.
        const CoordFromStringParser utm(settingsFor(CoordSysType::Utm));
        struct Row {
            const char* name;
            QString zone;
        };
        const std::vector<Row> rows = {
            // Zone letter is neither 'N' nor 'S' (the MGRS band letter is explicitly not accepted
            // here, §6.3.2) -- an "ambiguous hemisphere" zone suffix.
            {"AmbiguousHemisphereLetterFails", QStringLiteral("37X")},
            // (d) legacy's AnsiChar(VZoneLetter) in ['N','S'] truncates U+044E (Cyrillic ю) to its
            // low byte 0x4E = 'N' and accepts it as north; the port compares the full QChar and
            // rejects it (`u_CoordFromStringParser.pas:268,283`; `coord_from_string.cpp:230`).
            {"CyrillicYuZoneLetterFails", QStringLiteral(u"37ю")},
            // (a) an over-long digit run before the zone letter overflows legacy's zone StrToInt
            // (Integer, `_GetZoneParts`, `u_CoordFromStringParser.pas:212-238`, no try/except); the
            // port's QString::toInt overflow is a clean parse failure.
            {"OverflowingZoneDigitsFail", QStringLiteral("99999999999N")},
            // (b) legacy's `AnsiChar(P^) in ['0'..'9']` digit scan (`u_CoordFromStringParser.pas:223`)
            // truncates Cyrillic а (U+0430, low byte 0x30 = '0') into the digit run, so StrToInt then
            // raises on the non-digit tail; the port's isAsciiDigit never matches it, so the digit run
            // ends after '3' and "аN" fails the trailing-letter check cleanly.
            {"CyrillicDigitInZoneFails", QStringLiteral(u"3аN")},
        };
        for (const Row& row : rows) {
            SCOPED_TRACE(row.name);
            LonLat out{};
            EXPECT_FALSE(utm.tryStrToCoord(QStringLiteral("500000"), QStringLiteral("0"), row.zone, out));
        }
    }
    {
        // Gauss-Kruger (unlike UTM) requires zone 1..60 -- 0 is UTM's UPS-only exception, not GK's.
        const CoordFromStringParser gk(settingsFor(CoordSysType::Sk42Gk));
        LonLat out{};
        EXPECT_FALSE(
            gk.tryStrToCoord(QStringLiteral("500000"), QStringLiteral("0"), QStringLiteral("0N"), out));
    }
    {
        // MGRS: empty/too-short input (< 13 characters after stripping spaces) is rejected before
        // any zone/band/digraph parsing is attempted.
        const CoordFromStringParser mgrs(settingsFor(CoordSysType::Mgrs));
        LonLat out{};
        EXPECT_FALSE(mgrs.tryStrToCoord(QStringLiteral(""), out));
        EXPECT_FALSE(mgrs.tryStrToCoord(QStringLiteral("37UPB1234"), out));
        // D-25 (c), reliability audit 2026-09-28 (L-24): a pathologically long easting/northing
        // digit tail overflows legacy's StrToInt (`Proj4.UTM.pas:419,423`, unhandled exception);
        // the port's toInt() ok-flag failure is a clean rejection instead.
        EXPECT_FALSE(mgrs.tryStrToCoord(QStringLiteral("37UPB12345678901234567890"), out));
    }
}
