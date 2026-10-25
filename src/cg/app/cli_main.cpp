// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Inherits: Src/u_GlobalState.pas:1280-1289, frm_ProgressDownload.pas:306-393.
// docs/specs/app-cli.md 1.1, 1.7, 1.10, 1.11, 6.2.
#include "cg/app/cli_main.h"

#include "cg/app/app_context.h"
#include "cg/app/console.h"
#include "cg/app/download_runner.h"
#include "cg/app/export_runner.h"
#include "cg/app/fill_report.h"
#include "cg/app/job.h"
#include "cg/app/job_area.h"
#include "cg/app/job_checks.h"
#include "cg/app/machine_output.h"
#include "cg/app/probe.h"
#include "cg/app/snapshot.h"
#include "cg/app/snapshot_prune.h"
#include "cg/app/snapshot_resolver.h"
#include "cg/app/snapshot_store.h"
#include "cg/app/tile_estimate.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/content_type.h"
#include "cg/core/string_by_language.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/value_to_string.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/zmp_info.h"
#include "cg/region/download_session.h"
#include "cg/storage/simple_tile_storage_config.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(CliMain)
};

constexpr int kExitOk = 0;
constexpr int kExitInternal = 1;
constexpr int kExitUsage = 2;
constexpr int kExitRefused = 3;
constexpr int kExitDownloadFailed = 4;
constexpr int kExitExportFailed = 5;
constexpr int kExitInterrupted = 130; // main() replaces it with 143 after SIGTERM

using AreaPtr = std::shared_ptr<const geo::IGeometryLonLatPolygon>;

geo::ValueToStringConverter makeConverter() {
    return geo::ValueToStringConverter(geo::ValueFormatSettings{});
}

void printLines(Console& console, const QStringList& lines) {
    for (const QString& line : lines) {
        console.result(line);
    }
}

// app-cli.md 1.6.2: one line per grey source, always on stderr (Console::notice).
void printTermsOfUseWarning(const maptype::IMapType& map, Console& console) {
    if (const std::optional<QString> url = termsOfUseUrl(map)) {
        console.notice(Tr::tr("warning: \"%1\": the provider's terms of use may not allow downloading tiles "
                              "for offline use. Read %2 before downloading; you are responsible for "
                              "complying.")
                           .arg(sourceName(map), *url));
    }
}

// app-cli.md 1.6.5: one line per not-for-navigation source, always on stderr (Console::notice).
void printNavigationWarning(const maptype::IMapType& map, Console& console) {
    if (navigationWarning(map)) {
        console.notice(Tr::tr("warning: \"%1\": not for navigation. This source is not an official nautical "
                              "chart; do not use it to navigate.")
                           .arg(sourceName(map)));
    }
}

// `sources` (app-cli.md 1.1): GUID, map / layer, name, content type, EPSG, cache type, verdict.
int runSources(const Job& job, const AppContext& app, Console& console) {
    Job verdictJob = job;
    verdictJob.zooms.clear(); // the verdict is about the map, not about a zoom list

    const std::vector<maptype::IMapTypePtr> found = matchingSources(app, job.filter);
    for (const maptype::IMapTypePtr& map : found) { // app-cli.md 1.6.2
        printTermsOfUseWarning(*map, console);
    }

    const bool tsv = job.format == OutputFormat::Tsv;
    QList<QStringList> rows;
    rows.push_back({Tr::tr("GUID"), Tr::tr("kind"), Tr::tr("name"), Tr::tr("content type"), Tr::tr("EPSG"),
                    Tr::tr("cache"), Tr::tr("verdict")});
    for (const maptype::IMapTypePtr& map : found) {
        const bool layer = map->zmp()->isLayer();
        std::optional<QString> reason = refusalForExport(*map, verdictJob, layer);
        if (!reason) {
            reason = refusalForDownload(*map);
        }
        if (!reason) {
            reason = refusalForSnapshot(*map); // S-14 (T-0243)
        }
        if (tsv) {
            // T-0244: an invalid definition (an error) counts as `snapshot`
            const auto definition = snapshotDefinition(*map);
            const bool snapshot = !definition || definition->has_value();
            const core::IStringByLanguagePtr license = map->zmp()->license();
            // unreachable null: every ZmpInfo constructor sets it (zmp_info.cpp:190)
            const QString licenseText = withYear(
                license != nullptr ? license->getString(app.languages().currentLanguageIndex()) : QString(),
                QDateTime::currentDateTimeUtc().date().year()); // T-0249
            console.result(sourceRecord(*map, reason ? *reason : QStringLiteral("ok"),
                                        u"user", // field 10: always user (T-0291)
                                        snapshot ? u"snapshot" : u"static", licenseText,
                                        maxZoom(*map).value_or(std::nullopt), // T-0245: invalid = empty
                                        navigationWarning(*map)));            // field 14 (T-0285)
            continue;
        }
        const core::ContentTypeInfo* type = map->contentType();
        rows.push_back({guidText(*map), layer ? QStringLiteral("layer") : QStringLiteral("map"),
                        sourceName(*map), type != nullptr ? QString::fromLatin1(type->type) : QString(),
                        QString::number(map->projectionSet()->zoom(0)->projectionType()->projectionEpsg()),
                        QString::number(static_cast<int>(map->storageConfig()->cacheType())),
                        reason ? *reason : QStringLiteral("ok")});
    }
    if (!tsv) {
        printLines(console, alignColumns(rows));
    }
    return kExitOk;
}

