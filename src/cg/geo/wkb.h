// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GeometryFromStream.pas, i_GeometryToStream.pas, u_GeometryFromWKB.pas,
// u_GeometryToWKB.pas (docs/specs/geo.md §3.7), plus the JSON meta sidecar
// (u_GeometryMetaToStreamJson.pas, u_GeometryMetaFromStreamJson.pas, u_GeometryMetaJson.pas).
// FEAT-GEO-012.
//
// The WKB dialect is a restricted OGC WKB 1.0 subset, byte-level frozen because `marks`' SQLite
// backend writes it: little-endian only, XY-only points, uint32 counts with legacy's overflow
// guards, rings written CLOSED on the wire (one more vertex than IGeometryLonLatContour::points()
// reports) with no de-duplication performed by the reading code itself, and a degenerate Multi*
// (count == 1) written as the plain single type. See geo.md §3.7's nine rules; wkb.cpp cites each
// by number, and geometry_lonlat.h documents why the extra wire vertex never becomes an extra
// points()-visible vertex after a read.

#include "cg/core/result.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"

#include <QByteArray>
#include <QByteArrayView>

namespace cg::geo {

// Parse one geometry from a WKB blob. `meta`, when non-null, is the flattened elevation/timestamp
// sidecar indexed as if the geometry were the flattened NaN-separated stream (rule 9); polygons
// never receive meta. A malformed blob (short read, bad byte order, unsupported/reserved type
// code, an overflow-guard trip) fails rather than crashing -- a `marks` row with a corrupt blob
// must be skippable, not fatal (core.md §2.3).
// FEAT-GEO-012
[[nodiscard]] core::Result<GeometryLonLatPtr>
parseWkb(QByteArrayView wkb, const GeometryLonLatFactory& factory, const PointsMeta* meta = nullptr);

[[nodiscard]] QByteArray writeWkb(const IGeometryLonLat& geometry);

// The literal ASCII prefix "JSON" followed by {"v":1,"t":1,"g":[{"c":<count>,"m":[...]}, ...]} --
// one "g" element per sub-line of a (multi)line, "m" the per-point elevation/timestamp tags
// (empty when that sub-line has none). Points and polygons never carry meta -- an empty
// QByteArray is returned for them. Key order is written out byte-for-byte matching
// TGeometryMetaToStreamJson.DoSaveLines (u_GeometryMetaToStreamJson.pas:100-174) -- v/t/g at the
// root, c/m per geometry, t/n/d per meta tag -- NOT QJsonObject's (sorted) default order.
// u_GeometryMetaToStreamJson.pas / u_GeometryMetaFromStreamJson.pas.
[[nodiscard]] QByteArray writeMetaJson(const IGeometryLonLat& geometry);

// An empty payload parses to "no meta" (an empty PointsMeta, `isEmpty() == true`), not to an
// error -- so does a payload that fails the magic-prefix check (IsMagicOk in legacy). Only a
// payload that has the "JSON" prefix but is not valid JSON, or omits the required fields, fails.
[[nodiscard]] core::Result<PointsMeta> parseMetaJson(QByteArrayView data);

} // namespace cg::geo
