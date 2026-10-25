// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-NET-002 FEAT-NET-003: end-to-end tests of runCli against a local tile server (T-0193,
// docs/specs/app-cli.md 8.2). New code (no legacy unit): the expectations are the literals of the spec
// table 8.2 and of the ticket.
//
// Rig: HttpTestServer (tests/net, compiled in through http_test_server_shared.cpp) answers one distinct PNG
// per tile of a pyramid of zooms 0..3, at "/{z}/{x}/{y}.png" and again under "/alt/". Four ZMP folders
// written by cli_test_util.h point at it. The area is the rectangle lon 10..100, lat 30..60 at zooms 1-3,
// which is nine tiles (Web Mercator, y counted from the north): z1 (1,0); z2 (2,1) (3,1); z3 x 4..6 at y 2
// and y 3. The real worker, transport and export threads run; SQLite cache. Tests that start threads are
// TSan-relevant.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/platform/termination_signal.h"
#include "cg/core/platform/test_support.h"
#include "cg/geo/config_provider_helpers.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/storage/sqlite/sqlite_db.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <stop_token>
#include <thread>
#include <unistd.h>

#include "../maptype/maptype_test_util.h"
#include "../net/http_test_server.h"
#include "cli_test_util.h"

using namespace cg::app::test;
using namespace Qt::StringLiterals;
using cg::net::test::HttpTestServer;
using cg::storage::sqlite::SqliteDb;
using cg::storage::sqlite::SqliteStmt;
using namespace std::chrono_literals;

namespace {

// The nine tiles of the area, as the request paths of the template map.
QStringList areaPaths(const QString& prefix = {}) {
    QStringList paths = {u"/1/1/0.png"_s, u"/2/2/1.png"_s, u"/2/3/1.png"_s, u"/3/4/2.png"_s, u"/3/5/2.png"_s,
                         u"/3/6/2.png"_s, u"/3/4/3.png"_s, u"/3/5/3.png"_s, u"/3/6/3.png"_s};
    for (QString& path : paths) {
        path.prepend(prefix);
    }
    paths.sort();
    return paths;
}

// A small PNG that differs for every tile.
QByteArray tilePng(int z, int x, int y) {
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(qRgb(30 + z * 40, 20 + x * 30, 10 + y * 50));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString scalar(const QString& file, const QByteArray& sql) {
    auto db = SqliteDb::open(file, SqliteDb::OpenMode::ReadOnly);
    EXPECT_TRUE(db.has_value()) << qPrintable(file);
    if (!db) {
        return {};
    }
    auto stmt = db->prepare(sql);
    EXPECT_TRUE(stmt.has_value());
    if (!stmt) {
        return {};
    }
    auto step = stmt->step();
    EXPECT_TRUE(step.has_value() && *step == SqliteStmt::StepResult::Row);
    return stmt->columnText(0);
}

QByteArray blob(const QString& file, const QByteArray& sql) {
    auto db = SqliteDb::open(file, SqliteDb::OpenMode::ReadOnly);
    EXPECT_TRUE(db.has_value()) << qPrintable(file);
    if (!db) {
        return {};
    }
    auto stmt = db->prepare(sql);
    EXPECT_TRUE(stmt.has_value());
    if (!stmt) {
        return {};
    }
    auto step = stmt->step();
    EXPECT_TRUE(step.has_value() && *step == SqliteStmt::StepResult::Row);
    return stmt->columnBlob(0);
}

QString rows(const QString& file) {
    return scalar(file, "SELECT count(*) FROM tiles");
}

QStringList simplifiedLines(const QString& text) {
    QStringList result;
    for (const QString& line : text.split(u'\n', Qt::SkipEmptyParts)) {
        result.push_back(line.simplified());
    }
    return result;
}

// The "total" lines on stdout: the estimate table's first, then the fill report's.
QStringList totalLines(const QString& out) {
    QStringList result;
    for (const QString& line : simplifiedLines(out)) {
        if (line.startsWith(u"total "_s)) {
            result.push_back(line);
        }
    }
    return result;
}

class CliEndToEnd : public ::testing::Test {
protected:
    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
        ASSERT_TRUE(server_.start());
        ASSERT_TRUE(QDir().mkpath(maps()));
        ASSERT_TRUE(QDir().mkpath(outDir()));
        writeZmpSet(maps(), server_.port());
        writeIni("");
    }

    // chart-grabber.conf of the data directory: `extra` under [Internet]. The system proxy stays ON (the
    // production default, T-0201), so the downloads go through SystemProxyResolver::resolve.
    void writeIni(const QByteArray& extra) const {
        ASSERT_TRUE(QDir().mkpath(dataDir()));
        QFile ini(dataDir() + u"/chart-grabber.conf"_s);
        ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
        ini.write("[Internet]\r\n" + extra);
    }

    QString maps() const { return root_.filePath(u"Maps"_s); }
    QString outDir() const { return root_.filePath(u"out"_s); }
    QString outFile() const { return outDir() + u"/base.mbtiles"_s; }
    QString dataDir() const { return root_.filePath(u"data"_s); }

    // Serves the pyramid under "/" and "/alt/". `special` replaces the reply of a path; `delayMs` delays the
    // others.
    void servePyramid(int delayMs = 0, const QHash<QByteArray, HttpTestServer::Reply>& special = {}) {
        for (const QByteArray& prefix : {QByteArray(), QByteArray("/alt")}) {
            for (int z = 0; z <= 3; ++z) {
                for (int x = 0; x < (1 << z); ++x) {
                    for (int y = 0; y < (1 << z); ++y) {
                        const QByteArray path = prefix + '/' + QByteArray::number(z) + '/' +
                                                QByteArray::number(x) + '/' + QByteArray::number(y) + ".png";
                        if (const auto it = special.find(path); it != special.end()) {
                            server_.addReply(path, *it);
                            continue;
                        }
                        HttpTestServer::Reply reply;
                        reply.headers = {{"Content-Type", "image/png"}};
                        reply.body = tilePng(z, x, y);
                        reply.delayMs = delayMs;
                        server_.addReply(path, std::move(reply));
                    }
                }
            }
        }
    }

    static HttpTestServer::Reply status(int code, QByteArray reason) {
        HttpTestServer::Reply reply;
        reply.status = code;
        reply.reason = std::move(reason);
        reply.headers = {{"Content-Type", "text/plain"}};
        reply.body = "no";
        return reply;
    }

    QStringList common() const {
        return {
            u"--maps"_s,       maps(), u"--cache"_s, root_.filePath(u"cache"_s), u"--datadir="_s + dataDir(),
            u"--no-progress"_s};
    }

    // `command` JOB over the area, out = base.mbtiles; `more` are appended.
    QStringList job(const QString& command, const QString& source, const QStringList& more = {},
                    const QString& zooms = u"1-3"_s) const {
        return QStringList{command,      u"--source"_s, source,     u"--bbox"_s, u"10,30,100,60"_s,
                           u"--zooms"_s, zooms,         u"--out"_s, outFile()} +
               common() + more;
    }

    // T-0245: a map of the template map's URLs that stops at zoom 2 (0-based)
    void writeMaxTwo() const {
        writeZmp(maps(),
                 {.folder = u"w8.zmp"_s,
                  .guid = "{AB0000E8-0000-0000-0000-0000000000E8}",
                  .name = "Max Two Map",
                  .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
                  .extra = "MaxZoom=2\r\n"});
    }
    static QStringList withoutZoom3(const QStringList& paths) {
        return paths.filter(QRegularExpression(u"^/[12]/"_s));
    }

    // The request paths seen from index `from` on, sorted.
    QStringList requested(qsizetype from = 0) const {
        QStringList paths;
        const QList<QByteArray> log = server_.requestLog();
        for (qsizetype i = from; i < log.size(); ++i) {
            const QList<QByteArray> parts = log[i].left(log[i].indexOf("\r\n")).split(' '); // GET /p HTTP/1.1
            paths.push_back(QString::fromLatin1(parts.value(1)));
        }
        paths.sort();
        return paths;
    }
    qsizetype requestCount() const { return server_.requestCount(); }

    QStringList outFiles() const {
        QDir dir(outDir());
        QStringList names = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        names.sort();
        return names;
    }

    QStringList sessionFiles() const {
        QStringList found;
        QDirIterator it(dataDir(), {u"*.sls"_s}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            found.push_back(it.next());
        }
        return found;
    }

    // An interrupted run: the stop is requested once the server has seen `requestsToSee` requests. The server
    // has no notification when a request arrives, so the thread polls its request log, bounded by a deadline.
    Captured runInterrupted(const QStringList& args, qsizetype requestsToSee) {
        std::stop_source termination;
        Captured result;
        {
            std::jthread stopper([&] {
                const auto deadline = std::chrono::steady_clock::now() + 20s;
                while (requestCount() < requestsToSee && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(2ms);
                }
                termination.request_stop();
            });
            result = runCapture(args, termination.get_token());
        }
        return result;
    }

    // The bytes of every session file, by path (T-0278: a stopped run must leave them as they were).
    QMap<QString, QByteArray> sessionBytes() const {
        QMap<QString, QByteArray> bytes;
        for (const QString& file : sessionFiles()) {
            bytes.insert(file, readAll(file));
        }
        return bytes;
    }

    static void writeStale(const QString& path) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("stale");
    }
    static QByteArray readAll(const QString& path) {
        QFile f(path);
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return f.readAll();
    }

    QTemporaryDir root_;
    HttpTestServer server_;
};

// Spec 8.2 row 1: `run`, base map, rectangle, zooms 1-3.
TEST_F(CliEndToEnd, RunBaseMapDownloadsEachTileOnceAndExportsThem) {
    servePyramid();
    const Captured c = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requested(), areaPaths());                   // each tile of the area once, nothing else
    EXPECT_EQ(outFiles(), QStringList{u"base.mbtiles"_s}); // no .part file left
    EXPECT_EQ(rows(outFile()), u"9"_s);

    // fill report: tiles 9, present 9, marked missing 0, missing 0
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(totals[0].startsWith(u"total 9 9 "_s))
        << qPrintable(totals[0]); // estimate: tiles, bbox tiles
    EXPECT_TRUE(totals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[1]);

    // TMS rows: tile (z3, x4, y2) is tile_row 2^3 - 1 - 2 = 5, bytes as served
    EXPECT_EQ(
        blob(outFile(), "SELECT tile_data FROM tiles WHERE zoom_level=3 AND tile_column=4 AND tile_row=5"),
        tilePng(3, 4, 2));
    // an XYZ row (2) would be tile y 5: no such tile
    EXPECT_EQ(
        scalar(outFile(), "SELECT count(*) FROM tiles WHERE zoom_level=3 AND tile_column=4 AND tile_row=2"),
        u"0"_s);

    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='type'"), u"baselayer"_s);
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='format'"), u"png"_s);
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='minzoom'"), u"1"_s);
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='maxzoom'"), u"3"_s);
    // u_ExportTaskToMBTiles.pas:280-290: the tile rectangle of the first (lowest) zoom, here tile (1,0) of
    // zoom 1
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='bounds'"),
              u"0.00000000,0.00000000,180.00000000,85.05112878"_s);
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='name'"), u"Template Map"_s);

    const QRegularExpression written(u"^written: .*base\\.mbtiles {2}9 tiles {2}\\S.*$"_s);
    EXPECT_TRUE(std::ranges::any_of(c.out.split(u'\n'), [&](const QString& l) {
        return written.match(l).hasMatch();
    })) << qPrintable(c.out);
}

