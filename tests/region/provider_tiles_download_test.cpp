// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-REG-014 (split into N parts), FEAT-REG-019 (resume from .sls), FEAT-REG-011, FEAT-REG-012,
// FEAT-REG-020 (the options reach the worker): contribute only (docs/parity/held-open.md).
// Ports: Src/RegionProcess/Download/u_ProviderTilesDownload.pas (StartSession :194-272, StartBySLS :274-322,
// StartProcess :324-379). T-0181, proofs RD-8 4-7.
//
// Rig: a real map set holding a FakeMapType, a recording subsystem (every getRequestTask is logged, every
// download answers Ok with 5 bytes at once), an empty storage, the real task provider, session, progress
// object and worker. No sleep synchronises a test: a worker that ends by itself is joined by destroying its
// run; a worker that must not move is cancelled first; "nothing is requested" is a bounded condition-variable
// wait that can only pass late, never fail on a correct implementation.
//
// Two workers that pause at the same moment both read the wall clock
// (RegionProcessProgressInfoDownload::now(), QDateTime::currentDateTime -> glibc time zone code);
// ThreadSanitizer does not see glibc's tzset_lock, so tools/tsan_suppressions.txt carries a narrow
// suppression for it (T-0195; report in docs/handoffs/T-0181.md).
//
// Geometry (Google/3857, hand-derived in download_task_provider_test.cpp): the rectangle lon -170..-100,
// lat 30..-10 covers z3 x 0..1, y 3..4 (4 tiles) and z4 x 0..3, y 6..8 (12 tiles); the rectangle lon
// -170..-160, lat 20..10 covers the one tile (0,3) at z3. Parts of n tiles are n / parts each, the last part
// takes the remainder (2,2,2,3 for 9 in 4).
//
// Not proven (no seam without changing landed code): the child DownloadInfo of a run is private to its worker
// (the initial count and size of the .sls child, and "one child per worker", cannot be read); the
// cancel-between-begin()-and-the-test branch (:248).
#include "cg/core/config_provider_ini.h"
#include "cg/geo/datum.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"
#include "cg/net/download_stats.h"
#include "cg/net/global_download_config.h"
#include "cg/region/provider_tiles_download.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTime>
#include <QUuid>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "region_fake_map_type.h"
#include "tiles_download_rig.h"

using namespace std::chrono_literals;
using cg::geo::LonLatRect;
using cg::geo::PointI;
using cg::region::DownloadSessionRun;
using cg::region::ProviderTilesDownload;
using cg::region::TilesDownloadParams;
using cg::region::test::EmptyStorage;
using cg::region::test::FakeMapType;
using cg::region::test::RecordingSubsystem;
using cg::region::test::RequestRecord;
using cg::storage::TileDate;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;

namespace {

constexpr auto kFailureBound = 60s;
const QString kGuidText = QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}");

TileDate dt(int y, int m, int d, int h, int min) {
    return TileDate(QDate(y, m, d), QTime(h, min));
}

struct World {
    std::shared_ptr<cg::maptype::MapVersionFactoryChangeable> factory =
        std::make_shared<cg::maptype::MapVersionFactoryChangeable>(
            std::make_shared<cg::storage::MapVersionFactory>());
    std::shared_ptr<FakeMapType> map = std::make_shared<FakeMapType>();
    std::shared_ptr<RecordingSubsystem> subsystem = std::make_shared<RecordingSubsystem>();
    std::shared_ptr<EmptyStorage> storage = std::make_shared<EmptyStorage>();
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
    std::unique_ptr<ProviderTilesDownload> provider; // last: nothing else refers to it

    World() {
        cg::geo::ProjectionSetFactory setFactory{
            cg::geo::HashSeed{}, std::make_shared<const cg::geo::DatumFactory>(cg::geo::HashSeed{})};
        map->guidValue = QUuid::fromString(kGuidText);
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
        auto builder = cg::maptype::MapTypeSetBuilderFactory().build(false);
        builder->add(map);
        mapSet = builder->makeAndClear();
        config->setUseSessionLastSuccess(true);
    }

    // Built after a test has filled the storage: the workers read it unlocked.
    ProviderTilesDownload& start() {
        provider = std::make_unique<ProviderTilesDownload>(appClosing.get_token(), iterators, mapSet, lonLat,
                                                           projected, config, parentInfo);
        return *provider;
    }

    cg::region::PolygonPtr rect(double lonL, double latTop, double lonR, double latBottom) const {
        return lonLat->createPolygonByRect(LonLatRect{lonL, latTop, lonR, latBottom});
    }

