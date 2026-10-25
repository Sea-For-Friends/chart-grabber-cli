// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: Src/u_GlobalState.pas:1280-1289 (shutdown order: the runs are
// cancelled before the root is destroyed), Src/RegionProcess/Download/frm_ProgressDownload.pas:306-393 (the
// figures the window shows are the ones the console prints),
// CmdLineArgProcessor/u_CmdLineArgProcessorAPI.pas:67-134 (help). Ports: nothing. docs/specs/app-cli.md
// 1, 2.6, 5.2, 6.2.
//
// Everything `main` does after the Qt application exists. Requires an existing QGuiApplication and never
// creates one (a test owns its own).

#include "cg/core/platform/termination_signal.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <stop_token>

namespace cg::app {

// arguments[0] is the program name, as QCoreApplication::arguments(). Returns the exit code of
// app-cli.md 1.11; an interruption returns 130 (main replaces it with the code of the signal that arrived).
// T-0332: `watchStdin` starts the stdin reader (TerminationSignal::watchStdin) when a job command has
// --stop-on-stdin; NotPipe is exit 2, Invalid is a warning and the option is off. Empty = the option is
// inert.
using StdinWatcher = std::function<core::platform::StdinWatch()>;
[[nodiscard]] int runCli(const QStringList& arguments, const std::stop_token& termination,
                         const StdinWatcher& watchStdin = {});

} // namespace cg::app
