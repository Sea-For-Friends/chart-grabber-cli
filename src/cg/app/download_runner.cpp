// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Inherits: frm_ProgressDownload.pas:268-279, 306-393, 431-437, 586-595 (the
// window's loop and its buttons, without the window).
#include "cg/app/download_runner.h"

#include "cg/app/machine_output.h"
#include "cg/app/probe.h"
#include "cg/core/config_provider_ini.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/value_to_string.h"
#include "cg/region/download_session.h"
#include "cg/region/progress_download.h"
#include "cg/region/provider_tiles_download.h"
#include "cg/region/zoom_array_func.h"
#include "cg/storage/tile_date.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace cg::app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kFinishWait = std::chrono::seconds(30); // app-cli.md 6.1 step 3
constexpr auto kFinishPoll = std::chrono::milliseconds(100);

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(DownloadRunner)
};

// What the worker writes as its last line when the area is done (thread_download_tiles.cpp, the context of
// its own tr(); legacy SAS_MSG_ProcessFilesComplete, u_ResStrings.pas:63).
QString completedText() {
    return QCoreApplication::translate("cg::region", "The task is completed!");
}

// Production starter: region::ProviderTilesDownload (u_ProviderTilesDownload.pas) over the maps of the app.
class ProviderStarter final : public IRunStarter {
public:
    explicit ProviderStarter(AppContext& app)
        : provider_(app.appClosing(), app.tileIteratorFactory(), app.fullMapsSet(), app.lonLatFactory(),
                    app.projectedFactory(), app.downloadConfig(), app.downloadInfo()) {}

    std::vector<StartedRun> startProcess(const region::PolygonPtr& area,
                                         const region::TilesDownloadParams& params) override {
        std::vector<StartedRun> result;
        for (auto& run : provider_.startProcess(area, params)) {
            result.push_back(wrap(std::move(run)));
        }
        return result;
    }

    StartedRun startBySls(const QString& fileName, bool startPaused) override {
        return wrap(provider_.startBySls(fileName, startPaused));
    }

private:
    static StartedRun wrap(region::DownloadSessionRun&& run) {
        StartedRun started;
        started.progress = run.progressInfo;
        started.cancelNotifier = run.cancelNotifier;
        started.owner = std::make_shared<region::DownloadSessionRun>(std::move(run));
        return started;
    }

    region::ProviderTilesDownload provider_;
};

// One run and everything the poll keeps about it. Members are declared so that `progress` (whose destructor
// cancels) dies before `started` (whose owner joins the worker).
struct Slot {
    StartedRun started;
    QString sessionDir;
    QString label;
    QString guid; // of the slot's map, for the --progress=lines records
    std::unique_ptr<region::ProgressDownload> progress;
    std::uint64_t lastLogId = 0;
    // unattended pause policy
    bool pauseNoted = false;
    Clock::time_point pausedAt;
    int resumes = 0;
    qint64 processedAtResume = -1;
    bool saved = false;
    double elapsedSeen = 0; // days, the largest elapsedTime() reported so far (T-0233)
};

struct Context {
    AppContext& app;
    Console& console;
    DownloadPolicy policy;
    std::stop_token termination;
    bool records = false; // --progress=lines
    bool frameFallback =
        false; // T-0356: stop a run whose frame is not served (ResolvedSource::frameFallback)
};

QString newestLine(const Slot& slot) {
    std::uint64_t id = 0;
    const QStringList lines = slot.started.progress->logProvider()->since(id, 1);
    return lines.isEmpty() ? QString() : lines.last();
}

bool isCompleted(const Slot& slot) { // app-cli.md 6.3 row 3
    return slot.started.progress->finished() && newestLine(slot) == completedText();
}

void cancelAll(std::vector<Slot>& runs) { // region-download.md section 7: cancel before destroy
    for (auto& slot : runs) {
        if (slot.started.cancelNotifier) {
            slot.started.cancelNotifier->cancel();
        }
    }
}