// `probe` (app-cli.md 1.1, 3.13): GUID, name, result per source; exit 4 when a probed source did not pass.
// Skipped sources do not count.
int runProbe(const Job& job, const AppContext& app, Console& console, const std::stop_token& termination) {
    const std::vector<ProbeRow> probed = probeSources(app, job, termination);
    QList<QStringList> rows;
    rows.push_back({Tr::tr("GUID"), Tr::tr("name"), Tr::tr("result")});
    bool allPassed = true;
    for (const ProbeRow& row : probed) {
        rows.push_back({row.guid, row.name, row.result});
        allPassed = allPassed && row.status != ProbeStatus::Failed;
    }
    printLines(console, alignColumns(rows));
    if (termination.stop_requested()) {
        return kExitInterrupted;
    }
    return allPassed ? kExitOk : kExitDownloadFailed;
}

// One fill report on stdout; the exit code is kExitOk unless the pass could not be made.
int printFillReport(const maptype::IMapType& map, const geo::IGeometryLonLatPolygon& area,
                    const std::vector<quint8>& zooms, const AppContext& app, Console& console,
                    const std::stop_token& termination, bool& anyMissing, bool tsv, bool records) {
    if (records) {
        console.progressRecord(phaseRecord(u"fill", guidText(map)));
    }
    auto fills = fillReport(map, area, zooms, app, termination);
    if (!fills) {
        if (fills.error().code() == core::ErrorCode::Cancelled) {
            return kExitInterrupted;
        }
        console.error(fills.error().what());
        return kExitInternal;
    }
    if (tsv) {
        printLines(console, fillRecords(guidText(map), *fills));
    } else {
        console.result(sourceName(map));
        printLines(console, fillTable(*fills, makeConverter()));
    }
    for (const ZoomFill& fill : *fills) {
        anyMissing = anyMissing || fill.missing > 0;
    }
    return kExitOk;
}

// The exit code of a finished download of one source; 0 = Completed (the caller applies --strict).
int downloadExit(DownloadOutcome outcome) {
    switch (outcome) {
    case DownloadOutcome::Completed:
        return kExitOk;
    case DownloadOutcome::Interrupted:
        return kExitInterrupted;
    case DownloadOutcome::Stalled:
    case DownloadOutcome::SaveFailed:
    case DownloadOutcome::WorkerFailed:
    case DownloadOutcome::FrameNotServed: // runJobCommand replaces it by a rerun; `resume` never gets it
        return kExitDownloadFailed;
    case DownloadOutcome::Refused:
        return kExitRefused;
    }
    return kExitInternal;
}

