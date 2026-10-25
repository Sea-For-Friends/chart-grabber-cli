// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/cli_main (new code; docs/specs/app-cli.md 1.1, 1.7, 1.10, 1.11). T-0193.
// No FEAT tag: FEAT-NET-002 / -003 are proven in cli_end_to_end_test.cpp.
//
// New code, so the expectations are the literals of the spec and the ticket (T-0193 proofs 3, 4, 5), not
// Pascal results. The usage paths, `sources` and the exit codes that need no network. No server: the ZMPs
// point at a port nobody listens on, and none of these commands sends a request.
#include "cg/app/app_context.h"
#include "cg/app/job.h"
#include "cg/app/job_area.h"
#include "cg/app/job_checks.h"
#include "cg/app/tile_estimate.h"
#include "cg/geo/projection_set.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <vector>

#include "../maptype/maptype_test_util.h"
#include "cli_test_util.h"

using namespace cg::app;
using namespace cg::app::test;
using namespace Qt::StringLiterals;

namespace {

const QString kBboxArgs = QStringLiteral("10,30,100,60");

QStringList lines(const QString& text) {
    return text.split(u'\n', Qt::SkipEmptyParts);
}

class CliMainTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        cg::maptype::test::ensureGuiApp();
        root_ = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(root_->isValid());
        ASSERT_TRUE(QDir().mkpath(maps()));
        writeZmpSet(maps(), 9);
    }
    static void TearDownTestSuite() { root_.reset(); }

    static QString maps() { return root_->filePath(QStringLiteral("Maps")); }

    // arguments after the command, with the directories of this suite
    static QStringList common() {
        return {QStringLiteral("--maps"),
                maps(),
                QStringLiteral("--cache"),
                root_->filePath(QStringLiteral("cache")),
                QStringLiteral("--datadir=") + root_->filePath(u"data"_s),
                QStringLiteral("--no-progress")};
    }

    static QStringList estimateArgs(const QString& source) {
        return QStringList{QStringLiteral("estimate"),
                           QStringLiteral("--source"),
                           source,
                           QStringLiteral("--bbox"),
                           kBboxArgs,
                           QStringLiteral("--zooms"),
                           QStringLiteral("1-3")} +
               common();
    }

    static inline std::unique_ptr<QTemporaryDir> root_;
};

// Proof 4: no arguments -> exit 2, the usage line on stderr, nothing on stdout.
TEST_F(CliMainTest, NoArgumentsIsUsageErrorOnStderrOnly) {
    const Captured c = runCapture({});
    EXPECT_EQ(c.rc, 2);
    EXPECT_EQ(c.out, QString());
    EXPECT_TRUE(c.err.startsWith(u"error: "_s)) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"Usage: chart-grabber-cli COMMAND [options]; chart-grabber-cli --help"_s))
        << qPrintable(c.err);
}

// Proof 5 (code 2): every parse failure is exit 2 with `error:` on stderr and nothing on stdout.
TEST_F(CliMainTest, UsageErrorsAreExitTwo) {
    const std::vector<QStringList> cases = {
        {u"--bogus"_s},
        {u"frobnicate"_s},
        {u"estimate"_s, u"--bbox"_s, u"1,2,3"_s},
        {u"estimate"_s, u"--source"_s, u"x"_s, u"--bbox"_s, u"1,2,3,4"_s, u"--zooms"_s, u"9-3"_s},
    };
    for (const QStringList& args : cases) {
        const Captured c = runCapture(args);
        EXPECT_EQ(c.rc, 2) << qPrintable(args.join(u' '));
        EXPECT_EQ(c.out, QString()) << qPrintable(args.join(u' '));
        EXPECT_TRUE(c.err.startsWith(u"error: "_s)) << qPrintable(c.err);
    }
}

// Proof 4: --help / -h -> stdout, exit 0, no error.
TEST_F(CliMainTest, HelpGoesToStdoutWithExitZero) {
    for (const QString& flag : {u"--help"_s, u"-h"_s}) {
        const Captured c = runCapture({flag});
        EXPECT_EQ(c.rc, 0) << qPrintable(flag);
        EXPECT_TRUE(c.out.contains(u"--corridor-nm"_s)) << qPrintable(flag);
        EXPECT_TRUE(c.out.contains(u"--stall-timeout"_s)) << qPrintable(flag);
        EXPECT_EQ(c.err, QString()) << qPrintable(flag);
    }
}

