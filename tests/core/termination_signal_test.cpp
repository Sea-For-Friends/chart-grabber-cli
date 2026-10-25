// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/core/platform/termination_signal and terminal (new code; inherits
// frm_ProgressDownload.pas FormClose :586-595, where closing the window cancels).
// Qt defines `signals` as a keyword macro; the test names a variable `signals` (T-0335: QFile is used below).
#define QT_NO_KEYWORDS
#include "cg/core/platform/terminal.h"
#include "cg/core/platform/termination_signal.h"
#include "cg/core/platform/test_support.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <unistd.h>

using cg::core::platform::isTerminal;
using cg::core::platform::TerminationSignal;

namespace {

// True when the token is stopped within `limit` (a stop_callback releases the semaphore).
bool stoppedWithin(const std::stop_token& token, std::chrono::milliseconds limit) {
    std::binary_semaphore stopped(0);
    const std::stop_callback callback(token, [&stopped] { stopped.release(); });
    return stopped.try_acquire_for(limit);
}

} // namespace

TEST(TerminationSignalTest, SigintStopsTokenWithin1sAndExitCode130) {
    TerminationSignal signals;
    EXPECT_FALSE(signals.token().stop_requested());
    EXPECT_EQ(signals.exitCode(), 0);
    ::raise(SIGINT);
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 130);
}

TEST(TerminationSignalTest, SigtermStopsTokenWithin1sAndExitCode143) {
    TerminationSignal signals;
    ::raise(SIGTERM);
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 143);
}

TEST(TerminationSignalTest, DestructorWakesTheWatcherAtOnce) {
    // The watcher polls every 100 ms; a destructor that waited for the poll would take >= 80 ms after the
    // 20 ms settle delay (which lets the watcher enter its wait: a watcher not yet started exits at once).
    std::chrono::steady_clock::time_point start;
    {
        TerminationSignal signals;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        start = std::chrono::steady_clock::now();
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(70));
}

TEST(TerminationSignalTest, AfterDestructionANewObjectStartsWithoutASignal) {
    {
        TerminationSignal first;
        ::raise(SIGINT);
        ASSERT_TRUE(stoppedWithin(first.token(), std::chrono::milliseconds(1000)));
    }
    TerminationSignal second;
    EXPECT_FALSE(second.token().stop_requested());
    EXPECT_EQ(second.exitCode(), 0);
}

// T-0335: the Windows console events (the handler itself needs a console; its mapping is pinned here). The
// event numbers are the winbase.h values: CTRL_C 0, CTRL_BREAK 1, CTRL_CLOSE 2, CTRL_LOGOFF 5,
// CTRL_SHUTDOWN 6.
TEST(TerminationSignalTest, ConsoleEventsMapToSigintAndSigterm) {
    using cg::core::platform::signalForConsoleEvent;
    EXPECT_EQ(signalForConsoleEvent(0), SIGINT);
    EXPECT_EQ(signalForConsoleEvent(1), SIGINT);
    EXPECT_EQ(signalForConsoleEvent(2), SIGTERM);
    EXPECT_EQ(signalForConsoleEvent(5), SIGTERM);
    EXPECT_EQ(signalForConsoleEvent(6), SIGTERM);
    EXPECT_EQ(signalForConsoleEvent(99), 0);
}

// std::signal returns the handler it replaces, on both systems (T-0335: this replaced a sigaction-based
// version that could not compile on Windows); the object restores what it found.
TEST(TerminationSignalTest, DestructorRestoresTheOldHandlers) {
    const auto before = std::signal(SIGINT, SIG_DFL);
    std::signal(SIGINT, before);
    {
        TerminationSignal signals;
        const auto during = std::signal(SIGINT, SIG_DFL);
        EXPECT_NE(during, before);
        std::signal(SIGINT, during);
    }
    EXPECT_EQ(std::signal(SIGINT, SIG_DFL), before);
    EXPECT_EQ(std::signal(SIGTERM, SIG_DFL), SIG_DFL);
}

