// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/CoordConvert/u_CoordConverterSimpleByProjectionSet.pas (ICoordConverterSimple,
// i_CoordConverterSimple.pas)
//
// The script's `Converter` variable: tile/lon-lat/metre conversions over one projection set. Zoom
// arguments above 23 address pixel positions at zoom - 8 (:103-109, :139-145).

#include "cg/geo/projection_set.h"
#include "cg/script/script_host.h"

namespace cg::maptype {

class CoordConverterSimpleByProjectionSet final : public script::ICoordConverter {
public:
    // Throws std::invalid_argument for a null set (legacy: Assert, then an access violation).
    explicit CoordConverterSimpleByProjectionSet(geo::ProjectionSetPtr projectionSet);

    [[nodiscard]] script::DoublePoint pos2LonLat(script::Point xy, quint8 zoom) const override;
    [[nodiscard]] script::Point lonLat2Pos(script::DoublePoint ll, quint8 zoom) const override;
    [[nodiscard]] script::DoublePoint lonLat2Metr(script::DoublePoint ll) const override;
    [[nodiscard]] script::DoublePoint metr2LonLat(script::DoublePoint mm) const override;
    [[nodiscard]] qint32 tilesAtZoom(quint8 zoom) const override;
    [[nodiscard]] qint32 pixelsAtZoom(quint8 zoom) const override;
    [[nodiscard]] script::Point tilePos2PixelPos(script::Point xy, quint8 zoom) const override;
    [[nodiscard]] script::Rect tilePos2PixelRect(script::Point xy, quint8 zoom) const override;

private:
    // FProjectionSet.Zooms[index]: legacy's TList raises EListError 'List index out of bounds (N)' for an
    // index of 24 or more; reproduced as std::out_of_range with that text (a script error).
    [[nodiscard]] const geo::IProjection& projection(unsigned index) const;

    geo::ProjectionSetPtr projectionSet_;
    geo::ProjectionTypePtr projectionType_;
};

} // namespace cg::maptype