TEST_F(CliMainTest, VersionPrintsProgramNameAndApplicationVersion) {
    QCoreApplication::setApplicationVersion(u"9.8.7"_s);
    const Captured c = runCapture({u"--version"_s});
    EXPECT_EQ(c.rc, 0);
    EXPECT_EQ(c.out, u"chart-grabber-cli 9.8.7\n"_s);
    EXPECT_EQ(c.err, QString());
}

// Proof 3: `sources` lists every map sorted by name (the ZMP folders were written in another order) with
// the verdict `ok`, or the reason of refusal for the WebP map. Stdout only; exit 0.
TEST_F(CliMainTest, SourcesListsEveryMapSortedByNameWithItsVerdict) {
    const Captured c = runCapture(QStringList{u"sources"_s} + common());
    EXPECT_EQ(c.rc, 0);
    EXPECT_EQ(c.err, QString());
    const QStringList rows = lines(c.out);
    ASSERT_EQ(rows.size(), 5) << qPrintable(c.out); // header + 4
    const QStringList simple = [&] {
        QStringList s;
        for (const QString& row : rows) {
            s.push_back(row.simplified());
        }
        return s;
    }();
    EXPECT_TRUE(simple[0].startsWith(u"GUID kind name"_s)) << qPrintable(simple[0]);
    const QString webpReason =
        u"WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export "
        "them anyway"_s;
    // GUID, map / layer, name, content type, EPSG, cache type, verdict
    EXPECT_TRUE(QRegularExpression(
                    u"^\\{AB0000A1-0000-0000-0000-0000000000A1\\} map Alt Path Map image/png 3857 \\d+ ok$"_s)
                    .match(simple[1])
                    .hasMatch())
        << qPrintable(simple[1]);
    EXPECT_TRUE(
        QRegularExpression(
            u"^\\{AB0000D4-0000-0000-0000-0000000000D4\\} layer Scripted Layer image/png 3857 \\d+ ok$"_s)
            .match(simple[2])
            .hasMatch())
        << qPrintable(simple[2]);
    EXPECT_TRUE(QRegularExpression(
                    u"^\\{AB0000C3-0000-0000-0000-0000000000C3\\} map Template Map image/png 3857 \\d+ ok$"_s)
                    .match(simple[3])
                    .hasMatch())
        << qPrintable(simple[3]);
    EXPECT_TRUE(simple[4].startsWith(u"{AB0000B2-0000-0000-0000-0000000000B2} map WebP Map "_s))
        << qPrintable(simple[4]);
    EXPECT_TRUE(simple[4].contains(u" 3857 "_s)) << qPrintable(simple[4]);
    EXPECT_TRUE(simple[4].endsWith(webpReason)) << qPrintable(simple[4]);
}

// --filter: a case-insensitive substring of the name, the file name or the GUID.
TEST_F(CliMainTest, SourcesFilterMatchesNameFileNameAndGuid) {
    struct Row {
        QString filter;
        QString guidInRow; // the only row expected
    };
    const Row table[] = {
        {u"tEMPLATE"_s, QString::fromLatin1(kGuidTemplate)},
        {u"w2"_s, QString::fromLatin1(kGuidWebp)},
        {u"ab0000d4"_s, QString::fromLatin1(kGuidScripted)},
    };
    for (const Row& row : table) {
        const Captured c = runCapture(QStringList{u"sources"_s, u"--filter"_s, row.filter} + common());
        EXPECT_EQ(c.rc, 0) << qPrintable(row.filter);
        const QStringList rows = lines(c.out);
        ASSERT_EQ(rows.size(), 2) << qPrintable(row.filter) << "\n" << qPrintable(c.out);
        EXPECT_TRUE(rows[1].startsWith(row.guidInRow)) << qPrintable(rows[1]);
    }
}

