// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/region_process_progress_info (Src/RegionProcess/u_RegionProcessProgressInfo.pas).
// No FEAT tag: shared base unit (T-0169).
#include "cg/region/region_process_progress_info.h"

#include <QString>

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <latch>
#include <stop_token>
#include <thread>
#include <vector>

using cg::region::RegionProcessProgressInfo;

// Initial state :78-79 and Finish :82-85.
TEST(RegionProcessProgressInfo, InitialStateAndFinishIsSticky) {
    RegionProcessProgressInfo info{std::stop_token{}};
    EXPECT_EQ(info.processedRatio(), 0.0);
    EXPECT_FALSE(info.finished());
    EXPECT_TRUE(info.caption().isEmpty());
    EXPECT_TRUE(info.firstLine().isEmpty());
    EXPECT_TRUE(info.secondLine().isEmpty());

    info.finish();
    EXPECT_TRUE(info.finished());
    info.setProcessedRatio(0.5); // nothing resets finished
    EXPECT_TRUE(info.finished());
    EXPECT_EQ(info.processedRatio(), 0.5);
}

// SetProcessedRatio :132-141: < 0 -> 0, > 1 -> 1, else the value; NaN fails both tests and is stored as is.
TEST(RegionProcessProgressInfo, RatioClampRows) {
    struct Row {
        double in;
        double out;
    };
    const Row rows[] = {{-0.5, 0.0}, {1.5, 1.0},    {0.25, 0.25}, {0.0, 0.0},
                        {1.0, 1.0},  {-1e300, 0.0}, {7.0, 1.0}};
    for (const Row& r : rows) {
        RegionProcessProgressInfo info{std::stop_token{}};
        info.setProcessedRatio(0.75); // so that a stale value is detected
        info.setProcessedRatio(r.in);
        EXPECT_EQ(info.processedRatio(), r.out) << r.in;
    }
    RegionProcessProgressInfo info{std::stop_token{}};
    info.setProcessedRatio(0.75);
    info.setProcessedRatio(std::nan("")); // legacy order of the tests: NaN is stored as is
    EXPECT_TRUE(std::isnan(info.processedRatio()));
}

// The three strings are independent (:92-130, :143-146).
TEST(RegionProcessProgressInfo, StringsAreIndependent) {
    RegionProcessProgressInfo info{std::stop_token{}};
    info.setCaption(QStringLiteral("Заголовок"));
    info.setFirstLine(QStringLiteral("first"));
    info.setSecondLine(QStringLiteral("вторая"));
    EXPECT_EQ(info.caption(), QStringLiteral("Заголовок"));
    EXPECT_EQ(info.firstLine(), QStringLiteral("first"));
    EXPECT_EQ(info.secondLine(), QStringLiteral("вторая"));
    info.setFirstLine(QStringLiteral("changed"));
    EXPECT_EQ(info.caption(), QStringLiteral("Заголовок"));
    EXPECT_EQ(info.secondLine(), QStringLiteral("вторая"));
}

// GetCancelNotifier/GetOperationID :87-90, :107-110: the token given to the constructor.
TEST(RegionProcessProgressInfo, StopTokenIsTheGivenOne) {
    std::stop_source source;
    RegionProcessProgressInfo info{source.get_token()};
    EXPECT_FALSE(info.stopToken().stop_requested());
    source.request_stop();
    EXPECT_TRUE(info.stopToken().stop_requested());
}

// One writer, one reader. Pins: every string read is empty or 64 equal characters, as written (no torn
// string), the ratio stays inside [0,1], and the unmutated class gives no TSan report. It does NOT detect a
// removed string mutex under TSan: the unsynchronised access is inside the uninstrumented libQt6Core (see the
// pool below).
TEST(RegionProcessProgressInfo, ConcurrentWriterAndReaderSeeNoTornString) {
    constexpr int kWrites = 2000;
    RegionProcessProgressInfo info{std::stop_token{}};
    std::latch start(2);
    std::latch readerBegan(1);
    std::atomic<bool> writerDone{false};
    std::atomic<int> readsWhileWriting{0};
    std::atomic<int> torn{0};
    std::atomic<int> badRatio{0};

    const auto valid = [](const QString& s) {
        if (s.isEmpty()) {
            return true;
        }
        if (s.size() != 64) {
            return false;
        }
        for (const QChar c : s) {
            if (c != s.front()) {
                return false;
            }
        }
        return true;
    };

    // The written strings come from a pool built before the threads start and living past their join, so the
    // writer never drops the last reference to a buffer while the test runs. Reason: libQt6Core is not
    // instrumented by TSan; when the writer's QString::operator= frees the old buffer, TSan does not see the
    // Qt reference count that the reader released first and reports a false "race" with free() (first gate
    // run, saved in docs/handoffs/T-0169.md).
    std::vector<QString> pool;
    for (int i = 0; i < 26; ++i) {
        pool.emplace_back(64, QChar(u'a' + i));
    }

    std::thread writer([&] {
        start.arrive_and_wait();
        for (int i = 0; i < kWrites; ++i) {
            if (i == kWrites / 2) {
                readerBegan.wait(); // the second half of the writes is certainly after the first read
            }
            const QString& s = pool[static_cast<std::size_t>(i % 26)];
            info.setProcessedRatio(static_cast<double>(i) / kWrites);
            info.setCaption(s);
            info.setFirstLine(s);
            info.setSecondLine(s);
        }
        writerDone.store(true);
    });
    std::thread reader([&] {
        start.arrive_and_wait();
        bool first = true;
        do {
            const bool wasDone = writerDone.load();
            const double r = info.processedRatio();
            const bool ok = valid(info.caption()) && valid(info.firstLine()) && valid(info.secondLine());
            (void)info.finished();
            if (!ok) {
                ++torn;
            }
            if (!(r >= 0.0 && r <= 1.0)) {
                ++badRatio;
            }
            if (!wasDone) {
                ++readsWhileWriting;
            }
            if (first) {
                first = false;
                readerBegan.count_down();
            }
        } while (!writerDone.load());
    });
    writer.join();
    reader.join();

    EXPECT_GT(readsWhileWriting.load(), 0); // overlap happened
    EXPECT_EQ(torn.load(), 0);
    EXPECT_EQ(badRatio.load(), 0);
}