// `resume` (app-cli.md 1.1, 1.9, 6.2): every file is loaded and its map checked (S-4 ... S-9) before any run
// starts; then the runs, then the fill report of the map, area and zooms each session names.
int runResume(const Job& job, AppContext& app, Console& console, const std::stop_token& termination) {
    // One report per map and zoom list (a split session is N files of one job); the sessions are kept for it.
    std::vector<region::DownloadSession> sessions;
    sessions.reserve(job.slsFiles.size());
    std::map<QString, QString> versions;            // map GUID -> the version the sessions name (T-0244)
    std::vector<maptype::IMapTypePtr> snapshotMaps; // snapshot maps in the order of their first file
    for (const QString& file : job.slsFiles) {
        try {
            const auto ini = core::ConfigProviderIni::open(file);
            if (!ini) {
                console.error(QStringLiteral("%1: %2").arg(file, ini.error().what()));
                return kExitRefused;
            }
            // as ProviderTilesDownload::startBySls does (:282-299)
            region::DownloadSession session;
            session.load((*ini)->subItem(u"Session"), *app.fullMapsSet(), *app.downloadConfig(),
                         *app.lonLatFactory());
            Job check = job;
            check.zooms = session.zoomArr(); // 0-based
            std::optional<QString> reason = refusalForExport(*session.mapType(), check, false);
            if (!reason) {
                reason = refusalForDownload(*session.mapType());
            }
            if (!reason) {
                reason = refusalForSnapshot(*session.mapType()); // S-14 (T-0244)
            }
            if (reason) {
                console.error(QStringLiteral("%1: %2").arg(file, *reason));
                return kExitRefused;
            }
            // weather-sources.md 4.3: a snapshot map continues the frame the session names
            const auto definition = snapshotDefinition(*session.mapType());
            if (definition && definition->has_value()) {
                const storage::MapVersionInfoPtr named = session.versionForDownload();
                const QString version = named ? named->storeString() : QString();
                if (version.isEmpty()) {
                    console.error(Tr::tr("%1: the session names no snapshot of \"%2\"")
                                      .arg(file, sourceName(*session.mapType())));
                    return kExitRefused;
                }
                const auto [at, added] = versions.emplace(guidText(*session.mapType()), version);
                if (!added && at->second != version) {
                    console.error(Tr::tr("%1: the sessions name two snapshots of \"%2\"")
                                      .arg(file, sourceName(*session.mapType())));
                    return kExitRefused;
                }
                if (added) {
                    snapshotMaps.push_back(session.mapType());
                }
            }
            sessions.push_back(std::move(session));
        } catch (const std::exception& e) {
            console.error(QStringLiteral("%1: %2").arg(file, QString::fromUtf8(e.what())));
            return kExitRefused;
        }
    }

    // T-0244 (weather-sources.md 4.3): no resolve, no store file written; the time from the store file when
    // its version equals, else from the version when that is a time, else unknown.
    for (const maptype::IMapTypePtr& map : snapshotMaps) {
        const QString guid = guidText(*map);
        const QString& version = versions.at(guid);
        applySnapshot(*map, version);
        const auto stored = loadSnapshot(app.snapshotDir(), guid);
        Snapshot taken{version, {}, false};
        if (stored && stored->version == version) {
            taken = *stored;
        } else if (auto parsed = snapshotFromText(version, version)) {
            taken = *parsed;
        }
        if (job.format == OutputFormat::Tsv) {
            console.result(snapshotRecord(guid, taken));
        } else {
            console.result(Tr::tr("%1: snapshot %2").arg(sourceName(*map), snapshotTimeText(taken)));
        }
    }

    QStringList sourceNames; // sessions[i] belongs to slsFiles[i]: every file was loaded above
    for (const region::DownloadSession& session : sessions) {
        sourceNames.push_back(sourceName(*session.mapType()));
    }
    std::set<QString> navigationWarned;                       // app-cli.md 1.6.5: once per source of the job
    for (const region::DownloadSession& session : sessions) { // app-cli.md 1.6.2: per session file
        printTermsOfUseWarning(*session.mapType(), console);
        if (navigationWarned.insert(guidText(*session.mapType())).second) {
            printNavigationWarning(*session.mapType(), console);
        }
    }
    DownloadRunner runner(app, console, job, termination,
                          DownloadPolicy{.stallTimeoutSec = job.stallTimeoutSec,
                                         .saveRetries = job.saveRetries,
                                         .maxConnectionErrors = job.maxConnectionErrors});
    // The fill report below shows what is still missing (the deviation of T-0226 (deviations.md): a resume
    // leaves no tile out).
    const DownloadOutcome outcome = runner.resume(job.slsFiles, sourceNames);
    int rc = downloadExit(outcome);
    if (outcome == DownloadOutcome::Interrupted || outcome == DownloadOutcome::Refused) {
        return rc;
    }
    // the zoom list is keyed as a QByteArray: a std::vector<quint8> key trips the GCC 14 -Wstringop-overread
    // false positive (T-0265)
    std::set<std::pair<QString, QByteArray>> reported;
    bool anyMissing = false;
    for (const region::DownloadSession& session : sessions) {
        const std::vector<quint8>& zooms = session.zoomArr();
        if (!reported
                 .emplace(guidText(*session.mapType()),
                          QByteArray(reinterpret_cast<const char*>(zooms.data()),
                                     static_cast<qsizetype>(zooms.size())))
                 .second) {
            continue;
        }
        const int code = printFillReport(*session.mapType(), *session.polygon(), session.zoomArr(), app,
                                         console, termination, anyMissing, job.format == OutputFormat::Tsv,
                                         job.progressMode == ProgressMode::Lines);
        if (code != kExitOk) {
            return code;
        }
    }
    if (rc == kExitOk && job.strict && anyMissing) {
        console.error(Tr::tr("tiles are missing in the cache (--strict)"));
        rc = kExitDownloadFailed;
    }
    return rc;
}

