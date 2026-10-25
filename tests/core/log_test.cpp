// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-185; docs/specs/core.md 2.7-2.8, test plan 7.18
#include "cg/core/log.h"
#include "cg/core/log_ring_buffer.h"
#include "cg/core/platform/terminal.h"

#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <latch>
#include <memory>
#include <set>
#include <stop_token>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

using cg::core::Log;
using cg::core::logCore;
using cg::core::LogRingBuffer;

namespace {

class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        QLoggingCategory::setFilterRules(QStringLiteral("cg.*=true"));
        Log::setMinimumLevel(QtDebugMsg);
    }
    void TearDown() override {
        Log::shutdown();
        Log::setMinimumLevel(QtDebugMsg);
    }

    [[nodiscard]] static QString readLog(const QString& dir) {
        QFile file(Log::defaultLogFileName(dir));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    QTemporaryDir tempDir;
};

} // namespace

TEST_F(LogTest, InstallWithoutDirWritesToStderrOnly) {
    ASSERT_TRUE(Log::install(QString()).has_value());
    qCWarning(logCore) << "no file please";
    EXPECT_FALSE(QFile::exists(Log::defaultLogFileName(tempDir.path())));
}

TEST_F(LogTest, InstallCreatesLogFile) {
    ASSERT_TRUE(tempDir.isValid());
    ASSERT_TRUE(Log::install(tempDir.path()).has_value());
    qCWarning(logCore) << "hello";
    EXPECT_TRUE(QFile::exists(Log::defaultLogFileName(tempDir.path())));
    EXPECT_TRUE(readLog(tempDir.path()).contains(QStringLiteral("hello")));
}

TEST_F(LogTest, LineFormatIsTimestampCategoryMessage) {
    ASSERT_TRUE(Log::install(tempDir.path()).has_value());
    qCWarning(logCore) << "formatted";
    const QString contents = readLog(tempDir.path());
    const QRegularExpression pattern(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[cg\.core\] formatted\n$)"));
    EXPECT_TRUE(pattern.match(contents).hasMatch()) << contents.toStdString();
}

TEST_F(LogTest, TruncateOnStartClearsPreviousFile) {
    ASSERT_TRUE(Log::install(tempDir.path()).has_value());
    qCWarning(logCore) << "first run";
    ASSERT_TRUE(Log::install(tempDir.path(), true).has_value());
    qCWarning(logCore) << "second run";
    const QString contents = readLog(tempDir.path());
    EXPECT_FALSE(contents.contains(QStringLiteral("first run")));
    EXPECT_TRUE(contents.contains(QStringLiteral("second run")));
}

TEST_F(LogTest, MinimumLevelFiltersMessages) {
    ASSERT_TRUE(Log::install(tempDir.path()).has_value());
    Log::setMinimumLevel(QtWarningMsg);
    EXPECT_EQ(Log::minimumLevel(), QtWarningMsg);
    qCDebug(logCore) << "chatty";
    qCWarning(logCore) << "important";
    const QString contents = readLog(tempDir.path());
    EXPECT_FALSE(contents.contains(QStringLiteral("chatty")));
    EXPECT_TRUE(contents.contains(QStringLiteral("important")));
}

// Finding 1 (T-0002A rework): minimumLevel() is read by handler() on arbitrary threads while
// setMinimumLevel() is written from another; under TSan this is a data race unless minimumLevel
// is std::atomic (spec §3 "Log (statics): thread-safe"; §2.7 "callable from any thread"). This
// test hammers both sides concurrently with real logging calls so TSan (and the CI asan/tsan
// presets) can catch a regression back to a plain field.
TEST_F(LogTest, MinimumLevelIsAtomicUnderConcurrentAccess) {
    ASSERT_TRUE(Log::install(QString()).has_value());
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    threads.emplace_back([&stop] {
        while (!stop.load(std::memory_order_relaxed)) {
            Log::setMinimumLevel(QtDebugMsg);
            Log::setMinimumLevel(QtWarningMsg);
        }
    });
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&stop] {
            while (!stop.load(std::memory_order_relaxed)) {
                static_cast<void>(Log::minimumLevel());
                qCWarning(logCore) << "concurrent";
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) {
        t.join();
    }
    SUCCEED();
}

TEST_F(LogTest, RingBufferReceivesLines) {
    ASSERT_TRUE(Log::install(QString()).has_value());
    auto ring = std::make_shared<LogRingBuffer>(10);
    Log::setRingBuffer(ring);
    EXPECT_EQ(Log::ringBuffer(), ring);
    qCWarning(logCore) << "ring me";
    std::uint64_t lastId = 0;
    const QStringList lines = ring->since(lastId, 10);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines.at(0).contains(QStringLiteral("ring me")));
}

#ifndef O_BINARY
#define O_BINARY 0 // T-0335: the Windows flag; the capture file must not turn "\n" into "\r\n"
#endif