void makeSlots(std::vector<StartedRun>& started, const QString& sessionDir, const QString& label,
               Context& ctx, std::vector<Slot>& runs, bool perFileDir, const QStringList& files) {
    for (std::size_t i = 0; i < started.size(); ++i) {
        Slot slot;
        slot.started = std::move(started[i]);
        slot.sessionDir =
            perFileDir ? QFileInfo(files.at(static_cast<qsizetype>(i))).absolutePath() : sessionDir;
        slot.label = label;
        Console& console = ctx.console;
        slot.progress = std::make_unique<region::ProgressDownload>(
            slot.started.progress, slot.started.cancelNotifier, ctx.app.autoSaveDir(),
            [&console](const QString& text) { console.log(text); }); // the window's memo (:381-393)
        runs.push_back(std::move(slot));
    }
}

// The sums over the runs that the text line and the progress record both show.
struct Sums {
    qint64 processed = 0;
    qint64 total = 0;
    qint64 downloaded = 0;
    quint64 downloadSize = 0;
    double elapsed = 0; // days
    int zoom = 0;       // 0-based
};

// Once a run is finished, elapsedTime() of the progress info falls back to the session's stored value (legacy
// u_RegionProcessProgressInfoDownload.pas:237-239), which is 0 for a run that was never paused. The runner
// keeps the largest value seen per run so the reported elapsed never decreases (T-0233).
Sums sumsOf(std::vector<Slot>& runs) {
    Sums sums;
    for (auto& slot : runs) {
        const auto& info = *slot.started.progress;
        slot.elapsedSeen = std::max(slot.elapsedSeen, info.elapsedTime());
        sums.processed += info.processed();
        sums.total += info.totalToProcess();
        sums.downloaded += info.downloaded();
        sums.downloadSize += info.downloadSize();
        sums.elapsed = std::max(sums.elapsed, slot.elapsedSeen);
        quint8 z = 0;
        geo::PointI point;
        int size = 0;
        info.getLastTileInfo(z, point, size);
        sums.zoom = std::max(sums.zoom, static_cast<int>(z));
    }
    return sums;
}

QString progressLine(std::vector<Slot>& runs) {
    const Sums s = sumsOf(runs);
    const geo::ValueToStringConverter converter{geo::ValueFormatSettings{}};
    const double downloadKb = static_cast<double>(s.downloadSize) / 1024; // :317
    return Tr::tr("%1  z%2  %3/%4 (%5)  downloaded %6 tiles %7  left ~%8 ~%9")
        .arg(runs.front().label)
        .arg(s.zoom + 1) // :321 'z' + IntToStr(VZoom + 1)
        .arg(s.processed)
        .arg(s.total)
        .arg(region::percentText(s.total, s.processed))
        .arg(s.downloaded)
        .arg(converter.dataSizeConvert(downloadKb))
        .arg(region::remainingSize(s.total, s.processed, s.downloaded, downloadKb, converter))
        .arg(region::remainingTime(s.total, s.processed, s.elapsed));
}

// The `progress download` record of the same sums; the zoom stays 0-based, elapsedTime() days become seconds.
QString progressRecordOf(std::vector<Slot>& runs) {
    const Sums s = sumsOf(runs);
    return downloadProgressRecord(runs.front().guid, s.zoom, s.processed, s.total, s.downloaded,
                                  s.downloadSize, qRound64(s.elapsed * 86400.0));
}

// Written only when the first run has a map GUID (resume: the session file can change between the GUID read
// in sessionMapGuid and the start, so the GUID may be empty): a record with an empty GUID field would break
// the record contract.
void writeProgressRecord(Console& console, std::vector<Slot>& runs) {
    if (!runs.front().guid.isEmpty()) {
        console.progressRecord(progressRecordOf(runs));
    }
}