// True for a source with a SnapshotIndex. The keys were checked by refusalForSnapshot (resolveJob, every
// command), so an invalid definition cannot arrive here.
bool isSnapshotSource(const maptype::IMapType& map) {
    const auto definition = snapshotDefinition(map);
    return definition && definition->has_value();
}

// download / run: resolve, apply, store; export: apply the stored snapshot; estimate: nothing. Returns the
// exit code (kExitOk = go on).
int fixSnapshots(const Job& job, AppContext& app, Console& console, const std::stop_token& termination,
                 std::vector<ResolvedSource>& sources) {
    if (job.command == Command::Estimate) {
        return kExitOk;
    }
    for (ResolvedSource& source : sources) {
        const auto definition = snapshotDefinition(*source.map);
        if (!definition || !*definition) {
            continue;
        }
        const QString guid = guidText(*source.map);
        if (job.command == Command::Export) {
            const std::optional<Snapshot> stored = loadSnapshot(app.snapshotDir(), guid);
            if (!stored) {
                console.error(Tr::tr("\"%1\": no snapshot of this source was downloaded yet; run download or "
                                     "run first")
                                  .arg(sourceName(*source.map)));
                return kExitRefused;
            }
            applySnapshot(*source.map, stored->version);
            source.snapshot = *stored;
            continue;
        }
        SnapshotDefinition wanted = **definition;
        wanted.time = job.time; // T-0283: only a wmts-default source has one (resolveJob refused the rest)
        wanted.sourceName = sourceName(*source.map);
        const auto taken = resolveSnapshot(wanted, *source.map, app, termination);
        if (!taken) {
            if (taken.error().code() == core::ErrorCode::Cancelled) {
                return kExitInterrupted;
            }
            if (taken.error().code() == core::ErrorCode::Unsupported) { // --time is not a time of the layer
                console.error(taken.error().what());
                return kExitRefused;
            }
            console.error(Tr::tr("cannot take the snapshot of \"%1\": %2")
                              .arg(sourceName(*source.map), taken.error().what()));
            return kExitDownloadFailed;
        }
        applySnapshot(*source.map, taken->version);
        source.map->tileStorage()->setRecordSkippedSaves(true); // T-0352: the prune reads the record
        if (const auto saved = saveSnapshot(app.snapshotDir(), guid, *taken, QDateTime::currentDateTimeUtc());
            !saved) {
            console.error(Tr::tr("cannot write %1: %2")
                              .arg(app.snapshotDir() + QLatin1Char('/') + guid + QStringLiteral(".conf"),
                                   saved.error().what()));
            return kExitDownloadFailed;
        }
        source.snapshot = *taken;
        // T-0356: a newest frame younger than an hour may not be served yet
        source.frameFallback = taken->newest && !taken->dateOnly && taken->time.isValid() &&
                               taken->time.secsTo(QDateTime::currentDateTimeUtc()) < kYoungFrameSeconds;
    }
    return kExitOk;
}

