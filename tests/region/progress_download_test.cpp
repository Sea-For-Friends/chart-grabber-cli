// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/progress_download (Src/RegionProcess/Download/frm_ProgressDownload.pas :166-167,
// 251-261, 316-321, 395-437, 451-577, 586-595; the headless half). T-0182, proofs 1-7.
// FEAT-REG-016 (autosave) and FEAT-REG-017 (save session): contribute only (docs/parity/held-open.md).
//
// Rig: a real RegionProcessProgressInfoDownload over a real DownloadSession; the progress object's clock
// (days) is `Rig::days`, the file-name clock (legacy Now) is `Rig::wall`, and the injected sleep advances
// `wall` by the slept time, so "the next attempt with a later clock value" is deterministic.
// All expected values are literals from the Pascal.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/log_ring_buffer.h"
#include "cg/core/operation_notifier.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/value_to_string.h"
#include "cg/region/download_session.h"
#include "cg/region/progress_download.h"
#include "cg/storage/map_version.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "region_fake_map_type.h"

using cg::core::ConfigProviderIni;
using cg::core::OperationNotifier;
using cg::region::DownloadSession;
using cg::region::ProgressDownload;
using cg::region::RegionProcessProgressInfoDownload;
using cg::region::test::FakeMapType;

namespace {

QString readKey(const QString& path, const QString& section, const QString& key) {
    auto ini = ConfigProviderIni::open(path);
    EXPECT_TRUE(ini.has_value());
    return (*ini)->file().readString(section, key);
}

struct Rig {
    QTemporaryDir dir;
    double days = 0; // the progress object's clock; elapsedTime() = days - 0
    QDateTime wall = QDateTime(QDate(2024, 3, 5), QTime(14, 7, 9, 25));
    std::vector<int> sleeps;
    QStringList memo;
    cg::storage::MapVersionFactory versions;
    cg::geo::GeometryLonLatFactory lonLat;
    std::shared_ptr<DownloadSession> session;
    std::shared_ptr<RegionProcessProgressInfoDownload> info;
    std::shared_ptr<OperationNotifier> notifier = std::make_shared<OperationNotifier>();
    std::unique_ptr<ProgressDownload> pd;

    Rig(int interval, const QString& prefix, int workersCount = 1, int workerIndex = 0) {
        auto map = std::make_shared<FakeMapType>();
        map->guidValue = QUuid::fromString(QStringLiteral("{0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9}"));
        session = std::make_shared<DownloadSession>(
            map, nullptr, versions.byStoreString(QStringLiteral("dl-v7")), 7, std::vector<quint8>{3, 5},
            lonLat.createPolygonByRect({1.0, 4.0, 3.0, 2.0}), false, cg::storage::TileDate{}, false, false,
            false, cg::storage::TileDate{}, false, interval, prefix, workersCount, workerIndex);
        info = std::make_shared<RegionProcessProgressInfoDownload>(
            std::make_shared<cg::core::LogRingBuffer>(10), session, false, [this] { return days; });
        useDir(dir.path());
    }

    void useDir(const QString& autoSaveDir) {
        pd = std::make_unique<ProgressDownload>(
            info, notifier, autoSaveDir, [this](const QString& line) { memo << line; },
            [this] { return wall; },
            [this](int ms) {
                sleeps.push_back(ms);
                wall = wall.addMSecs(ms);
            });
    }

    // One timer tick: `minutes` of progress time, one second of wall time, then the check.
    void tick(double minutes) {
        days += minutes / 1440.0;
        wall = wall.addMSecs(1000);
        pd->checkSessionAutosave();
    }
    [[nodiscard]] QString path(const QString& rel) const { return dir.path() + QLatin1Char('/') + rel; }
    [[nodiscard]] QStringList files(const QString& sub = {}) const {
        return QDir(sub.isEmpty() ? dir.path() : path(sub)).entryList(QDir::Files, QDir::Name);
    }
};

void touch(const QString& path) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Proof 1: the interval (:529-540). Whole minutes, Delphi Round (ties to even), compared with `<`.
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadAutosave, IntervalZeroNeverSaves) {
    Rig r(0, {});
    r.tick(0);
    r.tick(600);
    EXPECT_TRUE(r.files().isEmpty());
    EXPECT_TRUE(r.pd->sessionFileName().isEmpty());
}

