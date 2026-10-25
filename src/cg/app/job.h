// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: CmdLineArgProcessor/u_CmdLineArgProcessorAPI.pas:67-134 (switch form,
// help), u_CmdLineArgProcessorHelpers.pas:124-143 (decimal point) Ports: nothing.
// docs/specs/app-cli.md 3.3, 5.2.
//
// The command line and the INI job file of chart-grabber-cli. One option table drives the parser, the job
// file and helpText(). No map, file-content or network access: everything that needs them is checked by later
// units.

#include "cg/app/snapshot.h"
#include "cg/geo/geo_types.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace cg::app {

enum class Command : std::uint8_t { Sources, Estimate, Download, Export, Run, Resume, Probe, Help, Version };
enum class AreaKind : std::uint8_t { None, BBox, PolygonFile, Route };
enum class OutputFormat : std::uint8_t { Text, Tsv };
enum class ProgressMode : std::uint8_t { Auto, Lines, Off };

struct Job {
    Command command = Command::Help;
    QString source, overlay; // as typed
    AreaKind areaKind = AreaKind::None;
    geo::LonLatRect bbox;
    QString polygonFile, routeFile, routeName;
    double corridorNm = 0;
    std::vector<quint8> zooms;        // 0-based, sorted, unique
    std::optional<SnapshotTime> time; // --time (T-0283); nullopt = latest
    QString out, overlayOut, name, description, attribution;
    bool overwrite = false;
    int threads = 1;
    bool restart = false;
    bool replace = false, replaceIfSizeDiffers = false, retryMissing = false;
    QDateTime replaceOlderThan, retryMissingOlderThan; // invalid = off
    int autosaveMin = 5, stallTimeoutSec = 300, saveRetries = 3, assumeTileKb = 20, maxConnectionErrors = 60;
    bool strict = false, allowWebp = false;
    QString proxy, cacheDir;
    QStringList mapsDirs; // --maps, absolute, in the order given (app-cli.md 1.6.1; T-0291)
    QStringList slsFiles; // resume, as typed
    QString filter;       // sources, probe
    geo::LonLat probeAt{0, 0};
    int probeZoom = 10;
    int verbosity = 0;        // -q = -1, -v = 1
    bool stopOnStdin = false; // T-0332: --stop-on-stdin
    bool progress = true;     // progressMode != Off: what the Console takes
    OutputFormat format = OutputFormat::Text;
    ProgressMode progressMode = ProgressMode::Auto;
};

struct CliError {
    int exitCode = 2;
    QString message;
};

// arguments[0] is the program name, as QCoreApplication::arguments(). Exit code 2 on a usage error, 3 when
// two areas are given (S-3).
[[nodiscard]] std::expected<Job, CliError> parseJob(const QStringList& arguments);
[[nodiscard]] QString helpText();
// "8-12,14": 0-based items, each N or A-B (A <= B), 0..23; sorted, unique. The error is the message without
// the option.
[[nodiscard]] std::expected<std::vector<quint8>, QString> parseZoomList(const QString& text);

} // namespace cg::app
