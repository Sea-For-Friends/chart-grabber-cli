// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit): tests of `probe` (T-0194, docs/specs/app-cli.md 3.13, ticket proofs 4 and 5).
// Rig: the HttpTestServer and the four ZMP folders of cli_test_util.h, the real download subsystem, worker
// threads and SQLite cache. Tests that start threads are TSan-relevant.
//
// The probed tile is always the one under lon 100, lat 40 at zoom 3 (0-based): Web Mercator tile x 6, y 3
// (floor((100+180)/360*8) = 6; y = 3.03), i.e. the paths "/3/6/3.png" and "/alt/3/6/3.png".
#include "cg/app/app_context.h"
#include "cg/app/job.h"
#include "cg/app/probe.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <stop_token>

#include "../maptype/maptype_test_util.h"
#include "../net/http_test_server.h"
#include "cli_test_util.h"

using namespace cg::app::test;
using namespace Qt::StringLiterals;
using cg::app::ProbeStatus;
using cg::net::test::HttpTestServer;
using namespace std::chrono_literals;

namespace {

QByteArray tilePng() {
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(qRgb(30, 90, 150));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

HttpTestServer::Reply pngReply(int delayMs = 0) {
    HttpTestServer::Reply reply;
    reply.headers = {{"Content-Type", "image/png"}};
    reply.body = tilePng();
    reply.delayMs = delayMs;
    return reply;
}

HttpTestServer::Reply notFoundReply() {
    HttpTestServer::Reply reply;
    reply.status = 404;
    reply.reason = "Not Found";
    reply.headers = {{"Content-Type", "text/plain"}};
    reply.body = "no";
    return reply;
}

QStringList simplifiedLines(const QString& text) {
    QStringList result;
    for (const QString& line : text.split(u'\n', Qt::SkipEmptyParts)) {
        result.push_back(line.simplified());
    }
    return result;
}

// The table line of the source called `name` (the GUID column comes first).
QString rowOf(const QString& out, const QString& name) {
    for (const QString& line : simplifiedLines(out)) {
        if (line.contains(u" "_s + name + u" "_s)) {
            return line;
        }
    }
    return {};
}

int filesBelow(const QString& dir) {
    int n = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

class ProbeTest : public ::testing::Test {
protected:
    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
        ASSERT_TRUE(server_.start());
        ASSERT_TRUE(QDir().mkpath(maps()));
        ASSERT_TRUE(QDir().mkpath(dataDir()));
        writeZmpSet(maps(), server_.port());
        QFile ini(dataDir() +
                  u"/chart-grabber.conf"_s); // no system proxy lookup, as in cli_end_to_end_test.cpp
        ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
        ini.write("[Internet]\r\n[Internet_Proxy]\r\nUseIEProxySettings=0\r\n");
    }

    QString maps() const { return root_.filePath(u"Maps"_s); }
    QString cache() const { return root_.filePath(u"cache"_s); }
    QString dataDir() const { return root_.filePath(u"data"_s); }

    QStringList probe(const QStringList& more = {}) const {
        return QStringList{u"probe"_s,         u"--maps"_s,      maps(),
                           u"--cache"_s,       cache(),          u"--datadir="_s + dataDir(),
                           u"--no-progress"_s, u"--at=100,40"_s, u"--probe-zoom=3"_s} +
               more;
    }

    // Every good source answers the probed tile; the paths of `bad` answer 404 instead.
    void serve(const QStringList& bad = {}) {
        for (const QByteArray& path : {QByteArray("/3/6/3.png"), QByteArray("/alt/3/6/3.png")}) {
            server_.addReply(path, bad.contains(QString::fromLatin1(path)) ? notFoundReply() : pngReply());
        }
    }

    QStringList requested() const {
        QStringList paths;
        for (const QByteArray& request : server_.requestLog()) {
            paths.push_back(QString::fromLatin1(request.split(' ').value(1)));
        }
        paths.sort();
        return paths;
    }

    QTemporaryDir root_;
    HttpTestServer server_;
};

// Proof 4: good sources are `ok <bytes> <content type>`, a WebP source is skipped without a request, the exit
// code is 0; the rows are in name order.
TEST_F(ProbeTest, GoodSourcesAreOkAndTheWebpSourceIsSkippedWithoutARequest) {
    serve();
    const Captured c = runCapture(probe());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList lines = simplifiedLines(c.out);
    ASSERT_EQ(lines.size(), 5) << qPrintable(c.out); // header + four sources
    EXPECT_EQ(lines[0], u"GUID name result"_s);
    const QString ok = u" ok %1 image/png"_s.arg(tilePng().size());
    EXPECT_EQ(lines[1], u"{AB0000A1-0000-0000-0000-0000000000A1} Alt Path Map"_s + ok);
    EXPECT_EQ(lines[2], u"{AB0000D4-0000-0000-0000-0000000000D4} Scripted Layer"_s + ok);
    EXPECT_EQ(lines[3], u"{AB0000C3-0000-0000-0000-0000000000C3} Template Map"_s + ok);
    EXPECT_TRUE(lines[4].startsWith(u"{AB0000B2-0000-0000-0000-0000000000B2} WebP Map skipped: "_s))
        << qPrintable(lines[4]);
    EXPECT_TRUE(lines[4].contains(u"WebP"_s, Qt::CaseInsensitive));
    // three requests, one per probed source, the tile under --at; the WebP map sent none
    EXPECT_EQ(requested(), (QStringList{u"/3/6/3.png"_s, u"/3/6/3.png"_s, u"/alt/3/6/3.png"_s}));
}

// A server path that answers 404 gives the not-exists class and exit 4; the others stay ok.
TEST_F(ProbeTest, ANotFoundTileIsTheNotExistsClassAndExitFour) {
    serve({u"/alt/3/6/3.png"_s});
    const Captured c = runCapture(probe());
    EXPECT_EQ(c.rc, 4) << qPrintable(c.err);
    const QString alt = rowOf(c.out, u"Alt Path Map"_s);
    EXPECT_TRUE(alt.contains(u" Alt Path Map not exists"_s)) << qPrintable(alt);
    EXPECT_TRUE(rowOf(c.out, u"Template Map"_s).contains(u" ok "_s)) << qPrintable(c.out);
}

// --filter limits the rows (case-insensitive, name contains) and so the requests.
TEST_F(ProbeTest, FilterLimitsTheRowsAndTheRequests) {
    serve();
    const Captured c = runCapture(probe({u"--filter=tEMPLATE"_s}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList lines = simplifiedLines(c.out);
    ASSERT_EQ(lines.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(lines[1].contains(u" Template Map ok "_s)) << qPrintable(lines[1]);
    EXPECT_EQ(requested(), QStringList{u"/3/6/3.png"_s});
}

// Skipped rows are not failures: only a skipped source is probed, exit 0.
TEST_F(ProbeTest, ASkippedSourceDoesNotFailTheRun) {
    serve();
    const Captured c = runCapture(probe({u"--filter=webp"_s}));
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(simplifiedLines(c.out).size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(rowOf(c.out, u"WebP Map"_s).contains(u" skipped: "_s));
    EXPECT_TRUE(requested().isEmpty());
}

// Proof 5: the help lists probe and its options, probe accepts all of them, and the help says the tile lands
// in the cache, which is what the cache directory shows.
TEST_F(ProbeTest, HelpNamesTheOptionsProbeAcceptsAndTheCacheWrite) {
    const Captured help = runCapture({u"--help"_s});
    ASSERT_EQ(help.rc, 0);
    EXPECT_TRUE(
        help.out.contains(u"chart-grabber-cli probe     [--filter TEXT] [--at LON,LAT] [--probe-zoom Z]"_s));
    EXPECT_TRUE(help.out.contains(u"probe writes the probed tile into the cache."_s));

    serve();
    ASSERT_FALSE(QFileInfo::exists(cache()));
    const Captured c = runCapture(probe({u"--filter=template"_s}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_GT(filesBelow(cache()), 0); // the help says the probed tile is written; the directory shows it
}

// The wait for a result is bounded: a server slower than the timeout is a failed row, not a hang.
TEST_F(ProbeTest, ASlowServerIsATimeoutRowNotAHang) {
    server_.addReply("/3/6/3.png", pngReply(2000));
    cg::app::AppOptions options;
    options.arguments = {u"--datadir="_s + dataDir()};
    options.mapsDirs = {maps()};
    options.cacheDir = cache();
    auto app = cg::app::AppContext::create(options);
    ASSERT_TRUE(app.has_value());
    ASSERT_TRUE((*app)->loadMaps().has_value());
    cg::app::Job job;
    job.command = cg::app::Command::Probe;
    job.filter = u"template"_s;
    job.probeAt = {100, 40};
    job.probeZoom = 3;
    const auto rows = cg::app::probeSources(**app, job, {}, 200ms);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].status, ProbeStatus::Failed);
    EXPECT_TRUE(rows[0].result.startsWith(u"timeout: "_s)) << qPrintable(rows[0].result);
    (*app)->requestClose();
}

// A termination request before the first source: no row, no request, exit 130.
TEST_F(ProbeTest, TerminationBeforeTheFirstSourceIsExit130WithoutRequests) {
    serve();
    std::stop_source stop;
    stop.request_stop();
    const Captured c = runCapture(probe(), stop.get_token());
    EXPECT_EQ(c.rc, 130) << qPrintable(c.err);
    EXPECT_TRUE(requested().isEmpty());
}

} // namespace