// The poll loop (UpdateTimerTimer :431-437) and the policy that replaces the human. Returns the reason the
// runner stops the runs, or nullopt when every run finished by itself.
std::optional<DownloadOutcome> poll(Context& ctx, std::vector<Slot>& runs) {
    std::stop_source wake; // either termination source wakes the wait
    const std::stop_callback onTermination(ctx.termination, [&wake] { wake.request_stop(); });
    const std::stop_callback onClosing(ctx.app.appClosing(), [&wake] { wake.request_stop(); });
    std::mutex mutex;
    std::condition_variable_any sleeper;

    auto lastChange = Clock::now();
    qint64 lastSum = -1;

    while (true) {
        for (auto& slot : runs) { // :381-393 UpdateMemoProgressForm
            for (const QString& line : slot.started.progress->logProvider()->since(slot.lastLogId, 100)) {
                ctx.console.log(line);
            }
            slot.progress->checkSessionAutosave(); // :431-437
        }
        ctx.console.progress(progressLine(runs));
        if (ctx.records) {
            writeProgressRecord(ctx.console, runs);
        }

        const auto now = Clock::now();
        if (wake.stop_requested()) {
            return DownloadOutcome::Interrupted;
        }

        for (auto& slot : runs) { // frm_ProgressDownload.pas:268-279 btnPauseClick: Resume
            auto& info = *slot.started.progress;
            if (!info.isPaused()) {
                slot.pauseNoted = false;
                continue;
            }
            if (!slot.pauseNoted) {
                slot.pauseNoted = true;
                slot.pausedAt = now;
            }
            if (now - slot.pausedAt < std::chrono::seconds(ctx.policy.resumeDelaySec)) {
                continue;
            }
            const qint64 processed = info.processed();
            if (processed > slot.processedAtResume) {
                slot.resumes = 0; // a tile was processed since the last resume
            }
            slot.processedAtResume = processed;
            info.resume();
            slot.pauseNoted = false;
            if (++slot.resumes >= ctx.policy.saveRetries) {
                ctx.console.error(Tr::tr("cannot store tiles: %1").arg(newestLine(slot)));
                return DownloadOutcome::SaveFailed;
            }
        }

        qint64 sum = 0;
        for (const auto& slot : runs) {
            sum += slot.started.progress->processed();
        }
        if (sum != lastSum) {
            lastSum = sum;
            lastChange = now;
        } else if (ctx.policy.stallTimeoutSec > 0 &&
                   now - lastChange >= std::chrono::seconds(ctx.policy.stallTimeoutSec)) {
            const auto it = std::find_if(runs.begin(), runs.end(),
                                         [](const Slot& s) { return !s.started.progress->finished(); });
            ctx.console.error(Tr::tr("no progress for %1 s: %2")
                                  .arg(ctx.policy.stallTimeoutSec)
                                  .arg(it == runs.end() ? QString() : newestLine(*it)));
            return DownloadOutcome::Stalled;
        }

        // T-0271 (deviates from L-271): a streak of connection errors in one worker stops the download.
        if (ctx.policy.maxConnectionErrors > 0) {
            const auto it = std::find_if(runs.begin(), runs.end(), [&ctx](const Slot& s) {
                return s.started.progress->connectionErrorStreak() >= ctx.policy.maxConnectionErrors;
            });
            if (it != runs.end()) {
                ctx.console.error(Tr::tr("no connection to the server for %1 tiles in a row: %2")
                                      .arg(it->started.progress->connectionErrorStreak())
                                      .arg(newestLine(*it)));
                return DownloadOutcome::Stalled;
            }
        }

        // DEVIATION: the D row of T-0356. The frame is not out when no tile of it was stored or skipped as
        // present (other == 0) and either every run's latest notServedAnswers answers are Unavailable (HTTP
        // 5xx, the ban tile: the worker asks for the same tile again; fewer when the run has fewer tiles), or
        // all runs have finished with at least one answer in the class (404, the empty tile, a TNE row: the
        // next tile is asked).
        if (ctx.frameFallback) {
            const auto unserved = [&ctx](const Slot& s) {
                const auto& info = *s.started.progress;
                const qint64 need =
                    std::max<qint64>(1, std::min<qint64>(ctx.policy.notServedAnswers, info.totalToProcess()));
                const region::FrameAnswers answers = info.frameAnswers();
                return answers.other == 0 && answers.unavailableRun >= need;
            };
            if (std::all_of(runs.begin(), runs.end(), unserved)) {
                return DownloadOutcome::FrameNotServed;
            }
        }

        if (std::all_of(runs.begin(), runs.end(),
                        [](const Slot& s) { return s.started.progress->finished(); })) {
            if (ctx.frameFallback) {
                qint64 inClass = 0;
                bool anyOther = false;
                for (const Slot& s : runs) {
                    const region::FrameAnswers answers = s.started.progress->frameAnswers();
                    inClass += answers.inClass;
                    anyOther = anyOther || answers.other > 0;
                }
                if (inClass > 0 && !anyOther) {
                    return DownloadOutcome::FrameNotServed;
                }
            }
            return std::nullopt;
        }

        std::unique_lock lock(mutex);
        sleeper.wait_for(lock, wake.get_token(), std::chrono::milliseconds(ctx.policy.pollMs),
                         [] { return false; });
    }
}

