// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/download_runner (new code; inherits frm_ProgressDownload.pas:268-279, 306-393,
// 431-437, 586-595 and the policy of docs/specs/app-cli.md 1.8, 1.9, 5.2, 6.1-6.3).
// Covers: FEAT-REG-018 (progress, pause, resume): contributes only (docs/parity/held-open.md).
//
// New code, so the expectations are the literals of the spec and the ticket (T-0191 proofs 1-7), not Pascal
// results.
//
// Rig A: a scripted IRegionProcessProgressInfoDownload behind the IRunStarter seam (no worker thread). The
// test drives it through counters and through a hook that resume() calls on the polling thread, so a
// sequence like "pause, resume, pause again without progress" needs no clock and no helper thread. A
// scripted run is "finished" once it is cancelled (the real worker ends at its next test), or when the test
// says so.
// Rig B: the real ProviderTilesDownload with the T-0181 recording subsystem (every request is answered Ok
// with 5 bytes on the calling thread) behind the same seam, wrapped as the production starter wraps it.
// There is no tile cache in Rig B: "the cache holds N tiles" is "N distinct tiles were requested, answered
// and counted as downloaded". The default starter (ProviderStarter over the AppContext maps) is not reached
// by any test: an AppContext holds only maps built from ZMP folders.
#include "cg/app/app_context.h"
#include "cg/app/download_runner.h"
#include "cg/core/language_manager.h"
#include "cg/core/log_ring_buffer.h"
#include "cg/core/operation_notifier.h"
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/net/download_stats.h"
#include "cg/net/global_download_config.h"
#include "cg/net/tile_downloader_config.h"
#include "cg/region/provider_tiles_download.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTime>
#include <QUuid>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "../maptype/maptype_test_util.h"
#include "../maptype/zmp_info_gui_stub.h"
#include "../region/region_fake_map_type.h"
#include "../region/tiles_download_rig.h"

using namespace std::chrono_literals;
using cg::app::AppContext;
using cg::app::AppOptions;
using cg::app::Console;
using cg::app::DownloadOutcome;
using cg::app::DownloadPolicy;
using cg::app::DownloadRunner;
using cg::app::IRunStarter;
using cg::app::Job;
using cg::app::ResolvedSource;
using cg::app::StartedRun;
using cg::geo::LonLat;
using cg::geo::LonLatRect;
using cg::region::DownloadSessionRun;
using cg::region::test::FakeMapType;
using Clock = std::chrono::steady_clock;

namespace {

const QString kGuid = QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}");
const QString kDoneLine = QStringLiteral("The task is completed!");

// The id of a job whose source takes all of the job's zooms (what a source without MaxZoom gets).
QString jobIdOf(const Job& job, const cg::maptype::IMapType& map,
                const cg::geo::IGeometryLonLatPolygon& area) {
    return DownloadRunner::jobId(job, map, area, job.zooms);
}

// A capture of stdout / stderr.
class Stream {
public:
    Stream() : file_(std::tmpfile()) {}
    ~Stream() {
        if (file_ != nullptr) {
            std::fclose(file_);
        }
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    std::FILE* get() const { return file_; }

    QString text() const {
        std::fflush(file_);
        std::rewind(file_);
        QByteArray all;
        char buffer[256];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof buffer, file_)) > 0) {
            all.append(buffer, static_cast<qsizetype>(n));
        }
        std::fseek(file_, 0, SEEK_END);
        return QString::fromUtf8(all);
    }

private:
    std::FILE* file_;
};

// ---- Rig A ---------------------------------------------------------------------------------------------

// What the progress window reads and presses, scripted by the test. resume() runs `onResume` (n = 1-based
// count of resume calls) on the polling thread, after clearing the pause.
class Scripted final : public cg::region::IRegionProcessProgressInfoDownload {
public:
    std::atomic<qint64> processedV{0};
    std::atomic<qint64> totalV{10};
    std::atomic<qint64> downloadedV{0};
    std::atomic<bool> pausedV{false};
    std::atomic<bool> finishedV{false};
    std::atomic<qint64> streakV{0};
    std::atomic<qint64> unavailableRunV{0};
    std::atomic<qint64> inClassV{0};
    std::atomic<qint64> otherV{0};
    std::atomic<int> resumeCalls{0};
    std::atomic<int> isPausedCalls{0};
    Clock::time_point finishAt = Clock::time_point::max(); // set before the run starts
    std::function<void(Scripted&, int)> onResume;
    quint8 zoomV = 0;
    int workerIndexV = 0;
    int workersCountV = 1;
    std::shared_ptr<cg::core::LogRingBuffer> logV = std::make_shared<cg::core::LogRingBuffer>(100);
    std::shared_ptr<cg::core::OperationNotifier> notifier = std::make_shared<cg::core::OperationNotifier>();

    void say(const QString& line) { logV->append(line); }
    // The state of a worker that ended by itself having logged the completion line.
    void complete() {
        say(kDoneLine);
        finishedV = true;
    }
    [[nodiscard]] bool cancelled() const { return notifier->isCancelled(); }

    double processedRatio() const override { return 0; }
    bool finished() const override {
        return finishedV || notifier->isCancelled() || Clock::now() >= finishAt;
    }
    qint64 totalToProcess() const override { return totalV; }
    qint64 downloaded() const override { return downloadedV; }
    qint64 processed() const override { return processedV; }
    quint64 downloadSize() const override { return static_cast<quint64>(downloadedV) * 1024; }
    double elapsedV = 0; // days
    double elapsedWhenFinishedV =
        -1; // days; >= 0 mimics the real info: a finished run reports the stored value
    double elapsedTime() const override {
        return (elapsedWhenFinishedV >= 0 && finished()) ? elapsedWhenFinishedV : elapsedV;
    }
    quint8 zoom() const override { return zoomV; }
    std::vector<quint8> zoomArray() const override { return {zoomV}; }
    void getLastTileInfo(quint8& z, cg::geo::PointI& point, int& size) const override {
        z = zoomV;
        point = cg::geo::PointI{};
        size = 0;
    }
    std::shared_ptr<cg::core::LogRingBuffer> logProvider() const override { return logV; }
    void saveState(cg::core::IConfigWriter& section) override {
        section.writeValue(u"Marker", QVariant(QStringLiteral("scripted")));
    }
    bool isPaused() const override {
        const_cast<std::atomic<int>&>(isPausedCalls)++;
        return pausedV;
    }
    void pause() override { pausedV = true; }
    void resume() override {
        const int n = ++resumeCalls;
        pausedV = false;
        if (onResume) {
            onResume(*this, n);
        }
    }
    bool autoCloseAtFinish() const override { return false; }
    void setAutoCloseAtFinish(bool) override {}
    int sessionAutosaveInterval() const override { return 0; }
    QString sessionAutosavePrefix() const override { return {}; }
    void getWorkerInfo(int& index, int& count) const override {
        index = workerIndexV;
        count = workersCountV;
    }
    qint64 connectionErrorStreak() const override { return streakV; }
    cg::region::FrameAnswers frameAnswers() const override { return {unavailableRunV, inClassV, otherV}; }
};

using ScriptedPtr = std::shared_ptr<Scripted>;

ScriptedPtr completedRun() {
    auto run = std::make_shared<Scripted>();
    run->complete();
    return run;
}

StartedRun startedOf(const ScriptedPtr& run) {
    return StartedRun{run, run->notifier, run};
}

// Hands out the `prepared` runs in order: startProcess returns all that are left, startBySls one.
class ScriptedStarter final : public IRunStarter {
public:
    std::vector<ScriptedPtr> prepared;
    std::vector<ScriptedPtr> fresh; // if not empty, startProcess returns these instead
    std::set<QString> failing;      // file names whose startBySls throws "boom"
    QString watchDir;               // what the session directory holds when startProcess is called
    int startProcessCalls = 0;
    cg::region::TilesDownloadParams params;
    cg::region::PolygonPtr area;
    bool dirExistedAtFreshStart = false;
    QStringList entriesAtFreshStart;
    std::vector<QString> slsCalls; // absolute file names
    std::vector<bool> pausedArgs;
    std::function<void(const QString&)> onSls; // runs inside startBySls (a run that rewrites its file)

    std::vector<StartedRun> startProcess(const cg::region::PolygonPtr& polygon,
                                         const cg::region::TilesDownloadParams& p) override {
        ++startProcessCalls;
        params = p;
        area = polygon;
        if (!watchDir.isEmpty()) {
            dirExistedAtFreshStart = QDir(watchDir).exists();
            entriesAtFreshStart = QDir(watchDir).entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        }
        std::vector<StartedRun> result;
        for (const ScriptedPtr& run : fresh.empty() ? prepared : fresh) {
            result.push_back(startedOf(run));
        }
        prepared.clear();
        fresh.clear();
        return result;
    }