// Proof 5 (code 3): S-1, the maps directory is empty or missing.
TEST_F(CliMainTest, NoMapsAtAllIsExitThree) {
    QTemporaryDir empty;
    ASSERT_TRUE(empty.isValid());
    for (const QString& dir : {empty.path(), empty.filePath(u"missing"_s)}) {
        const Captured c =
            runCapture({u"sources"_s, u"--maps"_s, dir, u"--datadir="_s + empty.filePath(u"d"_s)});
        EXPECT_EQ(c.rc, 3) << qPrintable(dir);
        EXPECT_EQ(c.out, QString());
        EXPECT_TRUE(c.err.startsWith(u"error: "_s)) << qPrintable(c.err);
    }
}

// Proof 5 (code 3): S-2 (unknown source) and S-6 (WebP without --allow-webp); with --allow-webp the same job
// passes the checks and `estimate` ends with exit 0.
TEST_F(CliMainTest, RefusedJobsAreExitThreeAndAllowWebpLiftsTheWebpRefusal) {
    const Captured unknown = runCapture(estimateArgs(u"nosuch"_s));
    EXPECT_EQ(unknown.rc, 3);
    EXPECT_EQ(unknown.out, QString());
    EXPECT_EQ(unknown.err, u"error: no map source matches \"nosuch\"; run \"chart-grabber-cli sources\"\n"_s);

    const Captured webp = runCapture(estimateArgs(u"WebP Map"_s));
    EXPECT_EQ(webp.rc, 3);
    EXPECT_TRUE(webp.err.contains(u"pass --allow-webp to export them anyway"_s)) << qPrintable(webp.err);

    const Captured allowed = runCapture(estimateArgs(u"WebP Map"_s) + QStringList{u"--allow-webp"_s});
    EXPECT_EQ(allowed.rc, 0) << qPrintable(allowed.err);
}

// Proof 5 (code 130): a termination that has already arrived ends the command with 130 and no table.
TEST_F(CliMainTest, TerminationBeforeTheEstimateIsExit130) {
    std::stop_source stop;
    stop.request_stop();
    const Captured c = runCapture(estimateArgs(u"Template Map"_s), stop.get_token());
    EXPECT_EQ(c.rc, 130);
    EXPECT_FALSE(c.out.contains(u"total"_s)) << qPrintable(c.out);
}

// ---- T-0332: --stop-on-stdin ----

// stdin that is not a pipe: exit 2, one error line and the usage line, nothing runs
TEST_F(CliMainTest, StopOnStdinWithStdinNotAPipeIsExitTwoWithTheUsageLine) {
    int calls = 0;
    const Captured c =
        runCapture(estimateArgs(u"Template Map"_s) + QStringList{u"--stop-on-stdin"_s}, {}, [&calls] {
            ++calls;
            return cg::core::platform::StdinWatch::NotPipe;
        });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(c.rc, 2);
    EXPECT_TRUE(c.err.contains(u"--stop-on-stdin needs stdin to be a pipe"_s)) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"chart-grabber-cli --help"_s)) << qPrintable(c.err);
    EXPECT_FALSE(c.out.contains(u"total"_s)) << qPrintable(c.out);
}

// an invalid handle: one warning, the option is off, the command runs
TEST_F(CliMainTest, StopOnStdinWithAnInvalidHandleWarnsOnceAndRuns) {
    const Captured c = runCapture(estimateArgs(u"Template Map"_s) + QStringList{u"--stop-on-stdin"_s}, {},
                                  [] { return cg::core::platform::StdinWatch::Invalid; });
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(c.err.count(u"--stop-on-stdin disabled"_s), 1) << qPrintable(c.err);
}

// the option absent: stdin is not looked at; the option given: the reader starts once
TEST_F(CliMainTest, StopOnStdinAbsentNeverStartsTheReaderAndPresentStartsItOnce) {
    int calls = 0;
    const auto watcher = [&calls] {
        ++calls;
        return cg::core::platform::StdinWatch::Started;
    };
    EXPECT_EQ(runCapture(estimateArgs(u"Template Map"_s), {}, watcher).rc, 0);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(runCapture(estimateArgs(u"Template Map"_s) + QStringList{u"--stop-on-stdin"_s}, {}, watcher).rc,
              0);
    EXPECT_EQ(calls, 1);
}