TEST(ProgressDownloadAutosave, Interval5SavesFirstThenAtRoundedFive) {
    Rig r(5, {});
    r.tick(3); // not initialized: saves at once, whatever the elapsed time (3 min) is
    EXPECT_EQ(r.files().size(), 1);
    r.tick(4.4); // 4.4 since the save: rounds to 4 < 5
    EXPECT_EQ(r.files().size(), 1);
    r.tick(0.2); // 4.6 since the save: rounds to 5
    EXPECT_EQ(r.files().size(), 2);
}

TEST(ProgressDownloadAutosave, TiesGoToEven) {
    // clock steps of 1/2880 day = 0.5 min; the products must be exact or the ties prove nothing
    ASSERT_EQ(1 / 2880.0 * 24 * 60, 0.5);
    ASSERT_EQ(5 / 2880.0 * 24 * 60, 2.5);
    struct Row {
        int interval;
        int halfMinutes;
        bool saves;
    };
    const Row rows[] = {
        {1, 1, false}, // 0.5 -> 0 (half-up would give 1)
        {2, 5, true},  // 2.5 -> 2, not below 2
        {3, 5, false}, // 2.5 -> 2 < 3 (half-up would give 3)
        {2, 3, true},  // 1.5 -> 2
        {4, 7, true},  // 3.5 -> 4, not below 4
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.interval * 1000 + row.halfMinutes);
        Rig r(row.interval, {});
        r.tick(0);
        ASSERT_EQ(r.files().size(), 1);
        r.days = row.halfMinutes / 2880.0;
        r.wall = r.wall.addMSecs(1000);
        r.pd->checkSessionAutosave();
        EXPECT_EQ(r.files().size(), row.saves ? 2 : 1);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Proof 2: names (:474-527)
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadAutosave, FileNames) {
    struct Row {
        QString prefix;
        int workersCount;
        int workerIndex;
        QString expected;
    };
    const Row rows[] = {
        {"", 1, 0, "240305_140709_025.sls"}, // empty prefix: date part, 24-hour
        {"a/b_", 1, 0, "a/b_140709_025.sls"},   {"Город_", 2, 1, "Город_02_140709_025.sls"},
        {"", 3, 2, "03_240305_140709_025.sls"}, {"q_", 1, 4, "q_140709_025.sls"}, // one worker: no NN_ part
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.expected.toStdString());
        Rig r(5, row.prefix, row.workersCount, row.workerIndex);
        r.pd->checkSessionAutosave();
        EXPECT_EQ(r.pd->sessionFileName(), r.path(row.expected));
        EXPECT_TRUE(QFile::exists(r.path(row.expected)));
        EXPECT_TRUE(r.memo.size() == 1 && r.memo[0].startsWith(QStringLiteral("Session saved to file: ")));
    }
}

TEST(ProgressDownloadAutosave, CollisionTakesTheNextAttemptAfterSleep100) {
    Rig r(5, {});
    touch(r.path("240305_140709_025.sls"));
    r.pd->checkSessionAutosave();
    EXPECT_EQ(r.sleeps, (std::vector<int>{100}));
    EXPECT_EQ(r.pd->sessionFileName(), r.path("240305_140709_125.sls"));
    EXPECT_EQ(QFileInfo(r.path("240305_140709_025.sls")).size(), 0); // the old file is left alone
}

TEST(ProgressDownloadAutosave, TenCollisionsDoNotSaveAndTheNextCallSavesAtOnce) {
    Rig r(5, {});
    for (int i = 0; i < 10; ++i) { // attempts at .025, .125 ... .925
        touch(r.path(QStringLiteral("240305_140709_%1.sls").arg(25 + i * 100, 3, 10, QLatin1Char('0'))));
    }
    r.pd->checkSessionAutosave();
    EXPECT_EQ(r.sleeps.size(), 10u);
    EXPECT_TRUE(r.pd->sessionFileName().isEmpty());
    EXPECT_EQ(r.files().size(), 10);
    EXPECT_TRUE(r.memo.isEmpty()); // finding a: a silent Exit

    // the progress clock did not move (0 min < interval 5): only "not initialized" can save now
    r.pd->checkSessionAutosave();
    EXPECT_EQ(r.pd->sessionFileName(), r.path("240305_140710_025.sls"));
    EXPECT_EQ(r.files().size(), 11);
}

