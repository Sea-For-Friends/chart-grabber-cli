// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/region_process_progress_info_download
// (Src/RegionProcess/u_RegionProcessProgressInfoDownload.pas :128-506).
// FEAT-REG-013 (start paused) and FEAT-REG-018 (progress, pause, resume): contribute only.
// saveState route: DownloadSession::save is still the T-0164 placeholder, so the session behind the
// progress object is a forwarding fake (below) that records the call of save.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/log_ring_buffer.h"
#include "cg/region/region_process_progress_info_download.h"

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

using cg::geo::PointI;
using cg::region::DownloadSession;
using cg::region::RegionProcessProgressInfoDownload;

namespace {

const PointI kUnset{-1, -1};

// Forwards every call to a real DownloadSession; save only records its argument.
class ForwardingSession final : public cg::region::IDownloadSession {
public:
    explicit ForwardingSession(std::shared_ptr<DownloadSession> inner) : in(std::move(inner)) {}

    std::shared_ptr<DownloadSession> in;
    int saveCalls = 0;
    cg::core::IConfigWriter* savedSection = nullptr;
    double elapsedSeenBySave = -1;

    cg::maptype::IMapTypePtr mapType() const override { return in->mapType(); }
    cg::maptype::MapVersionRequestPtr versionForCheck() const override { return in->versionForCheck(); }
    cg::storage::MapVersionInfoPtr versionForDownload() const override { return in->versionForDownload(); }
    quint8 zoom() const override { return in->zoom(); }
    void setZoom(quint8 v) override { in->setZoom(v); }
    std::vector<quint8> zoomArr() const override { return in->zoomArr(); }
    cg::region::PolygonPtr polygon() const override { return in->polygon(); }
    bool secondLoadTne() const override { return in->secondLoadTne(); }
    cg::storage::TileDate replaceTneOlderDate() const override { return in->replaceTneOlderDate(); }
    bool replaceExistTiles() const override { return in->replaceExistTiles(); }
    bool checkExistTileSize() const override { return in->checkExistTileSize(); }
    bool checkExistTileDate() const override { return in->checkExistTileDate(); }
    cg::storage::TileDate checkTileDate() const override { return in->checkTileDate(); }
    qint64 processed() const override { return in->processed(); }
    void setProcessed(qint64 v) override { in->setProcessed(v); }
    quint64 downloadedSize() const override { return in->downloadedSize(); }
    void setDownloadedSize(quint64 v) override { in->setDownloadedSize(v); }
    qint64 downloadedCount() const override { return in->downloadedCount(); }
    void setDownloadedCount(qint64 v) override { in->setDownloadedCount(v); }
    PointI lastProcessedPoint() const override { return in->lastProcessedPoint(); }
    void setLastProcessedPoint(PointI v) override { in->setLastProcessedPoint(v); }
    qint64 lastProcessedCount() const override { return in->lastProcessedCount(); }
    PointI lastSuccessfulPoint() const override { return in->lastSuccessfulPoint(); }
    void setLastSuccessfulPoint(PointI v) override { in->setLastSuccessfulPoint(v); }
    double elapsedTime() const override { return in->elapsedTime(); }
    void setElapsedTime(double v) override { in->setElapsedTime(v); }
    bool autoCloseAtFinish() const override { return in->autoCloseAtFinish(); }
    void setAutoCloseAtFinish(bool v) override { in->setAutoCloseAtFinish(v); }
    int workersCount() const override { return in->workersCount(); }
    int workerIndex() const override { return in->workerIndex(); }
    QString autosavePrefix() const override { return in->autosavePrefix(); }
    void setAutosavePrefix(const QString& v) override { in->setAutosavePrefix(v); }
    int autosaveInterval() const override { return in->autosaveInterval(); }
    void setAutosaveInterval(int v) override { in->setAutosaveInterval(v); }
    void save(cg::core::IConfigWriter& section) override {
        ++saveCalls;
        savedSection = &section;
        elapsedSeenBySave = in->elapsedTime();
    }
    void load(const cg::core::IConfigProviderPtr&, const cg::maptype::IMapTypeSet&,
              const cg::net::IGlobalDownloadConfig&, const cg::geo::GeometryLonLatFactory&) override {}
};

struct Rig {
    double clock = 100.0;
    std::shared_ptr<DownloadSession> inner;
    std::shared_ptr<ForwardingSession> session;
    std::shared_ptr<cg::core::LogRingBuffer> log = std::make_shared<cg::core::LogRingBuffer>(10);
    std::unique_ptr<RegionProcessProgressInfoDownload> info;