TEST(TerminationSignalTest, SecondLiveObjectThrows) {
    TerminationSignal first;
    EXPECT_THROW(TerminationSignal second, std::logic_error);
    // the failed constructor must not disturb the first object
    ::raise(SIGINT);
    EXPECT_TRUE(stoppedWithin(first.token(), std::chrono::milliseconds(1000)));
}

TEST(TerminationSignalDeathTest, SecondSigintEndsTheProcessWithCode130) {
    EXPECT_EXIT(
        {
            TerminationSignal signals;
            ::raise(SIGINT);
            std::this_thread::sleep_for(std::chrono::milliseconds(150)); // a deliberate second Ctrl-C
            ::raise(SIGINT);
            std::_Exit(0); // reached only if the second signal did not end the process
        },
        ::testing::ExitedWithCode(130), "");
}

TEST(TerminationSignalDeathTest, SigintThenSigtermEndsTheProcessWithCode143) {
    EXPECT_EXIT(
        {
            TerminationSignal signals;
            ::raise(SIGINT);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            ::raise(SIGTERM);
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(143), "");
}

// T-0262 (G-8): `timeout -s INT` delivers the signal twice within microseconds (child, then process group); a
// download so interrupted exited 130 at once, without the session-saved lines. Fails on the code before
// T-0262.
TEST(TerminationSignalTest, TheSameSignalDeliveredTwiceAtOnceIsOneInterrupt) {
    TerminationSignal signals;
    ::raise(SIGINT);
    ::raise(SIGINT);
    ::raise(SIGINT);
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 130); // reached: the process was not ended
}

TEST(TerminalTest, TmpfileIsNotATerminal) {
    std::FILE* file = std::tmpfile();
    ASSERT_NE(file, nullptr);
    EXPECT_FALSE(isTerminal(file));
    std::fclose(file);
}

// T-0335: a stream made ready for output writes "\n" as one byte (Windows text mode would write CR LF). A
// file opened in text mode, not tmpfile (which is binary there), so the test can fail.
TEST(TerminalTest, PreparedOutputStreamWritesLineFeedsUnchanged) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("out.txt"));
    std::FILE* file = std::fopen(path.toLocal8Bit().constData(), "w");
    ASSERT_NE(file, nullptr);
    cg::core::platform::prepareOutputStream(file);
    std::fputs("a\nb\n", file);
    std::fclose(file);
    QFile written(path);
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    EXPECT_EQ(written.readAll(), QByteArrayLiteral("a\nb\n"));
}

// ---- T-0332: --stop-on-stdin (new code; the contract of the ticket) ----

namespace {

using cg::core::platform::StdinWatch;

// A pipe whose write end the test controls. Closing the write end gives EOF to the reader, which then ends
// (the reader never closes its descriptor). The read end is deliberately NOT closed: the reader is detached,
// so the test cannot know that its last read() is over, and ThreadSanitizer reports close() against it as a
// race (and a closed number could be reused under a late reader). Twenty-one descriptors leak per process.
class Pipe {
public:
    Pipe() { EXPECT_EQ(cg::core::platform::makePipe(fds_), 0); }
    ~Pipe() { closeWrite(); }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    int readFd() const { return fds_[0]; }
    void write(const std::string& text) const {
        EXPECT_EQ(::write(fds_[1], text.data(), static_cast<unsigned>(text.size())),
                  static_cast<long long>(text.size()));
    }
    void closeWrite() {
        if (fds_[1] >= 0) {
            ::close(fds_[1]);
            fds_[1] = -1;
        }
    }

private:
    int fds_[2]{-1, -1};
};

constexpr auto kQuiet = std::chrono::milliseconds(300);

} // namespace

TEST(StdinStopTest, StopLineStopsWithExitCode143) {
    Pipe pipe;
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    EXPECT_FALSE(stoppedWithin(signals.token(), std::chrono::milliseconds(150))); // nothing sent yet
    pipe.write("stop\n");
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 143);
    EXPECT_TRUE(signals.stoppedByStdin());
}