// ---- T-0202: --format=tsv ----

// Proof 2: one `source` record per map, 9 fields after the type, no header, the table's order; the verdict
// is the text table's verdict column. Expected values are literals of the rig (cli_test_util.h).
TEST_F(CliMainTest, SourcesTsvHasOneRecordPerMapInTheTablesOrderAndNoHeader) {
    const Captured tsv = runCapture(QStringList{u"sources"_s, u"--format=tsv"_s} + common());
    EXPECT_EQ(tsv.rc, 0);
    EXPECT_EQ(tsv.err, QString());
    const QStringList records = tsv.out.split(u'\n', Qt::SkipEmptyParts);
    ASSERT_EQ(records.size(), 4) << qPrintable(tsv.out);
    const QString webpReason =
        u"WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export them anyway"_s;
    struct Row {
        QString guid, kind, name, verdict;
    };
    const Row expected[] = {
        {u"{AB0000A1-0000-0000-0000-0000000000A1}"_s, u"map"_s, u"Alt Path Map"_s, u"ok"_s},
        {u"{AB0000D4-0000-0000-0000-0000000000D4}"_s, u"layer"_s, u"Scripted Layer"_s, u"ok"_s},
        {u"{AB0000C3-0000-0000-0000-0000000000C3}"_s, u"map"_s, u"Template Map"_s, u"ok"_s},
        {u"{AB0000B2-0000-0000-0000-0000000000B2}"_s, u"map"_s, u"WebP Map"_s, webpReason},
    };
    const QString folders[] = {u"w1.zmp"_s, u"w4.zmp"_s, u"w3.zmp"_s, u"w2.zmp"_s};
    const Captured text = runCapture(QStringList{u"sources"_s} + common());
    const QStringList textRows = lines(text.out);
    ASSERT_EQ(textRows.size(), 5);
    for (int i = 0; i < 4; ++i) {
        const QStringList f = records[i].split(u'\t');
        ASSERT_EQ(f.size(), 15) << qPrintable(records[i]);
        EXPECT_EQ(f[0], u"source"_s);
        EXPECT_EQ(f[1], expected[i].guid);
        EXPECT_EQ(f[2], expected[i].kind);
        EXPECT_EQ(f[3], expected[i].name);
        EXPECT_EQ(f[4], i == 3 ? u"image/webp"_s : u"image/png"_s);
        EXPECT_EQ(f[5], u"3857"_s);
        // the cache column of the text table, the same CacheType number
        const auto cache = QRegularExpression(u"\\s3857\\s+(\\d+)\\s"_s).match(textRows[i + 1]);
        ASSERT_TRUE(cache.hasMatch()) << qPrintable(textRows[i + 1]);
        EXPECT_EQ(f[6], cache.captured(1));
        EXPECT_EQ(f[7], expected[i].verdict);
        EXPECT_EQ(f[8], folders[i]);
        EXPECT_EQ(f[9], QString());  // no parent submenu in the rig's ZMPs
        EXPECT_EQ(f[10], u"user"_s); // always user (T-0291)
        EXPECT_TRUE(textRows[i + 1].simplified().endsWith(f[7])) << qPrintable(textRows[i + 1]);
    }
}