    explicit Rig(bool paused, std::shared_ptr<DownloadSession> in = std::make_shared<DownloadSession>())
        : inner(std::move(in)), session(std::make_shared<ForwardingSession>(inner)) {
        info = std::make_unique<RegionProcessProgressInfoDownload>(log, session, paused,
                                                                   [this] { return clock; });
    }
};

} // namespace

// Proof 1: :147-161. Distinct literals for each argument.
TEST(RegionProcessProgressInfoDownload, AddDownloadedTileThenProcessedTile) {
    Rig r(false);
    r.info->addDownloadedTile({3, 4}, 700);
    EXPECT_EQ(r.info->downloaded(), 1);
    EXPECT_EQ(r.info->downloadSize(), 700u);
    EXPECT_EQ(r.inner->lastSuccessfulPoint(), PointI(3, 4));
    EXPECT_EQ(r.info->processed(), 0); // the worker calls addProcessedTile next
    EXPECT_EQ(r.inner->lastProcessedPoint(), kUnset);

    r.info->addProcessedTile({5, 6}); // :190-201
    EXPECT_EQ(r.info->processed(), 1);
    EXPECT_EQ(r.inner->lastProcessedPoint(), PointI(5, 6));
    EXPECT_EQ(r.inner->lastSuccessfulPoint(), PointI(3, 4));

    r.info->addDownloadedTile({7, 8}, 300);
    EXPECT_EQ(r.info->downloaded(), 2);
    EXPECT_EQ(r.info->downloadSize(), 1000u);
}

// :163-175
TEST(RegionProcessProgressInfoDownload, AddManyProcessedTileAddsCount) {
    Rig r(false);
    r.info->addManyProcessedTile({8, 9}, 5);
    EXPECT_EQ(r.info->processed(), 5);
    EXPECT_EQ(r.inner->lastProcessedPoint(), PointI(8, 9));
    EXPECT_EQ(r.inner->lastSuccessfulPoint(), kUnset);
    r.info->addProcessedTile({1, 1});
    EXPECT_EQ(r.info->processed(), 6);
}

// Proofs 1 and 3: :177-188 and :399-417.
TEST(RegionProcessProgressInfoDownload, LastTileInfoSizeAndNotNecessaryTile) {
    Rig r(false);
    r.info->setZoom(10);
    quint8 z = 0;
    PointI p;
    int size = 99;

    r.info->addDownloadedTile({1, 2}, 700);
    r.info->addProcessedTile({1, 2});
    r.info->getLastTileInfo(z, p, size);
    EXPECT_EQ(z, 10);
    EXPECT_EQ(p, PointI(1, 2));
    EXPECT_EQ(size, 700); // points equal

    r.info->addProcessedTile({3, 4}); // processed point moves on
    r.info->getLastTileInfo(z, p, size);
    EXPECT_EQ(p, PointI(3, 4));
    EXPECT_EQ(size, 0);

    r.info->addNotNecessaryTile({3, 4}); // only the successful point moves, size -1
    EXPECT_EQ(r.inner->lastSuccessfulPoint(), PointI(3, 4));
    EXPECT_EQ(r.inner->lastProcessedPoint(), PointI(3, 4));
    EXPECT_EQ(r.info->processed(), 2);
    EXPECT_EQ(r.info->downloaded(), 1);
    r.info->getLastTileInfo(z, p, size);
    EXPECT_EQ(size, -1);

    r.info->addNotNecessaryTile({9, 9}); // differs from the processed point: 0
    r.info->getLastTileInfo(z, p, size);
    EXPECT_EQ(p, PointI(3, 4));
    EXPECT_EQ(size, 0);
}

// Proof 2: :297-313.
TEST(RegionProcessProgressInfoDownload, ProcessedRatio) {
    Rig r(false);
    r.info->addManyProcessedTile({0, 0}, 3);
    EXPECT_EQ(r.info->processedRatio(), 0.0); // total 0
    r.info->setTotalToProcess(12);
    EXPECT_EQ(r.info->totalToProcess(), 12);
    EXPECT_EQ(r.info->processedRatio(), 0.25);
    r.info->addManyProcessedTile({0, 0}, 9); // processed == total
    EXPECT_EQ(r.info->processedRatio(), 1.0);
    r.info->addManyProcessedTile({0, 0}, 1); // processed > total
    EXPECT_EQ(r.info->processedRatio(), 1.0);

    Rig f(false);
    f.info->setTotalToProcess(100);
    f.info->addManyProcessedTile({0, 0}, 5);
    EXPECT_EQ(f.info->processedRatio(), 0.05);
    f.info->finish();
    EXPECT_TRUE(f.info->finished());
    EXPECT_EQ(f.info->processedRatio(), 1.0);

    Rig z(false); // finished wins over total 0
    z.info->finish();
    EXPECT_EQ(z.info->processedRatio(), 1.0);
}

