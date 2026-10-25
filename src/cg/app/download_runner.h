// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: frm_ProgressDownload.pas:268-279, 306-393, 431-437, 586-595 (the
// window's loop and its buttons, without the window). Ports: nothing. docs/specs/app-cli.md 3.9, 5.2, 6.3;
// docs/specs/region-download.md section 7 (caller obligations).
//
// Starts or resumes the runs of one source, polls them once a second like the window's timer, applies the
// unattended policy of app-cli.md 1.8 where the window had a human, keeps the sessions of 1.9 and infers why
// the runs ended (6.3).

#include "cg/app/app_context.h"
#include "cg/app/console.h"
#include "cg/app/job.h"
#include "cg/app/job_checks.h"
#include "cg/core/operation_notifier.h"
#include "cg/region/region_process_progress_info_download.h"
#include "cg/region/tiles_download.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace cg::app {

enum class DownloadOutcome : std::uint8_t {
    Completed,
    Interrupted,
    Stalled,
    SaveFailed,
    WorkerFailed,
    FrameNotServed, // T-0356: stopped because the newest young frame has no served tile
                    // (source.frameFallback)
    Refused         // resume: no file could be started (exit 3); runCli checks the files first, so it stays
                    // unreachable there
};

struct DownloadPolicy {
    int stallTimeoutSec = 300; // 0 = never
    int saveRetries = 3;
    int maxConnectionErrors = 60; // T-0271; 0 = never
    int resumeDelaySec = 5;
    int pollMs = 1000;
    int notServedAnswers = 3; // T-0356: answers in a row of the class "not served yet" that stop a run
};

// What the runner needs of one started run. `owner` keeps the run (and so its worker thread) alive;
// destroying it joins the worker, so the runner cancels first (region-download.md section 7).
struct StartedRun {
    std::shared_ptr<region::IRegionProcessProgressInfoDownload> progress;
    std::shared_ptr<core::OperationNotifier> cancelNotifier;
    std::shared_ptr<void> owner;
};

// The test seam: production starts runs through region::ProviderTilesDownload (the default starter).
class IRunStarter {
public:
    virtual ~IRunStarter() = default;
    [[nodiscard]] virtual std::vector<StartedRun> startProcess(const region::PolygonPtr& area,
                                                               const region::TilesDownloadParams& params) = 0;
    [[nodiscard]] virtual StartedRun startBySls(const QString& fileName, bool startPaused) = 0; // may throw
};

class DownloadRunner final {
public:
    // `starter` empty = ProviderTilesDownload over the maps of `app` (created at the first run / resume).
    DownloadRunner(AppContext& app, Console& console, const Job& job, std::stop_token termination,
                   DownloadPolicy policy = {}, std::shared_ptr<IRunStarter> starter = {});

    [[nodiscard]] DownloadOutcome run(const ResolvedSource& source,
                                      const std::shared_ptr<const geo::IGeometryLonLatPolygon>& area);
    // `sourceNames[i]` is the name of the map of `slsFiles[i]` (the progress line shows it).
    [[nodiscard]] DownloadOutcome resume(const QStringList& slsFiles, const QStringList& sourceNames);

    // app-cli.md 1.9: the first 12 hex digits of SHA-1 over the inputs that decide what a session means.
    [[nodiscard]] static QString jobId(const Job& job, const maptype::IMapType& map,
                                       const geo::IGeometryLonLatPolygon& area,
                                       std::span<const quint8> zooms);

private:
    void printSessionRecord(const QString& file, const QDateTime& time);

    AppContext& app_;
    Console& console_;
    const Job& job_;
    std::stop_token termination_;
    DownloadPolicy policy_;
    std::shared_ptr<IRunStarter> starter_;
};

} // namespace cg::app