    StartedRun startBySls(const QString& fileName, bool startPaused) override {
        slsCalls.push_back(fileName);
        pausedArgs.push_back(startPaused);
        if (onSls) {
            onSls(fileName);
        }
        if (failing.contains(QFileInfo(fileName).fileName())) {
            throw std::runtime_error("boom");
        }
        if (prepared.empty()) {
            prepared.push_back(completedRun());
        }
        const ScriptedPtr run = prepared.front();
        prepared.erase(prepared.begin());
        return startedOf(run);
    }
};

// ---- Rig B ---------------------------------------------------------------------------------------------

// The map's name "Chart Name" (the progress line shows it).
std::shared_ptr<cg::maptype::MapTypeGuiConfig> chartNameGui() {
    static QTemporaryDir languageDir;
    static auto languages = std::make_shared<cg::core::LanguageManager>(languageDir.path());
    auto gui = std::make_shared<cg::maptype::test::ZmpGuiStub>();
    gui->nameEn = QStringLiteral("Chart Name");
    return std::make_shared<cg::maptype::MapTypeGuiConfig>(languages, gui);
}

struct RigB {
    std::shared_ptr<cg::maptype::MapVersionFactoryChangeable> factory =
        std::make_shared<cg::maptype::MapVersionFactoryChangeable>(
            std::make_shared<cg::storage::MapVersionFactory>());
    std::shared_ptr<FakeMapType> map = std::make_shared<FakeMapType>();
    std::shared_ptr<cg::region::test::RecordingSubsystem> subsystem =
        std::make_shared<cg::region::test::RecordingSubsystem>();
    std::shared_ptr<cg::region::test::EmptyStorage> storage =
        std::make_shared<cg::region::test::EmptyStorage>();
    cg::maptype::IMapTypeSetPtr mapSet;
    std::shared_ptr<const cg::geo::GeometryProjectedFactory> projected =
        std::make_shared<const cg::geo::GeometryProjectedFactory>();
    std::shared_ptr<const cg::geo::TileIteratorFactory> iterators =
        std::make_shared<const cg::geo::TileIteratorFactory>(projected);
    std::shared_ptr<const cg::geo::GeometryLonLatFactory> lonLat =
        std::make_shared<const cg::geo::GeometryLonLatFactory>();
    std::shared_ptr<cg::net::GlobalDownloadConfig> config = std::make_shared<cg::net::GlobalDownloadConfig>();
    std::shared_ptr<cg::net::DownloadInfo> parentInfo = std::make_shared<cg::net::DownloadInfo>();
    std::stop_source appClosing;

    explicit RigB(quint32 maxConnect) {
        cg::geo::ProjectionSetFactory setFactory{
            cg::geo::HashSeed{}, std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})};
        map->guidValue = QUuid::fromString(kGuid);
        map->guiConfigValue = chartNameGui();
        map->projectionSetValue =
            setFactory.byCode(cg::geo::kProjEpsgGoogle, cg::geo::kTileSplitQuadrate256x256);
        map->versionFactoryValue = factory;
        map->versionRequestValue = std::make_shared<cg::maptype::MapVersionRequestChangeable>(
            std::make_shared<cg::maptype::MapVersionRequestConfig>(QStringLiteral("base-ver")), factory);
        map->abilitiesValue = std::make_shared<cg::maptype::MapAbilitiesConfig>(
            std::make_shared<const cg::maptype::MapAbilitiesConfigStatic>(
                cg::maptype::MapAbilitiesConfigStatic{true, true}));
        map->tileDownloadSubsystemValue = subsystem;
        map->tileStorageValue = storage;
        cg::net::TileDownloaderSettings settings;
        settings.maxConnectToServerCount = maxConnect;
        map->tileDownloaderConfigValue = std::make_shared<cg::net::TileDownloaderConfig>(
            std::make_shared<cg::net::InetConfig>(), settings);
        auto builder = cg::maptype::MapTypeSetBuilderFactory().build(false);
        builder->add(map);
        mapSet = builder->makeAndClear();
        config->setUseSessionLastSuccess(true);
    }
};

// The production starter's wrapping (download_runner.cpp ProviderStarter), over the rig's own map set.
class RealStarter final : public IRunStarter {
public:
    explicit RealStarter(RigB& rig)
        : provider_(rig.appClosing.get_token(), rig.iterators, rig.mapSet, rig.lonLat, rig.projected,
                    rig.config, rig.parentInfo) {}

    std::vector<std::shared_ptr<cg::region::RegionProcessProgressInfoDownload>> progress;
    cg::region::TilesDownloadParams params;
    int runsStarted = 0;

    std::vector<StartedRun> startProcess(const cg::region::PolygonPtr& polygon,
                                         const cg::region::TilesDownloadParams& p) override {
        params = p;
        std::vector<StartedRun> result;
        for (auto& run : provider_.startProcess(polygon, p)) {
            progress.push_back(run.progressInfo);
            ++runsStarted;
            result.push_back(wrap(std::move(run)));
        }
        return result;
    }
    StartedRun startBySls(const QString& fileName, bool startPaused) override {
        return wrap(provider_.startBySls(fileName, startPaused));
    }

private:
    static StartedRun wrap(DownloadSessionRun&& run) {
        StartedRun started;
        started.progress = run.progressInfo;
        started.cancelNotifier = run.cancelNotifier;
        started.owner = std::make_shared<DownloadSessionRun>(std::move(run));
        return started;
    }

    cg::region::ProviderTilesDownload provider_;
};

// ---- fixture -------------------------------------------------------------------------------------------

class RunnerTest : public ::testing::Test {
public:
    static void SetUpTestSuite() {
        cg::maptype::test::ensureGuiApp();
        dataDir_ = std::make_unique<QTemporaryDir>();
        app_ = makeApp(dataDir_->path());
    }
    static void TearDownTestSuite() {
        app_.reset();
        dataDir_.reset();
    }

protected:
    static std::unique_ptr<AppContext> makeApp(const QString& dataDir) {
        AppOptions options;
        options.arguments = {QStringLiteral("--datadir=") + dataDir};
        options.warn = [](const QString&) {};
        options.mapsDirs = {dataDir + QStringLiteral("/Maps")};
        auto created = AppContext::create(options);
        if (!created) {
            throw std::runtime_error("AppContext::create failed");
        }
        return std::move(created.value());
    }

    void SetUp() override {
        QDir(app_->autoSaveDir()).removeRecursively();
        map_ = makeMap(kGuid, 4);
        job_.zooms = {4};
        area_ = rect(-170, 30, -100, -10);
        starter_ = std::make_shared<ScriptedStarter>();
    }

    static std::shared_ptr<FakeMapType> makeMap(const QString& guid, quint32 maxConnect) {
        auto map = std::make_shared<FakeMapType>();
        map->guidValue = QUuid::fromString(guid);
        map->guiConfigValue = chartNameGui();
        cg::net::TileDownloaderSettings settings;
        settings.maxConnectToServerCount = maxConnect;
        map->tileDownloaderConfigValue = std::make_shared<cg::net::TileDownloaderConfig>(
            std::make_shared<cg::net::InetConfig>(), settings);
        return map;
    }

    static std::shared_ptr<const cg::geo::IGeometryLonLatPolygon> rect(double l, double t, double r,
                                                                       double b) {
        return cg::geo::GeometryLonLatFactory{}.createPolygonByRect(LonLatRect{l, t, r, b});
    }

    [[nodiscard]] ResolvedSource source() const {
        return ResolvedSource{map_,      false,        QString(), QStringLiteral("Src"),
                              QString(), std::nullopt, job_.zooms};
    }

    [[nodiscard]] QString sessionDir() const {
        return app_->autoSaveDir() + QLatin1Char('/') + jobIdOf(job_, *map_, *area_) + QLatin1Char('/') +
               kGuid;
    }

    DownloadOutcome run(DownloadPolicy policy, std::stop_token stop = {}) {
        DownloadRunner runner(*app_, console_, job_, stop, policy, starter_);
        return runner.run(source(), area_);
    }

    static DownloadPolicy fast() {
        DownloadPolicy policy;
        policy.pollMs = 10;
        policy.resumeDelaySec = 0;
        policy.stallTimeoutSec = 0;
        return policy;
    }

    [[nodiscard]] QStringList sessionFiles() const {
        return QDir(sessionDir()).entryList({QStringLiteral("*.sls")}, QDir::Files, QDir::Name);
    }

