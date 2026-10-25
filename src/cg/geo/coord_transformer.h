// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_CoordTransformer.pas, Includes/Proj4/{Proj4.UTM,Proj4.GaussKruger}.pas.
// docs/specs/geo.md:2178-2238 (§6.4). FEAT-GEO-005, FEAT-GEO-006, FEAT-GEO-007.
//
// The zone/band/digraph arithmetic (UTM zone, MGRS square letters, Gauss-Kruger zone/false
// easting-northing) is ported natively -- table lookup and integer maths, not projection. Only
// the actual coordinate transform goes through IProjConverter (T-0020, proj_converter.h): SK-42/
// GSK-2011 geographic through one WGS84-anchored converter per datum, built once in the
// constructor; UTM/UPS/Gauss-Kruger projected through a converter built per call from the
// zone-specific init string (interned by ProjConverterFactory, so repeated calls for the same
// zone reuse the same PROJ object -- T-0020 D-10).
//
// The Gauss-Kruger projected transform chains two IProjConverter calls exactly as legacy's
// TGaussKruger.wgs84_to_proj/proj_to_wgs84 chain two separate proj4 CS conversions: WGS84 <->
// SK-42/GSK-2011 *geographic* (a real Helmert datum shift, `kSk42`/`kGsk2011`, which declare
// +towgs84) and SK-42/GSK-2011 geographic <-> Gauss-Kruger *projected* (a pure map projection --
// `kSk42GkFmt`/`kGsk2011GkFmt` declare no +towgs84 at all, only the krass/GSK-2011 ellipsoid, so
// PROJ applies no further datum shift there, matching legacy's own two-hop design). Both legs use
// the same WGS84-anchored ProjConverterFactory::byInitString seam as every other CRS in this
// project (ADR-0006); the second leg is simply fed the already-shifted geographic degrees instead
// of true WGS84 degrees.
//
// PROJ init strings are passed through exactly as legacy builds them (proj4_defines.h, T-0020).
// When PROJ is unavailable (IProjConverter::isAvailable() == false, or a specific CRS fails to
// build) every method here returns false, matching legacy's own failure sentinel for a failed
// proj4 call.

#include "cg/geo/coord_representation.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/proj_converter.h"

#include <QChar>
#include <QString>

namespace cg::geo {

// TUtmCoord, Proj4.UTM.pas:6-12.
struct UtmCoord {
    int zone = 0;
    QChar band;
    bool isNorth = false;
    double x = 0.0; // Easting
    double y = 0.0; // Northing
};

// TMgrsCoord, Proj4.UTM.pas:14-20.
struct MgrsCoord {
    int zone = 0;
    QChar band;
    QChar digraph[2] = {};
    int x = 0; // Easting
    int y = 0; // Northing
};

// TGaussKrugerCoord, Proj4.GaussKruger.pas:6-11.
struct GaussKrugerCoord {
    int zone = 0;
    bool isNorth = false;
    double x = 0.0;
    double y = 0.0;
};

// Ports TCoordTransformer (u_CoordTransformer.pas) plus the single-coordinate free functions of
// Proj4.UTM.pas / Proj4.GaussKruger.pas -- those need no persistent per-datum state, so they are
// ported as static members, matching the header shape in docs/specs/geo.md §6.4.
class CoordTransformer {
public:
    CoordTransformer(); // builds the SK-42 and GSK-2011 geographic engines

    // wgs84_to_geog / geog_to_wgs84 (TGaussKruger, Proj4.GaussKruger.pas:105-113). `type` must be
    // Sk42, Sk42Gk, Gsk2011 or Gsk2011Gk -- any other CoordSysType is a caller-contract violation
    // (legacy: FItems[cstWGS84/cstUTM/cstMGRS] = nil, Assert(Result <> nil) in GetItem).
    [[nodiscard]] bool wgs84ToGeographic(CoordSysType type, LonLat& inOut) const;
    [[nodiscard]] bool geographicToWgs84(CoordSysType type, LonLat& inOut) const;

    // wgs84_to_proj / proj_to_wgs84 (TGaussKruger, Proj4.GaussKruger.pas:126-143). `type` must be
    // Sk42Gk or Gsk2011Gk (or the plain Sk42/Gsk2011 -- legacy indexes the same FItems[] slot for
    // both, §6.4's FEAT-GEO-006).
    [[nodiscard]] bool wgs84ToGaussKruger(CoordSysType type, LonLat lonLat, GaussKrugerCoord& out) const;
    [[nodiscard]] bool gaussKrugerToWgs84(CoordSysType type, const GaussKrugerCoord& coord,
                                          LonLat& out) const;

    // geodetic_wgs84_to_utm / utm_to_wgs84 (Proj4.UTM.pas:145-170). wgs84ToUtm is false outside
    // -80 < lat < 84 (the UTM latitude band range); utmToWgs84 falls through to UPS for zone 0.
    [[nodiscard]] static bool wgs84ToUtm(LonLat lonLat, UtmCoord& out);
    [[nodiscard]] static bool utmToWgs84(const UtmCoord& coord, LonLat& out);

    // geodetic_wgs84_to_ups / ups_to_wgs84 (Proj4.UTM.pas:172-219). Switch latitude: `lat >= 84`
    // north, `lat <= -80` south (docs/specs/geo.md §6.4's "legacy bug, fixed here" / §10 D-9 --
    // legacy tests `<= 80`, unobservable on any input that actually reaches this path).
    [[nodiscard]] static bool wgs84ToUps(LonLat lonLat, UtmCoord& out);
    [[nodiscard]] static bool upsToWgs84(const UtmCoord& coord, LonLat& out);

    // geodetic_wgs84_to_mgrs (Proj4.UTM.pas:221-263): tries UTM, then UPS.
    [[nodiscard]] static bool wgs84ToMgrs(LonLat lonLat, MgrsCoord& out);
    // mgrs_to_utm (Proj4.UTM.pas:265-352).
    [[nodiscard]] static bool mgrsToUtm(const MgrsCoord& mgrs, UtmCoord& out);
    // mgrs_to_wgs84 (Proj4.UTM.pas:354-366).
    [[nodiscard]] static bool mgrsToWgs84(const MgrsCoord& mgrs, LonLat& out);

    // str_to_mgrs (Proj4.UTM.pas:368-432). Pure text grammar, no PROJ needed. Note:
    // coord_from_string.cpp's own MGRS overload ports this same grammar inline (its B1 slice
    // landed before this class existed and needs the intermediate zone/band/digraph/easting/
    // northing locals for its own validation flow, as legacy's TryStrToCoord(AStr) does not --
    // it calls this free function directly, Proj4.UTM.pas-side). This static keeps the CRS-table
    // class' declared surface complete for any other caller (spec §6.4).
    [[nodiscard]] static bool strToMgrs(const QString& text, MgrsCoord& out);

private:
    ProjConverterPtr sk42Geog_;
    ProjConverterPtr gsk2011Geog_;

    [[nodiscard]] ProjConverterPtr geogEngine(CoordSysType type) const;
    [[nodiscard]] static const char* projInitFmt(CoordSysType type);
};

} // namespace cg::geo