    TilesDownloadParams params(std::vector<quint8> zooms, int split) const {
        TilesDownloadParams p;
        p.mapType = map;
        p.zoomArray = std::move(zooms);
        p.splitCount = split;
        return p;
    }
};

// The area of 4 tiles at z3.
cg::region::PolygonPtr area4(const World& w) {
    return w.rect(-170, 30, -100, -10);
}

using Tile = std::tuple<int, int, int>; // zoom, x, y

std::vector<Tile> tilesOf(const std::vector<RequestRecord>& requests) {
    std::vector<Tile> out;
    for (const RequestRecord& r : requests) {
        out.emplace_back(r.zoom, r.tile.x(), r.tile.y());
    }
    return out;
}

std::vector<Tile> sorted(std::vector<Tile> v) {
    std::ranges::sort(v);
    return v;
}

// Cancels every run, then destroys them: the workers wake from the pause or end at their next test. A run is
// never destroyed uncancelled while its worker may still wait for a result (the contract of the caller): the
// destructor of the worker would drop that result and the worker would wait for it for ever. No test here has
// a signal for "the worker has ended by itself", so every test that lets workers run ends with this.
void cancelAndDestroy(std::vector<DownloadSessionRun>& runs) {
    for (const DownloadSessionRun& run : runs) {
        run.cancelNotifier->cancel();
    }
    runs.clear();
}

} // namespace

// Proof 4 (StartProcess :340-378): three parts of an area over two zooms. One task provider serves all three
// workers: together they request each tile exactly once; the sessions know their index and the count; every
// run has its own log, notifier, progress object and session; the parent counter holds the sum of the three.
TEST(ProviderTilesDownload, SplitIntoThreeWorkersCoversEveryTileOnce) {
    World w;
    auto& provider = w.start();
    auto runs = provider.startProcess(area4(w), w.params({3, 4}, 3));
    ASSERT_EQ(runs.size(), 3u);
    ASSERT_TRUE(w.subsystem->waitForRequests(16, kFailureBound));

    std::vector<std::shared_ptr<cg::region::RegionProcessProgressInfoDownload>> progress;
    std::vector<std::shared_ptr<cg::region::IDownloadSession>> sessions;
    for (const DownloadSessionRun& run : runs) {
        progress.push_back(run.progressInfo);
        sessions.push_back(run.session);
        EXPECT_NE(run.thread, nullptr);
    }
    EXPECT_NE(runs[0].log, runs[1].log);
    EXPECT_NE(runs[1].log, runs[2].log);
    EXPECT_NE(runs[0].cancelNotifier, runs[1].cancelNotifier);
    EXPECT_NE(runs[1].cancelNotifier, runs[2].cancelNotifier);
    EXPECT_NE(progress[0], progress[1]);
    EXPECT_NE(progress[1], progress[2]);
    EXPECT_NE(sessions[0], sessions[1]);
    EXPECT_NE(sessions[1], sessions[2]);
    cancelAndDestroy(runs);

    for (std::size_t i = 0; i < 3; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(sessions[i]->workerIndex(), static_cast<int>(i));
        EXPECT_EQ(sessions[i]->workersCount(), 3);
        EXPECT_EQ(sessions[i]->zoomArr(), (std::vector<quint8>{3, 4}));
        EXPECT_TRUE(progress[i]->finished());
    }
    // z3 x 0..1, y 3..4 and z4 x 0..3, y 6..8, each exactly once
    const std::vector<Tile> everyTile{{3, 0, 3}, {3, 0, 4}, {3, 1, 3}, {3, 1, 4}, {4, 0, 6}, {4, 0, 7},
                                      {4, 0, 8}, {4, 1, 6}, {4, 1, 7}, {4, 1, 8}, {4, 2, 6}, {4, 2, 7},
                                      {4, 2, 8}, {4, 3, 6}, {4, 3, 7}, {4, 3, 8}};
    const auto requests = w.subsystem->requests();
    EXPECT_EQ(sorted(tilesOf(requests)), everyTile);
    for (const RequestRecord& r : requests) {
        EXPECT_EQ(r.version, QStringLiteral("base-ver")); // VersionRequest.GetStatic.BaseVersion (:358)
        EXPECT_FALSE(r.checkSize);
    }
    // 4 / 3 = 1,1,2 at z3 and 12 / 3 = 4,4,4 at z4, the last part takes the remainder
    EXPECT_EQ(progress[0]->totalToProcess(), 5);
    EXPECT_EQ(progress[1]->totalToProcess(), 5);
    EXPECT_EQ(progress[2]->totalToProcess(), 6);
    // The runs were cancelled right after the sixteenth request, so the last tile of each worker may or may
    // not have been counted: 13..16 tiles of 5 bytes. The children forward to the parent: what the three
    // progress objects counted is what the parent holds.
    const qint64 downloaded =
        progress[0]->downloaded() + progress[1]->downloaded() + progress[2]->downloaded();
    EXPECT_GE(downloaded, 13);
    EXPECT_LE(downloaded, 16);
    EXPECT_EQ(static_cast<qint64>(w.parentInfo->tileCount()), downloaded);
    EXPECT_EQ(static_cast<qint64>(w.parentInfo->size()), 5 * downloaded);
}