    static void touch(const QString& path, const QDateTime& when) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("[Session]\n");
        file.close();
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(when, QFileDevice::FileModificationTime));
    }

    static inline std::unique_ptr<QTemporaryDir> dataDir_;
    static inline std::unique_ptr<AppContext> app_;

    Stream out_;
    Stream err_;
    Console console_{1, true, out_.get(), err_.get()};
    Job job_;
    std::shared_ptr<FakeMapType> map_;
    std::shared_ptr<const cg::geo::IGeometryLonLatPolygon> area_;
    std::shared_ptr<ScriptedStarter> starter_;
};

QDateTime at(int y, int mo, int d, int h, int mi, int s = 0) {
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s));
}

} // namespace

// ---- 0. policy constants -------------------------------------------------------------------------------

// app-cli.md 1.8: wait 5 s, three save retries, 300 s stall; 1.10: one poll a second.
TEST_F(RunnerTest, DefaultPolicyIsTheSpecs) {
    const DownloadPolicy policy;
    EXPECT_EQ(policy.stallTimeoutSec, 300);
    EXPECT_EQ(policy.saveRetries, 3);
    EXPECT_EQ(policy.maxConnectionErrors, 60); // T-0271
    EXPECT_EQ(policy.resumeDelaySec, 5);
    EXPECT_EQ(policy.pollMs, 1000);
}

// ---- 1. unsolicited pause ------------------------------------------------------------------------------

// A pause nobody asked for is resumed only after resumeDelaySec: the first resume() comes at or after one
// second from the start, and (bounded) not much later.
TEST_F(RunnerTest, UnsolicitedPauseIsResumedAfterTheDelay) {
    auto stuck = std::make_shared<Scripted>();
    stuck->pausedV = true;
    Clock::time_point resumedAt;
    stuck->onResume = [&](Scripted& self, int) {
        resumedAt = Clock::now();
        self.complete();
    };
    starter_->prepared = {stuck};
    DownloadPolicy policy = fast();
    policy.resumeDelaySec = 1;

    const auto started = Clock::now();
    EXPECT_EQ(run(policy), DownloadOutcome::Completed);
    EXPECT_EQ(stuck->resumeCalls, 1);
    EXPECT_GE(resumedAt - started, 1000ms);
    EXPECT_LT(resumedAt - started, 5000ms);
}

// Three resumes with no tile processed in between: SaveFailed after the third, with the newest log line in
// the message; every run is cancelled and a final.sls is written for it (NN_ with two runs).
TEST_F(RunnerTest, ThreePausesWithoutProgressAreSaveFailed) {
    auto stuck = std::make_shared<Scripted>();
    stuck->workersCountV = 2;
    stuck->workerIndexV = 0;
    stuck->pausedV = true;
    stuck->say(QStringLiteral("disk full"));
    stuck->onResume = [](Scripted& self, int) { self.pausedV = true; }; // pauses again at once
    auto other = std::make_shared<Scripted>();
    other->workersCountV = 2;
    other->workerIndexV = 1;
    starter_->prepared = {stuck, other};

    EXPECT_EQ(run(fast()), DownloadOutcome::SaveFailed);
    EXPECT_EQ(stuck->resumeCalls, 3);
    EXPECT_EQ(other->resumeCalls, 0);
    EXPECT_TRUE(stuck->cancelled());
    EXPECT_TRUE(other->cancelled());
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("01_final.sls"), QStringLiteral("02_final.sls")}));
    EXPECT_TRUE(err_.text().contains(QStringLiteral("error: cannot store tiles: disk full")));
    EXPECT_TRUE(
        err_.text().contains(QStringLiteral("session saved; run the same command again to continue")));
    EXPECT_TRUE(err_.text().contains(sessionDir() + QStringLiteral("/01_final.sls")));
}

// One run: its session file is final.sls without a prefix.
TEST_F(RunnerTest, OneRunSavesFinalSlsWithoutPrefix) {
    auto stuck = std::make_shared<Scripted>();
    stuck->pausedV = true;
    stuck->onResume = [](Scripted& self, int) { self.pausedV = true; };
    starter_->prepared = {stuck};
    EXPECT_EQ(run(fast()), DownloadOutcome::SaveFailed);
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("final.sls")}));
    QFile file(sessionDir() + QStringLiteral("/final.sls"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("Marker")); // the progress object's own state was written
}

// pause, resume, pause, resume, (a tile is processed), pause, resume, pause, resume, end: a tile between the
// second and the third pause restarts the count, so the fourth resume is only the second of a new series.
TEST_F(RunnerTest, ProgressBetweenPausesRestartsTheCount) {
    auto run1 = std::make_shared<Scripted>();
    run1->pausedV = true;
    run1->onResume = [](Scripted& self, int n) {
        if (n == 4) {
            self.complete();
            return;
        }
        if (n == 2) {
            self.processedV = 1; // a tile was stored after the second resume, then the next one cannot be
        }
        self.pausedV = true;
    };
    starter_->prepared = {run1};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    EXPECT_EQ(run1->resumeCalls, 4);
}

// ---- 2. stall ------------------------------------------------------------------------------------------

// processed() unchanged for stallTimeoutSec: Stalled, not before the timeout and within it plus a few polls.
TEST_F(RunnerTest, NoProgressForTheTimeoutIsStalled) {
    auto stuck = std::make_shared<Scripted>();
    stuck->say(QStringLiteral("proxy error"));
    starter_->prepared = {stuck};
    DownloadPolicy policy = fast();
    policy.stallTimeoutSec = 1;

    const auto started = Clock::now();
    EXPECT_EQ(run(policy), DownloadOutcome::Stalled);
    const auto took = Clock::now() - started;
    EXPECT_GE(took, 1000ms);
    EXPECT_LT(took, 1000ms + 2 * 10ms + 2000ms); // timeout + 2 polls + load slack
    EXPECT_TRUE(stuck->cancelled());
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("final.sls")}));
    EXPECT_TRUE(err_.text().contains(QStringLiteral("error: no progress for 1 s: proxy error")));
}

// The stall is on the SUM of the runs: one run stands still while the other advances, nothing stalls.
TEST_F(RunnerTest, StallIsMeasuredOnTheSumOfTheRuns) {
    auto stuck = std::make_shared<Scripted>();
    auto moving = std::make_shared<Scripted>();
    const auto end = Clock::now() + 1600ms; // longer than the 1 s timeout
    stuck->say(kDoneLine);
    stuck->finishAt = end;
    moving->say(kDoneLine);
    moving->finishAt = end;
    starter_->prepared = {stuck, moving};
    std::atomic<bool> stop{false};
    std::thread driver([&] {
        while (!stop) {
            moving->processedV++;
            std::this_thread::sleep_for(5ms);
        }
    });
    DownloadPolicy policy = fast();
    policy.stallTimeoutSec = 1;
    const DownloadOutcome outcome = run(policy);
    stop = true;
    driver.join();
    EXPECT_EQ(outcome, DownloadOutcome::Completed);
}

// --stall-timeout 0 never stalls: 50 polls with no progress end by themselves.
TEST_F(RunnerTest, ZeroStallTimeoutNeverStalls) {
    auto idle = std::make_shared<Scripted>();
    idle->say(kDoneLine);
    idle->finishAt = Clock::now() + 600ms; // 60 polls of 10 ms
    starter_->prepared = {idle};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    EXPECT_FALSE(QDir(sessionDir()).exists());
}

// T-0271 (deviates from L-271): a streak of connection errors stops the run with Stalled, at the limit and
// not one tile before; 0 never stops.
TEST_F(RunnerTest, ConnectionErrorStreakAtTheLimitStops) {
    auto lost = std::make_shared<Scripted>();
    lost->say(QStringLiteral("Error connecting to server"));
    lost->streakV = 60;
    lost->finishAt = Clock::now() + 2000ms; // a broken check ends in Completed, not in a hang
    starter_->prepared = {lost};
    DownloadPolicy policy = fast();
    policy.stallTimeoutSec = 0;
    policy.maxConnectionErrors = 60;
    EXPECT_EQ(run(policy), DownloadOutcome::Stalled);
    EXPECT_TRUE(lost->cancelled());
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("final.sls")}));
    EXPECT_TRUE(err_.text().contains(QStringLiteral(
        "error: no connection to the server for 60 tiles in a row: Error connecting to server")));
}

TEST_F(RunnerTest, ConnectionErrorStreakBelowTheLimitOrWithZeroLimitDoesNotStop) {
    for (const auto& [streak, limit] : {std::pair<qint64, int>{59, 60}, {1000, 0}}) {
        SCOPED_TRACE(streak);
        auto run1 = std::make_shared<Scripted>();
        run1->say(kDoneLine);
        run1->streakV = streak;
        run1->finishAt = Clock::now() + 300ms;
        starter_->prepared = {run1};
        DownloadPolicy policy = fast();
        policy.stallTimeoutSec = 0;
        policy.maxConnectionErrors = limit;
        EXPECT_EQ(run(policy), DownloadOutcome::Completed);
    }
}