// Steps 5 and 6 of app-cli.md 5.2: stop the runs, save what did not complete, decide the outcome.
DownloadOutcome finish(Context& ctx, std::vector<Slot>& runs, const std::optional<DownloadOutcome>& ownReason,
                       bool removeDirWhenCompleted) {
    // T-0308: the optional is read once, by const reference, into a flag and an enum; GCC 12 Release reports
    // a false maybe-uninitialized when the by-value optional parameter is tested and reset in the body.
    bool stopped = ownReason.has_value();
    DownloadOutcome reason = stopped ? *ownReason : DownloadOutcome::Completed;
    // T-0234: an interrupt that arrives after every run has completed (the last tile is in, the completion
    // line written) interrupts nothing: finish as completed, so the fill report runs and the state is
    // removed. Legacy has no such case (frm_ProgressDownload.pas:586-595 FormClose only cancels and deletes
    // the .bk).
    if (stopped && reason == DownloadOutcome::Interrupted &&
        std::all_of(runs.begin(), runs.end(), [](const Slot& s) { return isCompleted(s); })) {
        stopped = false;
        reason = DownloadOutcome::Completed;
    }
    if (stopped) {
        cancelAll(runs);
        const auto deadline = Clock::now() + kFinishWait;
        while (Clock::now() < deadline && !std::all_of(runs.begin(), runs.end(), [](const Slot& s) {
                   return s.started.progress->finished();
               })) {
            std::this_thread::sleep_for(kFinishPoll);
        }
    }
    ctx.console.progress(
        progressLine(runs)); // the final counters (T-0266): endProgress prints them if dropped
    ctx.console.endProgress();

    bool anySaved = false;
    QStringList savedFiles;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        Slot& slot = runs[i];
        if (isCompleted(slot) || reason == DownloadOutcome::FrameNotServed) { // T-0356: nothing to continue
            slot.progress->close();                                           // deletes the .bk
            continue;
        }
        QString name = QStringLiteral("final.sls");
        if (runs.size() > 1) {
            int workerIndex = 0;
            int workersCount = 0;
            slot.started.progress->getWorkerInfo(workerIndex, workersCount);
            const int number = (workersCount > 1 ? workerIndex : static_cast<int>(i)) + 1;
            name = QString::asprintf("%.2d_", number) + name;
        }
        const QString target = slot.sessionDir + QLatin1Char('/') + name;
        try {
            QDir().mkpath(slot.sessionDir);
            slot.progress->saveSession(target);
            slot.saved = true;
            anySaved = true;
            savedFiles.push_back(target);
        } catch (const std::exception& e) {
            ctx.console.error(
                Tr::tr("cannot save the session to %1: %2").arg(target, QString::fromUtf8(e.what())));
        }
    }

    // Outcome (6.3): the own reason first, else per run, the worst wins.
    DownloadOutcome outcome = reason;
    if (outcome == DownloadOutcome::Completed) {
        for (const auto& slot : runs) {
            if (!isCompleted(slot)) {
                ctx.console.error(newestLine(slot));
                outcome = DownloadOutcome::WorkerFailed;
            }
        }
    }

    if (outcome == DownloadOutcome::Completed || outcome == DownloadOutcome::FrameNotServed) {
        if (removeDirWhenCompleted) {
            QDir(runs.front().sessionDir).removeRecursively();
        }
    } else if (anySaved) {
        ctx.console.warning(Tr::tr("session saved; run the same command again to continue"));
        for (const QString& file : savedFiles) {
            ctx.console.warning(Tr::tr("to continue from this file: resume %1").arg(file));
        }
    }

    cancelAll(runs); // never destroy an unfinished run without cancel
    runs.clear();
    return outcome;
}