// Proof 4 (StartProcess :352-372): each option reaches the session at its own position. Three rows, each
// flag column has its own pattern, so a swap of two arguments shows in one row. The runs start paused: no
// tile is touched.
TEST(ProviderTilesDownload, FrameOptionsReachEverySessionAtTheirPosition) {
    struct Row {
        bool ignoreTne, replace, difSize, older, closeOnFinish;
        TileDate loadTneOlderDate;
        TileDate replaceDate;
        int autosave;
        QString prefix;
    };
    const Row rows[] = {
        {true, false, false, true, true, dt(2021, 3, 2, 17, 45), dt(2024, 6, 13, 22, 30), 15,
         QStringLiteral("Город-pre")},
        {false, true, false, true, false, TileDate(), dt(2010, 11, 12, 13, 14), 0, QString()},
        {false, false, true, false, true, dt(2001, 2, 3, 4, 5), dt(2030, 1, 2, 3, 4), 999,
         QStringLiteral("p2")},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.autosave);
        World w;
        auto& provider = w.start();
        TilesDownloadParams p = w.params({3}, 1);
        p.isStartPaused = true;
        p.isIgnoreTne = row.ignoreTne;
        p.isReplace = row.replace;
        p.isReplaceIfDifSize = row.difSize;
        p.isReplaceIfOlder = row.older;
        p.isCloseOnFinish = row.closeOnFinish;
        p.loadTneOlderDate = row.loadTneOlderDate;
        p.replaceDate = row.replaceDate;
        p.sessionAutosaveInterval = row.autosave;
        p.sessionAutosaveFilePrefix = row.prefix;
        auto runs = provider.startProcess(area4(w), p);
        ASSERT_EQ(runs.size(), 1u);
        // the session belongs to the worker while it lives: read it after the join
        std::vector<std::shared_ptr<cg::region::IDownloadSession>> sessions;
        for (const DownloadSessionRun& run : runs) {
            sessions.push_back(run.session);
        }
        cancelAndDestroy(runs);
        for (std::size_t i = 0; i < sessions.size(); ++i) {
            const auto& s = *sessions[i];
            EXPECT_EQ(s.mapType(), w.map);
            EXPECT_EQ(s.secondLoadTne(), row.ignoreTne);
            EXPECT_EQ(s.replaceExistTiles(), row.replace);
            EXPECT_EQ(s.checkExistTileSize(), row.difSize);
            EXPECT_EQ(s.checkExistTileDate(), row.older);
            EXPECT_EQ(s.autoCloseAtFinish(), row.closeOnFinish);
            EXPECT_EQ(s.replaceTneOlderDate(), row.loadTneOlderDate);
            EXPECT_EQ(s.checkTileDate(), row.replaceDate);
            EXPECT_EQ(s.autosaveInterval(), row.autosave);
            EXPECT_EQ(s.autosavePrefix(), row.prefix);
            EXPECT_EQ(s.workerIndex(), static_cast<int>(i));
            EXPECT_EQ(s.workersCount(), 1);
            ASSERT_NE(s.versionForDownload(), nullptr);
            EXPECT_EQ(s.versionForDownload()->storeString(), QStringLiteral("base-ver"));
            ASSERT_NE(s.versionForCheck(), nullptr);
            EXPECT_EQ(s.versionForCheck()->baseVersion, s.versionForDownload());
        }
        EXPECT_TRUE(w.subsystem->requests().empty());
    }
}