// ---- T-0356: the newest young frame is not served --------------------------------------------------------

namespace {
// Completed = the run goes on to its end; Early = the stop comes at once; End = the stop comes when the run
// finishes (the scripted run finishes after kFrameRunMs).
enum class FrameStop : std::uint8_t { Completed, Early, End };
constexpr auto kFrameRunMs = 1000ms;

struct NotServedCase {
    const char* name;
    bool frameFallback;
    qint64 total;
    qint64 run;     // Unavailable answers in a row
    qint64 inClass; // all answers in the class
    qint64 other;   // answers outside it (a stored tile among them)
    FrameStop stop;
};

// Runs one scripted download and checks the outcome and when it came.
void expectFrameStop(FrameStop stop, DownloadOutcome outcome, Clock::duration elapsed) {
    EXPECT_EQ(outcome,
              stop == FrameStop::Completed ? DownloadOutcome::Completed : DownloadOutcome::FrameNotServed);
    if (stop == FrameStop::Early) {
        EXPECT_LT(elapsed, kFrameRunMs * 7 / 10);
    } else {
        EXPECT_GE(elapsed, kFrameRunMs * 7 / 10);
    }
}
} // namespace

// The runner stops a run of a source with frameFallback (a) early, when nothing was stored and the latest
// min(3, total) answers are all Unavailable (5xx, ban), and (b) at the end, when the run finished with
// answers in the class only (404, empty, TNE). Every other state runs on.
TEST_F(RunnerTest, FrameNotServedStopsOnlyWhenNothingIsServedAndTheAnswersAreAllOfTheClass) {
    const std::vector<NotServedCase> cases{
        {"three 5xx in a row, nothing else", true, 10, 3, 3, 0, FrameStop::Early},
        {"more than three", true, 10, 7, 7, 0, FrameStop::Early},
        {"two 5xx are not enough to stop early", true, 10, 2, 2, 0, FrameStop::End},
        {"404 answers stop only at the end", true, 10, 0, 5, 0, FrameStop::End},
        {"three 5xx after a 404", true, 10, 3, 4, 0, FrameStop::Early},
        {"a tile was served before the 5xx", true, 10, 5, 5, 1, FrameStop::Completed},
        {"the area has two tiles: both answers count", true, 2, 2, 2, 0, FrameStop::Early},
        {"no answer at all", true, 10, 0, 0, 0, FrameStop::Completed},
        {"the source has no fallback (5xx)", false, 10, 5, 5, 0, FrameStop::Completed},
        {"the source has no fallback (404)", false, 10, 0, 10, 0, FrameStop::Completed},
        {"404 answers and a served tile", true, 10, 0, 7, 3, FrameStop::Completed},
    };
    for (const NotServedCase& c : cases) {
        SCOPED_TRACE(c.name);
        auto run1 = std::make_shared<Scripted>();
        run1->say(kDoneLine);
        run1->totalV = c.total;
        run1->unavailableRunV = c.run;
        run1->inClassV = c.inClass;
        run1->otherV = c.other;
        run1->finishAt = Clock::now() + kFrameRunMs;
        starter_->prepared = {run1};
        ResolvedSource src = source();
        src.frameFallback = c.frameFallback;
        DownloadRunner runner(*app_, console_, job_, {}, fast(), starter_);
        const Clock::time_point start = Clock::now();
        const DownloadOutcome outcome = runner.run(src, area_);
        expectFrameStop(c.stop, outcome, Clock::now() - start);
        EXPECT_TRUE(sessionFiles().isEmpty()); // nothing to continue after a stop, either
    }
    EXPECT_FALSE(err_.text().contains(QStringLiteral("session saved")));
}

// Two workers: the early stop needs both runs in agreement; the end stop needs no tile served in either.
TEST_F(RunnerTest, FrameNotServedNeedsEveryWorkerRunToAgree) {
    struct Pair {
        qint64 run1;
        qint64 other1;
        qint64 inClass1;
        qint64 run2;
        qint64 other2;
        qint64 inClass2;
        FrameStop stop;
    };
    const std::vector<Pair> pairs{
        {3, 0, 3, 0, 0, 0, FrameStop::End}, // the second run has no answer yet: no early stop
        {3, 0, 3, 3, 0, 3, FrameStop::Early},
        {3, 0, 3, 0, 1, 0, FrameStop::Completed}, // the second run stored a tile
        {0, 2, 2, 0, 0, 5, FrameStop::Completed}, // the first run stored a tile
        {0, 0, 5, 0, 2, 2, FrameStop::Completed},
    };
    for (const Pair& p : pairs) {
        SCOPED_TRACE(&p - pairs.data());
        std::vector<ScriptedPtr> workers;
        for (int index = 0; index < 2; ++index) {
            auto worker = std::make_shared<Scripted>();
            worker->workersCountV = 2;
            worker->workerIndexV = index;
            worker->unavailableRunV = index == 0 ? p.run1 : p.run2;
            worker->otherV = index == 0 ? p.other1 : p.other2;
            worker->inClassV = index == 0 ? p.inClass1 : p.inClass2;
            worker->say(kDoneLine);
            worker->finishAt = Clock::now() + kFrameRunMs;
            workers.push_back(worker);
        }
        starter_->prepared = workers;
        ResolvedSource src = source();
        src.frameFallback = true;
        DownloadRunner runner(*app_, console_, job_, {}, fast(), starter_);
        const Clock::time_point start = Clock::now();
        const DownloadOutcome outcome = runner.run(src, area_);
        expectFrameStop(p.stop, outcome, Clock::now() - start);
    }
}

// ---- 3. termination ------------------------------------------------------------------------------------

// The token is stopped while the runner waits in a 1000 ms poll: it returns at once (a plain sleep would
// keep it for the rest of the second), every run is cancelled and its session saved.
TEST_F(RunnerTest, TerminationWakesTheWaitAtOnce) {
    auto first = std::make_shared<Scripted>();
    first->workersCountV = 2;
    first->workerIndexV = 0;
    auto second = std::make_shared<Scripted>();
    second->workersCountV = 2;
    second->workerIndexV = 1;
    starter_->prepared = {first, second};
    DownloadPolicy policy = fast();
    policy.pollMs = 1000;

    std::stop_source stop;
    Clock::time_point stoppedAt;
    std::thread stopper([&] {
        const auto bound = Clock::now() + 30s;
        while (first->isPausedCalls < 1 && Clock::now() < bound) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(100ms); // the runner is now inside its wait
        stoppedAt = Clock::now();
        stop.request_stop();
    });
    const DownloadOutcome outcome = run(policy, stop.get_token());
    const auto returnedAt = Clock::now();
    stopper.join();

    EXPECT_EQ(outcome, DownloadOutcome::Interrupted);
    EXPECT_LT(returnedAt - stoppedAt, 400ms);
    EXPECT_TRUE(first->cancelled());
    EXPECT_TRUE(second->cancelled());
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("01_final.sls"), QStringLiteral("02_final.sls")}));
    EXPECT_TRUE(
        err_.text().contains(QStringLiteral("session saved; run the same command again to continue")));
}

// app.appClosing() is a termination too (region-download.md section 7). Own AppContext: the flag cannot be
// cleared.
TEST_F(RunnerTest, AppClosingIsTermination) {
    QTemporaryDir otherDir;
    const std::unique_ptr<AppContext> other = makeApp(otherDir.path());
    auto idle = std::make_shared<Scripted>();
    starter_->prepared = {idle};
    other->requestClose();
    DownloadRunner runner(*other, console_, job_, std::stop_token{}, fast(), starter_);
    EXPECT_EQ(runner.run(source(), area_), DownloadOutcome::Interrupted);
    EXPECT_TRUE(idle->cancelled());
}

// ---- progress and log ----------------------------------------------------------------------------------