// T-0356: the download of the newest young frame stopped, none of its tiles is served. One step back: the
// previous frame of the index, then the download again (with the legacy behaviour: no second step). When the
// previous frame cannot be taken, the newest frame is downloaded again as it was before this feature.
DownloadOutcome downloadPreviousFrame(ResolvedSource& source, const Job& job, AppContext& app,
                                      Console& console, const std::stop_token& termination,
                                      DownloadRunner& runner,
                                      const std::shared_ptr<const geo::IGeometryLonLatPolygon>& area) {
    source.frameFallback = false;
    const auto definition = snapshotDefinition(*source.map);
    if (!source.snapshot || !definition ||
        !*definition) { // unreachable: fixSnapshots set both (frameFallback)
        return runner.run(source, area);
    }
    const QString name = sourceName(*source.map);
    const QString newestText = snapshotTimeText(*source.snapshot);
    SnapshotDefinition wanted = **definition;
    wanted.time = job.time;
    wanted.sourceName = name;
    wanted.previous = true;
    const auto taken = resolveSnapshot(wanted, *source.map, app, termination);
    if (!taken) {
        if (taken.error().code() == core::ErrorCode::Cancelled) {
            return DownloadOutcome::Interrupted;
        }
        console.warning(
            Tr::tr("the newest frame %1 of \"%2\" is not served yet and the frame before it cannot be "
                   "taken (%3); trying the newest again")
                .arg(newestText, name, taken.error().what()));
        return runner.run(source, area);
    }
    const QString guid = guidText(*source.map);
    if (const auto saved = saveSnapshot(app.snapshotDir(), guid, *taken, QDateTime::currentDateTimeUtc());
        !saved) {
        console.error(Tr::tr("cannot write %1: %2")
                          .arg(app.snapshotDir() + QLatin1Char('/') + guid + QStringLiteral(".conf"),
                               saved.error().what()));
        return DownloadOutcome::SaveFailed;
    }
    applySnapshot(*source.map, taken->version);
    source.snapshot = *taken;
    console.warning(Tr::tr("the newest frame %1 of \"%2\" is not served yet; using the previous frame %3")
                        .arg(newestText, name, snapshotTimeText(*taken)));
    if (job.format == OutputFormat::Tsv) {
        console.result(snapshotRecord(guid, *taken)); // the later record is the frame of this run
    }
    return runner.run(source, area);
}