// T-0259: a rectangle of less than one pixel at zooms 0 and 1 (legacy count 0, the download fetches nothing
// there): the fill report shows `tiles 0` for them, so `--strict` does not exit 4; zoom 2 counts and fetches
// its one tile.
TEST_F(CliEndToEnd, StrictDownloadOfSubPixelAreaHasNoFalseMissingAtZoomsWithCountZero) {
    servePyramid();
    const Captured c = runCapture(QStringList{u"download"_s, u"--source"_s, u"Template Map"_s,
                                              u"--bbox=-1.5,50.5,-1.0,50.9"_s, u"--zooms"_s, u"0-2"_s,
                                              u"--strict"_s, u"--format=tsv"_s} +
                                  common());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), 1);
    QStringList fills;
    for (const QString& line : c.out.split(u'\n', Qt::SkipEmptyParts)) {
        if (line.startsWith(u"fill\t"_s)) {
            fills.push_back(line.section(u'\t', 2, 6)); // zoom, tiles, present, marked missing, missing
        }
    }
    EXPECT_EQ(fills, (QStringList{u"0\t0\t0\t0\t0"_s, u"1\t0\t0\t0\t0"_s, u"2\t1\t1\t0\t0"_s}))
        << qPrintable(c.out);
}

// Spec 8.2 row 2: --overlay gives a second file whose type is `overlay`.
TEST_F(CliEndToEnd, RunWithOverlayWritesTwoFiles) {
    servePyramid();
    const Captured c = runCapture(job(u"run"_s, u"Template Map"_s, {u"--overlay"_s, u"Scripted Layer"_s}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(outFiles(), (QStringList{u"base-overlay.mbtiles"_s, u"base.mbtiles"_s}));
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='type'"), u"baselayer"_s);
    const QString overlay = outDir() + u"/base-overlay.mbtiles"_s;
    EXPECT_EQ(scalar(overlay, "SELECT value FROM metadata WHERE name='type'"), u"overlay"_s);
    EXPECT_EQ(rows(overlay), u"9"_s);
    QStringList twice = areaPaths() + areaPaths(); // both maps ask the same URLs, one request each
    twice.sort();
    EXPECT_EQ(requested(), twice);
}

// Spec 8.2 row 3: the second `run` sends no request; the existing file is refused without --overwrite and
// rewritten with it.
TEST_F(CliEndToEnd, SecondRunSendsNothingAndRefusesOrReplacesTheFile) {
    servePyramid();
    ASSERT_EQ(runCapture(job(u"run"_s, u"Template Map"_s)).rc, 0);
    const qsizetype afterFirst = requestCount();
    ASSERT_EQ(afterFirst, 9);
    writeStale(outFile());

    const Captured refused = runCapture(job(u"run"_s, u"Template Map"_s));
    EXPECT_EQ(refused.rc, 3);
    EXPECT_TRUE(refused.err.contains(u"exists; pass --overwrite to replace it"_s)) << qPrintable(refused.err);
    EXPECT_EQ(readAll(outFile()), QByteArray("stale"));
    EXPECT_EQ(outFiles(), QStringList{u"base.mbtiles"_s});

    const Captured replaced = runCapture(job(u"run"_s, u"Template Map"_s, {u"--overwrite"_s}));
    EXPECT_EQ(replaced.rc, 0) << qPrintable(replaced.err);
    EXPECT_EQ(rows(outFile()), u"9"_s);
    EXPECT_EQ(outFiles(), QStringList{u"base.mbtiles"_s});
    EXPECT_EQ(requestCount(), afterFirst); // the present tiles were not requested again
}

// Spec 8.2 row 4: two tiles the server does not have (404). A 404 is saved as a "tile does not exist" marker
// only when the legacy option [Internet] SaveTNE is on (u_GlobalDownloadConfig.pas:63 default False; the spec
// table assumes it is on), so the test switches it on through chart-grabber.conf: exit 0, 2 marked missing,
// --strict still 0, the export omits them.
TEST_F(CliEndToEnd, NotFoundTilesAreMarkedWhenSaveTneIsOnAndStrictStillSucceeds) {
    writeIni("SaveTNE=1\r\n");
    servePyramid(0, {{"/3/6/2.png", status(404, "Not Found")}, {"/3/6/3.png", status(404, "Not Found")}});
    const Captured c = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 7 2 0 "_s)) << qPrintable(totals[1]);
    EXPECT_EQ(rows(outFile()), u"7"_s);
    EXPECT_EQ(scalar(outFile(), "SELECT count(*) FROM tiles WHERE zoom_level=3 AND tile_column=6"), u"0"_s);

    const Captured strict = runCapture(job(u"run"_s, u"Template Map"_s, {u"--strict"_s, u"--overwrite"_s}));
    EXPECT_EQ(strict.rc, 0) << qPrintable(strict.err);
    EXPECT_EQ(rows(outFile()), u"7"_s);
}

// --strict with the default configuration: the same two 404 tiles are neither present nor marked, so they are
// "missing": exit 0 without --strict, 4 with it, and then the export does not run.
TEST_F(CliEndToEnd, StrictFailsWithExitFourOnMissingTilesAndDoesNotExport) {
    servePyramid(0, {{"/3/6/2.png", status(404, "Not Found")}, {"/3/6/3.png", status(404, "Not Found")}});
    const Captured lax = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(lax.rc, 0) << qPrintable(lax.err);
    const QStringList totals = totalLines(lax.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(lax.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 7 0 2 "_s)) << qPrintable(totals[1]);
    EXPECT_EQ(rows(outFile()), u"7"_s);

    writeStale(outFile());
    const Captured strict = runCapture(job(u"run"_s, u"Template Map"_s, {u"--strict"_s, u"--overwrite"_s}));
    EXPECT_EQ(strict.rc, 4) << qPrintable(strict.err);
    EXPECT_TRUE(strict.err.contains(u"--strict"_s)) << qPrintable(strict.err);
    EXPECT_EQ(readAll(outFile()), QByteArray("stale")); // no export after a non-zero download
}

// Spec 8.2 row 5: the server answers 500 for ever, --stall-timeout 2: exit 4 within a few seconds, a session
// file exists, no output file.
TEST_F(CliEndToEnd, ServerErrorForEverStallsWithExitFour) {
    QHash<QByteArray, HttpTestServer::Reply> all;
    for (const QString& path : areaPaths()) {
        all.insert(path.toLatin1(), status(500, "Internal Server Error"));
    }
    servePyramid(0, all);
    const auto begin = std::chrono::steady_clock::now();
    const Captured c = runCapture(job(u"run"_s, u"Template Map"_s, {u"--stall-timeout"_s, u"2"_s}));
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - begin);
    EXPECT_EQ(c.rc, 4) << qPrintable(c.err);
    EXPECT_LT(seconds.count(), 30);
    EXPECT_TRUE(c.err.contains(u"no progress for 2 s"_s)) << qPrintable(c.err);
    EXPECT_FALSE(sessionFiles().isEmpty());
    EXPECT_EQ(outFiles(), QStringList());
}

// T-0271 (deviates from L-271): every zoom-3 tile answers 503, --max-connection-errors 2: exit 4 after two
// failed tiles (5 s wait each), the error line, a session file, no output. The server then answers 200 for
// all and the same `run` ends with exit 0, nothing missing, and requests the streak's tiles again.
TEST_F(CliEndToEnd, ConnectionErrorStreakStopsWithExitFourAndTheSecondRunContinues) {
    QHash<QByteArray, HttpTestServer::Reply> failing;
    for (const QString& path : areaPaths()) {
        if (path.startsWith(u"/3/"_s)) {
            failing.insert(path.toLatin1(), status(503, "Service Unavailable"));
        }
    }
    servePyramid(0, failing);
    const Captured first =
        runCapture(job(u"run"_s, u"Template Map"_s, {u"--max-connection-errors"_s, u"2"_s}));
    EXPECT_EQ(first.rc, 4) << qPrintable(first.err);
    EXPECT_TRUE(first.err.contains(u"no connection to the server for 2 tiles in a row"_s))
        << qPrintable(first.err);
    EXPECT_FALSE(sessionFiles().isEmpty());
    EXPECT_EQ(outFiles(), QStringList());
    const qsizetype afterFirst = requestCount();
    const QStringList firstPaths = requested();
    EXPECT_EQ(afterFirst, 5); // the three tiles of zoom 1 and 2, then the two failed ones
    EXPECT_EQ(firstPaths.filter(QRegularExpression(u"^/3/"_s)).size(), 2);

    server_.clearReplies();
    servePyramid(0);
    const Captured second = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    const QStringList secondPaths = requested(afterFirst);
    for (const QString& path : firstPaths.filter(QRegularExpression(u"^/3/"_s))) {
        EXPECT_TRUE(secondPaths.contains(path)) << qPrintable(path); // the streak's tiles are requested again
    }
    const QStringList totals = totalLines(second.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(second.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[1]);
    EXPECT_EQ(rows(outFile()), u"9"_s);
}

// T-0271: one failed tile among good ones is skipped as legacy does (default options): exit 0, one tile
// missing in the fill report.
TEST_F(CliEndToEnd, OneConnectionErrorTileIsSkippedWithExitZero) {
    servePyramid(0, {{"/3/6/2.png", status(503, "Service Unavailable")}});
    const Captured c = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 8 0 1 "_s)) << qPrintable(totals[1]);
    EXPECT_EQ(rows(outFile()), u"8"_s);
}

// Spec 8.2 row 6: the termination arrives after some tiles (server delay 400 ms per tile): exit 130, a
// session file exists, no output; a second `run` fetches only the rest and ends with exit 0.
TEST_F(CliEndToEnd, TerminationLeavesASessionAndTheSecondRunFetchesOnlyTheRest) {
    servePyramid(400);
    std::stop_source termination;
    Captured first;
    {
        // The server has no notification when a request arrives, so the thread polls its request log, bounded
        // by a deadline; it stops the run once two requests were seen.
        std::jthread stopper([&] {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (requestCount() < 2 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            termination.request_stop();
        });
        first = runCapture(job(u"run"_s, u"Template Map"_s), termination.get_token());
    }
    EXPECT_EQ(first.rc, 130) << qPrintable(first.err);
    EXPECT_FALSE(sessionFiles().isEmpty());
    EXPECT_EQ(outFiles(), QStringList());
    const qsizetype afterFirst = requestCount();
    EXPECT_GE(afterFirst, 2);
    EXPECT_LT(afterFirst, 9);
    const QStringList firstPaths = requested();

    const Captured second = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    const qsizetype secondCount = requestCount() - afterFirst;
    EXPECT_GE(secondCount, 1);
    EXPECT_LT(secondCount, 9); // not the whole area again
    QStringList all = firstPaths;
    all.append(requested(afterFirst));
    all.removeDuplicates();
    all.sort();
    EXPECT_EQ(all, areaPaths()); // together they fetched every tile of the area
    EXPECT_EQ(rows(outFile()), u"9"_s);
}

// T-0332 e2e: a real TerminationSignal and a real pipe. The "stop" line arrives after two requests: the run
// ends as after SIGTERM (rc 130 from runCli, exitCode() 143 for main), the session is saved and the second
// run (without the option) fetches only the rest.
TEST_F(CliEndToEnd, StopLineOnStdinPipeStopsLikeSigtermAndTheSessionIsResumable) {
    servePyramid(400);
    int fds[2];
    ASSERT_EQ(cg::core::platform::makePipe(fds), 0);
    Captured first;
    int exitCode = 0;
    bool byStdin = false;
    {
        cg::core::platform::TerminationSignal term;
        std::jthread writer([&] {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (requestCount() < 2 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            EXPECT_EQ(::write(fds[1], "junk\nstop\r\n", 11), 11);
        });
        first = runCapture(job(u"run"_s, u"Template Map"_s, {u"--stop-on-stdin"_s}), term.token(),
                           [&] { return term.watchStdin(fds[0]); });
        exitCode = term.exitCode();
        byStdin = term.stoppedByStdin();
    }
    ::close(fds[1]);
    std::this_thread::sleep_for(50ms);
    ::close(fds[0]);
    EXPECT_EQ(first.rc, 130) << qPrintable(first.err);
    EXPECT_EQ(exitCode, 143);
    EXPECT_TRUE(byStdin);
    EXPECT_FALSE(sessionFiles().isEmpty());
    EXPECT_EQ(outFiles(), QStringList());
    const qsizetype afterFirst = requestCount();
    EXPECT_LT(afterFirst, 9);

    const Captured second = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_LT(requestCount() - afterFirst, 9);
    EXPECT_EQ(rows(outFile()), u"9"_s);
}

// T-0332: EOF (the writer is gone) before the job starts: nothing is requested, exit code 143, no output
TEST_F(CliEndToEnd, EofOnStdinBeforeTheFirstTileIs143AndSendsNothing) {
    servePyramid(0);
    int fds[2];
    ASSERT_EQ(cg::core::platform::makePipe(fds), 0);
    ::close(fds[1]);
    Captured result;
    int exitCode = 0;
    {
        cg::core::platform::TerminationSignal term;
        result = runCapture(job(u"run"_s, u"Template Map"_s, {u"--stop-on-stdin"_s}), term.token(), [&] {
            const auto verdict = term.watchStdin(fds[0]);
            // the 100 ms watcher turns the flag into the token: wait for it, so that the
            // stop is there before the first stage (a real start takes longer than that)
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (!term.token().stop_requested() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            return verdict;
        });
        exitCode = term.exitCode();
    }
    ::close(fds[0]);
    EXPECT_EQ(result.rc, 130) << qPrintable(result.err);
    EXPECT_EQ(exitCode, 143);
    EXPECT_EQ(requestCount(), 0);
    EXPECT_EQ(outFiles(), QStringList());
}

// T-0278 (T-0262 Work 2): a continued `run --overwrite` stopped again during its estimate exits 130 with no
// session message, sends no request and leaves every session file byte-identical. The stop is requested
// before the run starts, so the estimate (which walks every tile under --overwrite) is the first stage that
// sees it.
TEST_F(CliEndToEnd, ContinuedRunInterruptedDuringEstimateKeepsTheSessionFile) {
    servePyramid(400);
    const Captured first = runInterrupted(job(u"run"_s, u"Template Map"_s, {u"--overwrite"_s}), 2);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QMap<QString, QByteArray> before = sessionBytes();
    ASSERT_FALSE(before.isEmpty());
    const qsizetype afterFirst = requestCount();

    std::stop_source termination;
    termination.request_stop();
    const Captured second =
        runCapture(job(u"run"_s, u"Template Map"_s, {u"--overwrite"_s}), termination.get_token());
    EXPECT_EQ(second.rc, 130) << qPrintable(second.err);
    EXPECT_FALSE(second.err.contains(u"session saved"_s)) << qPrintable(second.err);
    EXPECT_FALSE(second.err.contains(u"to continue from this file"_s)) << qPrintable(second.err);
    EXPECT_EQ(requestCount(), afterFirst);
    EXPECT_EQ(sessionBytes(), before);
}

// T-0278 (T-0262 Work 2, case b): a continued `run --overwrite` stopped again during its download saves the
// session once more, so both session warnings follow it (app-cli.md 1.9) and the session file changes.
TEST_F(CliEndToEnd, ContinuedRunInterruptedDuringDownloadWarns) {
    servePyramid(400);
    const Captured first = runInterrupted(job(u"run"_s, u"Template Map"_s, {u"--overwrite"_s}), 2);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QMap<QString, QByteArray> before = sessionBytes();
    ASSERT_FALSE(before.isEmpty());

    const Captured second =
        runInterrupted(job(u"run"_s, u"Template Map"_s, {u"--overwrite"_s}), requestCount() + 2);
    EXPECT_EQ(second.rc, 130) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"warning: session saved; run the same command again to continue"_s))
        << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"warning: to continue from this file: resume "_s))
        << qPrintable(second.err);
    EXPECT_NE(sessionBytes(), before);
}

// T-0272 (L-260 fixed): a route of two <trkseg> is a two-part corridor. The interrupted run leaves a session
// file that holds both parts (the writer used to lose the second one); `resume FILE` then covers both.
// Zoom 3: part 1 along lat 45, lon 10..60 -> tiles (4,2) (5,2); part 2 along lat -45, lon -100..-50 -> tiles
// (1,5) (2,5).
TEST_F(CliEndToEnd, ASessionOfARouteWithTwoSegmentsKeepsBothPartsAndResumeCoversThem) {
    servePyramid(400);
    const QString gpx = root_.filePath(u"r.gpx"_s);
    QFile f(gpx);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(
        "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "<trk><name>t</name><trkseg><trkpt lat=\"45\" lon=\"10\"/><trkpt lat=\"45\" lon=\"60\"/></trkseg>\n"
        "<trkseg><trkpt lat=\"-45\" lon=\"-100\"/><trkpt lat=\"-45\" lon=\"-50\"/></trkseg></trk>\n</gpx>\n");
    f.close();
    const QStringList args =
        QStringList{u"run"_s,           u"--source"_s, u"Template Map"_s, u"--route"_s, gpx,
                    u"--corridor-nm"_s, u"2"_s,        u"--zooms"_s,      u"3"_s,       u"--out"_s,
                    outFile()} +
        common();
    std::stop_source termination;
    Captured first;
    {
        std::jthread stopper([&] {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (requestCount() < 1 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            termination.request_stop();
        });
        first = runCapture(args, termination.get_token());
    }
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QStringList files = sessionFiles();
    ASSERT_FALSE(files.isEmpty());

    auto parsed = cg::core::IniFile::fromFile(files.first());
    ASSERT_TRUE(parsed.has_value());
    const auto root =
        cg::core::ConfigProviderIni::wrap(std::make_shared<cg::core::IniFile>(std::move(*parsed)));
    const auto section = root->subItem(u"Session");
    ASSERT_TRUE(section != nullptr);
    const cg::geo::GeometryLonLatFactory factory;
    const auto polygon = cg::geo::readPolygon(*section, factory);
    const auto multi = std::dynamic_pointer_cast<const cg::geo::IGeometryLonLatMultiPolygon>(polygon);
    ASSERT_TRUE(multi != nullptr);
    EXPECT_EQ(multi->count(), 2U);

    const Captured resumed =
        runCapture(QStringList{u"resume"_s, files.first(), u"--format=tsv"_s} + common());
    ASSERT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    // fill record of zoom 3: tiles, present, marked missing, missing as served
    const QStringList records = resumed.out.split(u'\n', Qt::SkipEmptyParts);
    ASSERT_FALSE(records.isEmpty());
    const QStringList fill = records.last().split(u'\t');
    ASSERT_GE(fill.size(), 7) << qPrintable(records.last());
    EXPECT_EQ(fill.mid(0, 1) + fill.mid(2, 5),
              (QStringList{u"fill"_s, u"3"_s, u"4"_s, u"4"_s, u"0"_s, u"0"_s}));
    QStringList all = requested();
    all.removeDuplicates();
    EXPECT_EQ(all, (QStringList{u"/3/1/5.png"_s, u"/3/2/5.png"_s, u"/3/4/2.png"_s, u"/3/5/2.png"_s}));
    // the cache now holds the tiles of both parts: the export writes four
    QStringList exportArgs = args;
    exportArgs[0] = u"export"_s;
    const Captured exported = runCapture(exportArgs);
    ASSERT_EQ(exported.rc, 0) << qPrintable(exported.err);
    EXPECT_EQ(rows(outFile()), u"4"_s);
}

// T-0202 proof 4: `run --format=tsv`: stdout holds only estimate, fill and written records, in this order;
// the written path is absolute, the file exists with the stated size. Empty cache: every figure is the
// assumed size (20 KB default = 20480 bytes per tile).
TEST_F(CliEndToEnd, RunTsvPrintsOnlyRecordsAndTheWrittenPathIsAbsolute) {
    servePyramid();
    // --out is given relative to the current directory: the record must still carry the absolute path
    const QString saved = QDir::currentPath();
    ASSERT_TRUE(QDir::setCurrent(root_.path()));
    const Captured c =
        runCapture(job(u"run"_s, u"Template Map"_s, {u"--format=tsv"_s, u"--out"_s, u"out/base.mbtiles"_s}));
    QDir::setCurrent(saved);
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QString guid = u"{AB0000C3-0000-0000-0000-0000000000C3}"_s;
    const QStringList records = c.out.split(u'\n', Qt::SkipEmptyParts);
    ASSERT_EQ(records.size(), 7) << qPrintable(c.out);
    // zoom 1..3: 1, 2 and 6 tiles
    EXPECT_EQ(records[0], u"estimate\t%1\t1\t1\t1\t0.0000\t0.0000\t20480\t1\t20480"_s.arg(guid));
    EXPECT_EQ(records[1], u"estimate\t%1\t2\t2\t2\t0.0000\t0.0000\t40960\t1\t40960"_s.arg(guid));
    EXPECT_EQ(records[2], u"estimate\t%1\t3\t6\t6\t0.0000\t0.0000\t122880\t1\t122880"_s.arg(guid));
    EXPECT_EQ(records[3], u"fill\t%1\t1\t1\t1\t0\t0\t%2"_s.arg(guid).arg(tilePng(1, 1, 0).size()));
    // fill z2 / z3: tiles, present, marked missing, missing as served; bytes = the sizes of the stored tiles
    const qint64 bytes2 = tilePng(2, 2, 1).size() + tilePng(2, 3, 1).size();
    qint64 bytes3 = 0;
    for (const int x : {4, 5, 6}) {
        for (const int y : {2, 3}) {
            bytes3 += tilePng(3, x, y).size();
        }
    }
    EXPECT_EQ(records[4], u"fill\t%1\t2\t2\t2\t0\t0\t%2"_s.arg(guid).arg(bytes2));
    EXPECT_EQ(records[5], u"fill\t%1\t3\t6\t6\t0\t0\t%2"_s.arg(guid).arg(bytes3));
    const QStringList written = records[6].split(u'\t');
    ASSERT_EQ(written.size(), 6) << qPrintable(records[6]);
    EXPECT_EQ(written[0], u"written"_s);
    EXPECT_EQ(written[1], guid);
    EXPECT_EQ(written[2], QFileInfo(root_.path()).canonicalFilePath() + u"/out/base.mbtiles"_s);
    EXPECT_TRUE(QFileInfo(written[2]).isAbsolute());
    EXPECT_EQ(written[3], u"9"_s);
    EXPECT_EQ(written[4].toLongLong(), QFileInfo(written[2]).size());
    EXPECT_GT(QFileInfo(written[2]).size(), 0);
}

// T-0203 proof 1: `run --progress=lines`: the phases and records in order on stderr, nothing else of
// progress.
TEST_F(CliEndToEnd, RunProgressLinesWritesPhasesAndRecordsInOrder) {
    servePyramid();
    QStringList args = job(u"run"_s, u"Template Map"_s, {u"--progress=lines"_s});
    args.removeAll(u"--no-progress"_s); // it wins over --progress=lines (job.cpp: applied in order)
    const Captured c = runCapture(args);
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QString guid = u"{AB0000C3-0000-0000-0000-0000000000C3}"_s;
    EXPECT_FALSE(c.err.contains(u'\r'));
    EXPECT_FALSE(c.err.contains(u"downloaded "_s)); // the text line
    EXPECT_FALSE(c.err.contains(u"export 100%"_s));
    QStringList kinds;
    for (const QString& line : c.err.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.split(u'\t');
        if (f[0] != u"phase"_s && f[0] != u"progress"_s) {
            continue; // a Qt message of the test server thread (a socket warning) may land on stderr
        }
        ASSERT_GE(f.size(), 3) << qPrintable(line);
        EXPECT_EQ(f[2], guid);
        const QString kind = f[0] + u' ' + f[1];
        if (kinds.isEmpty() || kinds.last() != kind) {
            kinds.push_back(kind);
        }
    }
    EXPECT_EQ(kinds, (QStringList{u"phase estimate"_s, u"phase download"_s, u"progress download"_s,
                                  u"phase fill"_s, u"phase export"_s, u"progress export"_s}));
    const QStringList lines = c.err.split(u'\n', Qt::SkipEmptyParts);
    EXPECT_EQ(lines.last(), u"progress\texport\t%1\t1000"_s.arg(guid));
    // the final download record: 14 tiles processed (1 + 2 + 6 + ... of the area), highest zoom 3 (the job
    // zooms are 1-3)
    const QString lastDownload = [&] {
        for (qsizetype i = lines.size() - 1; i >= 0; --i) {
            if (lines[i].startsWith(u"progress\tdownload\t"_s)) {
                return lines[i];
            }
        }
        return QString();
    }();
    const QStringList f = lastDownload.split(u'\t');
    ASSERT_EQ(f.size(), 9) << qPrintable(lastDownload);
    EXPECT_EQ(f[3], u"3"_s);
    EXPECT_EQ(f[4], u"9"_s);
    EXPECT_EQ(f[5], u"9"_s);
    EXPECT_EQ(f[6], u"9"_s);
}

// T-0202 proof 5: a repeated `run --format=tsv` after an interruption prints one `session` record (file
// exists, time = the file's modification time before the run) and the stderr warning is still there.
TEST_F(CliEndToEnd, RepeatedRunTsvPrintsOneSessionRecordAndKeepsTheWarning) {
    servePyramid(400);
    const auto interrupted = [&](qsizetype requestsToSee) {
        return runInterrupted(job(u"run"_s, u"Template Map"_s, {u"--format=tsv"_s}), requestsToSee);
    };
    const Captured first = interrupted(2);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    EXPECT_FALSE(first.out.contains(u"session\t"_s));
    EXPECT_FALSE(first.err.contains(u"continuing the session"_s));
    // the interrupted run leaves final.sls and a timestamped copy; the runner continues the newest file
    // (modification time, then name: app-cli.md 1.9)
    QStringList before = sessionFiles();
    ASSERT_FALSE(before.isEmpty());
    std::ranges::sort(before, [](const QString& a, const QString& b) {
        return std::make_pair(QFileInfo(a).lastModified(), a) <
               std::make_pair(QFileInfo(b).lastModified(), b);
    });
    const QString newest = before.back();
    const QString savedAt = QFileInfo(newest).lastModified().toString(u"yyyy-MM-dd HH:mm:ss"_s);

    const Captured second = interrupted(requestCount() + 2);
    ASSERT_EQ(second.rc, 130) << qPrintable(second.err);
    QStringList sessions;
    for (const QString& line : second.out.split(u'\n', Qt::SkipEmptyParts)) {
        if (line.startsWith(u"session\t"_s)) {
            sessions.push_back(line);
        }
    }
    ASSERT_EQ(sessions.size(), 1) << qPrintable(second.out);
    const QStringList f = sessions[0].split(u'\t');
    ASSERT_EQ(f.size(), 4);
    EXPECT_EQ(f[1], u"{AB0000C3-0000-0000-0000-0000000000C3}"_s);
    EXPECT_EQ(f[2], savedAt);
    EXPECT_EQ(f[3], QFileInfo(newest).absoluteFilePath());
    EXPECT_TRUE(QFileInfo(f[3]).isAbsolute());
    EXPECT_TRUE(QFileInfo::exists(f[3]));
    EXPECT_TRUE(f[3].endsWith(u".sls"_s));
    EXPECT_TRUE(second.err.contains(u"warning: continuing the session of "_s + savedAt))
        << qPrintable(second.err);
}

// T-0202 (resume half of the session record): `resume FILE --format=tsv` prints the session record of the map
// the .sls names, then its fill records; the file's modification time is the one it had before the command.
TEST_F(CliEndToEnd, ResumeTsvPrintsTheSessionRecordOfTheMapTheFileNames) {
    servePyramid(400);
    std::stop_source termination;
    Captured first;
    {
        // as in TerminationLeavesASessionAndTheSecondRunFetchesOnlyTheRest (poll of the request log)
        std::jthread stopper([&] {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (requestCount() < 2 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            termination.request_stop();
        });
        first = runCapture(job(u"run"_s, u"Template Map"_s), termination.get_token());
    }
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QStringList files = sessionFiles();
    ASSERT_FALSE(files.isEmpty());
    const QString file = files.first();
    const QString savedAt = QFileInfo(file).lastModified().toString(u"yyyy-MM-dd HH:mm:ss"_s);

    const Captured c = runCapture(QStringList{u"resume"_s, file, u"--format=tsv"_s} + common());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList records = c.out.split(u'\n', Qt::SkipEmptyParts);
    ASSERT_EQ(records.size(), 4) << qPrintable(c.out);
    EXPECT_EQ(records[0], u"session\t{AB0000C3-0000-0000-0000-0000000000C3}\t%1\t%2"_s.arg(
                              savedAt, QFileInfo(file).absoluteFilePath()));
    for (qsizetype i = 1; i < 4; ++i) {
        EXPECT_TRUE(records[i].startsWith(u"fill\t{AB0000C3-0000-0000-0000-0000000000C3}\t%1\t"_s.arg(i)))
            << qPrintable(records[i]);
    }
}

// Spec 8.2 row 7: `export` with an empty cache: exit 5, no file.
TEST_F(CliEndToEnd, ExportWithAnEmptyCacheIsExitFiveAndLeavesNoFile) {
    servePyramid();
    const Captured c = runCapture(job(u"export"_s, u"Template Map"_s));
    EXPECT_EQ(c.rc, 5) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"no tiles of"_s)) << qPrintable(c.err);
    EXPECT_EQ(outFiles(), QStringList());
    EXPECT_EQ(requestCount(), 0);
}

// Spec 8.2 row 8: `estimate` prints the counts of the area and sends nothing.
TEST_F(CliEndToEnd, EstimateCountsTheAreaAndSendsNoRequest) {
    servePyramid();
    const Captured c = runCapture(job(u"estimate"_s, u"Template Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), 0);
    const QStringList lines = simplifiedLines(c.out);
    // zoom, SAS z (zoom + 1), tiles, bbox tiles
    EXPECT_TRUE(std::ranges::any_of(lines, [](const QString& l) { return l.startsWith(u"1 2 1 1 "_s); }))
        << qPrintable(c.out);
    EXPECT_TRUE(std::ranges::any_of(lines, [](const QString& l) { return l.startsWith(u"2 3 2 2 "_s); }))
        << qPrintable(c.out);
    EXPECT_TRUE(std::ranges::any_of(lines, [](const QString& l) { return l.startsWith(u"3 4 6 6 "_s); }))
        << qPrintable(c.out);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 1) << qPrintable(c.out);
    EXPECT_TRUE(totals[0].startsWith(u"total 9 9 "_s))
        << qPrintable(totals[0]); // == the nine requests of `run`
    EXPECT_EQ(outFiles(), QStringList());
}

// T-0245 (Q-W1, D row): zooms above MaxZoom are skipped with a warning; the server sees zooms 1-2 only and
// the MBTiles maxzoom is 2.
TEST_F(CliEndToEnd, RunSkipsTheZoomsAboveMaxZoomWithAWarning) {
    servePyramid();
    writeMaxTwo();
    const Captured c = runCapture(job(u"run"_s, u"Max Two Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"warning: \"Max Two Map\": zooms 3 skipped, the source stops at zoom 2\n"_s))
        << qPrintable(c.err);
    EXPECT_EQ(requested(), withoutZoom3(areaPaths()));
    EXPECT_EQ(requested().size(), 3);
    EXPECT_EQ(rows(outFile()), u"3"_s);
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='maxzoom'"), u"2"_s);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 3 3 0 0 "_s)) << qPrintable(totals[1]); // the fill report too
}

// A base map without the key and an overlay with it: the base file has zooms 1-3, the overlay 1-2.
TEST_F(CliEndToEnd, MaxZoomLimitsTheOverlayAndNotTheBaseMap) {
    servePyramid();
    writeMaxTwo();
    const QString overlay = outDir() + u"/over.mbtiles"_s;
    const Captured c = runCapture(
        job(u"run"_s, u"Template Map"_s, {u"--overlay"_s, u"Max Two Map"_s, u"--overlay-out"_s, overlay}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(scalar(outFile(), "SELECT value FROM metadata WHERE name='maxzoom'"), u"3"_s);
    EXPECT_EQ(rows(outFile()), u"9"_s);
    EXPECT_EQ(scalar(overlay, "SELECT value FROM metadata WHERE name='maxzoom'"), u"2"_s);
    EXPECT_EQ(rows(overlay), u"3"_s);
}

TEST_F(CliEndToEnd, EveryChosenZoomAboveMaxZoomIsExitThreeWithoutARequest) {
    servePyramid();
    writeMaxTwo();
    const Captured c = runCapture(job(u"run"_s, u"Max Two Map"_s, {}, u"3"_s));
    EXPECT_EQ(c.rc, 3);
    EXPECT_EQ(c.err, u"error: \"Max Two Map\": every chosen zoom is above its maximum zoom 2\n"_s);
    EXPECT_EQ(requestCount(), 0);
    EXPECT_EQ(outFiles(), QStringList());
}

TEST_F(CliEndToEnd, EstimateListsOnlyTheZoomsUpToMaxZoomAndWarns) {
    servePyramid();
    writeMaxTwo();
    const Captured c = runCapture(job(u"estimate"_s, u"Max Two Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"zooms 3 skipped, the source stops at zoom 2"_s)) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), 0);
    const QStringList lines = simplifiedLines(c.out);
    EXPECT_TRUE(std::ranges::any_of(lines, [](const QString& l) { return l.startsWith(u"2 3 2 2 "_s); }));
    EXPECT_FALSE(std::ranges::any_of(lines, [](const QString& l) { return l.startsWith(u"3 4 "_s); }))
        << qPrintable(c.out);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 1) << qPrintable(c.out);
    EXPECT_TRUE(totals[0].startsWith(u"total 3 3 "_s)) << qPrintable(totals[0]);
}

// `sources`: field 13 is the number, empty without the key; an invalid key is the verdict.
TEST_F(CliEndToEnd, SourcesTsvMaxZoomFieldAndInvalidKeyVerdict) {
    writeMaxTwo();
    writeZmp(maps(), {.folder = u"w9.zmp"_s,
                      .guid = "{AB0000E9-0000-0000-0000-0000000000E9}",
                      .name = "Max Bad Map",
                      .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png",
                      .extra = "MaxZoom=24\r\n"});
    const Captured tsv = runCapture(QStringList{u"sources"_s, u"--format=tsv"_s} + common());
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    QHash<QString, QStringList> byName;
    for (const QString& line : tsv.out.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.split(u'\t');
        ASSERT_EQ(f.size(), 15) << qPrintable(line);
        byName.insert(f[3], f);
    }
    EXPECT_EQ(byName.value(u"Max Two Map"_s).value(13), u"2"_s);
    EXPECT_EQ(byName.value(u"Max Two Map"_s).value(7), u"ok"_s);
    EXPECT_EQ(byName.value(u"Template Map"_s).value(13), QString());
    EXPECT_EQ(byName.value(u"Max Bad Map"_s).value(13), QString());
    EXPECT_EQ(byName.value(u"Max Bad Map"_s).value(7),
              u"\"Max Bad Map\": MaxZoom=24 is not a zoom from 0 to 23"_s);
}

// Spec 8.2 row 9: a route with --corridor-nm 2: only the tiles on the corridor are requested. The route runs
// along lat 45 from lon 10 to lon 60: at zoom 3 that is tiles (4,2) and (5,2); a 1 NM half width does not
// reach the next row (lat 41 is 4 degrees away) or the next column.
TEST_F(CliEndToEnd, RouteCorridorRequestsOnlyTheTilesOnTheRoute) {
    servePyramid();
    const QString gpx = root_.filePath(u"r.gpx"_s);
    QFile f(gpx);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(
        "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "<rte><name>r</name><rtept lat=\"45\" lon=\"10\"/><rtept lat=\"45\" lon=\"60\"/></rte>\n</gpx>\n");
    f.close();
    const QStringList args =
        QStringList{u"run"_s,           u"--source"_s, u"Template Map"_s, u"--route"_s, gpx,
                    u"--corridor-nm"_s, u"2"_s,        u"--zooms"_s,      u"3"_s,       u"--out"_s,
                    outFile()} +
        common();
    const Captured c = runCapture(args);
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requested(), (QStringList{u"/3/4/2.png"_s, u"/3/5/2.png"_s}));
    EXPECT_EQ(rows(outFile()), u"2"_s);
}

// FEAT-NET-003: the scripted ZMP ("Scripted Layer", DefURLBase deliberately wrong) downloads the same nine
// tiles as the template map; only GetUrlScript.txt can have built these URLs.
TEST_F(CliEndToEnd, ScriptedMapDownloadsTheSameTilesAsTheTemplateMap) {
    servePyramid();
    const Captured c = runCapture(job(u"run"_s, u"Scripted Layer"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requested(), areaPaths());
    EXPECT_EQ(rows(outFile()), u"9"_s);
    EXPECT_EQ(
        blob(outFile(), "SELECT tile_data FROM tiles WHERE zoom_level=3 AND tile_column=4 AND tile_row=5"),
        tilePng(3, 4, 2));
}

// FEAT-NET-002: the DefURLBase path of params.txt ("/alt/...") is the one the requests use.
TEST_F(CliEndToEnd, ParamsTxtUrlBaseDecidesTheRequestedPaths) {
    servePyramid();
    const Captured c = runCapture(job(u"run"_s, u"Alt Path Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requested(), areaPaths(u"/alt"_s));
    EXPECT_EQ(rows(outFile()), u"9"_s);
}

// ---- T-0200 (UAT F-1; T-0193 review: tests owed) ----

// UAT F-1, spec 1.7 S-3: two areas are a refusal (exit 3), the message alone, no usage line; nothing is
// written and no request is sent. Flags (--bbox with --route) and the job file (BBox with Polygon).
TEST_F(CliEndToEnd, MoreThanOneAreaIsExitThreeWithoutTheUsageLine) {
    servePyramid();
    const QString jobFile = root_.filePath(u"two.ini"_s);
    QFile f(jobFile);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[Job]\nSource=Template Map\nBBox=10,30,100,60\nPolygon=a.hlg\nZooms=1-3\n");
    f.close();

    const Captured flags =
        runCapture(QStringList{u"run"_s, u"--source"_s, u"Template Map"_s, u"--bbox"_s, u"10,30,100,60"_s,
                               u"--route"_s, u"r.gpx"_s, u"--zooms"_s, u"1-3"_s, u"--out"_s, outFile()} +
                   common());
    EXPECT_EQ(flags.rc, 3);
    EXPECT_EQ(flags.err.toStdString(), "error: --bbox, --polygon, --route: only one may be given\n");
    EXPECT_EQ(flags.out, QString());

    const Captured file =
        runCapture(QStringList{u"run"_s, u"--job"_s, jobFile, u"--out"_s, outFile()} + common());
    EXPECT_EQ(file.rc, 3);
    EXPECT_EQ(
        file.err.toStdString(),
        u"error: --bbox, --polygon, --route: only one may be given (%1)\n"_s.arg(jobFile).toStdString());
    EXPECT_EQ(file.out, QString());

    EXPECT_EQ(requestCount(), 0);
    EXPECT_EQ(outFiles(), QStringList());
}

// T-0277 (app-cli.md 1.7 S-16; legacy L-18): the world at zoom 16 holds more than 2^31-1 tiles, more
// than the legacy 32-bit count; refused before the first request.
TEST_F(CliEndToEnd, AreaWhoseTileCountWouldWrapIsExitThreeAndSendsNothing) {
    servePyramid();
    const Captured c = runCapture(QStringList{u"estimate"_s, u"--source"_s, u"Template Map"_s,
                                              u"--bbox=-180,-85,180,85"_s, u"--zooms=16"_s} +
                                  common());
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_EQ(c.err,
              u"error: the area is too large at zoom 16 (0-based): more than 2147483647 tiles; choose a "
              u"smaller area or lower zooms\n"_s);
    EXPECT_EQ(requestCount(), 0);
}

// Spec 1.7 S-13: the area holds no tile at the chosen zooms -> exit 3, and `run` sends no request. The
// polygon file is three equal points (an area of no extent).
TEST_F(CliEndToEnd, AnAreaWithoutATileIsExitThreeAndSendsNothing) {
    servePyramid();
    const QString hlg = root_.filePath(u"point.hlg"_s);
    QFile f(hlg);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[HIGHLIGHTING]\nPointLon_1=50.5\nPointLat_1=40.5\nPointLon_2=50.5\nPointLat_2=40.5\n"
            "PointLon_3=50.5\nPointLat_3=40.5\n");
    f.close();
    const Captured c = runCapture(QStringList{u"run"_s, u"--source"_s, u"Template Map"_s, u"--polygon"_s, hlg,
                                              u"--zooms"_s, u"1-3"_s, u"--out"_s, outFile()} +
                                  common());
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_EQ(c.err, u"error: the area holds no tile at the chosen zooms\n"_s);
    EXPECT_EQ(requestCount(), 0);
    EXPECT_EQ(outFiles(), QStringList());
}

// The termination arrives while the export thread runs: exit 130, and neither the .mbtiles nor the .part file
// is left. The area is the whole world down to zoom 11 (about 5.6 million cache lookups on an empty cache),
// so the export is still running when the watcher sees the .part file appear in the output directory
// (inotify or a change notification: the wait is on the event, with a bound of 60 s).
TEST_F(CliEndToEnd, TerminationWhileTheExportThreadRunsIsExit130AndLeavesNoFile) {
    servePyramid();
    const cg::core::platform::DirectoryWatch watch(
        outDir()); // T-0335: inotify on Linux, a change notification on Windows
    ASSERT_TRUE(watch.valid());
    std::stop_source termination;
    std::atomic<bool> sawCreate{false};
    Captured c;
    {
        std::jthread watcher([&] {
            if (watch.waitForCreate(60000)) {
                sawCreate = true;
            }
            termination.request_stop();
        });
        c = runCapture(QStringList{u"export"_s, u"--source"_s, u"Template Map"_s, u"--bbox"_s,
                                   u"-180,-85,180,85"_s, u"--zooms"_s, u"1-11"_s, u"--out"_s, outFile()} +
                           common(),
                       termination.get_token());
    }
    EXPECT_TRUE(sawCreate.load()) << qPrintable(c.err);
    EXPECT_EQ(c.rc, 130) << qPrintable(c.err);
    EXPECT_EQ(outFiles(), QStringList());
    EXPECT_EQ(requestCount(), 0);
}

} // namespace

// T-0198 (UAT D-1) proof 2: a CacheType=9 map over nine tiles. The ZMP sets no MemCacheCapacity (the default
// 100 applies), so this test proves the substitution through the cachew5 directory; the small capacity is
// the test below (T-0211). Legacy would keep the last tiles in memory and export fewer; the program uses a
// file cache, so all nine are exported and none is missing.
TEST_F(CliEndToEnd, RamCacheMapIsDownloadedAndExportedThroughAFileCache) {
    servePyramid();
    writeZmp(maps(),
             {.folder = u"w5.zmp"_s,
              .guid = "{AB0000E5-0000-0000-0000-0000000000E5}",
              .name = "Ram Map",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
              .extra = "CacheType=9\r\n"});
    const Captured c = runCapture(job(u"run"_s, u"Ram Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(rows(outFile()), u"9"_s);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[1]);
    EXPECT_TRUE(QFileInfo(root_.filePath(u"cache/cache_sqlite/cachew5"_s)).isDir());
}

// T-0276 (D-160 row, L-75): a CacheType=9 map (region files under cache_sqlite/cachew5) downloaded once, then
// every region file overwritten with garbage. Legacy read the failure as "tile does not exist".
void damageRegionFiles(const QString& cacheDir) {
    int damaged = 0;
    QDirIterator it(cacheDir, {u"*.sqlitedb"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QByteArray(4096, 'x'));
        ++damaged;
    }
    ASSERT_GT(damaged, 0);
}

// `export` first counts what the cache holds (the fill report), so the damaged file ends the run there: exit
// 1 (app-cli.md 1.11, a storage error of the estimate or fill report), not 5.
TEST_F(CliEndToEnd, ExportOverADamagedRegionFileIsExitOneWithTheSqlErrorAndLeavesNoFile) {
    servePyramid();
    writeZmp(maps(),
             {.folder = u"w5.zmp"_s,
              .guid = "{AB0000E5-0000-0000-0000-0000000000E5}",
              .name = "Ram Map",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
              .extra = "CacheType=9\r\n"});
    ASSERT_EQ(runCapture(job(u"download"_s, u"Ram Map"_s)).rc, 0);
    damageRegionFiles(root_.filePath(u"cache"_s));

    const Captured c = runCapture(job(u"export"_s, u"Ram Map"_s));
    EXPECT_EQ(c.rc, 1) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: file is not a database"_s)) << qPrintable(c.err);
    EXPECT_FALSE(c.err.contains(u"no tiles of"_s)) << qPrintable(c.err); // not read as an empty cache
    EXPECT_EQ(outFiles(), QStringList());
}

TEST_F(CliEndToEnd, DownloadOverADamagedRegionFileIsExitOneAndSendsNoRequest) {
    servePyramid();
    writeZmp(maps(),
             {.folder = u"w5.zmp"_s,
              .guid = "{AB0000E5-0000-0000-0000-0000000000E5}",
              .name = "Ram Map",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
              .extra = "CacheType=9\r\n"});
    ASSERT_EQ(runCapture(job(u"download"_s, u"Ram Map"_s)).rc, 0);
    damageRegionFiles(root_.filePath(u"cache"_s));
    const qsizetype before = requestCount();

    const Captured c = runCapture(job(u"download"_s, u"Ram Map"_s));
    EXPECT_EQ(c.rc, 1) << qPrintable(c.err); // the estimate stage fails before any worker starts
    EXPECT_TRUE(c.err.contains(u"error: file is not a database"_s)) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), before) << "the damaged file must not look empty (every tile fetched again)";
}

// T-0202: the cache cell of a `source` record is the CacheType number of the ZMP (a non-default one, so a
// constant cannot pass), the same as the text table's cache column.
TEST_F(CliEndToEnd, SourcesTsvCacheCellIsTheZmpCacheType) {
    const QByteArray url = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png";
    writeZmp(maps(), {.folder = u"w7.zmp"_s,
                      .guid = "{AB0000E7-0000-0000-0000-0000000000E7}",
                      .name = "Sas Cache Map",
                      .urlBase = url,
                      .extra = "CacheType=2\r\n"});
    const Captured tsv =
        runCapture(QStringList{u"sources"_s, u"--filter"_s, u"Sas Cache"_s, u"--format=tsv"_s} + common());
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    const QStringList f = tsv.out.split(u'\n').first().split(u'\t');
    ASSERT_EQ(f.size(), 15) << qPrintable(tsv.out);
    EXPECT_EQ(f[6], u"2"_s);
    EXPECT_EQ(f[13], QString()); // T-0245: no MaxZoom
}

// FEAT-STO-018, T-0211 (UAT D-1 root cause): the same nine tiles with MemCacheCapacity=2 export all nine.
// Zoom 1 is the only tile of its region file; legacy defect L-94(a) (the creating write leaves the entry
// unreadable) would answer its read "no tile" once it has left the info cache. The deviation of T-0211
// (docs/parity/deviations.md) initialises the entry after the creating write.
TEST_F(CliEndToEnd, SmallMemCacheCapacityExportsEveryTile) {
    servePyramid();
    writeZmp(maps(),
             {.folder = u"w5.zmp"_s,
              .guid = "{AB0000E5-0000-0000-0000-0000000000E5}",
              .name = "Ram Map",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
              .extra = "CacheType=9\r\nMemCacheCapacity=2\r\n"});
    const Captured c = runCapture(job(u"run"_s, u"Ram Map"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requested(), areaPaths()); // all nine downloaded
    EXPECT_EQ(rows(outFile()), u"9"_s);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[1]);
}

// T-0198 proof 4: `sources` prints the same for a RAM map except the cache column (Default = 0, where the ZMP
// said 9); nothing on stderr; chart-grabber.conf is not rewritten and no Maps.ini appears.
TEST_F(CliEndToEnd, SourcesWithRamCacheMapPrintsNothingExtraAndWritesNoConfiguration) {
    const QByteArray url = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png";
    writeZmp(maps(), {.folder = u"w5.zmp"_s,
                      .guid = "{AB0000E5-0000-0000-0000-0000000000E5}",
                      .name = "Ram Map",
                      .urlBase = url,
                      .extra = "CacheType=9\r\n"});
    writeZmp(maps(), {.folder = u"w6.zmp"_s,
                      .guid = "{AB0000E6-0000-0000-0000-0000000000E6}",
                      .name = "Plain Map",
                      .urlBase = url});
    const QByteArray iniBefore = readAll(dataDir() + u"/chart-grabber.conf"_s);
    const Captured c = runCapture(QStringList{u"sources"_s, u"--filter"_s, u"map"_s} + common());
    ASSERT_EQ(c.rc, 0);
    EXPECT_EQ(c.err, QString());
    QStringList cells;
    for (const QString& line : simplifiedLines(c.out)) {
        if (line.contains(u"Ram Map"_s) || line.contains(u"Plain Map"_s)) {
            cells.push_back(line);
        }
    }
    ASSERT_EQ(cells.size(), 2) << qPrintable(c.out);
    // the cache column is the same for both: 0
    const auto cacheOf = [](const QString& line) { return line.split(u' ').at(line.split(u' ').size() - 2); };
    EXPECT_EQ(cacheOf(cells[0]), cacheOf(cells[1])) << qPrintable(c.out);
    EXPECT_EQ(cacheOf(cells[0]), u"0"_s);
    EXPECT_EQ(readAll(dataDir() + u"/chart-grabber.conf"_s), iniBefore);
    EXPECT_FALSE(QFileInfo::exists(maps() + u"/Maps.ini"_s));
}

// T-0216: the cg.net debug category is off by default, so libcurl's verbose trace (CURLOPT_VERBOSE, set
// in curl_session.cpp only when the category is debug-enabled) is not printed; a rule switches it on (what
// QT_LOGGING_RULES="cg.net.debug=true" does). New code (no legacy unit: the Pascal has no curl trace).
// The Qt messages are captured by a message handler instead of read from fd 2, because Qt writes to stderr
// only when QT_LOGGING_TO_CONSOLE is set or stderr is a terminal.
namespace {
QMutex gTraceMutex;
QStringList gTrace;
void captureTrace(QtMsgType, const QMessageLogContext& context, const QString& message) {
    if (context.category != nullptr && QByteArrayView(context.category) == "cg.net") {
        const QMutexLocker locker(&gTraceMutex);
        gTrace.push_back(message);
    }
}
bool hasCurlTraceLine(const QStringList& messages) {
    return std::ranges::any_of(messages, [](const QString& m) {
        return m.contains(u"> GET /"_s) || m.contains(u"< HTTP/1.1 200"_s) || m.contains(u"Trying "_s);
    });
}
} // namespace

TEST_F(CliEndToEnd, CurlTraceIsSilentByDefaultAndShownWhenSasNetDebugIsOn) {
    servePyramid();
    const QtMessageHandler previous = qInstallMessageHandler(captureTrace);

    QLoggingCategory::setFilterRules(QString()); // no rule: the category's own default severity decides
    const Captured quiet = runCapture(job(u"download"_s, u"Template Map"_s));
    QStringList quietTrace;
    {
        const QMutexLocker locker(&gTraceMutex);
        quietTrace = gTrace;
        gTrace.clear();
    }

    // second run of the same job against a fresh cache directory: the tiles are requested again
    QLoggingCategory::setFilterRules(u"cg.net.debug=true"_s);
    const Captured loud =
        runCapture(job(u"download"_s, u"Template Map"_s, {u"--cache"_s, root_.filePath(u"cache2"_s)}));
    QStringList loudTrace;
    {
        const QMutexLocker locker(&gTraceMutex);
        loudTrace = gTrace;
        gTrace.clear();
    }
    QLoggingCategory::setFilterRules(QString());
    qInstallMessageHandler(previous);

    ASSERT_EQ(quiet.rc, 0) << qPrintable(quiet.err);
    ASSERT_EQ(loud.rc, 0) << qPrintable(loud.err);
    EXPECT_EQ(quietTrace, QStringList()) << qPrintable(quietTrace.join(u'\n'));
    EXPECT_TRUE(hasCurlTraceLine(loudTrace)) << loudTrace.size();
}

// T-0220 (app-cli.md 1.6.2): the terms-of-use warning. No FEAT; the D row of the ticket.
class CliGreySources : public CliEndToEnd {
protected:
    void SetUp() override {
        CliEndToEnd::SetUp();
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(), {.folder = u"g1.zmp"_s,
                          .guid = "{AB0000E1-0000-0000-0000-0000000000E1}",
                          .name = "Grey Base",
                          .urlBase = host + "/{z}/{x}/{y}.png",
                          .extra = "TermsOfUseWarning=https://example.org/terms\r\n"});
        writeZmp(maps(), {.folder = u"g2.zmp"_s,
                          .guid = "{AB0000E2-0000-0000-0000-0000000000E2}",
                          .name = "Grey Layer",
                          .urlBase = host + "/{z}/{x}/{y}.png",
                          .layer = true,
                          .extra = "TermsOfUseWarning=https://example.org/terms\r\n"});
    }
    static QString line(const QString& name) {
        return u"warning: \""_s + name +
               u"\": the provider's terms of use may not allow downloading tiles for offline use. Read "
               "https://example.org/terms before downloading; you are responsible for complying."_s;
    }
    static int count(const QString& err) {
        return static_cast<int>(err.count(u"terms of use may not allow"_s));
    }
    Captured runStoppedAfter(const QStringList& args, qsizetype requests) {
        std::stop_source termination;
        Captured result;
        {
            std::jthread stopper([&] {
                const auto deadline = std::chrono::steady_clock::now() + 20s;
                while (requestCount() < requests && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(2ms);
                }
                termination.request_stop();
            });
            result = runCapture(args, termination.get_token());
        }
        return result;
    }
};

// Test 11 and 12: a grey base plus a grey overlay give two lines (also with -q), printed before the first
// request (a run stopped after its first request still shows both); exit code and requests as without the
// key.
TEST_F(CliGreySources, DownloadWarnsOncePerGreySourceBeforeTheFirstRequest) {
    servePyramid(400);
    const Captured stopped =
        runStoppedAfter(job(u"download"_s, u"Grey Base"_s, {u"--overlay"_s, u"Grey Layer"_s, u"-q"_s}), 1);
    EXPECT_EQ(stopped.rc, 130) << qPrintable(stopped.err);
    EXPECT_EQ(count(stopped.err), 2) << qPrintable(stopped.err);
    EXPECT_TRUE(stopped.err.contains(line(u"Grey Base"_s) + u"\n"_s)) << qPrintable(stopped.err);
    EXPECT_TRUE(stopped.err.contains(line(u"Grey Layer"_s) + u"\n"_s)) << qPrintable(stopped.err);
}

TEST_F(CliGreySources, CompletedDownloadOfTwoGreySourcesKeepsExitZero) {
    servePyramid();
    const Captured c = runCapture(job(u"download"_s, u"Grey Base"_s, {u"--overlay"_s, u"Grey Layer"_s}));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(count(c.err), 2) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), 18); // nine tiles for each of the two sources, as without the key
}

// T-0257 (app-cli.md 1.6.2 item 2): `run` warns once per grey source too, and its exit code is unchanged.
TEST_F(CliGreySources, RunWarnsOnceForAGreySourceAndKeepsExitZero) {
    servePyramid();
    const Captured c = runCapture(job(u"run"_s, u"Grey Base"_s));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(count(c.err), 1) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(line(u"Grey Base"_s) + u"\n"_s)) << qPrintable(c.err);
}

// The same grey source as --source and as --overlay is announced once (app-cli.md 1.6.2).
TEST_F(CliGreySources, TheSameGreySourceTwiceIsAnnouncedOnce) {
    servePyramid();
    const Captured c = runCapture(job(u"download"_s, u"Grey Layer"_s, {u"--overlay"_s, u"Grey Layer"_s}));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(count(c.err), 1) << qPrintable(c.err);
}

TEST_F(CliGreySources, PlainSourceGetsNoWarning) {
    servePyramid();
    const Captured c = runCapture(job(u"download"_s, u"Template Map"_s));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(count(c.err), 0) << qPrintable(c.err);
}

// Test 13: estimate and export of a grey source print no warning.
TEST_F(CliGreySources, EstimateAndExportPrintNoWarning) {
    servePyramid();
    const Captured estimate = runCapture(job(u"estimate"_s, u"Grey Base"_s));
    EXPECT_EQ(estimate.rc, 0) << qPrintable(estimate.err);
    EXPECT_EQ(count(estimate.err), 0) << qPrintable(estimate.err);
    const Captured exported = runCapture(job(u"export"_s, u"Grey Base"_s));
    EXPECT_EQ(exported.rc, 5) << qPrintable(exported.err); // empty cache, as ExportWithAnEmptyCache
    EXPECT_EQ(count(exported.err), 0) << qPrintable(exported.err);
}

// Test 14: `resume` of a session of a grey source prints one line per session file started.
TEST_F(CliGreySources, ResumePrintsOneLinePerSessionFile) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"download"_s, u"Grey Base"_s), 2);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(first.err);
    ASSERT_TRUE(match.hasMatch()) << qPrintable(first.err);
    const Captured resumed = runCapture(QStringList{u"resume"_s, match.captured(1)} + common());
    EXPECT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    EXPECT_EQ(count(resumed.err), 1) << qPrintable(resumed.err);
    EXPECT_TRUE(resumed.err.contains(line(u"Grey Base"_s) + u"\n"_s));
}