namespace {

// Redirects fd 2 to a file for the lifetime of the object; the previous fd 2 is restored by
// finish(). Lines of the file are the exact bytes our handler wrote to stderr.
class StderrCapture {
public:
    explicit StderrCapture(const QString& path) : path_(path) {
        std::fflush(stderr);
        saved_ = ::dup(2);
        if (saved_ < 0) {
            return; // cannot restore later, so fd 2 is left alone
        }
        const int fd =
            ::open(path.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_BINARY, 0600);
        if (fd >= 0) {
            ::dup2(fd, 2);
            ::close(fd);
        }
    }
    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;
    ~StderrCapture() { static_cast<void>(finish()); }
    [[nodiscard]] QByteArray finish() {
        if (saved_ >= 0) {
            std::fflush(stderr);
            ::dup2(saved_, 2);
            ::close(saved_);
            saved_ = -1;
        }
        QFile file(path_);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private:
    QString path_;
    int saved_ = -1;
};

std::atomic<int> g_previousHandlerCount{0};
void countingHandler(QtMsgType, const QMessageLogContext&, const QString&) {
    g_previousHandlerCount.fetch_add(1);
}

QString longPayload(int thread, int round, int message) {
    return QStringLiteral("T%1 R%2 M%3 %4")
        .arg(thread)
        .arg(round)
        .arg(message)
        .arg(QString(150, QLatin1Char('x')));
}

} // namespace

// T-0335: after install stderr is binary, so one fwrite of a line stays one line ("\n", not CR LF) on Windows
// (always true on POSIX). stderr starts in text mode there, so removing the call in Log::install fails this
// test.
TEST_F(LogTest, InstallMakesStderrBinary) {
    ASSERT_TRUE(tempDir.isValid());
    ASSERT_TRUE(Log::install(tempDir.path()).has_value());
    EXPECT_TRUE(cg::core::platform::isBinaryStream(stderr));
    Log::shutdown();
}

// CORE-LOG-1: N threads log through qCDebug/qCWarning at the same time; every stderr line is whole
// (one write under the mutex), the count is exact and each message arrives exactly once.
TEST_F(LogTest, ConcurrentLinesAreWholeOnStderr) {
    ASSERT_TRUE(tempDir.isValid());
    constexpr int kThreads = 4;
    constexpr int kRounds = 1000;
    constexpr int kMessages = 3;
    ASSERT_TRUE(Log::install(tempDir.path()).has_value()); // file sink active too
    StderrCapture capture(tempDir.filePath(QStringLiteral("stderr.txt")));
    for (int round = 0; round < kRounds; ++round) {
        std::latch start(kThreads);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&start, t, round] {
                start.arrive_and_wait();
                for (int m = 0; m < kMessages; ++m) {
                    if (m % 2 == 0) {
                        qCDebug(logCore).noquote() << longPayload(t, round, m);
                    } else {
                        qCWarning(logCore).noquote() << longPayload(t, round, m);
                    }
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    const QByteArray captured = capture.finish();
    Log::shutdown();
    // Both sinks are written in one critical section: same lines in the same order.
    EXPECT_EQ(readLog(tempDir.path()).toUtf8(), captured);
    const QRegularExpression pattern(QStringLiteral(
        R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[cg\.core\] T(\d+) R(\d+) M(\d+) x{150}$)"));
    const QList<QByteArray> lines = captured.split('\n');
    ASSERT_EQ(lines.size(), kThreads * kRounds * kMessages + 1); // trailing empty piece after last '\n'
    EXPECT_TRUE(lines.last().isEmpty());
    std::set<std::tuple<int, int, int>> seen;
    int malformed = 0;
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const auto match = pattern.match(QString::fromUtf8(lines.at(i)));
        if (!match.hasMatch()) {
            ++malformed;
            continue;
        }
        seen.emplace(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
    }
    EXPECT_EQ(malformed, 0);
    EXPECT_EQ(seen.size(), static_cast<std::size_t>(kThreads * kRounds * kMessages));
}

// Contract (log.h): after shutdown() messages go to the previous handler; install()/shutdown()
// racing with logging threads loses no message and writes no partial line: every message is
// either one whole stderr line or one call of the previous handler.
TEST_F(LogTest, InstallShutdownRaceWithLoggingThreads) {
    ASSERT_TRUE(tempDir.isValid());
    constexpr int kThreads = 3;
    constexpr int kMessages = 2000;
    g_previousHandlerCount = 0;
    const QtMessageHandler before = qInstallMessageHandler(countingHandler);
    StderrCapture capture(tempDir.filePath(QStringLiteral("stderr.txt")));

    std::latch start(kThreads + 1);
    std::vector<std::thread> loggers;
    for (int t = 0; t < kThreads; ++t) {
        loggers.emplace_back([&start, t] {
            start.arrive_and_wait();
            for (int m = 0; m < kMessages; ++m) {
                qCWarning(logCore).noquote() << longPayload(t, 0, m);
            }
        });
    }
    std::jthread toggler([&start](const std::stop_token& stop) {
        start.arrive_and_wait();
        while (!stop.stop_requested()) {
            ASSERT_TRUE(Log::install(QString()).has_value());
            std::this_thread::yield();
            Log::shutdown();
            std::this_thread::yield();
        }
    });
    for (auto& th : loggers) {
        th.join();
    }
    toggler.request_stop();
    toggler.join();

    // Deterministic part of the contract: after shutdown() nothing reaches our stderr sink.
    Log::shutdown();
    const int previousBefore = g_previousHandlerCount.load();
    qCWarning(logCore) << "after shutdown";
    EXPECT_EQ(g_previousHandlerCount.load(), previousBefore + 1);

    const QByteArray captured = capture.finish();
    qInstallMessageHandler(before);
    const QList<QByteArray> lines = captured.split('\n');
    int whole = 0;
    for (int i = 0; i + 1 < lines.size(); ++i) {
        EXPECT_TRUE(lines.at(i).contains(" [cg.core] T") && lines.at(i).endsWith("x"))
            << lines.at(i).toStdString();
        ++whole;
    }
    EXPECT_TRUE(lines.last().isEmpty());
    EXPECT_EQ(whole + g_previousHandlerCount.load(), kThreads * kMessages + 1);
}

TEST(LogRingBuffer, SinceReturnsOnlyNewLinesAndEvictsAtCapacity) {
    LogRingBuffer ring(3);
    EXPECT_EQ(ring.capacity(), 3);
    ring.append(QStringLiteral("one"));
    ring.append(QStringLiteral("two"));

    std::uint64_t lastId = 0;
    EXPECT_EQ(ring.since(lastId, 10), (QStringList{QStringLiteral("one"), QStringLiteral("two")}));
    EXPECT_TRUE(ring.since(lastId, 10).isEmpty());

    ring.append(QStringLiteral("three"));
    ring.append(QStringLiteral("four"));
    ring.append(QStringLiteral("five")); // evicts "one" and "two"
    EXPECT_EQ(ring.since(lastId, 10),
              (QStringList{QStringLiteral("three"), QStringLiteral("four"), QStringLiteral("five")}));

    // u_LogForTaskThread.pas:105-116: the NEWEST maxRows lines, not the oldest
    std::uint64_t fromStart = 0;
    EXPECT_EQ(ring.since(fromStart, 2), (QStringList{QStringLiteral("four"), QStringLiteral("five")}));
    EXPECT_EQ(fromStart, 5U);

    ring.clear();
    std::uint64_t afterClear = 0;
    EXPECT_TRUE(ring.since(afterClear, 10).isEmpty());
}

// T-0173: more than maxRows lines between two polls: the newest are returned, the older are skipped for
// ever (:105-116, :125); a smaller poll afterwards sees only what is newer.
TEST(LogRingBuffer, SinceSkipsOlderUnreadLinesBeyondMaxRows) {
    LogRingBuffer ring(10);
    for (const char* text : {"a", "b", "c", "d", "e"}) {
        ring.append(QString::fromLatin1(text));
    }
    std::uint64_t lastId = 1; // "a" was read before
    EXPECT_EQ(ring.since(lastId, 2), (QStringList{QStringLiteral("d"), QStringLiteral("e")}));
    EXPECT_EQ(lastId, 5U);
    EXPECT_TRUE(ring.since(lastId, 2).isEmpty()); // "b" and "c" are never delivered
    ring.append(QStringLiteral("f"));
    EXPECT_EQ(ring.since(lastId, 2), (QStringList{QStringLiteral("f")}));
}

// T-0156: level filter of LogRingBuffer::append(line, level) (u_LogForTaskThread.pas:135-150).
TEST(LogRingBuffer, LevelBelowMinimumIsDroppedAndConsumesNoId) {
    LogRingBuffer ring(10, 5);
    ring.append(QStringLiteral("low"), 4);
    ring.append(QStringLiteral("equal"), 5);
    ring.append(QStringLiteral("high"), 10);
    std::uint64_t lastId = 0;
    EXPECT_EQ(ring.since(lastId, 10), (QStringList{QStringLiteral("equal"), QStringLiteral("high")}));
    EXPECT_EQ(lastId, 2U); // the dropped line took no id
}

TEST(LogRingBuffer, DefaultMinimumStoresLevelZeroAndOne) {
    LogRingBuffer ring(10);
    ring.append(QStringLiteral("zero"), 0);
    ring.append(QStringLiteral("one"), 1);
    std::uint64_t lastId = 0;
    EXPECT_EQ(ring.since(lastId, 10), (QStringList{QStringLiteral("zero"), QStringLiteral("one")}));
}

TEST(LogRingBuffer, PlainAppendIgnoresMinimumLevel) {
    LogRingBuffer ring(10, 5); // core::Log's sink path has no level
    ring.append(QStringLiteral("sink"));
    std::uint64_t lastId = 0;
    EXPECT_EQ(ring.since(lastId, 10), (QStringList{QStringLiteral("sink")}));
}