// estimate, download, export, run.
int runJobCommand(const Job& job, AppContext& app, Console& console, const std::stop_token& termination) {
    const auto warn = [&console](const QString& text) { console.warning(text); };
    auto sources = resolveJob(job, app, warn);
    if (!sources) {
        console.error(sources.error().message);
        return sources.error().exitCode;
    }
    // The corridor is built in the projection of the first source (the base map, if any) at the highest zoom.
    const geo::ProjectionPtr corridorProjection =
        sources->front().map->projectionSet()->zoom(job.zooms.back());
    const auto area = buildArea(job, *app.lonLatFactory(), *corridorProjection, warn);
    if (!area) {
        console.error(area.error().message);
        return area.error().exitCode;
    }

    // S-16 (T-0277, app-cli.md 1.7): refused before the first request (fixSnapshots fetches a snapshot).
    for (const ResolvedSource& source : *sources) {
        if (const auto reason = refusalForAreaSize(*source.map, **area, source.zooms)) {
            console.error(*reason);
            return kExitRefused;
        }
    }

    // T-0243 (weather-sources.md 3.5): the snapshot of every snapshot source is fixed before the first
    // estimate. `estimate` takes none (the next download's frame is not known).
    if (const int code = fixSnapshots(job, app, console, termination, *sources); code != kExitOk) {
        return code;
    }

    // The estimate of every source (S-13: no tile at the chosen zooms refuses the job).
    for (const ResolvedSource& source : *sources) {
        const bool estimateOnly = job.command == Command::Estimate && isSnapshotSource(*source.map);
        if (job.progressMode == ProgressMode::Lines) {
            console.progressRecord(phaseRecord(u"estimate", guidText(*source.map)));
        }
        auto estimates = estimateTiles(*source.map, **area, source.zooms, app, job.assumeTileKb, job.replace,
                                       job.replaceOlderThan, job.retryMissing, job.retryMissingOlderThan,
                                       termination, estimateOnly);
        if (!estimates) {
            if (estimates.error().code() == core::ErrorCode::Cancelled) {
                return kExitInterrupted;
            }
            console.error(estimates.error().what());
            return kExitInternal;
        }
        if (job.format == OutputFormat::Tsv) {
            if (source.snapshot) {
                console.result(snapshotRecord(guidText(*source.map), *source.snapshot));
            }
            printLines(console, estimateRecords(guidText(*source.map), *estimates));
        } else {
            console.result(sourceName(*source.map));
            if (estimateOnly) {
                console.result(Tr::tr("snapshot: taken when downloading"));
            } else if (source.snapshot) {
                console.result(Tr::tr("snapshot %1").arg(snapshotTimeText(*source.snapshot)));
            }
            printLines(console, estimateTable(*estimates, makeConverter()));
        }
        if (!anyTile(*estimates)) {
            console.error(Tr::tr("the area holds no tile at the chosen zooms"));
            return kExitRefused;
        }
    }
    if (job.command == Command::Estimate) {
        return kExitOk;
    }

    if (job.command == Command::Download || job.command == Command::Run) {
        std::set<QString> warned; // app-cli.md 1.6.2: once per grey source of the job
        for (const ResolvedSource& source : *sources) {
            if (warned.insert(guidText(*source.map)).second) {
                printTermsOfUseWarning(*source.map, console);
                printNavigationWarning(*source.map, console); // app-cli.md 1.6.5
            }
        }
        DownloadRunner runner(app, console, job, termination,
                              DownloadPolicy{.stallTimeoutSec = job.stallTimeoutSec,
                                             .saveRetries = job.saveRetries,
                                             .maxConnectionErrors = job.maxConnectionErrors});
        bool anyMissing = false;
        for (ResolvedSource& source : *sources) {
            DownloadOutcome outcome = runner.run(source, *area);
            if (outcome == DownloadOutcome::FrameNotServed) { // T-0356
                outcome = downloadPreviousFrame(source, job, app, console, termination, runner, *area);
            }
            if (outcome == DownloadOutcome::Interrupted) {
                return kExitInterrupted;
            }
            // T-0246 (weather-sources.md 9): older frames of the area go; T-0351: before the fill report,
            // as the tiles moved to the new frame count as present in it
            if (outcome == DownloadOutcome::Completed && source.snapshot) {
                const auto removed = pruneOtherSnapshots(*source.map, **area, source.zooms, app, termination);
                if (!removed) {
                    if (removed.error().code() == core::ErrorCode::Cancelled) {
                        return kExitInterrupted;
                    }
                    console.warning(Tr::tr("cannot remove older snapshots of \"%1\": %2")
                                        .arg(sourceName(*source.map), removed.error().what()));
                } else {
                    console.log(Tr::tr("removed %1 tiles of older snapshots of \"%2\"")
                                    .arg(*removed)
                                    .arg(sourceName(*source.map)));
                }
            }
            const int code =
                printFillReport(*source.map, **area, source.zooms, app, console, termination, anyMissing,
                                job.format == OutputFormat::Tsv, job.progressMode == ProgressMode::Lines);
            if (code != kExitOk) {
                return code;
            }
            if (outcome != DownloadOutcome::Completed) {
                return downloadExit(outcome); // stop at the first source that did not complete
            }
        }
        if (job.strict && anyMissing) {
            console.error(Tr::tr("tiles are missing in the cache (--strict)"));
            return kExitDownloadFailed;
        }
    }

    if (job.command == Command::Export || job.command == Command::Run) {
        for (const ResolvedSource& source : *sources) {
            const ExportResult result = exportSource(source, job, *area, app, console, termination);
            switch (result.outcome) {
            case ExportOutcome::Written:
                if (job.format == OutputFormat::Tsv) {
                    // outFile is absolute already (parseJob: pathOpt); the call keeps the record's promise
                    console.result(writtenRecord(
                        guidText(*source.map), QFileInfo(source.outFile).absoluteFilePath(), result.tiles,
                        result.fileSize, source.snapshot ? snapshotTimeIso(*source.snapshot) : QString()));
                    break;
                }
                console.result(
                    Tr::tr("written: %1  %2 tiles  %3")
                        .arg(source.outFile)
                        .arg(result.tiles)
                        .arg(makeConverter().dataSizeConvert(static_cast<double>(result.fileSize) / 1024.0)) +
                    (source.snapshot ? Tr::tr("  snapshot %1").arg(snapshotTimeText(*source.snapshot))
                                     : QString()));
                break;
            case ExportOutcome::Interrupted:
                return kExitInterrupted;
            case ExportOutcome::Empty:
            case ExportOutcome::Failed:
                console.error(result.error);
                return kExitExportFailed;
            }
        }
    }
    return kExitOk;
}

} // namespace

