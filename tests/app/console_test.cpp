// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/console (new code; inherits frm_ProgressDownload.pas UpdateTimerTimer :431-437 and
// UpdateMemoProgressForm :381-393). Terminal path: the forceTerminal seam (no pseudo-terminal).
#include "cg/app/console.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdio>
#include <thread>

using cg::app::Console;

namespace {

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

    QByteArray text() const {
        std::fflush(file_);
        std::rewind(file_);
        QByteArray all;
        char buffer[256];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof buffer, file_)) > 0) {
            all.append(buffer, static_cast<qsizetype>(n));
        }
        std::fseek(file_, 0, SEEK_END);
        return all;
    }

private:
    std::FILE* file_;
};

} // namespace

TEST(ConsoleTest, ResultGoesToOutOnlyAndMessagesToErrWithPrefixes) {
    Stream out;
    Stream err;
    Console console(0, false, out.get(), err.get());
    console.result(QStringLiteral("R1"));
    console.warning(QStringLiteral("W1"));
    console.error(QStringLiteral("E1"));
    console.log(QStringLiteral("L1"));
    EXPECT_EQ(out.text(), QByteArray("R1\n"));
    EXPECT_EQ(err.text(), QByteArray("warning: W1\nerror: E1\n"));
}

// T-0335: the streams are text-mode files (what stdout/stderr are on Windows): the Console must make them
// binary, so
// "\n" stays one byte (the machine-readable output is a contract). tmpfile() is binary on Windows and could
// not fail here.
TEST(ConsoleTest, LineFeedsReachTextModeFilesUnchanged) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString outPath = dir.filePath(QStringLiteral("out.txt"));
    const QString errPath = dir.filePath(QStringLiteral("err.txt"));
    std::FILE* out = std::fopen(outPath.toLocal8Bit().constData(), "w");
    std::FILE* err = std::fopen(errPath.toLocal8Bit().constData(), "w");
    ASSERT_NE(out, nullptr);
    ASSERT_NE(err, nullptr);
    {
        Console console(0, false, out, err);
        console.result(QStringLiteral("R1"));
        console.warning(QStringLiteral("W1"));
    }
    std::fclose(out);
    std::fclose(err);
    QFile outFile(outPath);
    QFile errFile(errPath);
    ASSERT_TRUE(outFile.open(QIODevice::ReadOnly));
    ASSERT_TRUE(errFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(outFile.readAll(), QByteArray("R1\n"));
    EXPECT_EQ(errFile.readAll(), QByteArray("warning: W1\n"));
}

TEST(ConsoleTest, QuietDropsWarningKeepsErrorAndResult) {
    Stream out;
    Stream err;
    Console console(-1, false, out.get(), err.get());
    console.warning(QStringLiteral("W1"));
    console.error(QStringLiteral("E1"));
    console.result(QStringLiteral("R1"));
    EXPECT_EQ(out.text(), QByteArray("R1\n"));
    EXPECT_EQ(err.text(), QByteArray("error: E1\n"));
}

TEST(ConsoleTest, LogOnlyWithVerbosityOne) {
    Stream out0;
    Stream err0;
    Console quiet(0, false, out0.get(), err0.get());
    quiet.log(QStringLiteral("L1"));
    EXPECT_EQ(err0.text(), QByteArray());

    Stream out1;
    Stream err1;
    Console verbose(1, false, out1.get(), err1.get());
    verbose.log(QStringLiteral("L1"));
    EXPECT_EQ(err1.text(), QByteArray("L1\n"));
    EXPECT_EQ(out1.text(), QByteArray());
}

TEST(ConsoleTest, MessagesAreUtf8) {
    Stream out;
    Stream err;
    Console console(0, false, out.get(), err.get());
    console.result(QString::fromUtf8("Город"));
    EXPECT_EQ(out.text(), QByteArray("\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4\n"));
}

TEST(ConsoleTest, ProgressOffPrintsNothing) {
    Stream out;
    Stream err;
    Console console(0, false, out.get(), err.get(), [] { return qint64(0); });
    console.progress(QStringLiteral("P1"));
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray());
}

// T-0217 item 4: -q is "errors and results only" (app-cli.md 1.2): no progress line, on a terminal or not.
TEST(ConsoleTest, QuietPrintsNoProgressLine) {
    Stream out;
    Stream err;
    Console plain(-1, true, out.get(), err.get(), [] { return qint64(0); });
    plain.progress(QStringLiteral("P1"));
    Console terminal(-1, true, out.get(), err.get(), [] { return qint64(0); }, true);
    terminal.progress(QStringLiteral("P2"));
    terminal.endProgress();
    EXPECT_EQ(err.text(), QByteArray());
}