// T-0285 (app-cli.md 1.6.5): the not-for-navigation notice. No FEAT; a key of this project.
class CliNavigationSources : public CliGreySources {
protected:
    void SetUp() override {
        CliGreySources::SetUp();
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(), {.folder = u"n1.zmp"_s,
                          .guid = "{AB0000E3-0000-0000-0000-0000000000E3}",
                          .name = "Depth Both",
                          .urlBase = host + "/{z}/{x}/{y}.png",
                          .extra = "TermsOfUseWarning=https://example.org/terms\r\nNavigationWarning=1\r\n"});
        writeZmp(maps(), {.folder = u"n2.zmp"_s,
                          .guid = "{AB0000E4-0000-0000-0000-0000000000E4}",
                          .name = "Depth Layer",
                          .urlBase = host + "/{z}/{x}/{y}.png",
                          .layer = true,
                          .extra = "NavigationWarning=true\r\n"});
    }
    static QString navLine(const QString& name) {
        return u"warning: \""_s + name +
               u"\": not for navigation. This source is not an official nautical chart; do not use it to "
               "navigate.\n"_s;
    }
    static int navCount(const QString& err) { return static_cast<int>(err.count(u"not for navigation"_s)); }
};

TEST_F(CliNavigationSources, DownloadPrintsItOncePerSourceAlsoWithQuietAfterTheTermsLine) {
    servePyramid();
    const Captured c =
        runCapture(job(u"download"_s, u"Depth Both"_s, {u"--overlay"_s, u"Depth Layer"_s, u"-q"_s}));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(navCount(c.err), 2) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(navLine(u"Depth Layer"_s))) << qPrintable(c.err);
    const QString both = line(u"Depth Both"_s) + u"\n"_s + navLine(u"Depth Both"_s);
    EXPECT_TRUE(c.err.contains(both)) << qPrintable(c.err);
}