// Proof 3: `estimate --format=tsv`: per zoom one record whose numbers are the ZoomEstimate values of
// estimateTiles for the same job; no name line, no total row.
TEST_F(CliMainTest, EstimateTsvCarriesTheEstimateTilesValuesPerZoom) {
    const Captured c = runCapture(estimateArgs(u"Template Map"_s) + QStringList{u"--format"_s, u"tsv"_s});
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList records = c.out.split(u'\n', Qt::SkipEmptyParts);

    AppOptions options;
    options.arguments = {u"--datadir="_s + root_->filePath(u"data"_s)};
    options.mapsDirs = {maps()};
    options.cacheDir = root_->filePath(u"cache"_s);
    options.warn = [](const QString&) {};
    auto app = AppContext::create(options);
    ASSERT_TRUE(app.has_value());
    ASSERT_TRUE((*app)->loadMaps().has_value());
    const auto job = parseJob(QStringList{u"sas-tiles"_s} + estimateArgs(u"Template Map"_s));
    ASSERT_TRUE(job.has_value());
    const auto sources = resolveJob(*job, **app, [](const QString&) {});
    ASSERT_TRUE(sources.has_value());
    const auto area =
        buildArea(*job, *(*app)->lonLatFactory(),
                  *sources->front().map->projectionSet()->zoom(job->zooms.back()), [](const QString&) {});
    ASSERT_TRUE(area.has_value());
    const auto estimates =
        estimateTiles(*sources->front().map, **area, job->zooms, **app, job->assumeTileKb, job->replace,
                      job->replaceOlderThan, job->retryMissing, job->retryMissingOlderThan, {});
    ASSERT_TRUE(estimates.has_value());
    ASSERT_EQ(estimates->size(), 3U);
    ASSERT_EQ(records.size(), 3) << qPrintable(c.out);
    for (qsizetype i = 0; i < 3; ++i) {
        const ZoomEstimate& e = (*estimates)[static_cast<size_t>(i)];
        const QString expected =
            u"estimate\t{AB0000C3-0000-0000-0000-0000000000C3}\t%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8"_s.arg(e.zoom)
                .arg(e.tiles)
                .arg(e.bboxTiles)
                .arg(e.sharePresent, 0, 'f', 4)
                .arg(e.shareMarkedMissing, 0, 'f', 4)
                .arg(e.bytesToDownload)
                .arg(e.assumed ? 1 : 0)
                .arg(e.bytesOnDisk);
        EXPECT_EQ(records[i], expected);
    }
    EXPECT_FALSE(c.out.contains(u"total"_s));
    EXPECT_FALSE(c.out.contains(u"Template Map"_s)); // no name line
}

// Proof 1 at the program level: a bad value is exit 2 on stderr; --help is not affected by --format.
TEST_F(CliMainTest, FormatTsvIsAcceptedByEveryCommandAndUsageErrorsStayOnStderr) {
    const Captured bad = runCapture(QStringList{u"sources"_s, u"--format=xml"_s} + common());
    EXPECT_EQ(bad.rc, 2);
    EXPECT_EQ(bad.out, QString());
    EXPECT_TRUE(bad.err.contains(u"error: --format: expected text or tsv"_s)) << qPrintable(bad.err);
    const Captured help = runCapture({u"--help"_s, u"--format=tsv"_s});
    EXPECT_EQ(help.rc, 0);
    EXPECT_TRUE(help.out.contains(u"--format=FORMAT"_s));
}

