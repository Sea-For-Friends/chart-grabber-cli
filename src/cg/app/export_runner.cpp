// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: nothing. See export_runner.h.
#include "cg/app/export_runner.h"

#include "cg/app/machine_output.h"
#include "cg/app/probe.h"
#include "cg/app/snapshot.h"
#include "cg/core/platform/file_ops.h"
#include "cg/region/export_mbtiles.h"
#include "cg/region/export_provider_mbtiles.h"
#include "cg/region/region_process_progress_info.h"
#include "cg/storage/sqlite/sqlite_db.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <thread>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(ExportRunner)
};

constexpr auto kPollStep = std::chrono::seconds(1);

// C-13: the .part file and the side files SQLite may have left.
void removePartFiles(const QString& part) {
    for (const char* suffix : {"", "-journal", "-wal", "-shm"}) {
        QFile::remove(part + QLatin1String(suffix));
    }
}

ExportResult failed(const QString& part, ExportOutcome outcome, const QString& error) {
    removePartFiles(part);
    ExportResult r;
    r.outcome = outcome;
    r.error = error;
    return r;
}

// C-10: the number of rows of `tiles`, 0 for none.
core::Result<qint64> countTiles(const QString& part) {
    auto db = storage::sqlite::SqliteDb::open(part, storage::sqlite::SqliteDb::OpenMode::ReadOnly);
    if (!db) {
        return std::unexpected(db.error());
    }
    {
        auto stmt = db->prepare("SELECT 1 FROM tiles LIMIT 1");
        if (!stmt) {
            return std::unexpected(stmt.error());
        }
        auto step = stmt->step();
        if (!step) {
            return std::unexpected(step.error());
        }
        if (*step == storage::sqlite::SqliteStmt::StepResult::Done) {
            return qint64{0};
        }
    }
    auto stmt = db->prepare("SELECT count(*) FROM tiles");
    if (!stmt) {
        return std::unexpected(stmt.error());
    }
    auto step = stmt->step();
    if (!step) {
        return std::unexpected(step.error());
    }
    return stmt->columnInt64(0);
}

} // namespace