// The worker log reaches the console and the progress line is made of the sums over the runs: processed
// 3 + 4 of 10 + 10, downloaded 2 + 1 tiles, the highest zoom (0-based 6) shown as z7 (legacy shows VZoom + 1,
// frm_ProgressDownload.pas:321). The line starts with the source's name (app-cli.md 1.10), not with the
// ResolvedSource name ("Src", the MBTiles name, which `download` leaves empty).
TEST_F(RunnerTest, LogLinesAndTheSummedProgressLineReachTheConsole) {
    auto a = std::make_shared<Scripted>();
    auto b = std::make_shared<Scripted>();
    a->processedV = 3;
    a->downloadedV = 2;
    a->zoomV = 4;
    a->say(QStringLiteral("line from a"));
    b->processedV = 4;
    b->downloadedV = 1;
    b->zoomV = 6;
    b->say(QStringLiteral("line from b"));
    a->complete();
    b->complete();
    starter_->prepared = {a, b};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    const QString text = err_.text();
    EXPECT_TRUE(text.contains(QStringLiteral("line from a")));
    EXPECT_TRUE(text.contains(QStringLiteral("line from b")));
    EXPECT_TRUE(text.contains(QStringLiteral("Chart Name  z7  7/20 (35%)  downloaded 3 tiles ")));
}

// T-0267: the final line is made of the final counters (processed == total): 100%.
TEST_F(RunnerTest, NonTerminalFinalLineCarriesTheFinalCounters) {
    auto a = std::make_shared<Scripted>();
    auto b = std::make_shared<Scripted>();
    a->processedV = 10;
    b->processedV = 10;
    a->complete();
    b->complete();
    starter_->prepared = {a, b};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    const QStringList lines = err_.text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString last;
    for (const QString& l : lines) {
        if (l.startsWith(QStringLiteral("Chart Name  z"))) {
            last = l;
        }
    }
    EXPECT_TRUE(last.contains(QStringLiteral("20/20 (100%)"))) << qPrintable(last);
}

// ---- --progress=lines records (T-0203) ---------------------------------------------------------------

namespace {

QStringList recordLines(const QString& text, const QString& type) {
    QStringList found;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(type + QLatin1Char('\t'))) {
            found.push_back(line);
        }
    }
    return found;
}

} // namespace

// Proof 1-3 (runner part): phase first, the record carries the sums of the runs, the highest zoom 0-based (6,
// not the 7 of the text line), 0.5 s given in days rounds to 1; no text progress line at all.
TEST_F(RunnerTest, LinesModeWritesThePhaseAndTheSummedProgressRecordsOnly) {
    auto a = std::make_shared<Scripted>();
    auto b = std::make_shared<Scripted>();
    a->processedV = 3;
    a->downloadedV = 2;
    a->zoomV = 4;
    a->elapsedV = 0.5 / 86400.0;
    b->processedV = 4;
    b->downloadedV = 1;
    b->zoomV = 6;
    a->complete();
    b->complete();
    starter_->prepared = {a, b};
    job_.progressMode = cg::app::ProgressMode::Lines;
    console_.setProgressRecords(true);
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    const QString text = err_.text();
    EXPECT_TRUE(text.startsWith(QStringLiteral("phase\tdownload\t") + kGuid + QLatin1Char('\n')))
        << qPrintable(text);
    const QStringList records = recordLines(text, QStringLiteral("progress"));
    ASSERT_GE(records.size(), 2) << qPrintable(text); // one poll and the final one
    const QString expected =
        QStringLiteral("progress\tdownload\t") + kGuid + QStringLiteral("\t6\t7\t20\t3\t3072\t1");
    EXPECT_EQ(records.first(), expected);
    EXPECT_EQ(records.last(), expected);
    EXPECT_FALSE(text.contains(QStringLiteral("Src  z")));
    EXPECT_FALSE(text.contains(QLatin1Char('\r')));
}

TEST_F(RunnerTest, LinesModeElapsedSecondsAreZeroForZeroAndRoundedOtherwise) {
    for (const auto& [days, seconds] :
         std::vector<std::pair<double, int>>{{0.0, 0}, {1.4 / 86400.0, 1}, {90.0 / 86400.0, 90}}) {
        const qsizetype before = err_.text().size();
        auto a = std::make_shared<Scripted>();
        a->elapsedV = days;
        a->complete();
        starter_->prepared = {a};
        job_.progressMode = cg::app::ProgressMode::Lines;
        console_.setProgressRecords(true);
        EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
        const QStringList records = recordLines(err_.text().mid(before), QStringLiteral("progress"));
        ASSERT_FALSE(records.isEmpty());
        EXPECT_EQ(records.last().split(QLatin1Char('\t')).last(), QString::number(seconds)) << days;
    }
}

// T-0233: a finished run reports 0 (as the real info does when never paused); the records must not go back.
TEST_F(RunnerTest, LinesModeElapsedNeverDecreasesAfterTheRunFinishes) {
    auto a = std::make_shared<Scripted>();
    a->elapsedV = 3.0 / 86400.0;
    a->elapsedWhenFinishedV = 0;
    a->finishAt = Clock::now() + std::chrono::milliseconds(150);
    starter_->prepared = {a};
    job_.progressMode = cg::app::ProgressMode::Lines;
    console_.setProgressRecords(true);
    a->say(kDoneLine);
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    const QStringList records = recordLines(err_.text(), QStringLiteral("progress"));
    ASSERT_GE(records.size(), 2) << qPrintable(err_.text());
    qint64 previous = 0;
    for (const QString& record : records) {
        const qint64 seconds = record.split(QLatin1Char('\t')).last().toLongLong();
        EXPECT_GE(seconds, previous) << qPrintable(record);
        previous = seconds;
    }
    EXPECT_EQ(previous, 3); // the last record keeps the value seen while the run was going
}

// Proof 5: in Auto mode nothing of this is written.
TEST_F(RunnerTest, AutoModeWritesNoRecords) {
    auto a = std::make_shared<Scripted>();
    a->complete();
    starter_->prepared = {a};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    EXPECT_FALSE(err_.text().contains(QStringLiteral("phase\t")));
    EXPECT_FALSE(err_.text().contains(QStringLiteral("progress\t")));
}

// ---- 4. finish inference -------------------------------------------------------------------------------

// The newest line is the completion text: Completed, even with processed < total; the session directory
// (and what is in it) is removed.
TEST_F(RunnerTest, CompletionLineCompletesAndRemovesTheSessionDirectory) {
    auto done = std::make_shared<Scripted>();
    done->processedV = 3;
    done->totalV = 10;
    done->complete();
    starter_->prepared = {done};
    QDir().mkpath(sessionDir());
    QFile note(sessionDir() + QStringLiteral("/note.txt"));
    ASSERT_TRUE(note.open(QIODevice::WriteOnly));
    note.close();

    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    EXPECT_FALSE(QDir(sessionDir()).exists());
    EXPECT_FALSE(err_.text().contains(QStringLiteral("session saved")));
}

// Another newest line: WorkerFailed with that line printed, although processed == total.
TEST_F(RunnerTest, OtherLastLineIsWorkerFailed) {
    auto failed = std::make_shared<Scripted>();
    failed->processedV = 10;
    failed->totalV = 10;
    failed->say(kDoneLine);
    failed->say(QStringLiteral("Unexpected error in the worker"));
    failed->finishedV = true;
    starter_->prepared = {failed};

    EXPECT_EQ(run(fast()), DownloadOutcome::WorkerFailed);
    EXPECT_TRUE(err_.text().contains(QStringLiteral("error: Unexpected error in the worker")));
    EXPECT_TRUE(QDir(sessionDir()).exists()); // not removed
}

// Two runs, one completed and one failed: the worst outcome; only the run that did not complete is saved
// (as 02_final.sls), the completed one writes nothing.
TEST_F(RunnerTest, MixedRunsGiveTheWorstOutcomeAndSaveOnlyTheFailedOne) {
    auto good = std::make_shared<Scripted>();
    good->workersCountV = 2;
    good->workerIndexV = 0;
    good->complete();
    auto bad = std::make_shared<Scripted>();
    bad->workersCountV = 2;
    bad->workerIndexV = 1;
    bad->say(QStringLiteral("tile store broke"));
    bad->finishedV = true;
    starter_->prepared = {good, bad};

    EXPECT_EQ(run(fast()), DownloadOutcome::WorkerFailed);
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("02_final.sls")}));
    EXPECT_TRUE(err_.text().contains(QStringLiteral("error: tile store broke")));
}

// T-0234: the termination arrives when the last tile is in (every run completed): nothing is interrupted.
// Completed, no cancel is needed for the outcome, no "session saved", the session directory is removed.
TEST_F(RunnerTest, TerminationAfterTheLastTileStillCompletesAndRemovesTheState) {
    auto done = std::make_shared<Scripted>();
    done->processedV = 10;
    done->complete();
    starter_->prepared = {done};
    QDir().mkpath(sessionDir());
    QFile old(sessionDir() + QStringLiteral("/201615_584.sls"));
    ASSERT_TRUE(old.open(QIODevice::WriteOnly));
    old.close();
    std::stop_source stop;
    stop.request_stop();

    EXPECT_EQ(run(fast(), stop.get_token()), DownloadOutcome::Completed);
    EXPECT_FALSE(QDir(sessionDir()).exists());
    EXPECT_FALSE(err_.text().contains(QStringLiteral("session saved")));
}

