// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: u_ThreadDownloadTiles.pas:357-390, 500-575 (one request, result
// classes). Ports: nothing. docs/specs/app-cli.md 1.1 (probe), 3.13, 5.2.
//
// `probe`: for each source that passes the refusals of `sources`, ONE request for the tile under --at at
// --probe-zoom, made the way the worker makes it (the subsystem's getRequestTask, a finish notifier,
// download) and classified the way processResultAndCheckGotoNextTile classifies it. The tile is saved by the
// map's own saver into the map's own cache: the download subsystem is built over the map's storage and offers
// no other.

#include "cg/app/app_context.h"
#include "cg/app/job.h"
#include "cg/maptype/map_type.h"

#include <QString>

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <vector>

namespace cg::app {

enum class ProbeStatus : std::uint8_t { Passed, Failed, Skipped };

struct ProbeRow {
    QString guid, name, result;
    ProbeStatus status = ProbeStatus::Failed;
};

// The three helpers of `sources` and `probe`: the name, the GUID in braces, and the maps whose name, ZMP file
// name or GUID contains `filter` (case-insensitive; empty = all) in name order (GUID breaks a tie).
[[nodiscard]] QString sourceName(const maptype::IMapType& map);
[[nodiscard]] QString guidText(const maptype::IMapType& map);
[[nodiscard]] std::vector<maptype::IMapTypePtr> matchingSources(const AppContext& app, const QString& filter);

// One row per matching source, in name order, until `stop` is requested (the rest has no row). `timeout` is
// the wait for one result (spec: 60 s; a parameter for the tests).
[[nodiscard]] std::vector<ProbeRow>
probeSources(const AppContext& app, const Job& job, const std::stop_token& stop,
             std::chrono::milliseconds timeout = std::chrono::seconds(60));

} // namespace cg::app