// Proof 4: :128-145, :257-265, :434-462, :464-473.
TEST(RegionProcessProgressInfoDownload, PauseSemantics) {
    Rig r(true);
    EXPECT_TRUE(r.info->isPaused());
    EXPECT_TRUE(r.info->needPause());
    r.inner->setElapsedTime(2.0);
    r.clock = 100.5;
    EXPECT_EQ(r.info->elapsedTime(), 2.0); // paused at construction: does not grow
    r.info->resume();
    EXPECT_FALSE(r.info->isPaused());
    r.info->pause();
    EXPECT_TRUE(r.info->isPaused());
    r.info->setNeedPause(false);
    EXPECT_FALSE(r.info->isPaused());
    r.info->setNeedPause(true);
    EXPECT_TRUE(r.info->needPause());

    // isPaused reads needPause, not paused (:257-265).
    Rig s(false);
    s.info->setPaused();
    s.info->setNeedPause(false);
    EXPECT_FALSE(s.info->isPaused());
    s.info->setNeedPause(true);
    s.info->setStarted();
    EXPECT_TRUE(s.info->isPaused());
}

// Proof 5: :233-245, :464-484, :203-211.
TEST(RegionProcessProgressInfoDownload, ElapsedTimeWithClock) {
    Rig r(false); // startTime = 100.0
    r.inner->setElapsedTime(2.0);
    r.info->setStarted();
    r.clock = 100.5;
    EXPECT_EQ(r.info->elapsedTime(), 2.5);

    r.info->setPaused();
    EXPECT_EQ(r.inner->elapsedTime(), 2.5); // stretch folded into the session
    r.clock = 103.0;
    EXPECT_EQ(r.info->elapsedTime(), 2.5); // no longer follows the clock

    r.info->setStarted(); // startTime = 103.0
    r.clock = 103.25;
    EXPECT_EQ(r.info->elapsedTime(), 2.75);

    // LR-1 neighbour: finish() does NOT fold the stretch in, the value drops to the session total.
    r.info->finish();
    EXPECT_EQ(r.info->elapsedTime(), 2.5);
    EXPECT_EQ(r.inner->elapsedTime(), 2.5);
}

// Proof 5, DEVIATION L-266 (deviations.md, T-0274): saveState folds the stretch in once and restarts it.
TEST(RegionProcessProgressInfoDownload, SaveStateWhileRunningCountsStretchOnce) {
    Rig r(false); // startTime = 100.0
    r.inner->setElapsedTime(1.0);
    r.clock = 100.5;
    auto ini = cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());
    r.info->saveState(*ini);
    EXPECT_EQ(r.session->saveCalls, 1);
    EXPECT_EQ(r.session->savedSection, ini.get());
    EXPECT_EQ(r.session->elapsedSeenBySave, 1.5); // time is added BEFORE save
    EXPECT_EQ(r.inner->elapsedTime(), 1.5);
    EXPECT_EQ(r.info->elapsedTime(), 1.5); // 1.5 + (100.5 - 100.5): stretch restarted at the save
    r.clock = 101.0;
    EXPECT_EQ(r.info->elapsedTime(), 2.0); // 1.5 + (101.0 - 100.5)
    r.info->saveState(*ini);               // adds 0.5 only: the stretch is not counted again
    EXPECT_EQ(r.inner->elapsedTime(), 2.0);
    EXPECT_EQ(r.session->saveCalls, 2);
}

// Proof 5, DEVIATION L-266 (T-0274): three saves one unit apart while running add exactly three units.
TEST(RegionProcessProgressInfoDownload, SaveStateWhileRunningAddsEachStretchExactlyOnce) {
    Rig r(false); // startTime = 100.0
    r.inner->setElapsedTime(1.0);
    auto ini = cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());
    r.clock = 101.0;
    r.info->saveState(*ini);
    EXPECT_EQ(r.inner->elapsedTime(), 2.0);
    r.clock = 102.0;
    r.info->saveState(*ini);
    EXPECT_EQ(r.inner->elapsedTime(), 3.0);
    r.clock = 103.0;
    r.info->saveState(*ini);
    EXPECT_EQ(r.inner->elapsedTime(), 4.0); // start total 1.0 + 3 units
    EXPECT_EQ(r.session->saveCalls, 3);
}