// The same with two runs, one of them still working: the interruption stands, only the unfinished run is
// saved (02_final.sls) and the warning is printed.
TEST_F(RunnerTest, TerminationWithOneRunStillWorkingStaysInterrupted) {
    auto good = std::make_shared<Scripted>();
    good->workersCountV = 2;
    good->workerIndexV = 0;
    good->complete();
    auto busy = std::make_shared<Scripted>();
    busy->workersCountV = 2;
    busy->workerIndexV = 1;
    starter_->prepared = {good, busy};
    std::stop_source stop;
    stop.request_stop();

    EXPECT_EQ(run(fast(), stop.get_token()), DownloadOutcome::Interrupted);
    EXPECT_EQ(sessionFiles(), (QStringList{QStringLiteral("02_final.sls")}));
    EXPECT_TRUE(
        err_.text().contains(QStringLiteral("session saved; run the same command again to continue")));
}

// `resume` of a file in AutoSave/ whose run completed before the termination was seen: Completed, the
// session directory is removed (as for an uninterrupted resume).
TEST_F(RunnerTest, ResumeTerminatedAfterTheLastTileCompletesAndRemovesTheAutoSaveDirectory) {
    const QString file = sessionDir() + QStringLiteral("/201615_584.sls");
    touch(file, at(2024, 1, 1, 0, 0));
    starter_->prepared = {completedRun()};
    std::stop_source stop;
    stop.request_stop();
    DownloadRunner runner(*app_, console_, job_, stop.get_token(), fast(), starter_);

    EXPECT_EQ(runner.resume({file}, {QStringLiteral("M")}), DownloadOutcome::Completed);
    EXPECT_FALSE(QDir(sessionDir()).exists());
}

// ---- 5. resume selection -------------------------------------------------------------------------------

// Per worker prefix the newest file by modification time, not by name (01_a is newer than 01_b); equal times
// fall back to the name (02_d over 02_c). The program says which session it continues, with the file time.
TEST_F(RunnerTest, NewestFilePerWorkerPrefixIsResumed) {
    const QString dir = sessionDir();
    touch(dir + QStringLiteral("/01_a.sls"), at(2024, 5, 6, 7, 8, 9));
    touch(dir + QStringLiteral("/01_b.sls"), at(2024, 5, 6, 7, 0, 0));
    touch(dir + QStringLiteral("/02_c.sls"), at(2023, 1, 2, 3, 4, 5));
    touch(dir + QStringLiteral("/02_d.sls"), at(2023, 1, 2, 3, 4, 5));
    starter_->prepared = {completedRun(), completedRun()};

    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    ASSERT_EQ(starter_->slsCalls.size(), 2u);
    EXPECT_EQ(QFileInfo(starter_->slsCalls[0]).absoluteFilePath(),
              QFileInfo(dir + "/01_a.sls").absoluteFilePath());
    EXPECT_EQ(QFileInfo(starter_->slsCalls[1]).absoluteFilePath(),
              QFileInfo(dir + "/02_d.sls").absoluteFilePath());
    EXPECT_EQ(starter_->pausedArgs, (std::vector<bool>{false, false}));
    EXPECT_EQ(starter_->startProcessCalls, 0);
    EXPECT_TRUE(err_.text().contains(QStringLiteral("continuing the session of 2024-05-06 07:08:09")));
    EXPECT_TRUE(err_.text().contains(QStringLiteral("continuing the session of 2023-01-02 03:04:05")));
}

// T-0202: the time shown is the one of the file before the run started (the run may save over it at once),
// in `run`'s continue path and in `resume`.
TEST_F(RunnerTest, TheContinuingLineShowsTheFileTimeBeforeTheStart) {
    const QString dir = sessionDir();
    touch(dir + QStringLiteral("/01_a.sls"), at(2024, 5, 6, 7, 8, 9));
    starter_->onSls = [](const QString& f) { touch(f, at(2030, 1, 1, 0, 0, 0)); };
    starter_->prepared = {completedRun()};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    EXPECT_TRUE(err_.text().contains(QStringLiteral("continuing the session of 2024-05-06 07:08:09")));
    EXPECT_FALSE(err_.text().contains(QStringLiteral("2030")));

    QTemporaryDir other;
    const QString file = other.filePath(QStringLiteral("r.sls"));
    touch(file, at(2022, 2, 3, 4, 5, 6));
    starter_->prepared = {completedRun()};
    {
        DownloadRunner runner(*app_, console_, job_, std::stop_token{}, fast(), starter_);
        EXPECT_EQ(runner.resume({file}, {QStringLiteral("M")}), DownloadOutcome::Completed);
    }
    EXPECT_TRUE(err_.text().contains(QStringLiteral("continuing the session of 2022-02-03 04:05:06")));
    EXPECT_FALSE(err_.text().contains(QStringLiteral("2030")));
}

// Files without NN_ are one group: one startBySls with the newest.
TEST_F(RunnerTest, FilesWithoutWorkerPrefixAreOneGroup) {
    const QString dir = sessionDir();
    touch(dir + QStringLiteral("/x.sls"), at(2020, 1, 1, 0, 0));
    touch(dir + QStringLiteral("/y.sls"), at(2021, 1, 1, 0, 0));
    touch(dir + QStringLiteral("/a.sls"), at(2019, 1, 1, 0, 0));
    touch(dir + QStringLiteral("/z.bk"), at(2030, 1, 1, 0, 0)); // not a session file
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    ASSERT_EQ(starter_->slsCalls.size(), 1u);
    EXPECT_EQ(QFileInfo(starter_->slsCalls[0]).fileName(), QStringLiteral("y.sls"));
}

// --restart: nothing is loaded, the directory is emptied before the fresh start.
TEST_F(RunnerTest, RestartEmptiesTheDirectoryAndStartsFresh) {
    const QString dir = sessionDir();
    touch(dir + QStringLiteral("/01_a.sls"), at(2024, 5, 6, 7, 8, 9));
    job_.restart = true;
    starter_->watchDir = dir;
    starter_->prepared = {completedRun()};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    EXPECT_TRUE(starter_->slsCalls.empty());
    EXPECT_EQ(starter_->startProcessCalls, 1);
    EXPECT_TRUE(starter_->entriesAtFreshStart.isEmpty());
}

// A session file that does not load: a warning with the file and the reason, the directory emptied, a fresh
// start. The run started from the file before it is cancelled, never dropped running.
TEST_F(RunnerTest, UnreadableSessionFileWarnsAndStartsFresh) {
    const QString dir = sessionDir();
    touch(dir + QStringLiteral("/01_a.sls"), at(2024, 5, 6, 7, 8, 9));
    touch(dir + QStringLiteral("/02_b.sls"), at(2024, 5, 6, 7, 8, 9));
    starter_->failing = {QStringLiteral("02_b.sls")};
    starter_->watchDir = dir;
    auto first = std::make_shared<Scripted>(); // handed to 01_a
    auto fresh = completedRun();               // handed to the fresh start
    starter_->prepared = {first};
    starter_->fresh = {fresh};
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);

    EXPECT_TRUE(err_.text().contains(QStringLiteral("warning: cannot continue ") +
                                     QFileInfo(dir + "/02_b.sls").absoluteFilePath() +
                                     QStringLiteral(": boom")));
    EXPECT_EQ(starter_->startProcessCalls, 1);
    EXPECT_TRUE(starter_->entriesAtFreshStart.isEmpty());
    EXPECT_TRUE(first->cancelled());
}

// ---- fresh start parameters (ticket step 2) ------------------------------------------------------------

TEST_F(RunnerTest, FreshStartParamsCarryTheJob) {
    job_.zooms = {3, 4};
    job_.retryMissing = true;
    job_.retryMissingOlderThan = at(2021, 3, 2, 17, 45);
    job_.replace = true;
    job_.replaceIfSizeDiffers = true;
    job_.replaceOlderThan = at(2024, 6, 13, 22, 30);
    job_.autosaveMin = 7;
    starter_->prepared = {completedRun()};
    const QString id = jobIdOf(job_, *map_, *area_);

    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    const auto& p = starter_->params;
    EXPECT_EQ(p.mapType, map_);
    EXPECT_EQ(p.zoomArray, (std::vector<quint8>{3, 4}));
    EXPECT_TRUE(p.isIgnoreTne);
    EXPECT_EQ(p.loadTneOlderDate, at(2021, 3, 2, 17, 45));
    EXPECT_TRUE(p.isReplace);
    EXPECT_TRUE(p.isReplaceIfDifSize);
    EXPECT_TRUE(p.isReplaceIfOlder);
    EXPECT_EQ(p.replaceDate, at(2024, 6, 13, 22, 30));
    EXPECT_EQ(p.sessionAutosaveInterval, 7);
    EXPECT_EQ(p.sessionAutosaveFilePrefix, id + QStringLiteral("/{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}/"));
    EXPECT_EQ(p.splitCount, 1);
    EXPECT_FALSE(p.isStartPaused);
    EXPECT_EQ(starter_->area, area_);
}