// Zoom list, polygon points and replace / retry values as text (app-cli.md 1.9).
void appendPoints(QString& text, const geo::IGeometryLonLatContour& contour) {
    for (const geo::LonLat& p : contour.points()) {
        text += QString::asprintf("%.8f,%.8f;", p.lon, p.lat);
    }
}

void appendPolygon(QString& text, const geo::IGeometryLonLatPolygon& area) {
    if (const auto* contour = dynamic_cast<const geo::IGeometryLonLatContour*>(&area)) {
        appendPoints(text, *contour);
    } else if (const auto* single = dynamic_cast<const geo::IGeometryLonLatSinglePolygon*>(&area)) {
        appendPoints(text, *single->outerBorder());
        for (std::size_t i = 0; i < single->holeCount(); ++i) {
            appendPoints(text, *single->holeBorder(i));
        }
    } else if (const auto* multi = dynamic_cast<const geo::IGeometryLonLatMultiPolygon*>(&area)) {
        for (std::size_t i = 0; i < multi->count(); ++i) {
            appendPolygon(text, *multi->item(i));
        }
    }
}

QString dateText(const QDateTime& date) {
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

QString flag(bool value) {
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

// IMapType::guid(), not zmp()->guid() as machine_output's guidText: the runner's test double has no ZMP. Same
// text in production (MapType::guid returns zmp_->guid()).
QString mapGuidText(const maptype::IMapType& map) {
    return map.guid().toString(QUuid::WithBraces).toUpper();
}

// The GUID of the map a session file names (read as ProviderTilesDownload::startBySls does), empty when the
// file cannot be read: startBySls has not accepted it yet and will refuse it with its own message.
QString sessionMapGuid(AppContext& app, const QString& file) {
    try {
        const auto ini = core::ConfigProviderIni::open(file);
        if (!ini) {
            return {};
        }
        region::DownloadSession session;
        session.load((*ini)->subItem(u"Session"), *app.fullMapsSet(), *app.downloadConfig(),
                     *app.lonLatFactory());
        return mapGuidText(*session.mapType());
    } catch (const std::exception&) {
        return {};
    }
}

// The line of `run` and of `resume` for each session file they start.
void noteContinuing(Console& console, const QDateTime& time) {
    console.warning(
        Tr::tr("continuing the session of %1").arg(time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
}

} // namespace

DownloadRunner::DownloadRunner(AppContext& app, Console& console, const Job& job, std::stop_token termination,
                               DownloadPolicy policy, std::shared_ptr<IRunStarter> starter)
    : app_(app), console_(console), job_(job), termination_(std::move(termination)), policy_(policy),
      starter_(std::move(starter)) {}

QString DownloadRunner::jobId(const Job& job, const maptype::IMapType& map,
                              const geo::IGeometryLonLatPolygon& area, std::span<const quint8> zooms) {
    QString points;
    appendPolygon(points, area);
    const QStringList parts{
        mapGuidText(map),
        region::zoomArrayToStr(std::vector<quint8>(zooms.begin(), zooms.end())),
        points,
        QString::number(job.threads),
        flag(job.replace),
        flag(job.replaceIfSizeDiffers),
        flag(job.replaceOlderThan.isValid()),
        dateText(job.replaceOlderThan),
        flag(job.retryMissing),
        dateText(job.retryMissingOlderThan),
    };
    const QByteArray hash =
        QCryptographicHash::hash(parts.join(QLatin1Char('|')).toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(hash.toHex().left(12));
}

DownloadOutcome DownloadRunner::run(const ResolvedSource& source,
                                    const std::shared_ptr<const geo::IGeometryLonLatPolygon>& area) {
    if (!starter_) {
        starter_ = std::make_shared<ProviderStarter>(app_);
    }
    Context ctx{app_,
                console_,
                policy_,
                termination_,
                job_.progressMode == ProgressMode::Lines,
                source.frameFallback};

    const QString id = jobId(job_, *source.map, *area, source.zooms);
    const QString guid = mapGuidText(*source.map);
    if (ctx.records) {
        console_.progressRecord(phaseRecord(u"download", guid));
    }
    const QString sessionDir = app_.autoSaveDir() + QLatin1Char('/') + id + QLatin1Char('/') + guid;
    QDir dir(sessionDir);

    std::vector<Slot> runs;
    if (job_.restart) {
        dir.removeRecursively();
    } else {
        // newest file (modification time, then name) of each worker group (names starting NN_)
        static const QRegularExpression workerPrefix(QStringLiteral("^(\\d\\d)_"));
        std::map<QString, QFileInfo> newest;
        for (const QFileInfo& file : dir.entryInfoList({QStringLiteral("*.sls")}, QDir::Files)) {
            const auto match = workerPrefix.match(file.fileName());
            const QString group = match.hasMatch() ? match.captured(1) : QString();
            const auto it = newest.find(group);
            if (it == newest.end() || std::make_pair(it->second.lastModified(), it->second.fileName()) <
                                          std::make_pair(file.lastModified(), file.fileName())) {
                newest[group] = file;
            }
        }
        // T-0243 (weather-sources.md 4.3): a saved session of another snapshot is not continued. The legacy
        // resume (u_DownloadSession.pas:373-397) would continue it under its own version.
        if (source.snapshot) {
            for (const auto& [group, file] : newest) {
                QString saved; // a file that cannot be read counts as another snapshot
                if (const auto ini = core::ConfigProviderIni::open(file.absoluteFilePath())) {
                    if (const auto section = (*ini)->subItem(u"Session")) {
                        saved = section->readString(u"VersionDownload", {});
                    }
                }
                if (saved != source.snapshot->version) {
                    console_.warning(Tr::tr("the saved session of \"%1\" is of another snapshot (%2); "
                                            "starting afresh")
                                         .arg(sourceName(*source.map), saved));
                    dir.removeRecursively();
                    newest.clear();
                    break;
                }
            }
        }
        std::vector<StartedRun> started;
        QString current;
        try {
            for (const auto& [group, file] : newest) {
                current = file.absoluteFilePath();
                const QDateTime time = file.lastModified(); // before the start: a run may rewrite the file
                started.push_back(starter_->startBySls(current, false));
                noteContinuing(console_, time);
                if (job_.format == OutputFormat::Tsv) {
                    console_.result(sessionRecord(guid, time, file.absoluteFilePath()));
                }
            }
        } catch (const std::exception& e) {
            console_.warning(Tr::tr("cannot continue %1: %2").arg(current, QString::fromUtf8(e.what())));
            for (auto& run : started) {
                run.cancelNotifier->cancel(); // never destroy a started run without cancel
            }
            started.clear();
            dir.removeRecursively();
        }
        makeSlots(started, sessionDir, sourceName(*source.map), ctx, runs, false, {});
    }

    if (runs.empty()) { // a fresh start
        region::TilesDownloadParams params;
        params.mapType = source.map;
        params.zoomArray = source.zooms;
        params.isIgnoreTne = job_.retryMissing;
        params.loadTneOlderDate = job_.retryMissingOlderThan;
        params.isReplace = job_.replace;
        params.isReplaceIfDifSize = job_.replaceIfSizeDiffers;
        params.isReplaceIfOlder = job_.replaceOlderThan.isValid();
        params.replaceDate = job_.replaceOlderThan.isValid() ? job_.replaceOlderThan : storage::tileDateNow();
        params.sessionAutosaveInterval = job_.autosaveMin;
        params.sessionAutosaveFilePrefix = id + QLatin1Char('/') + guid + QLatin1Char('/');
        params.splitCount = region::clampSplitCount(*source.map, job_.threads);
        params.isStartPaused = false;
        if (params.splitCount < job_.threads) {
            console_.warning(Tr::tr("--threads %1 lowered to %2: the source allows %3 connections")
                                 .arg(job_.threads)
                                 .arg(params.splitCount)
                                 .arg(region::maxSplitCount(*source.map)));
        }
        std::vector<StartedRun> started = starter_->startProcess(area, params);
        makeSlots(started, sessionDir, sourceName(*source.map), ctx, runs, false, {});
    }

    if (runs.empty()) {
        return DownloadOutcome::Completed; // nothing was started: nothing to supervise
    }
    for (auto& slot : runs) {
        slot.guid = guid;
    }
    const auto reason = poll(ctx, runs);
    if (ctx.records) {
        writeProgressRecord(console_, runs); // the final values
    }
    return finish(ctx, runs, reason, true);
}

// `resume`: the GUID is the one of the map the .sls names (read as ProviderTilesDownload::startBySls does).
void DownloadRunner::printSessionRecord(const QString& file, const QDateTime& time) {
    try {
        const auto ini = core::ConfigProviderIni::open(file);
        if (!ini) {
            return; // startBySls has accepted the file; nothing to name
        }
        region::DownloadSession session;
        session.load((*ini)->subItem(u"Session"), *app_.fullMapsSet(), *app_.downloadConfig(),
                     *app_.lonLatFactory());
        console_.result(
            sessionRecord(mapGuidText(*session.mapType()), time, QFileInfo(file).absoluteFilePath()));
    } catch (const std::exception&) {
        return; // the file was started a moment ago; a read failure here only costs the record
    }
}

DownloadOutcome DownloadRunner::resume(const QStringList& slsFiles, const QStringList& sourceNames) {
    if (!starter_) {
        starter_ = std::make_shared<ProviderStarter>(app_);
    }
    Context ctx{app_, console_, policy_, termination_, job_.progressMode == ProgressMode::Lines};

    std::vector<StartedRun> started;
    QStringList files;
    QStringList guids;
    QStringList labels;
    for (qsizetype i = 0; i < slsFiles.size(); ++i) {
        const QString& file = slsFiles.at(i);
        try {
            const QString guid = ctx.records ? sessionMapGuid(app_, file) : QString();
            if (!guid.isEmpty()) {
                console_.progressRecord(phaseRecord(u"download", guid));
            }
            const QDateTime time =
                QFileInfo(file).lastModified(); // before the start: a run may rewrite the file
            started.push_back(starter_->startBySls(file, false));
            noteContinuing(console_, time);
            files.push_back(file);
            guids.push_back(guid);
            labels.push_back(sourceNames.at(i));
            if (job_.format == OutputFormat::Tsv) {
                printSessionRecord(file, time);
            }
        } catch (const std::exception& e) {
            console_.error(Tr::tr("cannot continue %1: %2").arg(file, QString::fromUtf8(e.what())));
        }
    }
    if (started.empty()) {
        return DownloadOutcome::Refused;
    }
    std::vector<Slot> runs;
    makeSlots(started, QString(), QString(), ctx, runs, true, files);
    for (std::size_t i = 0; i < runs.size(); ++i) {
        runs[i].label = labels.at(static_cast<qsizetype>(i)); // the progress line names the session's map
        runs[i].guid = guids.at(static_cast<qsizetype>(i));
    }
    const auto reason = poll(ctx, runs);
    if (ctx.records) {
        writeProgressRecord(console_, runs); // the final values
    }
    // The files of a resume are the user's: only a session of AutoSave/ whose runs all completed is removed
    // (as `run` does); a file elsewhere stays.
    const DownloadOutcome outcome = finish(ctx, runs, reason, false);
    if (outcome == DownloadOutcome::Completed) {
        const QString autoSave = QFileInfo(app_.autoSaveDir()).canonicalFilePath();
        for (const QString& file : files) {
            const QString dir = QFileInfo(file).dir().canonicalPath();
            if (!autoSave.isEmpty() && dir.startsWith(autoSave + QLatin1Char('/'))) {
                QDir(dir).removeRecursively();
            }
        }
    }
    return outcome;
}

} // namespace cg::app