// ---------------------------------------------------------------------------------------------------------
// Proof 3: rotation and close (:554-563, :586-595)
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadAutosave, RotationKeepsNewestAndOneBackupThenCloseDropsTheBackup) {
    Rig r(1, {});
    r.info->addProcessedTile({1, 1});
    r.tick(0); // save 1: Processed=1
    r.info->addProcessedTile({2, 2});
    r.tick(1); // save 2: Processed=2
    r.info->addProcessedTile({3, 3});
    r.tick(1); // save 3: Processed=3
    EXPECT_EQ(r.files(), (QStringList{"240305_140711_025.bk", "240305_140712_025.sls"}));
    EXPECT_EQ(readKey(r.path("240305_140711_025.bk"), "Session", "Processed"), "2");
    EXPECT_EQ(readKey(r.path("240305_140712_025.sls"), "Session", "Processed"), "3");
    EXPECT_EQ(r.pd->sessionFileName(), r.path("240305_140712_025.sls"));

    EXPECT_FALSE(r.notifier->isCancelled());
    r.pd->close();
    EXPECT_TRUE(r.notifier->isCancelled());
    EXPECT_EQ(r.files(), (QStringList{"240305_140712_025.sls"}));
}

// ---------------------------------------------------------------------------------------------------------
// Proof 4: LR-4, DoSaveSession (:451-470)
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadSaveSession, KeepsForeignKeysAndSectionsAndWritesTheStateKeys) {
    Rig r(15, QStringLiteral("pre"), 4, 2);
    const QString target = r.path("user.sls");
    {
        QFile f(target);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("[Session]\nForeign=keep\xD0\xB4\n[Other]\nx=y\n");
    }
    r.pd->saveSession(target);

    auto ini = ConfigProviderIni::open(target);
    ASSERT_TRUE(ini.has_value());
    const auto& file = (*ini)->file();
    EXPECT_EQ(file.readString(u"Session", u"Foreign"), QString::fromUtf8("keep\xD0\xB4"));
    EXPECT_EQ(file.readString(u"Other", u"x"), "y");
    EXPECT_EQ(file.readString(u"Session", u"AutoSavePrefix"), "pre");
    EXPECT_EQ(file.readString(u"Session", u"WorkersCount"), "4");
    EXPECT_EQ(file.readString(u"Session", u"AutoSaveInterval"), "15");

    // the written keys are exactly saveState's
    auto freshRoot = ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>());
    const auto section = freshRoot->orCreateSubItem(u"Session");
    r.info->saveState(*section);
    QStringList expected = freshRoot->file().keyNames(u"Session");
    expected.prepend(QStringLiteral("Foreign"));
    EXPECT_EQ(file.keyNames(u"Session"), expected);
    for (const QString& key : freshRoot->file().keyNames(u"Session")) {
        EXPECT_EQ(file.readString(u"Session", key), freshRoot->file().readString(u"Session", key))
            << key.toStdString();
    }
    ASSERT_EQ(r.memo.size(), 1);
    EXPECT_EQ(r.memo[0], "Session saved to file: " + target);
}

TEST(ProgressDownloadSaveSession, EmptyTargetDoesNothingAndAnUnwritableOneThrows) {
    Rig r(5, {});
    r.pd->saveSession({});
    EXPECT_TRUE(r.memo.isEmpty());
    EXPECT_TRUE(r.files().isEmpty());
    EXPECT_THROW(r.pd->saveSession(r.path("no-such-dir/x.sls")), std::runtime_error);
    EXPECT_TRUE(r.memo.isEmpty());
}

