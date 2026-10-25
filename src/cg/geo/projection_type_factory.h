// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ProjectionTypeFactory.pas, u_ProjectionTypeFactorySimple.pas:70-220
//
// Resolves a ZMP's `projection=`/`EPSG=`/`sradiusa=`/`sradiusb=` keys to one of the four interned
// projection types, or synthesizes an ad-hoc one for a non-standard radius (docs/specs/geo.md
// §2.4 -- "the +/-1 m rule"). Immutable snapshot, no locks (§8.1).

#include "cg/core/config_provider.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_type.h"

#include <QtGlobal>

#include <memory>
#include <stdexcept>

namespace cg::geo {

// Ports the SAS_ERR_MapProjectionUnexpectedType exception (u_ResStrings.pas).
class UnexpectedProjectionType : public std::runtime_error {
public:
    explicit UnexpectedProjectionType(int code)
        : std::runtime_error("Unexpected map projection type: " + std::to_string(code)) {}
};

class ProjectionTypeFactory {
public:
    ProjectionTypeFactory(HashSeed hash, std::shared_ptr<const DatumFactory> datums);

    // Throws cg::geo::UnexpectedProjectionType for an unknown EPSG code.
    [[nodiscard]] ProjectionTypePtr byCode(int projectionEpsg) const;

    // Reads EPSG / projection / sradiusa / sradiusb from a ZMP params.txt section (docs/specs/geo.md
    // §2.4). `config` should already be scoped to the `[PARAMS]` section (e.g. via
    // `core::IConfigProvider::subItem(u"PARAMS")`) and may be nullptr (all defaults apply, as
    // legacy's `AConfig <> nil` guard).
    [[nodiscard]] ProjectionTypePtr byConfig(const core::IConfigProvider* config) const;

private:
    HashSeed hash_;
    std::shared_ptr<const DatumFactory> datums_;
    ProjectionTypePtr google_;     // MercatorSphereProjectionType, datum 6055, EPSG 3857
    ProjectionTypePtr esri53004_;  // MercatorSphereProjectionType, datum 53004, EPSG 53004
    ProjectionTypePtr yandex_;     // MercatorEllipsoidProjectionType, datum 6326, EPSG 3395
    ProjectionTypePtr geographic_; // GeographicProjectionType, datum 6326, EPSG 4326
};

} // namespace cg::geo