TEST_F(CliNavigationSources, TheSameSourceAsBaseAndOverlayIsAnnouncedOnce) {
    servePyramid();
    const Captured c = runCapture(job(u"download"_s, u"Depth Layer"_s, {u"--overlay"_s, u"Depth Layer"_s}));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(navCount(c.err), 1) << qPrintable(c.err);
}

TEST_F(CliNavigationSources, RunPrintsItAndAPlainOrGreyOnlySourcePrintsNone) {
    servePyramid();
    const Captured run = runCapture(job(u"run"_s, u"Depth Layer"_s));
    EXPECT_EQ(run.rc, 0) << qPrintable(run.err);
    EXPECT_EQ(navCount(run.err), 1) << qPrintable(run.err);
    const Captured grey = runCapture(job(u"download"_s, u"Grey Base"_s));
    EXPECT_EQ(navCount(grey.err), 0) << qPrintable(grey.err);
}

TEST_F(CliNavigationSources, SourcesEstimateAndExportPrintNone) {
    servePyramid();
    const Captured sources = runCapture(QStringList{u"sources"_s} + common());
    EXPECT_EQ(sources.rc, 0) << qPrintable(sources.err);
    EXPECT_EQ(navCount(sources.err), 0) << qPrintable(sources.err);
    EXPECT_EQ(navCount(sources.out), 0) << qPrintable(sources.out);
    const Captured estimate = runCapture(job(u"estimate"_s, u"Depth Both"_s));
    EXPECT_EQ(navCount(estimate.err), 0) << qPrintable(estimate.err);
    const Captured exported = runCapture(job(u"export"_s, u"Depth Both"_s));
    EXPECT_EQ(navCount(exported.err), 0) << qPrintable(exported.err);
}

