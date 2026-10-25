// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_CoordTransformer.pas, Includes/Proj4/{Proj4.UTM,Proj4.GaussKruger}.pas.
// docs/specs/geo.md:2178-2238 (§6.4). FEAT-GEO-005, FEAT-GEO-006, FEAT-GEO-007.
#include "cg/geo/coord_transformer.h"

#include "cg/geo/proj/proj4_defines.h"

#include <QLatin1Char>

#include <cmath>
#include <cstring>

namespace cg::geo {

namespace {

// One factory shared by every CoordTransformer instance and every static free function below.
// ProjConverterFactory interns one IProjConverter per init string behind a weak_ptr cache
// (T-0020, D-10), so repeated calls for the same UTM/Gauss-Kruger zone reuse the same PROJ object
// instead of rebuilding it on every call the way legacy's own Proj4.Utils re-parses the init
// string every time.
ProjConverterFactory& sharedFactory() {
    static ProjConverterFactory instance;
    return instance;
}

// Legacy SysUtils.UpperCase converts only the 7-bit ASCII letters a..z (see
// docs/handoffs/T-0019B1-review.md finding 1, which applies equally to Proj4.UTM.pas:378's
// UpperCase call in str_to_mgrs). Duplicated here (rather than shared with coord_from_string.cpp)
// because each is a small anonymous-namespace helper private to its own translation unit, matching
// this module's existing pattern.
QString toUpperAscii(const QString& s) {
    QString result = s;
    for (QChar& c : result) {
        if (c >= QLatin1Char('a') && c <= QLatin1Char('z')) {
            c = QChar(c.unicode() - (u'a' - u'A'));
        }
    }
    return result;
}

bool isAsciiDigit(QChar c) {
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// CMgrsLatBands / CMgrsUtmCols / CMgrsUtmRows / CMgrsUpsCols / CMgrsUpsRows / CFeUps / CFnUps,
// Proj4.UTM.pas:44-61,71-73 -- verbatim.
constexpr const char kMgrsLatBands[] = "CDEFGHJKLMNPQRSTUVWXX"; // X repeats for 80..84N

constexpr const char* kMgrsUtmCols[3] = {"ABCDEFGH", "JKLMNPQR", "STUVWXYZ"};
constexpr const char* kMgrsUtmRows[2] = {"ABCDEFGHJKLMNPQRSTUV", "FGHJKLMNPQRSTUVABCDE"};
constexpr const char* kMgrsUpsCols[4] = {"JKLPQRSTUXYZZ", "ABCFGHJKLPQR", "JKLPQRSTUXYZZ", "ABCFGHJ"};
constexpr const char* kMgrsUpsRows[4] = {"ABCDEFGHJKLMNPQRSTUVWXYZ", "ABCDEFGHJKLMNPQRSTUVWXYZ",
                                         "ABCDEFGHJKLMNP", "ABCDEFGHJKLMNP"};
constexpr int kFeUps[4] = {8, 20, 8, 20};
constexpr int kFnUps[4] = {8, 8, 13, 13};

// Pos(AChar, AString) - 1, i.e. the 0-based index of the first occurrence, or -1 if not found.
// std::strchr(haystack, '\0') would "find" the terminating null, which QChar's default value
// (0x0000, e.g. an unset MgrsCoord::digraph/band) would otherwise spuriously match -- guarded
// against explicitly, matching what a Pascal Pos() against a real (non-embedded-NUL) string
// could never do.
int findChar(const char* haystack, QChar needle) {
    if (needle.unicode() == 0 || needle.unicode() > 0x7F) {
        return -1;
    }
    const char* p = std::strchr(haystack, static_cast<char>(needle.unicode()));
    return (p != nullptr) ? static_cast<int>(p - haystack) : -1;
}

// wgs84_long_to_utm_zone, Proj4.UTM.pas:75-81.
int wgs84LongToUtmZone(double lon) {
    const int zone = static_cast<int>(std::floor((lon + 180.0) / 6.0)) + 1;
    return zone > 60 ? 60 : zone; // lon == 180
}

// wgs84_longlat_to_utm_zone, Proj4.UTM.pas:83-109 -- the Norway (band V) and Svalbard (band X)
// exceptions, carried over verbatim.
bool wgs84LongLatToUtmZone(double lon, double lat, int& zone, QChar& band) {
    if (!(lat < 84.0 && lat > -80.0)) {
        return false;
    }
    zone = wgs84LongToUtmZone(lon);
    band = QLatin1Char(kMgrsLatBands[static_cast<int>(std::floor(lat / 8.0 + 10.0))]);

    if (band == QLatin1Char('V')) {
        if (zone == 31 && lon >= 3.0) {
            ++zone;
        }
    }
    if (band == QLatin1Char('X')) {
        if (zone == 32 && lon < 9.0) {
            --zone;
        }
        if (zone == 32 && lon >= 9.0) {
            ++zone;
        }
        if (zone == 34 && lon < 21.0) {
            --zone;
        }
        if (zone == 34 && lon >= 21.0) {
            ++zone;
        }
        if (zone == 36 && lon < 33.0) {
            --zone;
        }
        if (zone == 36 && lon >= 33.0) {
            ++zone;
        }
    }
    return true;
}

// get_utm_init(zone, isNorth), Proj4.UTM.pas:136-143.
QByteArray utmInit(int zone, bool isNorth) {
    return isNorth ? QString(QLatin1String(kUtmNorthFmt)).arg(zone).toLatin1()
                   : QString(QLatin1String(kUtmSouthFmt)).arg(zone).toLatin1();
}

// geog_long_to_zone, Proj4.GaussKruger.pas:65-72.
int gaussKrugerZoneFromLon(double lon) {
    if (lon > 0.0) {
        return static_cast<int>(std::floor(lon / 6.0)) + 1;
    }
    return static_cast<int>(std::floor((180.0 + lon) / 6.0)) + 31;
}

// GetProjInit, Proj4.GaussKruger.pas:87-103: zone-prefixed false easting, the Russian convention.
QByteArray gaussKrugerProjInit(const char* fmt, int zone, bool isNorth) {
    int lon0 = zone * 6 - 3;
    if (zone > 30) {
        lon0 -= 360;
    }
    const int x0 = zone * 1000000 + 500000;
    const int y0 = isNorth ? 0 : 10000000;
    return QString(QLatin1String(fmt)).arg(lon0).arg(x0).arg(y0).toLatin1();
}

} // namespace

CoordTransformer::CoordTransformer()
    : sk42Geog_(sharedFactory().byInitString(QByteArray(kSk42))),
      gsk2011Geog_(sharedFactory().byInitString(QByteArray(kGsk2011))) {}

ProjConverterPtr CoordTransformer::geogEngine(CoordSysType type) const {
    switch (type) {
    case CoordSysType::Sk42:
    case CoordSysType::Sk42Gk:
        return sk42Geog_;
    case CoordSysType::Gsk2011:
    case CoordSysType::Gsk2011Gk:
        return gsk2011Geog_;
    default:
        return nullptr;
    }
}

const char* CoordTransformer::projInitFmt(CoordSysType type) {
    switch (type) {
    case CoordSysType::Sk42:
    case CoordSysType::Sk42Gk:
        return kSk42GkFmt;
    case CoordSysType::Gsk2011:
    case CoordSysType::Gsk2011Gk:
        return kGsk2011GkFmt;
    default:
        return nullptr;
    }
}

bool CoordTransformer::wgs84ToGeographic(CoordSysType type, LonLat& inOut) const {
    const ProjConverterPtr engine = geogEngine(type);
    if (!engine) {
        return false;
    }
    const auto xy = engine->lonLat2XY(inOut);
    if (!xy) {
        return false;
    }
    inOut = LonLat{xy->x(), xy->y()};
    return true;
}

bool CoordTransformer::geographicToWgs84(CoordSysType type, LonLat& inOut) const {
    const ProjConverterPtr engine = geogEngine(type);
    if (!engine) {
        return false;
    }
    const auto ll = engine->xy2LonLat(DoublePoint(inOut.lon, inOut.lat));
    if (!ll) {
        return false;
    }
    inOut = *ll;
    return true;
}

bool CoordTransformer::wgs84ToGaussKruger(CoordSysType type, LonLat lonLat, GaussKrugerCoord& out) const {
    const ProjConverterPtr geog = geogEngine(type);
    const char* fmt = projInitFmt(type);
    if (!geog || fmt == nullptr) {
        return false;
    }
    const auto shifted = geog->lonLat2XY(lonLat); // wgs84_to_geog: the Helmert shift
    if (!shifted) {
        return false;
    }
    const int zone = gaussKrugerZoneFromLon(shifted->x());
    const bool isNorth = shifted->y() >= 0.0;
    const ProjConverterPtr proj = sharedFactory().byInitString(gaussKrugerProjInit(fmt, zone, isNorth));
    if (!proj) {
        return false;
    }
    const auto xy = proj->lonLat2XY(LonLat{shifted->x(), shifted->y()}); // geog_to_proj: pure tmerc
    if (!xy) {
        return false;
    }
    out = GaussKrugerCoord{zone, isNorth, xy->x(), xy->y()};
    return true;
}

bool CoordTransformer::gaussKrugerToWgs84(CoordSysType type, const GaussKrugerCoord& coord,
                                          LonLat& out) const {
    const ProjConverterPtr geog = geogEngine(type);
    const char* fmt = projInitFmt(type);
    if (!geog || fmt == nullptr) {
        return false;
    }
    const ProjConverterPtr proj =
        sharedFactory().byInitString(gaussKrugerProjInit(fmt, coord.zone, coord.isNorth));
    if (!proj) {
        return false;
    }
    const auto geogLonLat = proj->xy2LonLat(DoublePoint(coord.x, coord.y)); // proj_to_geog
    if (!geogLonLat) {
        return false;
    }
    const auto wgs = geog->xy2LonLat(DoublePoint(geogLonLat->lon, geogLonLat->lat)); // geog_to_wgs84
    if (!wgs) {
        return false;
    }
    out = *wgs;
    return true;
}

bool CoordTransformer::wgs84ToUtm(LonLat lonLat, UtmCoord& out) {
    int zone = 0;
    QChar band;
    if (!wgs84LongLatToUtmZone(lonLat.lon, lonLat.lat, zone, band)) {
        return false;
    }
    const bool isNorth = band >= QLatin1Char('N');
    // Zone/Band/IsNorth are written before the PROJ leg is attempted, matching legacy's own
    // mutate-the-var-parameter-in-place semantics (geodetic_wgs84_to_utm, Proj4.UTM.pas:145-155:
    // AUtm.Zone/.Band/.IsNorth are set unconditionally once wgs84_longlat_to_utm_zone succeeds,
    // regardless of whether the later geodetic_cs_to_projected_cs call fails). This also lets the
    // zone/band table tests (spec §11.2 "the first six") run without PROJ.
    out.zone = zone;
    out.band = band;
    out.isNorth = isNorth;
    out.x = 0.0;
    out.y = 0.0;

    const ProjConverterPtr conv = sharedFactory().byInitString(utmInit(zone, isNorth));
    if (!conv) {
        return false;
    }
    const auto xy = conv->lonLat2XY(lonLat);
    if (!xy) {
        return false;
    }
    out.x = xy->x();
    out.y = xy->y();
    return true;
}

bool CoordTransformer::utmToWgs84(const UtmCoord& coord, LonLat& out) {
    if (coord.zone >= 1 && coord.zone <= 60) {
        const ProjConverterPtr conv = sharedFactory().byInitString(utmInit(coord.zone, coord.isNorth));
        if (!conv) {
            return false;
        }
        const auto ll = conv->xy2LonLat(DoublePoint(coord.x, coord.y));
        if (!ll) {
            return false;
        }
        out = *ll;
        return true;
    }
    if (coord.zone == 0) {
        return upsToWgs84(coord, out);
    }
    return false;
}

bool CoordTransformer::wgs84ToUps(LonLat lonLat, UtmCoord& out) {
    out.zone = 0;

    const char* initStr = nullptr;
    bool isNorth = false;
    if (lonLat.lat >= 84.0) {
        initStr = kUpsNorth;
        isNorth = true;
    } else if (lonLat.lat <= -80.0) {
        // Spec §6.4 "legacy bug, fixed here" / §10 D-9: legacy tests `ALat <= 80`, which is
        // wrong-signed for any latitude that actually gets here (this function is only called
        // once geodetic_wgs84_to_utm has already failed, i.e. lat >= 84 (handled above) or
        // lat <= -80) -- observable only when the UTM PROJ leg fails and legacy falls through to
        // UPS; the conclusion is unchanged: both branches agree on every reachable input, so the
        // port writes the evidently-intended `-80` without changing observable behavior.
        initStr = kUpsSouth;
        isNorth = false;
    } else {
        return false;
    }
    out.isNorth = isNorth;
    // The band depends only on the sign of the input longitude (Proj4.UTM.pas:192-206), not on the
    // projected X/Y, so it is computed here, ahead of the PROJ call -- legacy only assigns it after
    // a successful transform, but the value is identical either way, and `out`'s contents are
    // unspecified on a false return regardless (this lets UpsNorthAbove84, spec §11.2 "the first
    // six", run without PROJ).
    if (isNorth) {
        out.band = (lonLat.lon < 0.0) ? QLatin1Char('Y') : QLatin1Char('Z');
    } else {
        out.band = (lonLat.lon < 0.0) ? QLatin1Char('A') : QLatin1Char('B');
    }
    out.x = 0.0;
    out.y = 0.0;

    const ProjConverterPtr conv = sharedFactory().byInitString(initStr);
    if (!conv) {
        return false;
    }
    const auto xy = conv->lonLat2XY(lonLat);
    if (!xy) {
        return false;
    }
    out.x = xy->x();
    out.y = xy->y();
    return true;
}

bool CoordTransformer::upsToWgs84(const UtmCoord& coord, LonLat& out) {
    const char* initStr = coord.isNorth ? kUpsNorth : kUpsSouth;
    const ProjConverterPtr conv = sharedFactory().byInitString(initStr);
    if (!conv) {
        return false;
    }
    const auto ll = conv->xy2LonLat(DoublePoint(coord.x, coord.y));
    if (!ll) {
        return false;
    }
    out = *ll;
    return true;
}

bool CoordTransformer::wgs84ToMgrs(LonLat lonLat, MgrsCoord& out) {
    UtmCoord utm;
    if (!wgs84ToUtm(lonLat, utm) && !wgs84ToUps(lonLat, utm)) {
        return false;
    }

    MgrsCoord result;
    result.zone = utm.zone;
    result.band = utm.band;

    const int col = static_cast<int>(std::floor(utm.x / 100000.0));
    const int row = static_cast<int>(std::floor(utm.y / 100000.0));

    if (result.zone > 0) {
        const int i = result.zone - 1;
        result.digraph[0] = QLatin1Char(kMgrsUtmCols[i % 3][col - 1]);
        const int rowsLen = static_cast<int>(std::strlen(kMgrsUtmRows[0]));
        result.digraph[1] = QLatin1Char(kMgrsUtmRows[i % 2][row % rowsLen]);
    } else {
        int i = -1;
        switch (result.band.toLatin1()) {
        case 'A':
            i = 0;
            break;
        case 'B':
            i = 1;
            break;
        case 'Y':
            i = 2;
            break;
        case 'Z':
            i = 3;
            break;
        default:
            return false;
        }
        result.digraph[0] = QLatin1Char(kMgrsUpsCols[i][col - kFeUps[i]]);
        result.digraph[1] = QLatin1Char(kMgrsUpsRows[i][row - kFnUps[i]]);
    }

    // Trunc, not rounding -- 1 m resolution (spec §6.4).
    result.x = static_cast<int>(std::trunc(utm.x)) % 100000;
    result.y = static_cast<int>(std::trunc(utm.y)) % 100000;

    out = result;
    return true;
}

bool CoordTransformer::mgrsToUtm(const MgrsCoord& mgrs, UtmCoord& out) {
    UtmCoord result;
    result.zone = mgrs.zone;
    result.band = mgrs.band;

    qint64 n2m = 0;
    qint64 e = 0;
    qint64 n = 0;

    if (result.zone > 0) {
        result.isNorth = mgrs.band >= QLatin1Char('N');

        const int i = mgrs.zone - 1;
        const int posCol = findChar(kMgrsUtmCols[i % 3], mgrs.digraph[0]);
        if (posCol < 0) {
            return false;
        }
        e = static_cast<qint64>(posCol + 1) * 100000;

        const int posRow = findChar(kMgrsUtmRows[i % 2], mgrs.digraph[1]);
        if (posRow < 0) {
            return false;
        }
        n = static_cast<qint64>(posRow) * 100000;

        const int posBand = findChar(kMgrsLatBands, mgrs.band);
        if (posBand < 0) {
            return false;
        }
        const int latMax = (posBand - 10) * 8;

        UtmCoord probe;
        if (!wgs84ToUtm(LonLat{3.0, static_cast<double>(latMax)}, probe)) {
            return false;
        }
        const qint64 nBand = static_cast<qint64>(std::trunc(probe.y / 100000.0)) * 100000;

        while (n2m + n + mgrs.y < nBand) {
            n2m += 2000000;
        }
    } else {
        result.isNorth = (mgrs.band == QLatin1Char('Y') || mgrs.band == QLatin1Char('Z'));

        int i = -1;
        switch (mgrs.band.toLatin1()) {
        case 'A':
            i = 0;
            break;
        case 'B':
            i = 1;
            break;
        case 'Y':
            i = 2;
            break;
        case 'Z':
            i = 3;
            break;
        default:
            return false;
        }

        const int posCol = findChar(kMgrsUpsCols[i], mgrs.digraph[0]);
        if (posCol < 0) {
            return false;
        }
        e = static_cast<qint64>(posCol + kFeUps[i]) * 100000;

        const int posRow = findChar(kMgrsUpsRows[i], mgrs.digraph[1]);
        if (posRow < 0) {
            return false;
        }
        n = static_cast<qint64>(posRow + kFnUps[i]) * 100000;
    }

    result.x = static_cast<double>(e + mgrs.x);
    result.y = static_cast<double>(n2m + n + mgrs.y);

    out = result;
    return true;
}

bool CoordTransformer::mgrsToWgs84(const MgrsCoord& mgrs, LonLat& out) {
    UtmCoord utm;
    if (!mgrsToUtm(mgrs, utm)) {
        return false;
    }
    if (utm.zone > 0) {
        return utmToWgs84(utm, out);
    }
    return upsToWgs84(utm, out);
}

bool CoordTransformer::strToMgrs(const QString& text, MgrsCoord& out) {
    QString s = toUpperAscii(text);
    s.remove(QLatin1Char(' '));
    const qsizetype len = s.size();
    if (len < 13) {
        return false;
    }

    qsizetype i = 0;
    while (i < len && isAsciiDigit(s.at(i))) {
        ++i;
    }
    if (i > 2) {
        return false;
    }

    MgrsCoord result;
    result.zone = 0;
    if (i > 0) {
        bool ok = false;
        result.zone = s.left(i).toInt(&ok);
        if (!ok || result.zone < 1 || result.zone > 60) {
            return false;
        }
    }

    result.band = s.at(i);
    result.digraph[0] = s.at(i + 1);
    result.digraph[1] = s.at(i + 2);

    for (qsizetype p = i + 3; p < len; ++p) {
        if (!isAsciiDigit(s.at(p))) {
            return false;
        }
    }

    // Legacy splits the remaining digits floor (easting) / ceil (northing); its final `I = VLen`
    // check is a tautology and never actually rejects an odd-length tail (docs/handoffs/
    // T-0019B1.md "legacy-vs-spec conflicts", confirmed by the B1 review).
    const qsizetype prefixLen = i + 3;
    const qsizetype digitsLen = len - prefixLen;
    const qsizetype eastingLen = digitsLen / 2;

    bool eastingOk = false;
    bool northingOk = false;
    const int easting = s.mid(prefixLen, eastingLen).toInt(&eastingOk);
    const int northing = s.mid(prefixLen + eastingLen).toInt(&northingOk);
    // D-25 (docs/parity/deviations.md): legacy's StrToInt raises on overflow, unhandled; the port
    // fails cleanly instead (same ruling as coord_from_string.cpp's zone/MGRS StrToInt sites).
    if (!eastingOk || !northingOk) {
        return false;
    }

    result.x = easting;
    result.y = northing;
    out = result;
    return true;
}

} // namespace cg::geo