// ---------------------------------------------------------------------------------------------------------
// Proof 5: the exception path (:486-488, :566-576), finding b
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadAutosave, DirectoryUnderARegularFileLogsOnceThenReusesThePrefix) {
    Rig r(5, {}, 2, 1);
    // autoSaveDir = <tmp>/blocker, a regular file
    touch(r.path("blocker"));
    r.useDir(r.path("blocker"));
    r.pd->checkSessionAutosave();
    ASSERT_EQ(r.memo.size(), 1);
    EXPECT_TRUE(r.memo[0].startsWith(QStringLiteral("Failed to save session to file: \r\n")));
    EXPECT_TRUE(r.memo[0].contains(QStringLiteral("runtime_error: ")));
    EXPECT_TRUE(r.pd->sessionFileName().isEmpty());

    // initialized is now true: nothing happens before the interval ...
    r.tick(1);
    EXPECT_EQ(r.memo.size(), 1);
    EXPECT_TRUE(r.sleeps.empty());
    // ... and after it the stale prefix <tmp>/blocker/ is used: file creation fails, 10 sleeps, silent
    r.tick(5);
    EXPECT_EQ(r.memo.size(), 1);
    EXPECT_EQ(r.sleeps.size(), 10u);

    // make the directory real: the next save uses the stored prefix, which has no "02_" part
    QFile::remove(r.path("blocker"));
    ASSERT_TRUE(QDir().mkpath(r.path("blocker")));
    r.tick(5);
    EXPECT_EQ(r.files("blocker").size(), 1);
    EXPECT_TRUE(r.files("blocker")[0].startsWith(QStringLiteral("2403")) &&
                !r.files("blocker")[0].startsWith(QStringLiteral("02_")));
    EXPECT_EQ(r.memo.size(), 2);
    EXPECT_TRUE(r.memo[1].startsWith(QStringLiteral("Session saved to file: ")));
}

// ---------------------------------------------------------------------------------------------------------
// Proof 6: the texts (:316-321, :395-429)
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadTexts, PercentText) {
    using cg::region::percentText;
    EXPECT_EQ(percentText(0, 5), "~%");
    EXPECT_EQ(percentText(3, 1), "33%");
    EXPECT_EQ(percentText(3, 2), "67%");
    EXPECT_EQ(percentText(8, 1), "12%"); // 12.5 -> even
    EXPECT_EQ(percentText(8, 3), "38%"); // 37.5 -> even
    EXPECT_EQ(percentText(8, 5), "62%"); // 62.5 -> even
}

TEST(ProgressDownloadTexts, RemainingSize) {
    const QLocale saved;
    QLocale::setDefault(QLocale::c());
    const cg::geo::ValueToStringConverter converter{cg::geo::ValueFormatSettings{}};
    using cg::region::remainingSize;
    EXPECT_EQ(remainingSize(100, 40, 0, 8.0, converter), "~ Kb");
    // 8 Kb / 4 tiles * 60 left = 120 Kb
    EXPECT_EQ(remainingSize(100, 40, 4, 8.0, converter), "120.0 KB");
    // 4096 / 2 * 1000 = 2048000 Kb = 1.95 Gb
    EXPECT_EQ(remainingSize(1000, 0, 2, 4096.0, converter), "2.0 GB");
    QLocale::setDefault(saved);
}

TEST(ProgressDownloadTexts, RemainingTime) {
    using cg::region::remainingTime;
    EXPECT_EQ(remainingTime(4, 0, 1.0), "~");
    EXPECT_EQ(remainingTime(4, 1, 1 / 24.0), "03:00:00");          // 1 h at 25 %
    EXPECT_EQ(remainingTime(2, 1, 30 / 24.0), "1 days, 06:00:00"); // 30 h at 50 %
    EXPECT_EQ(remainingTime(2, 1, 3723 / 86400.0), "01:02:03");
    EXPECT_EQ(remainingTime(2, 1, 2.0), "2 days, 00:00:00");
}

// ---------------------------------------------------------------------------------------------------------
// Proof 7: the destructor (:251-261)
// ---------------------------------------------------------------------------------------------------------

TEST(ProgressDownloadLifetime, DestructorCancelsTheNotifier) {
    Rig r(5, {});
    EXPECT_FALSE(r.notifier->isCancelled());
    r.pd.reset();
    EXPECT_TRUE(r.notifier->isCancelled());
}