TEST_F(CliNavigationSources, ResumePrintsItOnce) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"download"_s, u"Depth Both"_s), 2);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(first.err);
    ASSERT_TRUE(match.hasMatch()) << qPrintable(first.err);
    const Captured resumed = runCapture(QStringList{u"resume"_s, match.captured(1)} + common());
    EXPECT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    EXPECT_EQ(navCount(resumed.err), 1) << qPrintable(resumed.err);
}

// T-0199 (docs/specs/app-cli.md 1.9, 6.2, 8.2): `resume`.
class CliResume : public CliEndToEnd {
protected:
    // A run of `args` that the termination stops once `requests` requests reached the server (the polling
    // thread is bounded by a deadline).
    Captured runStoppedAfter(const QStringList& args, qsizetype requests) {
        std::stop_source termination;
        Captured result;
        {
            std::jthread stopper([&] {
                const auto deadline = std::chrono::steady_clock::now() + 20s;
                while (requestCount() < requests && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(2ms);
                }
                termination.request_stop();
            });
            result = runCapture(args, termination.get_token());
        }
        return result;
    }

    // The file named by the line "to continue from this file: resume <file>".
    static QString resumeFile(const QString& err) {
        const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(err);
        EXPECT_TRUE(match.hasMatch()) << qPrintable(err);
        return match.captured(1);
    }

