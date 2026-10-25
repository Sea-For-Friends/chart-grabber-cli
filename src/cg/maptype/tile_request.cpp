// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_request.h"

#include <utility>

namespace cg::maptype {

TileRequest::TileRequest(geo::PointI tile, quint8 zoom, storage::MapVersionInfoPtr versionInfo)
    : tile_(tile), zoom_(zoom), versionInfo_(std::move(versionInfo)) {}

geo::PointI TileRequest::tile() const {
    return tile_;
}

quint8 TileRequest::zoom() const {
    return zoom_;
}

storage::MapVersionInfoPtr TileRequest::versionInfo() const {
    return versionInfo_;
}

} // namespace cg::maptype