TEST(ConsoleTest, NonTerminalProgressPrintsFirstThenOnePer30Seconds) {
    Stream out;
    Stream err;
    qint64 now = 1000;
    Console console(0, true, out.get(), err.get(), [&now] { return now; });
    console.progress(QStringLiteral("P1"));
    now = 1000 + 29999;
    console.progress(QStringLiteral("P2"));
    EXPECT_EQ(err.text(), QByteArray("P1\n"));
    now = 1000 + 30000;
    console.progress(QStringLiteral("P3"));
    now = 1000 + 30000 + 29999;
    console.progress(QStringLiteral("P4"));
    EXPECT_EQ(err.text(), QByteArray("P1\nP3\n"));
    console.endProgress(); // not a terminal: no newline; the dropped P4 is the last text of the run (T-0266)
    EXPECT_EQ(err.text(), QByteArray("P1\nP3\nP4\n"));
}

TEST(ConsoleTest, NonTerminalFirstLineIsPrintedAtClockZero) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), [] { return qint64(0); });
    console.progress(QStringLiteral("P1"));
    EXPECT_EQ(err.text(), QByteArray("P1\n"));
}

TEST(ConsoleTest, TerminalShorterLineIsPaddedToThePreviousLength) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), {}, true);
    console.progress(QStringLiteral("abcdef"));
    console.progress(QStringLiteral("xy"));
    EXPECT_EQ(err.text(), QByteArray("\rabcdef\rxy    "));
}

TEST(ConsoleTest, TerminalLongerLineIsNotPadded) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), {}, true);
    console.progress(QStringLiteral("ab"));
    console.progress(QStringLiteral("abcd"));
    EXPECT_EQ(err.text(), QByteArray("\rab\rabcd"));
}

TEST(ConsoleTest, TerminalWarningDuringProgressStartsOnANewLine) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), {}, true);
    console.progress(QStringLiteral("abc"));
    console.warning(QStringLiteral("W1"));
    console.progress(QStringLiteral("de"));
    EXPECT_EQ(err.text(), QByteArray("\rabc\nwarning: W1\n\rde"));
}

TEST(ConsoleTest, TerminalResultOnOutEndsProgressLineOnErr) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), {}, true);
    console.progress(QStringLiteral("abc"));
    console.result(QStringLiteral("R1"));
    EXPECT_EQ(err.text(), QByteArray("\rabc\n"));
    EXPECT_EQ(out.text(), QByteArray("R1\n"));
}

TEST(ConsoleTest, TerminalEndProgressPrintsExactlyOneNewline) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), {}, true);
    console.endProgress(); // nothing showing
    EXPECT_EQ(err.text(), QByteArray());
    console.progress(QStringLiteral("abc"));
    console.endProgress();
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray("\rabc\n"));
}

TEST(ConsoleTest, TerminalNewLineAfterEndProgressIsNotPaddedByTheOldOne) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), {}, true);
    console.progress(QStringLiteral("abcdef"));
    console.endProgress();
    console.progress(QStringLiteral("x"));
    EXPECT_EQ(err.text(), QByteArray("\rabcdef\n\rx"));
}

TEST(ConsoleTest, TerminalProgressOffStaysSilentEvenIfForced) {
    Stream out;
    Stream err;
    Console console(0, false, out.get(), err.get(), {}, true);
    console.progress(QStringLiteral("abc"));
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray());
}

// T-0203: with the records on, progress() never prints (terminal or not); a record is one whole line on err.
TEST(ConsoleTest, ProgressRecordsSwitchTheHumanProgressLineOff) {
    for (const bool terminal : {true, false}) {
        Stream out;
        Stream err;
        qint64 now = 0;
        Console console(0, true, out.get(), err.get(), [&now] { return now; }, terminal);
        console.setProgressRecords(true);
        console.progress(QStringLiteral("human"));
        now = 100000;
        console.progress(QStringLiteral("human"));
        console.progressRecord(QStringLiteral("progress\texport\t{G}\t5"));
        console.endProgress();
        EXPECT_EQ(err.text(), QByteArray("progress\texport\t{G}\t5\n")) << terminal;
        EXPECT_EQ(out.text(), QByteArray());
    }
}

TEST(ConsoleTest, ProgressRecordEndsAShowingProgressLineFirstAndIgnoresVerbosity) {
    {
        Stream out;
        Stream err;
        Console console(0, true, out.get(), err.get(), {}, true);
        console.progress(QStringLiteral("abc"));
        console.progressRecord(QStringLiteral("phase\tfill\t{G}"));
        EXPECT_EQ(err.text(), QByteArray("\rabc\nphase\tfill\t{G}\n"));
    }
    { // -q hides the human line (T-0217) but not a record
        Stream out;
        Stream err;
        Console console(-1, true, out.get(), err.get(), {}, true);
        console.progress(QStringLiteral("abc"));
        console.progressRecord(QStringLiteral("phase\tfill\t{G}"));
        EXPECT_EQ(err.text(), QByteArray("phase\tfill\t{G}\n"));
    }
}

