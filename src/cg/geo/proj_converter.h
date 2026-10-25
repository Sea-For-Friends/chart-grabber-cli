// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ProjConverter.pas, u_ProjConverterByProj4.pas, u_ProjConverterFactory.pas.
//
// Custom-CRS support: the datum shifts UTM/MGRS/SK-42/GSK-2011 display depends on, and anything
// else that needs a CRS PROJ knows about but the built-in projection types (projection_type.h)
// don't implement. The interface is unchanged from legacy. Nothing above this header knows PROJ
// exists -- `proj/proj_converter.cpp` is the only translation unit in the project allowed to
// include the PROJ C header (docs/specs/geo.md §7.1, ADR-0006 decision 3).
//
// Legacy raises on failure and returns a bare TDoublePoint; the port returns std::optional
// because CoordToStringConverter already has a defined "NaN" behavior for failure and an
// exception across that boundary would be worse.
//
// FEAT-GEO-002 (custom CRS via PROJ4 strings / EPSG).

#include "cg/geo/geo_types.h"

#include <QByteArray>
#include <QHash> // std::hash<QByteArray> specialization, for cache_ below

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

// CG_HAVE_PROJ is a build-configuration macro (CMake's `find_package(PROJ 9 QUIET)`, mirrored by
// a pkg-config probe in tools/nocmake_build.sh), not a platform #ifdef; it never leaks past this
// header + proj/proj_converter.cpp (docs/specs/geo.md §7 Notes). Absent -> 0, so a build without
// PROJ still compiles this header and the stub factory below.
#ifndef CG_HAVE_PROJ
#define CG_HAVE_PROJ 0
#endif

namespace cg::geo {

class IProjConverter {
public:
    virtual ~IProjConverter() = default;

    [[nodiscard]] virtual std::optional<DoublePoint> lonLat2XY(LonLat lonLat) const = 0;
    [[nodiscard]] virtual std::optional<LonLat> xy2LonLat(DoublePoint xy) const = 0;
};
using ProjConverterPtr = std::shared_ptr<const IProjConverter>;

// Ports TProjConverterFactory. A build without PROJ (CG_HAVE_PROJ=0) still links: isAvailable()
// is false and byEpsg/byInitString always return nullptr, so custom-CRS display is disabled
// rather than the app failing to build or start (ADR-0006 "Consequences").
class ProjConverterFactory {
public:
    ProjConverterFactory();

    // nullptr when PROJ is unavailable or the resulting CRS can't be built.
    [[nodiscard]] ProjConverterPtr byEpsg(int epsg) const;
    // Also accepts the legacy "EPSG:<n>" string prefix (u_ProjConverterFactory.pas:115-131),
    // in which case it defers to byEpsg().
    [[nodiscard]] ProjConverterPtr byInitString(const QByteArray& proj4Args) const;
    [[nodiscard]] bool isAvailable() const noexcept;

private:
    // D-10 / ADR-0006 decision 9: legacy re-parsed the init string on every call (a projection
    // per status-bar update); the port interns one ProjConverter per init string instead, behind
    // a weak_ptr so an unused CRS is freed once nothing references it.
    mutable std::mutex mutex_;
    mutable std::unordered_map<QByteArray, std::weak_ptr<const IProjConverter>> cache_;

    [[nodiscard]] ProjConverterPtr intern(const QByteArray& key, bool isEpsgFallback) const;
};

} // namespace cg::geo