ExportResult exportSource(const ResolvedSource& source, const Job& job,
                          const std::shared_ptr<const geo::IGeometryLonLatPolygon>& area, AppContext& app,
                          Console& console, const std::stop_token& termination) {
    // 1. the parameters of the frame's controls (app-cli.md 3.10)
    region::ExportMbtilesParams params;
    (source.asOverlay ? params.overlay : params.map) = source.map;
    const QString part = source.outFile + QLatin1String(".part");
    params.path = QFileInfo(part).absoluteFilePath(); // T-0172: the task wants an absolute path
    params.zooms = source.zooms;
    params.name = source.name;
    if (source.snapshot && source.snapshot->time.isValid() &&
        (source.asOverlay || job.name.trimmed().isEmpty())) {
        // T-0327: the frame time in the default name, so that repeated exports differ; --name wins unchanged.
        // T-0330: also for an overlay; --name names the base only (resolveJob), so it never stops an overlay.
        params.name += QStringLiteral(" ") + snapshotTimeText(*source.snapshot);
    }
    params.description = job.description;
    if (params.description.isEmpty() && source.snapshot) {
        // T-0249 (Q-W4): the default description of a snapshot export; legacy leaves it empty
        // (fr_ExportMBTiles.pas:336-338). S-11: the apostrophe is removed from a derived text.
        params.description =
            sourceName(*source.map) + QStringLiteral(", snapshot ") + snapshotTimeText(*source.snapshot);
        if (params.description.contains(u'\'')) {
            params.description.remove(u'\'');
            console.warning(Tr::tr("the apostrophe is removed from the description of \"%1\"")
                                .arg(sourceName(*source.map)));
        }
    }
    params.attribution = source.attribution;
    if (job.attribution.trimmed().isEmpty()) {
        // T-0249 (Q-W3): {year} of the default attribution (--attribution stays verbatim); the snapshot's
        // UTC year, else the current one. The apostrophe was removed by resolveJob (S-11).
        const bool known = source.snapshot && source.snapshot->time.isValid();
        params.attribution =
            withYear(params.attribution,
                     (known ? source.snapshot->time.toUTC() : QDateTime::currentDateTimeUtc()).date().year());
    }
    params.forceDropTarget = true; // C-12
    params.replaceExistingTiles = true;
    params.useXYZScheme = false; // C-4
    params.makeTileMillCompatibility = false;

    // 2. a stale file of an earlier run
    removePartFiles(part);

    // 3. u_ExportProviderAbstract.pas:182-202
    const bool records = job.progressMode == ProgressMode::Lines;
    // IMapType::guid(), as the download runner: the test doubles have no ZMP; same text as guidText.
    const QString guid = source.map->guid().toString(QUuid::WithBraces).toUpper();
    if (records) {
        console.progressRecord(phaseRecord(u"export", guid));
    }
    std::stop_source stop;
    auto progress = std::make_shared<region::RegionProcessProgressInfo>(stop.get_token());
    core::Result<void> result;
    {
        std::shared_ptr<region::IRegionProcessTask> task =
            region::ExportProviderMbtiles(app.tileIteratorFactory()).prepareTask(params, area, progress);

        // 4. the worker; this thread polls once a second
        std::mutex mutex;
        std::condition_variable_any wake;
        bool done = false;
        std::jthread worker([&] {
            core::Result<void> r = task->processRegion();
            std::lock_guard lock(mutex);
            result = std::move(r);
            done = true;
            wake.notify_all();
        });
        const auto progressText = [&](int percent) {
            return QStringLiteral("%1  export  %2%").arg(source.name).arg(percent);
        };
        const auto reachedPercent = [&] { return static_cast<int>(progress->processedRatio() * 100); };
        bool stopRequested = false;
        for (;;) {
            bool finishedNow = false;
            {
                std::unique_lock lock(mutex);
                // After the stop request the token is already signalled: a wait on it would return at once.
                finishedNow = stopRequested
                                  ? wake.wait_for(lock, kPollStep, [&] { return done; })
                                  : wake.wait_for(lock, termination, kPollStep, [&] { return done; });
            }
            if (finishedNow) {
                break;
            }
            if (termination.stop_requested() && !stopRequested) {
                stopRequested = true;
                stop.request_stop();
            }
            console.progress(progressText(reachedPercent()));
            if (records) {
                console.progressRecord(
                    exportProgressRecord(guid, static_cast<int>(progress->processedRatio() * 1000)));
            }
        }
        worker.join();
        task.reset(); // flips finished()
        // the task refreshes the ratio every 100 tiles only (u_ExportTaskToMBTiles.pas:266): a finished task
        // is 100 % (T-0267); a cancel or an error keeps the ratio reached
        console.progress(
            progressText(result ? 100 : reachedPercent())); // endProgress prints it if dropped (T-0266)
    }
    console.endProgress();

    // 5. the result
    if (!result) {
        if (result.error().code() == core::ErrorCode::Cancelled) {
            return failed(part, ExportOutcome::Interrupted, {});
        }
        return failed(part, ExportOutcome::Failed, result.error().what());
    }

    const auto count = countTiles(part);
    if (!count) {
        return failed(part, ExportOutcome::Failed, count.error().what());
    }
    if (*count == 0) {
        return failed(part, ExportOutcome::Empty,
                      QCoreApplication::translate("cg::app", "no tiles of %1 in the cache for this area")
                          .arg(source.name));
    }

    // app-cli.md 1.5: --overwrite must replace the old file and never lose it on a failure. POSIX rename(2)
    // does that atomically; on Windows replaceFile is MoveFileExW(REPLACE_EXISTING): it replaces, but is not
    // atomic across a crash (T-0335).
    if (!job.overwrite && QFileInfo::exists(source.outFile)) {
        return failed(part, ExportOutcome::Failed,
                      QCoreApplication::translate("cg::app", "the file %1 exists").arg(source.outFile));
    }
    if (const std::error_code ec = core::platform::replaceFile(part, source.outFile)) {
        return failed(
            part, ExportOutcome::Failed,
            QCoreApplication::translate("cg::app", "cannot rename %1 to %2").arg(part, source.outFile));
    }
    ExportResult r;
    r.outcome = ExportOutcome::Written;
    r.tiles = *count;
    r.fileSize = QFileInfo(source.outFile).size();
    if (records) {
        console.progressRecord(exportProgressRecord(guid, 1000));
    }
    return r;
}

} // namespace cg::app
