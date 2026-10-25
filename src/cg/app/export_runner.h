// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
// New code (no legacy unit). Inherits: RegionProcess/u_ExportProviderAbstract.pas:182-202 (call sequence).
// docs/specs/app-cli.md 1.5, 3.10, 5.2; docs/specs/region-export.md 9.
//
// One MBTiles export on its own thread into "<file>.part"; the file is renamed to its name only after the
// empty-result check (C-10) passed. An interrupted or failed export deletes the .part file and its SQLite
// side files (C-13). Prints no result line (the caller does).

#include "cg/app/app_context.h"
#include "cg/app/console.h"
#include "cg/app/job.h"
#include "cg/app/job_checks.h"
#include "cg/geo/geometry_lonlat.h"

#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <stop_token>

namespace cg::app {

enum class ExportOutcome : std::uint8_t { Written, Empty, Interrupted, Failed };

struct ExportResult {
    ExportOutcome outcome = ExportOutcome::Failed;
    qint64 tiles = 0;
    qint64 fileSize = 0;
    QString error;
};

[[nodiscard]] ExportResult exportSource(const ResolvedSource& source, const Job& job,
                                        const std::shared_ptr<const geo::IGeometryLonLatPolygon>& area,
                                        AppContext& app, Console& console,
                                        const std::stop_token& termination);

} // namespace cg::app