// The test in saveState is on needPause, not on paused.
TEST(RegionProcessProgressInfoDownload, SaveStateTestsNeedPause) {
    auto ini = cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());

    Rig a(true); // needPause = true: total unchanged, save still called
    a.inner->setElapsedTime(1.0);
    a.clock = 100.5;
    a.info->saveState(*ini);
    EXPECT_EQ(a.inner->elapsedTime(), 1.0);
    EXPECT_EQ(a.session->saveCalls, 1);

    Rig b(false); // paused (worker flag) but needPause false: time is still added
    b.clock = 100.5;
    b.info->setPaused(); // session 0.5
    b.clock = 101.0;
    b.info->saveState(*ini);
    EXPECT_EQ(b.inner->elapsedTime(), 1.5);

    Rig c(false); // needPause set while the worker still runs: not added
    c.clock = 100.5;
    c.info->pause();
    c.info->saveState(*ini);
    EXPECT_EQ(c.inner->elapsedTime(), 0.0);
}

// Proof 6: :369-397 forwarding and :315-357, :486-506 round trips with distinct values.
TEST(RegionProcessProgressInfoDownload, ForwardsToSession) {
    auto session = std::make_shared<DownloadSession>(
        nullptr, nullptr, nullptr, 7, std::vector<quint8>{3, 5, 7}, nullptr, false, cg::storage::TileDate{},
        false, false, false, cg::storage::TileDate{}, false, 15, QStringLiteral("preфix"), 4, 2);
    Rig r(false, session);
    EXPECT_EQ(r.info->zoom(), 7);
    EXPECT_EQ(r.info->zoomArray(), (std::vector<quint8>{3, 5, 7}));
    EXPECT_EQ(r.info->sessionAutosaveInterval(), 15);
    EXPECT_EQ(r.info->sessionAutosavePrefix(), QStringLiteral("preфix"));
    int idx = -1;
    int cnt = -1;
    r.info->getWorkerInfo(idx, cnt);
    EXPECT_EQ(idx, 2);
    EXPECT_EQ(cnt, 4);

    EXPECT_FALSE(r.info->autoCloseAtFinish());
    r.info->setAutoCloseAtFinish(true);
    EXPECT_TRUE(r.info->autoCloseAtFinish());
    EXPECT_TRUE(session->autoCloseAtFinish());

    r.info->addProcessedTile({4, 4});
    r.info->setZoom(7); // unchanged zoom keeps the points
    EXPECT_EQ(session->lastProcessedPoint(), PointI(4, 4));
    r.info->setZoom(9); // changed zoom resets them
    EXPECT_EQ(session->zoom(), 9);
    EXPECT_EQ(r.info->zoom(), 9);
    quint8 z = 0;
    PointI p;
    int size = 5;
    r.info->getLastTileInfo(z, p, size);
    EXPECT_EQ(z, 9);
    EXPECT_EQ(p, kUnset);
}

// Proof 7: :267-275.
TEST(RegionProcessProgressInfoDownload, LogAndLogProviderReturnTheBuffer) {
    Rig r(false);
    EXPECT_EQ(r.info->log(), r.log);
    EXPECT_EQ(r.info->logProvider(), r.log);
}