// Proof 4 (StartSession :250-268): the session's options reach the worker at their own position. One worker,
// the first tile of the area is on disk with the date 2024-06-13 12:00. A skip is seen as "the first request
// is for the second tile"; the size flag is seen in the request the worker makes.
TEST(ProviderTilesDownload, WorkerDecisionsFollowTheSessionOptions) {
    struct Row {
        const char* name;
        TileInfoType type;
        std::function<void(TilesDownloadParams&)> set;
        bool requested;
        bool checkSize;
    };
    const TileDate older = dt(2024, 1, 1, 0, 0);
    const TileDate newer = dt(2025, 1, 1, 0, 0);
    const Row rows[] = {
        {"no tile: downloaded", TileInfoType::Unknown, [](TilesDownloadParams&) {}, true, false},
        {"exists, replace off: skipped", TileInfoType::Exists, [](TilesDownloadParams&) {}, false, false},
        {"exists, replace on: downloaded", TileInfoType::Exists,
         [](TilesDownloadParams& p) { p.isReplace = true; }, true, false},
        {"exists, replace on, size check: downloaded with the size flag", TileInfoType::Exists,
         [](TilesDownloadParams& p) {
             p.isReplace = true;
             p.isReplaceIfDifSize = true;
         },
         true, true},
        {"exists, replace if older, tile newer than the date: skipped", TileInfoType::Exists,
         [&](TilesDownloadParams& p) {
             p.isReplace = true;
             p.isReplaceIfOlder = true;
             p.replaceDate = older;
         },
         false, false},
        {"exists, replace if older, tile older than the date: downloaded", TileInfoType::Exists,
         [&](TilesDownloadParams& p) {
             p.isReplace = true;
             p.isReplaceIfOlder = true;
             p.replaceDate = newer;
         },
         true, false},
        {"exists, the date is ignored without replace-if-older: downloaded", TileInfoType::Exists,
         [&](TilesDownloadParams& p) {
             p.isReplace = true;
             p.replaceDate = older;
         },
         true, false},
        {"tne, not ignored: skipped", TileInfoType::TneExists, [](TilesDownloadParams&) {}, false, false},
        {"tne, ignored, no date: downloaded", TileInfoType::TneExists,
         [](TilesDownloadParams& p) { p.isIgnoreTne = true; }, true, false},
        {"tne, ignored, tne newer than the date: skipped", TileInfoType::TneExists,
         [&](TilesDownloadParams& p) {
             p.isIgnoreTne = true;
             p.loadTneOlderDate = older;
         },
         false, false},
        {"tne, ignored, tne older than the date: downloaded", TileInfoType::TneExists,
         [&](TilesDownloadParams& p) {
             p.isIgnoreTne = true;
             p.loadTneOlderDate = newer;
         },
         true, false},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        World w;
        if (row.type != TileInfoType::Unknown) {
            TileInfo info;
            info.type = row.type;
            info.loadDate = dt(2024, 6, 13, 12, 0);
            info.size = row.type == TileInfoType::Exists ? 10 : 0;
            w.storage->infos[{0, 3}] = info;
        }
        auto& provider = w.start();
        TilesDownloadParams p = w.params({3}, 1);
        row.set(p);
        auto runs = provider.startProcess(area4(w), p);
        // The tile under test is the first of the area; the second has no info and is always requested, so
        // the first request tells which way the first tile went.
        ASSERT_TRUE(w.subsystem->waitForRequests(1, kFailureBound));
        cancelAndDestroy(runs);
        const auto requests = w.subsystem->requests();
        ASSERT_FALSE(requests.empty());
        if (row.requested) {
            EXPECT_EQ(tilesOf(requests).front(), (Tile{3, 0, 3}));
            EXPECT_EQ(requests.front().checkSize, row.checkSize);
        } else {
            EXPECT_EQ(tilesOf(requests).front(), (Tile{3, 1, 3}));
            EXPECT_EQ(std::ranges::count(tilesOf(requests), Tile{3, 0, 3}), 0);
        }
    }
}

// Proof 7 (FEAT-REG-013 through StartSession :221-227): a run started paused reports it at once and requests
// nothing until resume(); after it the whole area is requested.
TEST(ProviderTilesDownload, StartPausedRequestsNothingUntilResume) {
    World w;
    auto& provider = w.start();
    TilesDownloadParams p = w.params({3}, 2);
    p.isStartPaused = true;
    auto runs = provider.startProcess(area4(w), p);
    ASSERT_EQ(runs.size(), 2u);
    for (const DownloadSessionRun& run : runs) {
        EXPECT_TRUE(run.progressInfo->isPaused());
    }
    EXPECT_FALSE(w.subsystem->waitForRequests(1, 400ms));
    for (const DownloadSessionRun& run : runs) {
        run.progressInfo->resume();
    }
    ASSERT_TRUE(w.subsystem->waitForRequests(4, kFailureBound));
    cancelAndDestroy(runs);
    EXPECT_EQ(sorted(tilesOf(w.subsystem->requests())),
              (std::vector<Tile>{{3, 0, 3}, {3, 0, 4}, {3, 1, 3}, {3, 1, 4}}));
}