TEST(StdinStopTest, StopWithCarriageReturnStops) {
    Pipe pipe;
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    pipe.write("stop\r\n");
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 143);
}

TEST(StdinStopTest, EofStops) {
    Pipe pipe;
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    pipe.closeWrite();
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 143);
}

TEST(StdinStopTest, EofBeforeTheReaderStartsStops) {
    Pipe pipe;
    pipe.write("junk\n");
    pipe.closeWrite();
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 143);
}

TEST(StdinStopTest, JunkLinesAreIgnoredThenStopStops) {
    Pipe pipe;
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    // lines that are not exactly "stop": a prefix, a suffix, other case, spaces, a stop inside a longer line,
    // a very long line, an empty line
    pipe.write("stopp\nSTOP\n stop\nstop \nhello stop\n\n" + std::string(500, 'x') + "stop\n");
    EXPECT_FALSE(stoppedWithin(signals.token(), kQuiet));
    EXPECT_EQ(signals.exitCode(), 0);
    pipe.write("sto");
    EXPECT_FALSE(stoppedWithin(signals.token(), std::chrono::milliseconds(150))); // a line is not complete
    pipe.write("p\n"); // the line arrives in two reads
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    EXPECT_EQ(signals.exitCode(), 143);
}

TEST(StdinStopTest, AStopLineAfterAVeryLongLineStillCounts) {
    Pipe pipe;
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    pipe.write(std::string(1000, 'y') + "\nstop\n");
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
}

TEST(StdinStopTest, NotAPipeIsRefusedAndNothingStops) {
    // a regular file: a valid descriptor that is not a pipe (T-0335: portable, unlike /dev/null or NUL)
    std::FILE* regular = std::tmpfile();
    ASSERT_NE(regular, nullptr);
    const int regularFd = ::fileno(regular);
    ASSERT_GE(regularFd, 0);
    TerminationSignal signals;
    EXPECT_EQ(signals.watchStdin(regularFd), StdinWatch::NotPipe);
    EXPECT_FALSE(stoppedWithin(signals.token(),
                               kQuiet)); // EOF if read, but no reader started
    std::fclose(regular);
}

TEST(StdinStopTest, AnInvalidDescriptorIsReportedAndNothingStops) {
    TerminationSignal signals;
    EXPECT_EQ(signals.watchStdin(-1), StdinWatch::Invalid);
    EXPECT_FALSE(stoppedWithin(signals.token(), kQuiet));
}

TEST(StdinStopTest, WithoutWatchStdinTheInputIsIgnored) {
    Pipe pipe;
    TerminationSignal signals;
    pipe.write("stop\n");
    pipe.closeWrite();
    EXPECT_FALSE(stoppedWithin(signals.token(), kQuiet));
    EXPECT_EQ(signals.exitCode(), 0);
}

TEST(StdinStopTest, SignalsStillWorkAndASignalBeforeTheStopKeepsItsCode) {
    Pipe pipe;
    TerminationSignal signals;
    ASSERT_EQ(signals.watchStdin(pipe.readFd()), StdinWatch::Started);
    ::raise(SIGINT);
    EXPECT_TRUE(stoppedWithin(signals.token(), std::chrono::milliseconds(1000)));
    pipe.write("stop\n");
    std::this_thread::sleep_for(kQuiet);
    EXPECT_EQ(signals.exitCode(), 130);
    EXPECT_FALSE(signals.stoppedByStdin());
}

TEST(StdinStopTest, ALateReaderOfADestroyedObjectStopsNothingInTheNextOne) {
    Pipe pipe;
    {
        TerminationSignal first;
        ASSERT_EQ(first.watchStdin(pipe.readFd()), StdinWatch::Started);
    }
    TerminationSignal second; // its constructor resets the flag, so it must exist before the late stop line
    pipe.write("stop\n");     // the reader of the first object is still blocked in read
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(stoppedWithin(second.token(), kQuiet));
    EXPECT_EQ(second.exitCode(), 0);
}