    // The WebP map answers the area paths with a PNG body, slowly: only the requests matter.
    void serveWebp() {
        for (const QString& path : areaPaths()) {
            HttpTestServer::Reply reply;
            reply.headers = {{"Content-Type", "image/webp"}};
            reply.body = tilePng(1, 1, 1);
            reply.delayMs = 400;
            server_.addReply(QString(path).replace(u".png"_s, u".webp"_s).toLatin1(), std::move(reply));
        }
    }

    // A session of the WebP map, from a `download --allow-webp` stopped after its first request.
    QString webpSession() {
        serveWebp();
        const Captured c = runStoppedAfter(job(u"download"_s, u"WebP Map"_s, {u"--allow-webp"_s}), 1);
        EXPECT_EQ(c.rc, 130) << qPrintable(c.err);
        return resumeFile(c.err);
    }
};

// The tile paths requested twice across the two lists (sorted), each name once per extra request.
static QStringList requestedTwice(const QStringList& firstPaths, const QStringList& secondPaths) {
    QStringList all = firstPaths + secondPaths;
    all.sort();
    QStringList repeated;
    for (qsizetype i = 1; i < all.size(); ++i) {
        if (all[i] == all[i - 1]) {
            repeated.push_back(all[i]);
        }
    }
    return repeated;
}