// Proof 6 (StartSession :214-219): cancelling the notifier of a run ends its worker; the run is destroyed
// without a hang, the progress object is finished and nothing was requested (the runs were paused).
TEST(ProviderTilesDownload, CancelEndsEveryWorkerAndTheRunsDestroyCleanly) {
    World w;
    auto& provider = w.start();
    TilesDownloadParams p = w.params({3}, 2);
    p.isStartPaused = true;
    auto runs = provider.startProcess(area4(w), p);
    ASSERT_EQ(runs.size(), 2u);
    std::vector<std::shared_ptr<cg::region::RegionProcessProgressInfoDownload>> progress;
    std::vector<std::shared_ptr<cg::core::OperationNotifier>> notifiers;
    for (const DownloadSessionRun& run : runs) {
        progress.push_back(run.progressInfo);
        notifiers.push_back(run.cancelNotifier);
        EXPECT_FALSE(run.cancelNotifier->isCancelled());
    }
    cancelAndDestroy(runs);
    for (std::size_t i = 0; i < progress.size(); ++i) {
        EXPECT_TRUE(progress[i]->finished());
        EXPECT_TRUE(notifiers[i]->isCancelled());
    }
    EXPECT_TRUE(w.subsystem->requests().empty());
}

namespace {

// A .sls file in a temporary directory; the text is written as given, behind a UTF-8 byte order mark.
struct SlsFile {
    QTemporaryDir dir;
    QString path;

    explicit SlsFile(const QString& body = QString(),
                     const QString& fileName = QStringLiteral("resume.sls")) {
        path = dir.filePath(fileName);
        QFile out(path);
        EXPECT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(QByteArray("\xEF\xBB\xBF").append(body.toUtf8()));
    }
};

QString session(const QString& guid, const QString& extra = QString()) {
    return QStringLiteral("[Session]\nMapGUID=") + guid + QStringLiteral("\n") + extra;
}

// The area lon -170..-100, lat 30..-10; the file's Zoom and ZoomArr are 1-based and come first.
const QString kAreaPoints =
    QStringLiteral("PointLon_0=-170\nPointLat_0=30\nPointLon_1=-100\nPointLat_1=30\n"
                   "PointLon_2=-100\nPointLat_2=-10\nPointLon_3=-170\nPointLat_3=-10\n");
const QString kZoom3Keys = QStringLiteral("Zoom=4\nZoomArr=4\n");
const QString kZoom4Keys = QStringLiteral("Zoom=5\nZoomArr=5\n");

QString messageOf(const std::function<void()>& call) {
    try {
        call();
    } catch (const std::runtime_error& e) {
        return QString::fromUtf8(e.what());
    } catch (...) {
        return QStringLiteral("<other exception>");
    }
    return QStringLiteral("<no exception>");
}

} // namespace

