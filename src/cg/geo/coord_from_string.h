// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_CoordFromStringParser.pas, u_CoordFromStringParser.pas, the str2r half of
// u_GeoToStrFunc.pas. docs/specs/geo.md:2087-2164 (§6.3 incl. §6.3.1-§6.3.3). FEAT-GEO-008.
//
// UTM/Gauss-Kruger (§6.3.2) and MGRS (§6.3.3) text syntax is scanned and range-checked here; the
// projected/grid -> WGS84 transform itself (T-0019B2, coord_transformer.h) is delegated to a
// CoordTransformer member, exactly as legacy's TCoordFromStringParser holds its own FTransformer
// (u_CoordFromStringParser.pas:38, u_CoordTransformer.pas).

#include "cg/geo/coord_representation.h"
#include "cg/geo/coord_transformer.h"
#include "cg/geo/geo_types.h"

#include <QString>

namespace cg::geo {

// Ports TCoordFromStringParser (u_CoordFromStringParser.pas). Not an interface: one implementation
// (ICoordFromStringParser's DI wrapper is `ui`'s, like CoordToStringConverter's own precedent,
// coord_to_string.h).
class CoordFromStringParser {
public:
    explicit CoordFromStringParser(const CoordRepresentationSettings& settings);

    // TryStrToCoord(ALon, ALat), u_CoordFromStringParser.pas:171-203. Geographic: two already-
    // separated, already-labelled strings. **Lat/lon order is never inferred** -- the caller
    // supplies two labelled strings; `isLatitude` (baked into which argument is which) only selects
    // the 90 deg/180 deg range limit (§6.3 intro). Returns false on a range violation, or when the
    // Sk42/Gsk2011 geog_to_wgs84 transform fails (PROJ unavailable or out of range).
    [[nodiscard]] bool tryStrToCoord(const QString& lon, const QString& lat, LonLat& out) const;

    // TryStrToCoord(AX, AY, AZone), u_CoordFromStringParser.pas:205-302. UTM / Gauss-Kruger:
    // easting, northing, and a zone string ("<digits><N|S>"). Zone syntax and range are validated
    // here (§6.3.2); the projected -> WGS84 transform is CoordTransformer's.
    [[nodiscard]] bool tryStrToCoord(const QString& x, const QString& y, const QString& zone,
                                     LonLat& out) const;

    // TryStrToCoord(AStr), u_CoordFromStringParser.pas:304-321. MGRS: one string. str_to_mgrs's text
    // grammar (Proj4.UTM.pas:368-432, §6.3.3) is validated here; mgrs_to_wgs84 is CoordTransformer's.
    [[nodiscard]] bool tryStrToCoord(const QString& s, LonLat& out) const;

private:
    CoordRepresentationSettings settings_;
    CoordTransformer transformer_;
};

} // namespace cg::geo