// Proof 1: a terminated `run`, then `resume <the file it printed>`. the deviation of T-0226 (deviations.md),
// was L-265 (the port in region/download_task_provider.cpp getTasksList): the resumed zoom (z3, 0-based 2)
// used to end one tile early, (6,3), the last in iteration order (x fastest, then y). Now every tile of the
// area is requested.
TEST_F(CliResume, ResumeContinuesRemovesTheSessionAndLeavesNoTileMissing) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"run"_s, u"Template Map"_s), 5); // the 5th is a z3 tile
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QString file = resumeFile(first.err);
    const qsizetype afterFirst = requestCount();
    const QStringList firstPaths = requested();

    QStringList resumeArgs = QStringList{u"resume"_s, file} + common();
    resumeArgs.removeAll(u"--no-progress"_s); // the progress line is what names the map (T-0254)
    const Captured resumed = runCapture(resumeArgs);
    ASSERT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    EXPECT_TRUE(resumed.err.contains(u"continuing the session of "_s)) << qPrintable(resumed.err);
    // the progress line of the resume names the session's map: `<source>  z<zoom>  ...` (app-cli.md 1.10)
    EXPECT_TRUE(resumed.err.contains(u"Template Map  z"_s)) << qPrintable(resumed.err);
    const QStringList secondPaths = requested(afterFirst);

    // No tile path twice across both runs, except the one in flight when the termination came: its request
    // is aborted, nothing is stored, the resume asks for it again (the 5th request, x 5 y 2).
    EXPECT_EQ(requestedTwice(firstPaths, secondPaths), QStringList{u"/3/5/2.png"_s});
    QStringList all = firstPaths + secondPaths;
    all.sort();
    all.removeDuplicates();
    EXPECT_EQ(all, areaPaths());
    // the fill report of the resume: 9 tiles, 9 present, 0 marked, 0 missing
    const QStringList totals = totalLines(resumed.out);
    ASSERT_EQ(totals.size(), 1) << qPrintable(resumed.out);
    EXPECT_TRUE(totals[0].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[0]);
    // the session of AutoSave/ is gone
    EXPECT_TRUE(sessionFiles().isEmpty());
    EXPECT_FALSE(QFileInfo::exists(file));

    // a following `run` requests no tile
    const qsizetype afterResume = requestCount();
    const Captured again = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(again.rc, 0) << qPrintable(again.err);
    EXPECT_EQ(requested(afterResume), QStringList());
    const QStringList againTotals = totalLines(again.out);
    ASSERT_EQ(againTotals.size(), 2) << qPrintable(again.out);
    EXPECT_TRUE(againTotals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(againTotals[1]);
    EXPECT_EQ(rows(outFile()), u"9"_s);
}

// T-0265: the fill report of `resume` is printed once per (source, zoom list) pair: two sessions of the same
// map and zooms give one report, a session with other zooms its own. The key is a QByteArray of the zoom list
// (a std::vector key trips the GCC 14 -Wstringop-overread false positive).
TEST_F(CliResume, ResumeReportsEachSourceAndZoomListOnce) {
    servePyramid(100);
    QStringList files;
    qsizetype stopAfter = 2; // the tiles of an earlier session are cached: few new requests each
    // the second session has the zooms of the first over another area (its own session file)
    const std::array<std::pair<QString, QString>, 3> sessions{
        {{u"1-3"_s, u"10,30,100,60"_s}, {u"1-3"_s, u"10,30,99,59"_s}, {u"2-3"_s, u"10,30,100,60"_s}}};
    for (const auto& [zooms, bbox] : sessions) {
        QStringList args = job(u"download"_s, u"Template Map"_s, {}, zooms);
        args.replace(args.indexOf(u"10,30,100,60"_s), bbox);
        const Captured c = runStoppedAfter(args, stopAfter);
        ASSERT_EQ(c.rc, 130) << qPrintable(c.err);
        files.push_back(resumeFile(c.err));
        stopAfter = requestCount() + 2;
    }
    files.removeDuplicates();
    ASSERT_EQ(files.size(), 3) << "the three sessions are three files";

    QStringList resumeArgs{u"resume"_s};
    resumeArgs += files;
    resumeArgs += common();
    const Captured resumed = runCapture(resumeArgs);
    ASSERT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    // zooms 1-3 once (not twice), zooms 2-3 once
    EXPECT_EQ(totalLines(resumed.out).size(), 2) << qPrintable(resumed.out);
}

// T-0291 (app-cli.md 1.6.1, 1.9): a session of a map that exists only in the second --maps folder resumes
// only when that folder is given again. No FEAT; the D row of the ticket.
TEST_F(CliResume, ResumeNeedsTheFolderThatHoldsTheSessionMap) {
    servePyramid(400);
    const QString folderA = root_.filePath(u"folderA"_s);
    const QString folderB = root_.filePath(u"folderB"_s);
    writeZmp(folderA,
             {.folder = u"a.zmp"_s,
              .guid = "{AB0000F1-0000-0000-0000-0000000000F1}",
              .name = "Only In A",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png"});
    writeZmp(folderB,
             {.folder = u"b.zmp"_s,
              .guid = "{AB0000F2-0000-0000-0000-0000000000F2}",
              .name = "Only In B",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png"});
    const QStringList rest = {u"--cache"_s, root_.filePath(u"cache"_s), u"--datadir="_s + dataDir(),
                              u"--no-progress"_s};
    const QStringList both = QStringList{u"--maps"_s, folderA, u"--maps"_s, folderB} + rest;
    const QStringList runArgs =
        QStringList{u"run"_s,     u"--source"_s, u"Only In B"_s, u"--bbox"_s, u"10,30,100,60"_s,
                    u"--zooms"_s, u"1-3"_s,      u"--out"_s,     outFile()} +
        both;
    const Captured first = runStoppedAfter(runArgs, 5);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QString file = resumeFile(first.err);

    const Captured without = runCapture(QStringList{u"resume"_s, file, u"--maps"_s, folderA} + rest);
    EXPECT_EQ(without.rc, 3) << qPrintable(without.err);

    const Captured resumed = runCapture(QStringList{u"resume"_s, file} + both);
    EXPECT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    EXPECT_TRUE(resumed.err.contains(u"continuing the session of "_s)) << qPrintable(resumed.err);
}

// T-0291 (app-cli.md 1.6.1): `run` without --maps is refused before anything is created.
TEST_F(CliResume, RunWithoutMapsIsRefusedAndCreatesNoSessionDirectory) {
    const QString freshData = root_.filePath(u"freshdata"_s);
    const Captured c =
        runCapture(QStringList{u"run"_s, u"--source"_s, u"Template Map"_s, u"--bbox"_s, u"10,30,100,60"_s,
                               u"--zooms"_s, u"1-3"_s, u"--out"_s, outFile(), u"--datadir="_s + freshData});
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(c.out.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(freshData));
    EXPECT_FALSE(QFileInfo::exists(outFile()));
}

// T-0203: `resume --progress=lines` names the map of the session file in `phase download`.
TEST_F(CliResume, ResumeProgressLinesStartsWithThePhaseOfTheMapTheFileNames) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"run"_s, u"Template Map"_s), 5);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QString file = resumeFile(first.err);
    QStringList args = QStringList{u"resume"_s, file} + common() + QStringList{u"--progress=lines"_s};
    args.removeAll(u"--no-progress"_s);
    const Captured resumed = runCapture(args);
    ASSERT_EQ(resumed.rc, 0) << qPrintable(resumed.err);
    const QString guid = u"{AB0000C3-0000-0000-0000-0000000000C3}"_s;
    const QStringList lines = resumed.err.split(u'\n', Qt::SkipEmptyParts);
    const qsizetype phase = lines.indexOf(u"phase\tdownload\t%1"_s.arg(guid));
    ASSERT_GE(phase, 0) << qPrintable(resumed.err);
    qsizetype firstProgress = -1;
    for (qsizetype i = 0; i < lines.size() && firstProgress < 0; ++i) {
        if (lines[i].startsWith(u"progress\tdownload\t%1\t"_s.arg(guid))) {
            firstProgress = i;
        }
    }
    EXPECT_GT(firstProgress, phase) << qPrintable(resumed.err);
    EXPECT_GE(lines.indexOf(u"phase\tfill\t%1"_s.arg(guid)), firstProgress);
    EXPECT_FALSE(resumed.err.contains(u'\r'));
}

// T-0226 proof 2: the same interruption, then the same `run` repeated (it continues the AutoSave session,
// app-cli 1.9): the same two assertions, and the session directory is removed at the end.
TEST_F(CliResume, RepeatedRunContinuesTheSessionAndLeavesNoTileMissing) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"run"_s, u"Template Map"_s), 5);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    EXPECT_FALSE(sessionFiles().isEmpty());
    const qsizetype afterFirst = requestCount();
    const QStringList firstPaths = requested();

    const Captured second = runCapture(job(u"run"_s, u"Template Map"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"continuing the session of "_s)) << qPrintable(second.err);
    const QStringList secondPaths = requested(afterFirst);
    EXPECT_EQ(requestedTwice(firstPaths, secondPaths), QStringList{u"/3/5/2.png"_s});
    QStringList all = firstPaths + secondPaths;
    all.sort();
    all.removeDuplicates();
    EXPECT_EQ(all, areaPaths());
    EXPECT_EQ(rows(outFile()), u"9"_s);
    const QStringList totals = totalLines(second.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(second.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[1]);
    EXPECT_TRUE(sessionFiles().isEmpty());
}