// T-0220, T-0291 (app-cli.md 1.6.1, 1.6.2). No FEAT; the D rows of the tickets.
class CliMapsFolders : public ::testing::Test {
protected:
    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
    }
    QString folderA() const { return root_.filePath(u"folderA"_s); }
    QString folderB() const { return root_.filePath(u"folderB"_s); }
    QStringList datadir() const { return {u"--datadir="_s + root_.filePath(u"data"_s), u"--no-progress"_s}; }
    // a grey and a plain source in `dir`
    void writeGreyAndPlain(const QString& dir) const {
        writeZmp(dir, {.folder = u"g.zmp"_s,
                       .guid = "{AB0000E1-0000-0000-0000-0000000000E1}",
                       .name = "Grey Source",
                       .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png",
                       .extra = "TermsOfUseWarning=  https://example.org/terms \r\n"});
        writeZmp(dir, {.folder = u"p.zmp"_s,
                       .guid = "{AB0000E2-0000-0000-0000-0000000000E2}",
                       .name = "Plain Source",
                       .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png"});
    }
    static QString greyLine() {
        return u"warning: \"Grey Source\": the provider's terms of use may not allow downloading tiles for "
               "offline use. Read https://example.org/terms before downloading; you are responsible for "
               "complying."_s;
    }
    QTemporaryDir root_;
};

// Test 10: one warning line for the grey source of `sources`, the verdict stays ok, in every format and with
// -q.
TEST_F(CliMapsFolders, SourcesWarnsOncePerGreySourceAndKeepsTheVerdictOk) {
    const QString maps = root_.filePath(u"maps"_s);
    writeGreyAndPlain(maps);
    const QStringList base = QStringList{u"sources"_s, u"--maps"_s, maps} + datadir();

    const Captured text = runCapture(base);
    EXPECT_EQ(text.rc, 0) << qPrintable(text.err);
    EXPECT_EQ(text.err, greyLine() + u"\n"_s);
    const QStringList rows = lines(text.out);
    ASSERT_EQ(rows.size(), 3) << qPrintable(text.out);
    EXPECT_TRUE(rows[1].simplified().endsWith(u" Grey Source image/png 3857 0 ok"_s)) << qPrintable(rows[1]);
    EXPECT_TRUE(rows[2].simplified().endsWith(u" Plain Source image/png 3857 0 ok"_s)) << qPrintable(rows[2]);

    const Captured quiet = runCapture(base + QStringList{u"-q"_s});
    EXPECT_EQ(quiet.err, greyLine() + u"\n"_s);
    EXPECT_EQ(quiet.out, text.out);

    const Captured tsv = runCapture(base + QStringList{u"--format=tsv"_s});
    EXPECT_EQ(tsv.err, greyLine() + u"\n"_s);
    const QStringList records = lines(tsv.out);
    ASSERT_EQ(records.size(), 2) << qPrintable(tsv.out);
    for (const QString& record : records) {
        EXPECT_EQ(record.split(u'\t').value(7), u"ok"_s) << qPrintable(record);
    }
}

// T-0285 (app-cli.md 1.10.1): `source` field 14 navigationWarning is 1 / 0; the text table and the earlier
// fields do not change; `sources` prints no notice.
TEST_F(CliMapsFolders, SourcesTsvField14CarriesTheNavigationFlagAndTheTextTableIsUnchanged) {
    const auto write = [&](const QString& dir, const QByteArray& extra) {
        writeZmp(dir, {.folder = u"a.zmp"_s,
                       .guid = "{AB0000F1-0000-0000-0000-0000000000F1}",
                       .name = "Depth A",
                       .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png",
                       .extra = extra});
        writeZmp(dir, {.folder = u"b.zmp"_s,
                       .guid = "{AB0000F2-0000-0000-0000-0000000000F2}",
                       .name = "Plain B",
                       .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png"});
    };
    write(folderA(), "NavigationWarning=1\r\n");
    write(folderB(), "");
    const QStringList format = {u"--format=tsv"_s};
    const Captured tsv = runCapture(QStringList{u"sources"_s, u"--maps"_s, folderA()} + format + datadir());
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    EXPECT_EQ(tsv.err, QString());
    const QStringList records = lines(tsv.out);
    ASSERT_EQ(records.size(), 2) << qPrintable(tsv.out);
    const QStringList a = records[0].split(u'\t');
    const QStringList b = records[1].split(u'\t');
    ASSERT_EQ(a.size(), 15) << qPrintable(records[0]);
    ASSERT_EQ(b.size(), 15) << qPrintable(records[1]);
    EXPECT_EQ(a[3], u"Depth A"_s);
    EXPECT_EQ(a[14], u"1"_s);
    EXPECT_EQ(b[3], u"Plain B"_s);
    EXPECT_EQ(b[14], u"0"_s);
    // the same definitions without the key: identical fields 1-13 and text table
    const Captured plain = runCapture(QStringList{u"sources"_s, u"--maps"_s, folderB()} + format + datadir());
    const QStringList pa = lines(plain.out)[0].split(u'\t');
    EXPECT_EQ(pa.mid(0, 14), a.mid(0, 14));
    EXPECT_EQ(pa[14], u"0"_s);
    const Captured withKey = runCapture(QStringList{u"sources"_s, u"--maps"_s, folderA()} + datadir());
    const Captured without = runCapture(QStringList{u"sources"_s, u"--maps"_s, folderB()} + datadir());
    EXPECT_EQ(withKey.out, without.out);
    EXPECT_EQ(withKey.err, QString());
    EXPECT_EQ(lines(withKey.out)[0].simplified(), u"GUID kind name content type EPSG cache verdict"_s);
}

// A --filter hides the grey source: its line is not printed (the warning is for the listed sources).
TEST_F(CliMapsFolders, SourcesFilterThatHidesTheGreySourceHidesItsWarning) {
    const QString maps = root_.filePath(u"maps"_s);
    writeGreyAndPlain(maps);
    const Captured c =
        runCapture(QStringList{u"sources"_s, u"--maps"_s, maps, u"--filter"_s, u"Plain"_s} + datadir());
    EXPECT_EQ(c.rc, 0);
    EXPECT_EQ(c.err, QString());
}

static const QString kNoMapsLine = u"error: no map sources given: pass --maps DIR, a folder of map-source "
                                   "definitions (repeat --maps for more folders)"_s;

// T-0291: every command except -h / --version is refused without --maps, before anything is created.
TEST_F(CliMapsFolders, CommandsWithoutMapsAreRefusedWithExitThreeAndCreateNothing) {
    const QString data = root_.filePath(u"d"_s);
    const QString jobFile = root_.filePath(u"job.ini"_s);
    {
        QFile f(jobFile);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("[Job]\r\nSource=Template Map\r\nBBox=10,30,100,60\r\nZooms=1-3\r\n");
    }
    const QStringList datadir = {u"--datadir="_s + data, u"--no-progress"_s};
    const std::vector<QStringList> cases = {
        QStringList{u"sources"_s} + datadir,
        QStringList{u"estimate"_s, u"--job"_s, jobFile} + datadir,
        QStringList{u"resume"_s, u"x.sls"_s} + datadir,
        QStringList{u"probe"_s} + datadir,
    };
    for (const QStringList& args : cases) {
        const Captured c = runCapture(args);
        EXPECT_EQ(c.rc, 3) << qPrintable(args.join(u' '));
        EXPECT_EQ(c.err, kNoMapsLine + u"\n"_s) << qPrintable(args.join(u' '));
        EXPECT_EQ(c.out, QString()) << qPrintable(args.join(u' '));
        EXPECT_FALSE(QFileInfo::exists(data)) << qPrintable(args.join(u' '));
    }
    for (const QString& flag : {u"--help"_s, u"--version"_s}) {
        const Captured c = runCapture({flag, u"--datadir="_s + data});
        EXPECT_EQ(c.rc, 0) << qPrintable(flag);
        EXPECT_FALSE(QFileInfo::exists(data));
    }
    // a command line with a usage error and no --maps is still exit 2
    const Captured usage = runCapture(QStringList{u"estimate"_s, u"--bbox"_s, u"1,2,3"_s} + datadir);
    EXPECT_EQ(usage.rc, 2);
}

// T-0291: a --maps path that is a file is refused, and so are all folders empty (S-1 names both).
TEST_F(CliMapsFolders, AFileAsFolderAndEmptyFoldersAreExitThree) {
    ASSERT_TRUE(QDir().mkpath(folderA()));
    ASSERT_TRUE(QDir().mkpath(folderB()));
    const Captured empty =
        runCapture(QStringList{u"sources"_s, u"--maps"_s, folderA(), u"--maps"_s, folderB()} + datadir());
    EXPECT_EQ(empty.rc, 3);
    EXPECT_EQ(empty.out, QString());
    EXPECT_TRUE(
        empty.err.startsWith(u"error: no map source loaded from %1, %2: "_s.arg(folderA(), folderB())))
        << qPrintable(empty.err);

    const QString file = root_.filePath(u"plain.txt"_s);
    QFile f(file);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    const Captured notDir = runCapture(QStringList{u"sources"_s, u"--maps"_s, file} + datadir());
    EXPECT_EQ(notDir.rc, 3);
    EXPECT_EQ(notDir.err, u"error: --maps: %1 is not a directory\n"_s.arg(file));
}

// Two folders: the table has the header of one folder (no origin column), nothing about the folders on
// stderr; field 10 of every TSV record is `user` and the field count is 14.
TEST_F(CliMapsFolders, SourcesOverTwoFoldersHasNoOriginColumnNoNoticeAndUserInField10) {
    writeZmpSet(folderA(), 9);
    writeZmp(folderB(), {.folder = u"n.zmp"_s,
                         .guid = "{AB0000E5-0000-0000-0000-0000000000E5}",
                         .name = "Second Folder Map",
                         .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png"});
    const QStringList both = {u"--maps"_s, folderA(), u"--maps"_s, folderB()};
    const Captured text = runCapture(QStringList{u"sources"_s} + both + datadir());
    EXPECT_EQ(text.rc, 0) << qPrintable(text.err);
    EXPECT_EQ(text.err, QString());
    const QStringList rows = lines(text.out);
    ASSERT_EQ(rows.size(), 6) << qPrintable(text.out); // header + 4 + 1
    EXPECT_TRUE(rows[0].simplified().startsWith(u"GUID kind name"_s));
    EXPECT_TRUE(rows[0].simplified().endsWith(u" verdict"_s)) << qPrintable(rows[0]);
    EXPECT_FALSE(text.out.contains(u"origin"_s));
    EXPECT_FALSE(text.out.contains(u"shipped"_s));

    const Captured tsv = runCapture(QStringList{u"sources"_s, u"--format=tsv"_s} + both + datadir());
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    const QStringList records = lines(tsv.out);
    ASSERT_EQ(records.size(), 5) << qPrintable(tsv.out);
    for (const QString& record : records) {
        const QStringList f = record.split(u'\t');
        EXPECT_EQ(f.size(), 15) << qPrintable(record);
        EXPECT_EQ(f.value(10), u"user"_s) << qPrintable(record);
    }
}

// A grey source in the SECOND folder still gets its terms-of-use warning, once (app-cli.md 1.6.2).
TEST_F(CliMapsFolders, GreySourceOfTheSecondFolderWarnsOnce) {
    writeZmp(folderA(), {.folder = u"p.zmp"_s,
                         .guid = "{AB0000E2-0000-0000-0000-0000000000E2}",
                         .name = "Plain Source",
                         .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png"});
    writeZmp(folderB(), {.folder = u"g.zmp"_s,
                         .guid = "{AB0000E1-0000-0000-0000-0000000000E1}",
                         .name = "Grey Source",
                         .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png",
                         .extra = "TermsOfUseWarning=  https://example.org/terms \r\n"});
    const Captured c =
        runCapture(QStringList{u"sources"_s, u"--maps"_s, folderA(), u"--maps"_s, folderB()} + datadir());
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(c.err, greyLine() + u"\n"_s);
    EXPECT_EQ(lines(c.out).size(), 3) << qPrintable(c.out);
}

// The cross-folder warning: stderr, `warning: ` prefix, one line per GUID, hidden by -q; the table lists
// the later folder's definition.
TEST_F(CliMapsFolders, CrossFolderReplacementIsOneWarningLineHiddenByQuiet) {
    const QByteArray guid = "{AB0000E1-0000-0000-0000-0000000000E1}";
    writeZmp(folderA(), {.folder = u"a.zmp"_s,
                         .guid = guid,
                         .name = "Name From A",
                         .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png"});
    writeZmp(folderB(), {.folder = u"b.zmp"_s,
                         .guid = guid,
                         .name = "Name From B",
                         .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png"});
    const QStringList args = {u"sources"_s, u"--maps"_s, folderA(), u"--maps"_s, folderB()};
    const Captured c = runCapture(args + datadir());
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(c.err, u"warning: map source %1: %2/b.zmp replaces %3/a.zmp\n"_s.arg(QString::fromLatin1(guid),
                                                                                   folderB(), folderA()));
    EXPECT_TRUE(c.out.contains(u"Name From B"_s));
    EXPECT_FALSE(c.out.contains(u"Name From A"_s));

    const Captured quiet = runCapture(args + QStringList{u"-q"_s} + datadir());
    EXPECT_EQ(quiet.rc, 0);
    EXPECT_EQ(quiet.err, QString());
    EXPECT_EQ(quiet.out, c.out);
}

} // namespace
