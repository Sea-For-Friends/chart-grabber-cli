// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Includes/Proj4/Proj4.Defines.pas, verbatim (docs/specs/geo.md §7.2, ADR-0006 decision
// 4). CRS objects are always built from these proj-strings, never from a bare EPSG code, so the
// datum shift is the Helmert one the Windows build used.
//
// Included by proj/proj_converter.cpp (the only place these strings are fed to PROJ) and by
// proj_converter_test.cpp (to build oracles and pass known init strings directly).
//
// FEAT-GEO-003: kSk42/kGsk2011 are the SK-42/GSK-2011 Helmert-shift datum strings.

#include <QByteArray>

namespace cg::geo {

// EPSG:4326 = WGS 84 - WGS84 - World Geodetic System 1984, used in GPS
inline constexpr char kWgs84[] = "+proj=longlat +datum=WGS84 +no_defs +type=crs";

// ESRI:53004 = Sphere Mercator
inline constexpr char kEsri53004[] = "+proj=merc +lat_ts=0 +lon_0=0 +k=1 +x_0=0 +y_0=0 "
                                     "+a=6371000 +b=6371000 +units=m +no_defs +type=crs";

// EPSG:3857 = WGS 84 / Pseudo-Mercator - Google Maps, OpenStreetMap, Bing, ArcGIS, ESRI
inline constexpr char kEpsg3857[] = "+proj=merc +a=6378137 +b=6378137 +lat_ts=0 +lon_0=0 +x_0=0 "
                                    "+y_0=0 +k=1 +units=m +nadgrids=@null +wktext +no_defs +type=crs";

// EPSG:3395 = WGS 84 / World Mercator
inline constexpr char kEpsg3395[] = "+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 "
                                    "+units=m +no_defs +type=crs";

// EPSG:4269 = NAD83
inline constexpr char kNad83[] = "+proj=longlat +ellps=GRS80 +datum=NAD83 +no_defs";

// GOST P 51794-2008 (http://www.sasgis.org/mantis/view.php?id=3179)
inline constexpr char kSk42[] = "+proj=longlat +ellps=krass "
                                "+towgs84=23.57,-140.95,-79.8,0,0.35,0.79,-0.22 +no_defs";

// EPSG:2463-2491 = Pulkovo 1995 / Gauss-Kruger CM; EPSG:2492-2522 = Pulkovo 1942 / Gauss-Kruger CM
inline constexpr char kSk42GkFmt[] = "+proj=tmerc +lat_0=0 +lon_0=%1 +k=1 +x_0=%2 +y_0=%3 "
                                     "+ellps=krass +units=m +no_defs";

// GSK-2011
inline constexpr char kGsk2011[] = "+proj=longlat +a=6378136.5 +rf=298.2564151 "
                                   "+towgs84=0.013,-0.092,-0.03,-0.001738,0.003559,-0.004263,0.0074 "
                                   "+no_defs";

// GSK-2011 / Gauss-Kruger CM
inline constexpr char kGsk2011GkFmt[] = "+proj=tmerc +lat_0=0 +lon_0=%1 +k=1 +x_0=%2 +y_0=%3 "
                                        "+a=6378136.5 +rf=298.2564151 +units=m +no_defs";

// EPSG:32601-32660 = WGS 84 / UTM Zone North
inline constexpr char kUtmNorthFmt[] = "+proj=utm +zone=%1 +ellps=WGS84 +datum=WGS84 +units=m +no_defs";

// EPSG:32701-32760 = WGS 84 / UTM Zone South
inline constexpr char kUtmSouthFmt[] =
    "+proj=utm +zone=%1 +south +ellps=WGS84 +datum=WGS84 +units=m +no_defs";

// EPSG:32661 = WGS 84 / UPS North
inline constexpr char kUpsNorth[] = "+proj=stere +lat_0=90 +lon_0=0 +k=0.994 +x_0=2000000 "
                                    "+y_0=2000000 +datum=WGS84 +units=m +no_defs +type=crs";

// EPSG:32761 = WGS 84 / UPS South
inline constexpr char kUpsSouth[] = "+proj=stere +lat_0=-90 +lon_0=0 +k=0.994 +x_0=2000000 "
                                    "+y_0=2000000 +datum=WGS84 +units=m +no_defs +type=crs";
// kSk42, kGsk2011, kGsk2011GkFmt, kUpsNorth, kUpsSouth are not reachable from proj4ArgsByEpsg
// below in legacy either -- CoordTransformer (out of scope, T-0019B) uses them directly.

// Ports Proj4ArgsByEpsg (Proj4.Defines.pas:53-96). Returns an empty QByteArray for a code legacy
// did not know; the caller then falls through to "EPSG:<code>" via proj.db (ADR-0006 decision 5:
// the legacy `+init=epsg:` fallback and share/proj/epsg are both dead under PROJ 9).
[[nodiscard]] QByteArray proj4ArgsByEpsg(int epsg);

} // namespace cg::geo