// T-0203 proof 4: two threads, records and warnings into one FILE*: every line is whole.
TEST(ConsoleTest, RecordsAndWarningsFromTwoThreadsNeverInterleaveInsideALine) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get());
    console.setProgressRecords(true);
    constexpr int kCount = 2000;
    const QString padding(200, QLatin1Char('x'));
    {
        std::jthread records([&] {
            for (int i = 0; i < kCount; ++i) {
                console.progressRecord(QStringLiteral("progress\tdownload\t{G}\t%1\t%2").arg(i).arg(padding));
            }
        });
        std::jthread warnings([&] {
            for (int i = 0; i < kCount; ++i) {
                console.warning(QStringLiteral("w%1 %2").arg(i).arg(padding));
            }
        });
    }
    const QList<QByteArray> lines = err.text().split('\n');
    ASSERT_EQ(lines.size(), 2 * kCount + 1);
    int nRecords = 0;
    int nWarnings = 0;
    for (const QByteArray& line : lines) {
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith("progress\tdownload\t{G}\t")) {
            ++nRecords;
            EXPECT_TRUE(line.endsWith(padding.toUtf8())) << line.left(40).constData();
            EXPECT_EQ(line.count('\t'), 4);
        } else if (line.startsWith("warning: w")) {
            ++nWarnings;
            EXPECT_TRUE(line.endsWith(padding.toUtf8())) << line.left(40).constData();
        } else {
            ADD_FAILURE() << line.left(60).constData();
        }
    }
    EXPECT_EQ(nRecords, kCount);
    EXPECT_EQ(nWarnings, kCount);
}

// T-0266: a non-terminal run that ends inside the 30 s window still shows the last text it was given.
TEST(ConsoleTest, NonTerminalEndProgressPrintsTheDroppedLastLine) {
    Stream out;
    Stream err;
    qint64 now = 0;
    Console console(0, true, out.get(), err.get(), [&now] { return now; });
    console.progress(QStringLiteral("A"));
    now = 1000;
    console.progress(QStringLiteral("B"));
    now = 2000;
    console.progress(QStringLiteral("C"));
    EXPECT_EQ(err.text(), QByteArray("A\n"));
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray("A\nC\n"));
}

// T-0266: nothing is printed twice; a second endProgress after the final line adds nothing.
TEST(ConsoleTest, NonTerminalEndProgressPrintsNothingWhenLastLineWasPrinted) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), [] { return qint64(0); });
    console.progress(QStringLiteral("A"));
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray("A\n"));
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray("A\n"));

    // A dropped line is superseded when a later one is printed: the final line is the printed C, not B.
    Stream out2;
    Stream err2;
    qint64 now = 0;
    Console later(0, true, out2.get(), err2.get(), [&now] { return now; });
    later.progress(QStringLiteral("A"));
    now = 1000;
    later.progress(QStringLiteral("B"));
    now = 30000;
    later.progress(QStringLiteral("C"));
    later.endProgress();
    EXPECT_EQ(err2.text(), QByteArray("A\nC\n"));
}

// T-0266: -q (verbosity -1) and --progress=lines (records) print no progress text, so no final line either.
TEST(ConsoleTest, NonTerminalFinalLineRespectsQuietAndRecords) {
    Stream outQuiet;
    Stream errQuiet;
    Console quiet(-1, true, outQuiet.get(), errQuiet.get(), [] { return qint64(0); });
    quiet.progress(QStringLiteral("A"));
    quiet.progress(QStringLiteral("B"));
    quiet.endProgress();
    EXPECT_EQ(errQuiet.text(), QByteArray());

    Stream outRecords;
    Stream errRecords;
    Console records(0, true, outRecords.get(), errRecords.get(), [] { return qint64(0); });
    records.setProgressRecords(true);
    records.progress(QStringLiteral("A"));
    records.progress(QStringLiteral("B"));
    records.endProgress();
    EXPECT_EQ(errRecords.text(), QByteArray());
}

// T-0266: a terminal keeps its rewrite-in-place behaviour; the final endProgress only ends the line.
TEST(ConsoleTest, TerminalEndProgressUnchanged) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), [] { return qint64(0); }, true);
    console.progress(QStringLiteral("A"));
    console.progress(QStringLiteral("B"));
    console.endProgress();
    EXPECT_EQ(err.text(), QByteArray("\rA\rB\n"));
}

// T-0267: endProgress never repeats the text printed last, even when the throttle stored it as pending.
TEST(ConsoleTest, NonTerminalEndProgressDoesNotRepeatThePrintedLine) {
    Stream out;
    Stream err;
    Console console(0, true, out.get(), err.get(), [] { return qint64(0); });
    console.progress(QStringLiteral("Б"));
    console.progress(QStringLiteral("Б"));
    console.endProgress();
    EXPECT_EQ(err.text(), QString(QStringLiteral("Б\n")).toUtf8());

    Stream out2;
    Stream err2;
    Console other(0, true, out2.get(), err2.get(), [] { return qint64(0); });
    other.progress(QStringLiteral("Б"));
    other.progress(QStringLiteral("В"));
    other.progress(QStringLiteral("Б"));
    other.endProgress();
    EXPECT_EQ(err2.text(), QString(QStringLiteral("Б\n")).toUtf8());
}
