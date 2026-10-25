// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapCoverageInfo.pas, u_MapCoverageInfo.pas
//
// Where a map has data: the ZMP key MapCenter = "lon,lat,zoom" (spec maptype.md §2.6.6).
// Immutable. Legacy leaves bounds/polygon/zoom range as a to-do; so does the port.

#include "cg/geo/geo_types.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::maptype {

// FEAT-NET-002
class MapCoverageInfo final {
public:
    // Any parse failure (not exactly three comma-separated items, bad number) gives an empty
    // centre (geo::emptyDoublePoint) and zoom 0.
    explicit MapCoverageInfo(const QString& center);

    [[nodiscard]] geo::DoublePoint centerPos() const { return centerPos_; }
    // Legacy keeps the zoom as Integer and returns it through a Byte: 256 reads as 0, -1 as 255
    // (docs/parity/legacy-defects.md L-122).
    [[nodiscard]] quint8 centerZoom() const { return static_cast<quint8>(centerZoom_); }

private:
    [[nodiscard]] bool parseCenterValue(const QString& value);

    geo::DoublePoint centerPos_;
    qint32 centerZoom_ = 0;
};
using MapCoverageInfoPtr = std::shared_ptr<const MapCoverageInfo>;

} // namespace cg::maptype