int runCli(const QStringList& arguments, const std::stop_token& termination, const StdinWatcher& watchStdin) {
    const auto job = parseJob(arguments);
    if (!job) {
        Console early(0, false);
        const QString usage = Tr::tr("Usage: chart-grabber-cli COMMAND [options]; chart-grabber-cli --help");
        early.error(job.error().exitCode != kExitUsage ||
                            job.error().message.contains(QLatin1String("chart-grabber-cli --help"))
                        ? job.error().message
                        : job.error().message + QLatin1Char('\n') + usage);
        return job.error().exitCode;
    }
    Console console(job->verbosity, job->progress);
    console.setProgressRecords(job->progressMode == ProgressMode::Lines);
    if (job->command == Command::Help) {
        console.result(helpText());
        return kExitOk;
    }
    if (job->command == Command::Version) {
        console.result(QStringLiteral("chart-grabber-cli %1").arg(QCoreApplication::applicationVersion()));
        return kExitOk;
    }
    // T-0332: before anything else, so that a stop line or EOF sent at once is not lost (the token is stopped
    // and every stage below checks it); the sources / probe commands do not run a job and ignore the option.
    if (job->stopOnStdin && watchStdin && job->command != Command::Sources &&
        job->command != Command::Probe) {
        switch (watchStdin()) {
        case core::platform::StdinWatch::Started:
            break;
        case core::platform::StdinWatch::NotPipe:
            console.error(Tr::tr("--stop-on-stdin needs stdin to be a pipe") + QLatin1Char('\n') +
                          Tr::tr("Usage: chart-grabber-cli COMMAND [options]; chart-grabber-cli --help"));
            return kExitUsage;
        case core::platform::StdinWatch::Invalid:
            console.warning(Tr::tr("--stop-on-stdin disabled: stdin is not a valid handle"));
            break;
        }
    }
    std::unique_ptr<AppContext> app; // destroyed after every runner (they live in the functions below)
    try {
        AppOptions options;
        options.arguments = arguments;
        options.mapsDirs = job->mapsDirs;
        options.cacheDir = job->cacheDir;
        options.proxy = job->proxy;
        options.warn = [&console](const QString& text) { console.warning(text); };
        auto created = AppContext::create(options);
        if (!created) {
            console.error(created.error().what());
            return kExitRefused;
        }
        app = std::move(*created);

        QElapsedTimer loadTime;
        loadTime.start();
        if (const auto loaded = app->loadMaps(); !loaded) {
            console.error(loaded.error().what());
            return loaded.error().code() == core::ErrorCode::NotFound ? kExitRefused : kExitInternal;
        }
        console.log(Tr::tr("maps loaded in %1 ms, %2 ZMP files failed")
                        .arg(loadTime.elapsed())
                        .arg(app->zmpErrorCount()));

        switch (job->command) {
        case Command::Sources:
            return runSources(*job, *app, console);
        case Command::Probe:
            return runProbe(*job, *app, console, termination);
        case Command::Resume:
            return runResume(*job, *app, console, termination);
        default:
            return runJobCommand(*job, *app, console, termination);
        }
    } catch (const std::exception& e) {
        console.error(Tr::tr("internal: %1").arg(QString::fromUtf8(e.what())));
        if (app) {
            app->requestClose();
        }
        return kExitInternal;
    }
}

} // namespace cg::app
