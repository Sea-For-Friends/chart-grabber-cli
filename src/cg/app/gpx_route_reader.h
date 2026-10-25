// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Ports: nothing.
// Inherits: Vector/u_VectorItemTreeImporterXML.pas:162-186, 757-857;
// VectorLoaders/u_XmlVectorObjects.pas:356-362, 490-494 (element handling);
// the closed vsagps parser is replaced (ADR-0009 row 4c).
//
// Own minimal GPX reader: routes (rte) and tracks (trk) as lines only. Waypoints, metadata and
// every extension are skipped. Deviation from the mark importer (no register row, the importer
// is not ported): a part with exactly one point is dropped here (a corridor needs a line); the
// importer keeps it in the mark.

#include "cg/core/result.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"

#include <QIODevice>
#include <QString>

#include <vector>

namespace cg::app {

struct GpxRoute {
    QString name;
    bool isTrack = false;
    std::vector<std::vector<geo::LonLat>> parts; // each with two or more points

    // The parts joined with the NaN separator through the factory's line constructor.
    [[nodiscard]] geo::GeometryLonLatPtr toLine(const geo::GeometryLonLatFactory& factory) const;
};

struct GpxReadResult {
    std::vector<GpxRoute> routes; // all routes in file order, then all tracks in file order
    int skippedPoints = 0;        // points with a missing / non-numeric / out-of-range lat or lon
    int droppedParts = 0;         // parts with exactly one point
};

// FEAT-GEO-019
[[nodiscard]] core::Result<GpxReadResult> readGpxRoutes(QIODevice& device);

} // namespace cg::app