// Proof 8: one writer, three readers on every getter. Proves consistent counters and a quiet TSan;
// nothing about the clock (constant here).
TEST(RegionProcessProgressInfoDownload, WriterAndReadersKeepCountersConsistent) {
    constexpr int kCalls = 3000;
    constexpr int kReaders = 3;
    auto session = std::make_shared<DownloadSession>();
    auto fwd = std::make_shared<ForwardingSession>(session);
    RegionProcessProgressInfoDownload info(std::make_shared<cg::core::LogRingBuffer>(10), fwd, false,
                                           [] { return 100.0; });
    info.setTotalToProcess(1000000);

    std::latch readersReady(kReaders);
    std::atomic<int> readsWhileWriting{0};
    std::atomic<bool> writerDone{false};
    std::atomic<int> violations{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            qint64 lastDownloaded = 0;
            for (int n = 0; n < kCalls; ++n) {
                const qint64 d = info.downloaded();
                const qint64 p = info.processed();
                const quint64 sz = info.downloadSize();
                quint8 z = 0;
                PointI pt;
                int size = 0;
                info.getLastTileInfo(z, pt, size);
                (void)info.zoomArray();
                (void)info.elapsedTime();
                (void)info.isPaused();
                (void)info.finished();
                const double ratio = info.processedRatio();
                if (d < lastDownloaded || ratio < 0.0 || ratio > 1.0 || (z != 0 && z != 5)) {
                    ++violations;
                }
                // every downloaded tile weighs 3 and is read under separate locks: only bounds hold
                if (sz > static_cast<quint64>(kCalls) * 3 || d > kCalls || p > kCalls) {
                    ++violations;
                }
                lastDownloaded = d;
                if (n == 0) {
                    readersReady.count_down();
                }
                if (!writerDone.load()) {
                    ++readsWhileWriting;
                }
            }
        });
    }
    std::thread writer([&] {
        readersReady.wait(); // every reader has read once before the first write
        for (int i = 0; i < kCalls; ++i) {
            info.addDownloadedTile({1, 1}, 3);
            info.addProcessedTile({2, 2});
            info.setZoom(5);
        }
        writerDone = true;
    });
    writer.join();
    for (auto& t : readers) {
        t.join();
    }
    EXPECT_GE(readsWhileWriting.load(), kReaders); // the first reads preceded the writer
    EXPECT_EQ(violations.load(), 0);
    EXPECT_EQ(info.downloaded(), kCalls);
    EXPECT_EQ(info.processed(), kCalls);
    EXPECT_EQ(info.downloadSize(), 3u * kCalls);
    EXPECT_EQ(info.zoom(), 5);
}

// T-0271 (no legacy counterpart): the streak of connection errors counts up, one answer resets it, and
// it never reaches the session (nothing of it is saved; no counter of the session moves).
TEST(RegionProcessProgressInfoDownload, ConnectionErrorStreakCountsAndResets) {
    Rig r(false);
    EXPECT_EQ(r.info->connectionErrorStreak(), 0);
    r.info->noteDownloadAnswer(true);
    r.info->noteDownloadAnswer(true);
    r.info->noteDownloadAnswer(true);
    EXPECT_EQ(r.info->connectionErrorStreak(), 3);
    auto ini = cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());
    r.info->saveState(*ini);
    EXPECT_EQ(r.info->connectionErrorStreak(), 3); // saving does not reset it
    r.info->noteDownloadAnswer(false);
    EXPECT_EQ(r.info->connectionErrorStreak(), 0);
    r.info->noteDownloadAnswer(true);
    EXPECT_EQ(r.info->connectionErrorStreak(), 1);
    EXPECT_EQ(r.info->processed(), 0);
    EXPECT_EQ(r.info->downloaded(), 0);
    EXPECT_EQ(r.inner->lastSuccessfulPoint(), kUnset);
    EXPECT_EQ(r.session->saveCalls, 1);
}

// T-0356 (no legacy counterpart): Unavailable answers count in a row (any other answer restarts that run);
// the class total and the answers outside it only grow.
TEST(RegionProcessProgressInfoDownload, FrameAnswersCountTheRunTheClassAndTheOthers) {
    using cg::region::FrameAnswer;
    Rig r(false);
    auto expectAnswers = [&r](qint64 run, qint64 inClass, qint64 other) {
        const auto answers = r.info->frameAnswers();
        EXPECT_EQ(answers.unavailableRun, run);
        EXPECT_EQ(answers.inClass, inClass);
        EXPECT_EQ(answers.other, other);
    };
    expectAnswers(0, 0, 0);
    r.info->noteFrameAnswer(FrameAnswer::Unavailable);
    r.info->noteFrameAnswer(FrameAnswer::Unavailable);
    expectAnswers(2, 2, 0);
    r.info->noteFrameAnswer(FrameAnswer::Missing); // in the class, but not an Unavailable one
    expectAnswers(0, 3, 0);
    r.info->noteFrameAnswer(FrameAnswer::Unavailable);
    r.info->noteFrameAnswer(FrameAnswer::Other);
    expectAnswers(0, 4, 1);
    auto ini = cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());
    r.info->saveState(*ini);
    expectAnswers(0, 4, 1); // saving does not reset them
}