// Every option off: no "older than" flag, the replace date is now, the retry date is invalid.
TEST_F(RunnerTest, FreshStartParamsOfADefaultJob) {
    starter_->prepared = {completedRun()};
    const QDateTime before = QDateTime::currentDateTime();
    EXPECT_EQ(run(fast()), DownloadOutcome::Completed);
    const QDateTime after = QDateTime::currentDateTime();
    const auto& p = starter_->params;
    EXPECT_FALSE(p.isIgnoreTne);
    EXPECT_FALSE(p.loadTneOlderDate.isValid());
    EXPECT_FALSE(p.isReplace);
    EXPECT_FALSE(p.isReplaceIfDifSize);
    EXPECT_FALSE(p.isReplaceIfOlder);
    EXPECT_TRUE(p.replaceDate.isValid());
    EXPECT_GE(p.replaceDate, before.addSecs(-61));
    EXPECT_LE(p.replaceDate, after.addSecs(1));
    EXPECT_EQ(p.sessionAutosaveInterval, 5);
}

// ---- resume FILE.sls -----------------------------------------------------------------------------------

// A file that does not load is reported and skipped; the others run. Nothing could be started: Refused.
TEST_F(RunnerTest, ResumeSkipsUnreadableFilesAndRefusesWhenNoneStarts) {
    QTemporaryDir dir;
    starter_->failing = {QStringLiteral("bad.sls"), QStringLiteral("worse.sls")};
    const QString good = dir.filePath(QStringLiteral("good.sls"));
    const QString bad = dir.filePath(QStringLiteral("bad.sls"));
    const QString worse = dir.filePath(QStringLiteral("worse.sls"));
    starter_->prepared = {completedRun()};
    {
        DownloadRunner runner(*app_, console_, job_, std::stop_token{}, fast(), starter_);
        EXPECT_EQ(runner.resume({bad, good}, {QStringLiteral("A"), QStringLiteral("B")}),
                  DownloadOutcome::Completed);
    }
    EXPECT_EQ(starter_->slsCalls, (std::vector<QString>{bad, good}));
    EXPECT_TRUE(
        err_.text().contains(QStringLiteral("error: cannot continue ") + bad + QStringLiteral(": boom")));

    {
        DownloadRunner runner(*app_, console_, job_, std::stop_token{}, fast(), starter_);
        EXPECT_EQ(runner.resume({bad, worse}, {QStringLiteral("A"), QStringLiteral("B")}),
                  DownloadOutcome::Refused);
    }
    EXPECT_TRUE(
        err_.text().contains(QStringLiteral("error: cannot continue ") + worse + QStringLiteral(": boom")));
}

// T-0217 item 2: the progress line of `resume` names the session's map, not the file stem ("final").
TEST_F(RunnerTest, ResumeProgressLineNamesTheSessionsMap) {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("final.sls"));
    starter_->prepared = {completedRun()};
    DownloadRunner runner(*app_, console_, job_, std::stop_token{}, fast(), starter_);
    EXPECT_EQ(runner.resume({file}, {QStringLiteral("Map of the file")}), DownloadOutcome::Completed);
    EXPECT_TRUE(err_.text().contains(QStringLiteral("Map of the file  z")));
    EXPECT_FALSE(err_.text().contains(QStringLiteral("final  z")));
}

// T-0231: a session file whose map GUID cannot be read (a directory in its place: IniFile::fromFile cannot
// open it, the first return of sessionMapGuid) is still resumed, but --progress=lines writes no record with
// an empty GUID field: neither phase nor progress.
TEST_F(RunnerTest, ResumeLinesModeWritesNoRecordWithAnEmptyGuid) {
    QTemporaryDir dir;
    const QString file = dir.path(); // a directory: the file cannot be opened
    auto a = std::make_shared<Scripted>();
    a->processedV = 5;
    a->complete();
    starter_->prepared = {a};
    job_.progressMode = cg::app::ProgressMode::Lines;
    console_.setProgressRecords(true);
    {
        DownloadRunner runner(*app_, console_, job_, std::stop_token{}, fast(), starter_);
        EXPECT_EQ(runner.resume({file}, {QStringLiteral("M")}), DownloadOutcome::Completed);
    }
    EXPECT_EQ(starter_->slsCalls.size(), 1u);
    EXPECT_FALSE(err_.text().contains(QStringLiteral("phase\t"))) << qPrintable(err_.text());
    EXPECT_FALSE(err_.text().contains(QStringLiteral("progress\t"))) << qPrintable(err_.text());
}

// The session of a resumed file is saved next to that file.
TEST_F(RunnerTest, ResumeSavesTheUnfinishedSessionInTheFilesOwnDirectory) {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("01_x.sls"));
    touch(file, at(2024, 1, 1, 0, 0));
    auto idle = std::make_shared<Scripted>();
    starter_->prepared = {idle};
    std::stop_source stop;
    stop.request_stop();
    DownloadRunner runner(*app_, console_, job_, stop.get_token(), fast(), starter_);
    EXPECT_EQ(runner.resume({file}, {QStringLiteral("Resumed Map")}), DownloadOutcome::Interrupted);
    EXPECT_TRUE(idle->cancelled());
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("final.sls"))));
}

// ---- 7. the job id -------------------------------------------------------------------------------------