// Proof 5 (StartBySLS :282-321): the resumed run continues after the last processed tile of the file; its
// session carries the file's counters. Zoom 4 has 12 tiles (0,6)(1,6)(2,6)(3,6)(0,7)(1,7)(2,7)(3,7)(0,8)(1,8)
// (2,8)(3,8); the file says 6 are done and (1,7) was the last. The seek onto (1,7) takes one unit of the
// budget of 12 - 6 + 1 = 7, so six tiles follow, (3,8) included (the deviation of T-0226 (deviations.md), was
// LR-2 / L-265).
TEST(ProviderTilesDownload, ResumeFromSlsContinuesAfterTheLastProcessedTile) {
    SlsFile file(
        session(kGuidText, kZoom4Keys + kAreaPoints +
                               QStringLiteral("StartX=1\nStartY=7\nLastSuccessfulStartX=1\n"
                                              "LastSuccessfulStartY=7\nLastProcessedCount=6\n"
                                              "Processed=6\nProcessedTileCount=701\nProcessedSize=12.5\n"
                                              "WorkersCount=1\nWorkerIndex=0\n")));
    World w;
    auto& provider = w.start();
    auto runs = std::vector<DownloadSessionRun>{};
    runs.push_back(provider.startBySls(file.path, false));
    const auto progress = runs[0].progressInfo;
    const auto session = runs[0].session;
    EXPECT_NE(runs[0].thread, nullptr);
    EXPECT_FALSE(progress->isPaused());
    ASSERT_TRUE(w.subsystem->waitForRequests(6, kFailureBound));
    cancelAndDestroy(runs);
    EXPECT_EQ(tilesOf(w.subsystem->requests()),
              (std::vector<Tile>{{4, 2, 7}, {4, 3, 7}, {4, 0, 8}, {4, 1, 8}, {4, 2, 8}, {4, 3, 8}}));
    EXPECT_EQ(session->zoom(), 4);
    EXPECT_EQ(session->zoomArr(), (std::vector<quint8>{4}));
    EXPECT_EQ(session->workersCount(), 1);
    // The last tile may have been cut off by the cancel, so 5 or 6 were counted. The session continues the
    // file's counters (701 tiles, 12.5 KiB) with what this run downloaded, and the parent counter holds only
    // what this run downloaded, not the file's totals.
    const qint64 mine = static_cast<qint64>(w.parentInfo->tileCount());
    EXPECT_GE(mine, 5);
    EXPECT_LE(mine, 6);
    EXPECT_EQ(progress->downloaded(), 701 + mine);
    EXPECT_EQ(progress->downloadSize(), 12800u + 5u * static_cast<quint64>(mine));
    EXPECT_EQ(w.parentInfo->size(), 5u * static_cast<quint64>(mine));
}

// Proof 5/7: the start-paused argument of StartBySLS reaches the progress object.
TEST(ProviderTilesDownload, ResumeFromSlsStartedPausedWaitsForResume) {
    SlsFile file(session(kGuidText, kZoom3Keys + kAreaPoints));
    World w;
    auto& provider = w.start();
    auto run = provider.startBySls(file.path, true);
    EXPECT_TRUE(run.progressInfo->isPaused());
    EXPECT_FALSE(w.subsystem->waitForRequests(1, 400ms));
    run.cancelNotifier->cancel();
    run = DownloadSessionRun{};
    EXPECT_TRUE(w.subsystem->requests().empty());
}

// Proof 5: the errors of Load pass through unchanged, and no worker was started (StartBySLS :294-299).
TEST(ProviderTilesDownload, ResumeFromSlsErrorsKeepTheTextOfLoad) {
    struct Row {
        const char* name;
        QString body; // null body: no file at all
        QString message;
    };
    const Row rows[] = {
        {"unknown GUID",
         session(QStringLiteral("{FFFFFFFF-0000-0000-0000-000000000001}"), kZoom3Keys + kAreaPoints),
         QStringLiteral("Map with GUID = {FFFFFFFF-0000-0000-0000-000000000001} not found")},
        {"no GUID", QStringLiteral("[Session]\n") + kZoom3Keys + kAreaPoints,
         QStringLiteral("Map GUID is empty")},
        {"no Session section", QStringLiteral("[Other]\nMapGUID=") + kGuidText + QStringLiteral("\n"),
         QStringLiteral("No SLS data")},
        {"no polygon", session(kGuidText, QStringLiteral("Zoom=4\n")), QStringLiteral("Empty polygon")},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        SlsFile file(row.body);
        World w;
        auto& provider = w.start();
        EXPECT_EQ(messageOf([&] { (void)provider.startBySls(file.path, false); }), row.message);
        EXPECT_TRUE(w.subsystem->requests().empty());
    }
}

// Proof 5: a file that does not exist is an empty INI (TMemIniFile), so there is no [Session] section and
// Load raises its own text (u_DownloadSession.pas:359-361).
TEST(ProviderTilesDownload, ResumeFromAMissingFileRaisesNoSlsData) {
    QTemporaryDir dir;
    World w;
    auto& provider = w.start();
    EXPECT_EQ(
        messageOf([&] { (void)provider.startBySls(dir.filePath(QStringLiteral("absent.sls")), false); }),
        QStringLiteral("No SLS data"));
}

// A file that cannot be read (here a directory) raises with the path in the text (the TMemIniFile
// constructor raises in legacy, :282).
TEST(ProviderTilesDownload, ResumeFromAnUnreadableFileRaises) {
    QTemporaryDir dir;
    World w;
    auto& provider = w.start();
    const QString text = messageOf([&] { (void)provider.startBySls(dir.path(), false); });
    EXPECT_TRUE(text.contains(dir.path())) << text.toStdString();
}