// T-0245: the job id hashes the effective zoom list: a run stopped with --zooms 1-3 continues in a run with
// --zooms 1-2 when the source stops at 2 (both are the list 1,2).
TEST_F(CliResume, MaxZoomMakesDifferentChosenZoomsOneSession) {
    servePyramid(400);
    writeMaxTwo();
    const Captured first = runStoppedAfter(job(u"run"_s, u"Max Two Map"_s, {}, u"1-3"_s), 1);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    ASSERT_FALSE(sessionFiles().isEmpty());
    const Captured second = runCapture(job(u"run"_s, u"Max Two Map"_s, {}, u"1-2"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"continuing the session of "_s)) << qPrintable(second.err);
    EXPECT_EQ(rows(outFile()), u"3"_s);
}

// T-0226 proof 3: two workers over a polygon area (not a rectangle, so the polygon iterator runs).
TEST_F(CliResume, ResumeWithTwoWorkersOverAPolygonLeavesNoTileMissing) {
    servePyramid(400);
    const QString hlg = root_.filePath(u"area.hlg"_s);
    QFile f(hlg);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[HIGHLIGHTING]\nPointLon_1=10\nPointLat_1=30\nPointLon_2=100\nPointLat_2=30\n"
            "PointLon_3=100\nPointLat_3=60\nPointLon_4=40\nPointLat_4=60\n");
    f.close();
    const QStringList args{u"run"_s, u"--source"_s, u"Template Map"_s, u"--polygon"_s,
                           hlg,      u"--zooms"_s,  u"1-3"_s,          u"--threads"_s,
                           u"2"_s,   u"--out"_s,    outFile()};
    const Captured first = runStoppedAfter(args + common(), 6);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    EXPECT_EQ(sessionFiles().size(), 4); // final.sls and a timestamped copy of each of the two workers
    const qsizetype afterFirst = requestCount();
    const QStringList firstPaths = requested();

    // the same `run` again continues both workers' sessions (`resume FILE` would start one worker only)
    const Captured second = runCapture(args + common());
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    const QStringList secondPaths = requested(afterFirst);
    // at most the tile in flight of each worker is requested a second time
    EXPECT_LE(requestedTwice(firstPaths, secondPaths).size(), 2);
    QStringList all = firstPaths + secondPaths;
    all.sort();
    all.removeDuplicates();
    EXPECT_EQ(
        all, (QStringList{u"/1/1/0.png"_s, u"/2/2/1.png"_s, u"/2/3/1.png"_s, u"/3/4/2.png"_s, u"/3/4/3.png"_s,
                          u"/3/5/2.png"_s, u"/3/5/3.png"_s, u"/3/6/2.png"_s, u"/3/6/3.png"_s}));
    const QStringList totals = totalLines(second.out);
    ASSERT_EQ(totals.size(), 2) << qPrintable(second.out);
    EXPECT_TRUE(totals[1].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[1]);
}

// Proof 2: S-4 ... S-9 are checked before any run starts: a session of the WebP map without --allow-webp is
// exit 3 naming the file, and the server sees nothing; with --allow-webp the run starts.
TEST_F(CliResume, ResumeOfAWebpSessionNeedsAllowWebpAndStartsNothingWithout) {
    const QString file = webpSession();
    const qsizetype before = requestCount();

    const Captured refused = runCapture(QStringList{u"resume"_s, file} + common());
    EXPECT_EQ(refused.rc, 3) << qPrintable(refused.err);
    EXPECT_TRUE(refused.err.contains(file)) << qPrintable(refused.err);
    EXPECT_TRUE(refused.err.contains(u"--allow-webp"_s)) << qPrintable(refused.err);
    EXPECT_EQ(requestCount(), before);
    EXPECT_FALSE(refused.err.contains(u"continuing the session"_s));

    const Captured allowed =
        runStoppedAfter(QStringList{u"resume"_s, file, u"--allow-webp"_s} + common(), before + 1);
    EXPECT_EQ(allowed.rc, 130) << qPrintable(allowed.err);
    EXPECT_GT(requestCount(), before);
}

// Proof 3: a file that cannot be loaded is exit 3 naming it; no run starts even when a good file comes with
// it (the good file first, so a check after the start would let it run).
TEST_F(CliResume, ResumeOfAnUnreadableOrNonSessionFileIsExitThreeAndStartsNothing) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"run"_s, u"Template Map"_s), 2);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QString good = resumeFile(first.err);
    const qsizetype before = requestCount();

    const QString missing = root_.filePath(u"nothing.sls"_s);
    const QString garbage = root_.filePath(u"garbage.sls"_s);
    writeStale(garbage);
    for (const QString& bad : {missing, garbage}) {
        const Captured c = runCapture(QStringList{u"resume"_s, good, bad} + common());
        EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
        EXPECT_TRUE(c.err.contains(bad)) << qPrintable(c.err);
        EXPECT_FALSE(c.err.contains(u"continuing the session"_s)) << qPrintable(c.err);
    }
    EXPECT_EQ(requestCount(), before);
    EXPECT_TRUE(QFileInfo::exists(good));
}

// Proof 4: a file outside AutoSave/ that completes stays where it is (the user's file); the session of
// AutoSave/ it was copied from is not touched.
TEST_F(CliResume, ResumeOfAFileOutsideAutoSaveLeavesTheFileInPlace) {
    servePyramid(400);
    const Captured first = runStoppedAfter(job(u"run"_s, u"Template Map"_s), 5);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QString original = resumeFile(first.err);
    const QString mine = root_.filePath(u"mine/final.sls"_s);
    ASSERT_TRUE(QDir().mkpath(root_.filePath(u"mine"_s)));
    ASSERT_TRUE(QFile::copy(original, mine));

    const Captured c = runCapture(QStringList{u"resume"_s, mine} + common());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"continuing the session of "_s)) << qPrintable(c.err);
    EXPECT_TRUE(QFileInfo::exists(mine));
    EXPECT_TRUE(QFileInfo::exists(original));
}

// T-0275 (D row, L-44, S-15): a cache folder that cannot be used stops every command at start with the
// reason, before a request is sent. A path below a regular file cannot be created by anyone, so it also holds
// as root.
TEST_F(CliEndToEnd, CacheBelowARegularFileIsRefusedAtStartForDownloadAndRun) {
    servePyramid();
    const QString file = root_.filePath(u"plainfile"_s);
    {
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    for (const QString& command : {u"download"_s, u"run"_s}) { // the others only read the cache
        const Captured c = runCapture(job(command, u"Template Map"_s, {u"--cache"_s, file + u"/sub"_s}));
        EXPECT_EQ(c.rc, 3) << qPrintable(command + u": "_s + c.err);
        EXPECT_TRUE(c.err.contains(u"error: \"Template Map\": cannot write the cache folder "_s))
            << qPrintable(command + u": "_s + c.err);
        EXPECT_TRUE(c.err.contains(file)) << qPrintable(command + u": "_s + c.err);
        EXPECT_EQ(requestCount(), 0) << qPrintable(command);
    }
}

TEST_F(CliEndToEnd, ReadOnlyCacheFolderIsRefusedAtStartWithoutARequest) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root can write to a 0555 folder";
    }
    servePyramid();
    const QString dir = root_.filePath(u"ro"_s);
    ASSERT_TRUE(QDir().mkpath(dir));
    ASSERT_TRUE(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const Captured c = runCapture(job(u"run"_s, u"Template Map"_s, {u"--cache"_s, dir}));
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: \"Template Map\": cannot write the cache folder "_s))
        << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(dir)) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), 0);
}

// T-0275 (S-15): a single-file cache (CacheType 72-76, NameInCache with an extension) lives in the file named
// by the path (prepareFileName, u_TileStorageSQLiteFileConnectionBuilder.pas:210-214); the check creates
// nothing, so the file is created by the first save and an existing one is a good cache.
TEST_F(CliEndToEnd, SingleFileCacheIsDownloadedWhetherTheFileIsThereOrNot) {
    servePyramid();
    writeZmp(maps(),
             {.folder = u"w72.zmp"_s,
              .guid = "{AB0000E8-0000-0000-0000-0000000000E8}",
              .name = "File Cache Map",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
              .nameInCache = "x.mbtiles",
              .extra = "CacheType=72\r\n"});
    for (const bool existing : {false, true}) {
        const Captured c = runCapture(job(u"download"_s, u"File Cache Map"_s));
        EXPECT_EQ(c.rc, 0) << existing << qPrintable(c.err);
        QStringList found; // below the cache folder of the type
        QDirIterator it(root_.filePath(u"cache"_s), {u"x.mbtiles"_s}, QDir::AllEntries | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            found.push_back(it.next());
        }
        ASSERT_EQ(found.size(), 1) << existing << qPrintable(found.join(u' '));
        EXPECT_TRUE(QFileInfo(found.first()).isFile()) << existing;
        EXPECT_GT(QFileInfo(found.first()).size(), 0) << existing;
    }
}

// T-0275 (S-15): CacheType=9 with an empty NameInCache keeps RAM (app_context.cpp useFileCacheForRamMaps) and
// UseMemCache=0 makes the RAM type refuse to build: the cause stops the job at start, no request is sent.
TEST_F(CliEndToEnd, RamCacheWithoutMemCacheIsRefusedAtStartWithItsCause) {
    servePyramid();
    writeZmp(maps(),
             {.folder = u"w9.zmp"_s,
              .guid = "{AB0000E9-0000-0000-0000-0000000000E9}",
              .name = "Ram Broken",
              .urlBase = "http://127.0.0.1:" + QByteArray::number(server_.port()) + "/{z}/{x}/{y}.png",
              .nameInCache = QByteArray(),
              .extra = "CacheType=9\r\nUseMemCache=0\r\n"});
    const Captured c = runCapture(job(u"download"_s, u"Ram Broken"_s));
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: \"Ram Broken\": cannot open the cache: "_s)) << qPrintable(c.err);
    EXPECT_EQ(requestCount(), 0);
}

// The cache folder of the map exists but is read-only (mkpath succeeds, so only the writability test sees
// it).
TEST_F(CliEndToEnd, ExistingReadOnlyMapCacheFolderIsRefusedAtStart) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root can write to a 0555 folder";
    }
    servePyramid();
    const QString cache = root_.filePath(u"cache3"_s);
    ASSERT_EQ(runCapture(job(u"download"_s, u"Template Map"_s, {u"--cache"_s, cache})).rc, 0);
    const qsizetype before = requestCount();
    QStringList dirs;
    QDirIterator it(cache, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        dirs.push_back(it.next());
    }
    ASSERT_FALSE(dirs.isEmpty());
    const auto readOnly = QFileDevice::ReadOwner | QFileDevice::ExeOwner;
    for (const QString& d : dirs) {
        ASSERT_TRUE(QFile::setPermissions(d, readOnly));
    }
    const Captured c = runCapture(job(u"download"_s, u"Template Map"_s, {u"--cache"_s, cache}));
    for (const QString& d : dirs) {
        QFile::setPermissions(d, readOnly | QFileDevice::WriteOwner);
    }
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: \"Template Map\": cannot write the cache folder "_s))
        << qPrintable(c.err);
    EXPECT_EQ(requestCount(), before);
}