TEST_F(RunnerTest, JobIdIsStableAndChangesWithEveryInput) {
    job_.zooms = {3, 4};
    job_.threads = 2;
    const QString base = jobIdOf(job_, *map_, *area_);
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{12}$")).match(base).hasMatch())
        << base.toStdString();
    EXPECT_EQ(jobIdOf(job_, *map_, *area_), base);
    EXPECT_EQ(jobIdOf(Job(job_), *map_, *rect(-170, 30, -100, -10)), base);

    // what does not decide a session does not change its id
    Job same = job_;
    same.autosaveMin = 1;
    same.restart = true;
    same.stallTimeoutSec = 9;
    same.out = QStringLiteral("x.mbtiles");
    EXPECT_EQ(jobIdOf(same, *map_, *area_), base);

    std::set<QString> ids{base};
    const auto expectNew = [&](const QString& id, const char* what) {
        EXPECT_TRUE(ids.insert(id).second) << what;
    };
    const auto changed2 = [&] {
        Job job = job_;
        job.zooms = {9}; // not read: the explicit list decides
        return job;
    };
    const auto changed = [&](auto edit) {
        Job job = job_;
        edit(job);
        return jobIdOf(job, *map_, *area_);
    };
    expectNew(changed([](Job& j) { j.zooms = {3, 5}; }), "zoom");
    expectNew(changed([](Job& j) { j.zooms = {3}; }), "zoom count");
    // T-0245: the id hashes the effective list of the source, not job.zooms
    expectNew(DownloadRunner::jobId(job_, *map_, *area_, std::vector<quint8>{3, 6}), "effective zooms");
    EXPECT_EQ(DownloadRunner::jobId(job_, *map_, *area_, std::vector<quint8>{3, 4}), base);
    EXPECT_EQ(DownloadRunner::jobId(changed2(), *map_, *area_, std::vector<quint8>{3, 4}), base);
    expectNew(changed([](Job& j) { j.threads = 3; }), "threads");
    expectNew(changed([](Job& j) { j.replace = true; }), "replace");
    expectNew(changed([](Job& j) { j.replaceIfSizeDiffers = true; }), "replaceIfSizeDiffers");
    expectNew(changed([](Job& j) { j.replaceOlderThan = at(2024, 1, 2, 3, 4); }), "replaceOlderThan");
    expectNew(changed([](Job& j) { j.retryMissing = true; }), "retryMissing");
    expectNew(changed([](Job& j) { j.retryMissingOlderThan = at(2024, 1, 2, 3, 4); }),
              "retryMissingOlderThan");
    // two different dates differ, too
    Job dated = job_;
    dated.replaceOlderThan = at(2024, 1, 2, 3, 5);
    expectNew(jobIdOf(dated, *map_, *area_), "replaceOlderThan date");
    dated = job_;
    dated.retryMissingOlderThan = at(2024, 1, 2, 3, 5);
    expectNew(jobIdOf(dated, *map_, *area_), "retryMissingOlderThan date");
    expectNew(jobIdOf(job_, *map_, *rect(-170, 30, -100, -10.001)), "polygon point");
    expectNew(jobIdOf(job_, *map_, *rect(-170, 30, -100.001, -10)), "polygon point 2");
    expectNew(jobIdOf(job_, *makeMap(QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8FA}"), 4), *area_),
              "source guid");
}

// The directory is <autoSaveDir>/<job id>/<GUID in braces, upper case>: the map's guid is lower case here.
TEST_F(RunnerTest, SessionDirectoryIsJobIdSlashUpperCaseGuid) {
    map_ = makeMap(QStringLiteral("{0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9}"), 4);
    auto idle = std::make_shared<Scripted>();
    starter_->prepared = {idle};
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(run(fast(), stop.get_token()), DownloadOutcome::Interrupted);
    const QString expected = app_->autoSaveDir() + QLatin1Char('/') + jobIdOf(job_, *map_, *area_) +
                             QStringLiteral("/{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}");
    EXPECT_TRUE(QFile::exists(expected + QStringLiteral("/final.sls")));
    EXPECT_EQ(starter_->params.sessionAutosaveFilePrefix,
              jobIdOf(job_, *map_, *area_) + QStringLiteral("/{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}/"));
}

// ---- 6. Rig B: real runs -------------------------------------------------------------------------------

namespace {

// Tiles of z4 (0-based) the lon -170..-120, lat 30..-10 rectangle covers: x 0..2, y 6..8 (hand-derived in
// tests/region/provider_tiles_download_test.cpp for the neighbouring rectangle, tile width 22.5 degrees).
DownloadPolicy rigPolicy() {
    DownloadPolicy policy;
    policy.pollMs = 10;
    return policy;
}

std::set<std::tuple<int, int, int>> nineTiles() {
    std::set<std::tuple<int, int, int>> tiles;
    for (int x = 0; x <= 2; ++x) {
        for (int y = 6; y <= 8; ++y) {
            tiles.insert({4, x, y});
        }
    }
    return tiles;
}

std::set<std::tuple<int, int, int>> requestedTiles(const RigB& rig) {
    std::set<std::tuple<int, int, int>> tiles;
    for (const auto& r : rig.subsystem->requests()) {
        tiles.insert({r.zoom, r.tile.x(), r.tile.y()});
    }
    return tiles;
}

} // namespace

// A 3 x 3 tile area at one zoom completes: nine distinct tiles answered, counted as downloaded (45 bytes),
// outcome Completed, the session directory gone.
TEST_F(RunnerTest, RealRunOfNineTilesCompletes) {
    RigB rig(4);
    auto real = std::make_shared<RealStarter>(rig);
    const ResolvedSource src{rig.map,   false,        QString(), QStringLiteral("Src"),
                             QString(), std::nullopt, job_.zooms};
    const auto area = rect(-170, 30, -120, -10);
    DownloadRunner runner(*app_, console_, job_, std::stop_token{}, rigPolicy(), real);
    EXPECT_EQ(runner.run(src, area), DownloadOutcome::Completed);

    EXPECT_EQ(real->runsStarted, 1);
    EXPECT_EQ(requestedTiles(rig), nineTiles());
    EXPECT_EQ(rig.subsystem->requests().size(), 9u);
    ASSERT_EQ(real->progress.size(), 1u);
    EXPECT_EQ(real->progress[0]->downloaded(), 9);
    EXPECT_EQ(real->progress[0]->processed(), 9);
    EXPECT_EQ(real->progress[0]->downloadSize(), 45u);
    EXPECT_FALSE(QDir(app_->autoSaveDir() + QLatin1Char('/') + jobIdOf(job_, *rig.map, *area) +
                      QLatin1Char('/') + kGuid)
                     .exists());
}

// Proof 2 (real rig): the last progress record equals the final values of the progress info, zoom 0-based.
TEST_F(RunnerTest, RealRunLinesRecordEqualsTheFinalProgressInfo) {
    RigB rig(4);
    auto real = std::make_shared<RealStarter>(rig);
    const ResolvedSource src{rig.map,   false,        QString(), QStringLiteral("Src"),
                             QString(), std::nullopt, job_.zooms};
    job_.progressMode = cg::app::ProgressMode::Lines;
    console_.setProgressRecords(true);
    DownloadRunner runner(*app_, console_, job_, std::stop_token{}, rigPolicy(), real);
    EXPECT_EQ(runner.run(src, rect(-170, 30, -120, -10)), DownloadOutcome::Completed);

    ASSERT_EQ(real->progress.size(), 1u);
    const auto& info = *real->progress[0];
    const QStringList records = recordLines(err_.text(), QStringLiteral("progress"));
    ASSERT_FALSE(records.isEmpty());
    const QStringList f = records.last().split(QLatin1Char('\t'));
    ASSERT_EQ(f.size(), 9) << qPrintable(records.last());
    EXPECT_EQ(f[2], kGuid);
    EXPECT_EQ(f[3], QStringLiteral("4")); // job_.zooms = {4}, 0-based
    EXPECT_EQ(f[4].toLongLong(), info.processed());
    EXPECT_EQ(f[5].toLongLong(), info.totalToProcess());
    EXPECT_EQ(f[6].toLongLong(), info.downloaded());
    EXPECT_EQ(f[7].toULongLong(), info.downloadSize());
    EXPECT_EQ(f[4], QStringLiteral("9"));
    EXPECT_EQ(f[6], QStringLiteral("9"));
    EXPECT_EQ(f[7], QStringLiteral("45"));
}

// T-0245: the run asks for the zooms of the source (job zooms 3 and 4, the source stops at 3 -> 0-based {3}
// is not the nine tiles of zoom 4: only zoom 3 is requested).
TEST_F(RunnerTest, RealRunRequestsOnlyTheZoomsOfTheSource) {
    RigB rig(4);
    auto real = std::make_shared<RealStarter>(rig);
    job_.zooms = {4, 5};
    ResolvedSource src{rig.map, false, QString(), QStringLiteral("Src"), QString(), std::nullopt, {4}};
    DownloadRunner runner(*app_, console_, job_, std::stop_token{}, rigPolicy(), real);
    EXPECT_EQ(runner.run(src, rect(-170, 30, -120, -10)), DownloadOutcome::Completed);

    EXPECT_EQ(real->params.zoomArray, (std::vector<quint8>{4}));
    EXPECT_EQ(requestedTiles(rig), nineTiles());
    EXPECT_EQ(rig.subsystem->requests().size(), 9u);
}

// --threads 2 against MaxConnectToServerCount = 1: the cap warning is printed and one run is started.
TEST_F(RunnerTest, RealRunThreadsAboveTheSourceCapWarnAndStartOneRun) {
    RigB rig(1);
    auto real = std::make_shared<RealStarter>(rig);
    const ResolvedSource src{rig.map,   false,        QString(), QStringLiteral("Src"),
                             QString(), std::nullopt, job_.zooms};
    job_.threads = 2;
    DownloadRunner runner(*app_, console_, job_, std::stop_token{}, rigPolicy(), real);
    EXPECT_EQ(runner.run(src, rect(-170, 30, -120, -10)), DownloadOutcome::Completed);

    EXPECT_EQ(real->params.splitCount, 1);
    EXPECT_EQ(real->runsStarted, 1);
    EXPECT_TRUE(err_.text().contains(
        QStringLiteral("warning: --threads 2 lowered to 1: the source allows 1 connections")));
    EXPECT_EQ(requestedTiles(rig), nineTiles());
}

// --threads 2 with room for 4: two runs, no warning; together they request every tile once.
TEST_F(RunnerTest, RealRunWithTwoWorkersCoversTheAreaOnce) {
    RigB rig(4);
    auto real = std::make_shared<RealStarter>(rig);
    const ResolvedSource src{rig.map,   false,        QString(), QStringLiteral("Src"),
                             QString(), std::nullopt, job_.zooms};
    job_.threads = 2;
    DownloadRunner runner(*app_, console_, job_, std::stop_token{}, rigPolicy(), real);
    EXPECT_EQ(runner.run(src, rect(-170, 30, -120, -10)), DownloadOutcome::Completed);

    EXPECT_EQ(real->params.splitCount, 2);
    EXPECT_EQ(real->runsStarted, 2);
    EXPECT_FALSE(err_.text().contains(QStringLiteral("lowered")));
    EXPECT_EQ(requestedTiles(rig), nineTiles());
    EXPECT_EQ(rig.subsystem->requests().size(), 9u);
}
