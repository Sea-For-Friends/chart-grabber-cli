// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: Src/CoordConvert/u_ProjConverterByProj4.pas:127-150 (LonLat2XY) and
// :152-175 (XY2LonLat): the converter raises when PROJ fails, the script sees a runtime error.
// Ports: nothing.
//
// Legacy has one IProjConverterFactory; the port has two: geo::ProjConverterFactory (geo/proj_converter.h,
// returns std::optional) and script::IProjConverterFactory (script/script_host.h, returns plain values).
// The adapter wraps the first and implements the second. docs/specs/app-cli.md 3.2.

#include "cg/geo/proj_converter.h"
#include "cg/script/script_host.h"

#include <QByteArray>

#include <memory>

namespace cg::app {

class ProjConverterFactoryAdapter final : public script::IProjConverterFactory {
public:
    explicit ProjConverterFactoryAdapter(std::shared_ptr<const geo::ProjConverterFactory> factory);

    // nullptr when the geo factory returns nullptr (PROJ unavailable, unknown code): the script sees nil.
    [[nodiscard]] std::shared_ptr<const script::IProjConverter> byEpsg(int epsg) const override;
    [[nodiscard]] std::shared_ptr<const script::IProjConverter>
    byInitString(const QByteArray& args) const override;

private:
    std::shared_ptr<const geo::ProjConverterFactory> factory_;
};

} // namespace cg::app
