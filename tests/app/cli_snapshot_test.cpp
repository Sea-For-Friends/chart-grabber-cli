// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit): end-to-end tests of the snapshot sources of T-0243
// (docs/specs/weather-sources.md 3.5, 4.1-4.4, 5.1, 5.3, 5.4; ticket acceptance 1-10). No FEAT tag (T-0243:
// Features none). Legacy facts the design rests on, pinned here: the version of a map is a request setting
// that the tile is saved under (u_ProviderTilesDownload.pas:354-355); a saved session keeps VersionDownload /
// VersionCheck / VersionCheckOther (u_DownloadSession.pas:252-254) and its resume takes VersionDownload
// (u_DownloadSession.pas:373-397).
//
// Rig: HttpTestServer serves the index at /index.json (scripted: first reply frame A, second frame B, the
// last repeats) and the tiles of the area under /t/A2/... and /t/B/... with different bytes. The ZMP "Radar"
// is a layer with SnapshotIndexType=json-last on a SQLite versioned cache (CacheType=71), the ZMP "Template
// Map" is a static source. Area: lon 10..100, lat 30..60 at zooms 1-3 = nine tiles. The real worker,
// transport and export threads run: the tests are TSan-relevant.
#include "cg/core/config_provider_ini.h"
#include "cg/core/platform/qt_compat.h"
#include "cg/storage/sqlite/sqlite_db.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <stop_token>
#include <thread>

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

const QByteArray kGuidRadar = "{AB0000F1-0000-0000-0000-0000000000F1}";
const QString kGuidRadarText = u"{AB0000F1-0000-0000-0000-0000000000F1}"_s;

// The nine tiles of the area (0-based zoom in the path), as the request paths of the template.
QStringList areaPaths(const QString& prefix = {}) {
    QStringList paths = {u"/1/1/0.png"_s, u"/2/2/1.png"_s, u"/2/3/1.png"_s, u"/3/4/2.png"_s, u"/3/5/2.png"_s,
                         u"/3/6/2.png"_s, u"/3/4/3.png"_s, u"/3/5/3.png"_s, u"/3/6/3.png"_s};
    for (QString& path : paths) {
        path.prepend(prefix);
    }
    paths.sort();
    return paths;
}

// A small PNG; `shade` tells the snapshots (and the tiles) apart.
QByteArray tilePng(int shade, int z, int x, int y) {
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(qRgb(shade, 20 + x * 30 + z, 10 + y * 50));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

constexpr int kShadeA2 = 40;
constexpr int kShadeB = 200;

// Index answers (acceptance rig): A = two frames, the last is /A2 (20:10 UTC); B = a third frame /B (21:10
// UTC).
const QByteArray kIndexA =
    R"({"radar":{"past":[{"time":1792094400,"path":"/A"},{"time":1792095000,"path":"/A2"}]}})";
const QByteArray kIndexB =
    R"({"radar":{"past":[{"time":1792094400,"path":"/A"},{"time":1792095000,"path":"/A2"},)"
    R"({"time":1792098600,"path":"/B"}]}})";

HttpTestServer::Reply jsonReply(const QByteArray& body, int delayMs = 0) {
    HttpTestServer::Reply reply;
    reply.headers = {{"Content-Type", "application/json"}};
    reply.body = body;
    reply.delayMs = delayMs;
    return reply;
}

HttpTestServer::Reply statusReply(int code, QByteArray reason) {
    HttpTestServer::Reply reply;
    reply.status = code;
    reply.reason = std::move(reason);
    reply.headers = {{"Content-Type", "text/plain"}};
    reply.body = "no";
    return reply;
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

QString rowCount(const QString& file) {
    return QString::fromLatin1(blob(file, "SELECT count(*) FROM tiles"));
}

// The tile bytes at one TMS row of the MBTiles (zoom 3, column 4, tile y 2 = row 5).
QByteArray tileZ3x4y2(const QString& file) {
    return blob(file, "SELECT tile_data FROM tiles WHERE zoom_level=3 AND tile_column=4 AND tile_row=5");
}

QStringList lines(const QString& text) {
    QStringList result;
    for (const QString& line : text.split(u'\n', Qt::SkipEmptyParts)) {
        result.push_back(line.simplified());
    }
    return result;
}

class CliSnapshot : public ::testing::Test {
protected:
    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
        ASSERT_TRUE(server_.start());
        ASSERT_TRUE(QDir().mkpath(maps()));
        ASSERT_TRUE(QDir().mkpath(outDir()));
        ASSERT_TRUE(QDir().mkpath(dataDir()));
        writeRadar(u"CacheType=71\r\n"_s.toLatin1());
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(), {.folder = u"s2.zmp"_s,
                          .guid = kGuidTemplate,
                          .name = "Template Map",
                          .urlBase = host + "/{z}/{x}/{y}.png"});
        QFile ini(dataDir() + u"/chart-grabber.conf"_s); // no system proxy lookup
        ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
        ini.write("[Internet]\r\n[Internet_Proxy]\r\nUseIEProxySettings=0\r\n");
    }

    // The snapshot source; `cacheAndKeys` replaces the CacheType line (and may add keys).
    void writeRadar(const QByteArray& cacheLine, const QByteArray& indexType = "json-last") {
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(),
                 {.folder = u"s1.zmp"_s,
                  .guid = kGuidRadar,
                  .name = "Radar",
                  .urlBase = host + "/t{ver}/{z}/{x}/{y}.png",
                  .layer = true,
                  .extra = "SnapshotIndex=" + host + "/index.json\r\nSnapshotIndexType=" + indexType +
                           "\r\nSnapshotItem=radar.past\r\nSnapshotVersionField=path\r\n" + cacheLine});
    }

    QString maps() const { return root_.filePath(u"Maps"_s); }
    QString outDir() const { return root_.filePath(u"out"_s); }
    QString outFile() const { return outDir() + u"/base.mbtiles"_s; }
    QString dataDir() const { return root_.filePath(u"data"_s); }
    QString storeFile() const { return dataDir() + u"/Snapshots/"_s + kGuidRadarText + u".conf"_s; }
    QString indexUrl() const { return QString::fromLatin1(server_.url("/index.json")); }

    // The index answers in order; the last repeats.
    void serveIndex(const QList<QByteArray>& answers) {
        for (const QByteArray& answer : answers) {
            server_.addReply("/index.json", jsonReply(answer));
        }
    }

    // The nine tiles of `version` ("/A2" -> /t/A2/...) in the shade of the snapshot.
    void serveTiles(const QByteArray& version, int shade, int delayMs = 0) {
        for (const QString& path : areaPaths()) {
            const QStringList parts = path.split(u'/', Qt::SkipEmptyParts); // z, x, y.png
            HttpTestServer::Reply reply;
            reply.headers = {{"Content-Type", "image/png"}};
            reply.body = tilePng(shade, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
            reply.delayMs = delayMs;
            server_.addReply("/t" + version + path.toLatin1(), std::move(reply));
        }
    }

    void serveStatic() {
        for (const QString& path : areaPaths()) {
            const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
            HttpTestServer::Reply reply;
            reply.headers = {{"Content-Type", "image/png"}};
            reply.body = tilePng(100, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
            server_.addReply(path.toLatin1(), std::move(reply));
        }
    }

    QStringList common() const {
        return {
            u"--maps"_s,       maps(), u"--cache"_s, root_.filePath(u"cache"_s), u"--datadir="_s + dataDir(),
            u"--no-progress"_s};
    }

    // `command` over the area with the overlay "Radar" (alone, unless `more` names a source).
    QStringList job(const QString& command, const QStringList& more = {}) const {
        return QStringList{command,      u"--overlay"_s, u"Radar"_s, u"--bbox"_s, u"10,30,100,60"_s,
                           u"--zooms"_s, u"1-3"_s,       u"--out"_s, outFile()} +
               common() + more;
    }

    QStringList requestPaths(qsizetype from = 0) const {
        QStringList paths;
        const QList<QByteArray> log = server_.requestLog();
        for (qsizetype i = from; i < log.size(); ++i) {
            const QList<QByteArray> parts = log[i].left(log[i].indexOf("\r\n")).split(' '); // GET /p HTTP/1.1
            paths.push_back(QString::fromLatin1(parts.value(1)));
        }
        return paths;
    }
    // Sorted, without the index requests.
    QStringList tileRequests(qsizetype from = 0) const {
        QStringList paths = requestPaths(from);
        paths.removeAll(u"/index.json"_s);
        paths.sort();
        return paths;
    }
    qsizetype indexRequests(qsizetype from = 0) const { return requestPaths(from).count(u"/index.json"_s); }

    // A run of `args` that the termination stops once `requests` requests reached the server (a polling
    // thread bounded by a deadline: the server has no notification, as in cli_end_to_end_test.cpp).
    Captured runStoppedAfter(const QStringList& args, qsizetype requests) {
        std::stop_source termination;
        Captured result;
        {
            std::jthread stopper([&] {
                const auto deadline = std::chrono::steady_clock::now() + 20s;
                while (server_.requestCount() < requests && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(2ms);
                }
                termination.request_stop();
            });
            result = runCapture(args, termination.get_token());
        }
        return result;
    }

    QStringList sessionFiles() const {
        QStringList found;
        QDirIterator it(dataDir(), {u"*.sls"_s}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            found.push_back(it.next());
        }
        return found;
    }

    QByteArray storeText() const {
        QFile f(storeFile());
        EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << qPrintable(storeFile());
        return f.readAll();
    }

    QTemporaryDir root_;
    HttpTestServer server_;
};

} // namespace

// Acceptance 1: `run`: the index once, every tile of the snapshot once, the MBTiles hold the tiles of /A2
// (the last entry of the index), the store file holds the version, the time and the resolve time.
TEST_F(CliSnapshot, RunTakesTheLastFrameOnceAndStoresIt) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2);
    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    const Captured c = runCapture(job(u"run"_s));
    const QDateTime after = QDateTime::currentDateTimeUtc().addSecs(1);
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(indexRequests(), 1);
    EXPECT_EQ(tileRequests(), areaPaths(u"/t/A2"_s)); // each tile once, nothing of another frame
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeA2, 3, 4, 2));

    const QList<QByteArray> store = storeText().split('\n');
    ASSERT_EQ(store.size(), 6) << storeText().constData(); // five lines and the trailing LF; no CR
    EXPECT_EQ(store[0], "[Snapshot]");
    EXPECT_EQ(store[1], "Version=/A2");
    EXPECT_EQ(store[2], "Time=2026-10-15T20:10:00Z");
    EXPECT_EQ(store[3], "DateOnly=0");
    ASSERT_TRUE(store[4].startsWith("Resolved=")) << store[4].constData();
    const QDateTime resolved =
        QDateTime::fromString(QString::fromLatin1(store[4].mid(9)), u"yyyy-MM-ddTHH:mm:ssZ"_s);
    ASSERT_TRUE(resolved.isValid()) << store[4].constData();
    const QDateTime resolvedUtc = cg::core::platform::utcDateTime(resolved.date(), resolved.time());
    EXPECT_GE(resolvedUtc, before);
    EXPECT_LE(resolvedUtc, after);
    EXPECT_TRUE(store[5].isEmpty());
}

// Acceptance 2 and 3 (T-0240: a second run an hour later gets newer tiles): the second `run` resolves frame
// /B and requests every tile of it once, none of /A2; the MBTiles hold only the B bytes; the store file says
// /B. A third run with the index still /B asks for no tile (the same frame, cached).
TEST_F(CliSnapshot, SecondRunTakesTheNewerFrameAndThirdRunOfTheSameFrameRequestsNoTile) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    serveTiles("/B", kShadeB);
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    const qsizetype afterFirst = server_.requestCount();

    const Captured second = runCapture(job(u"run"_s, {u"--overwrite"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_EQ(indexRequests(afterFirst), 1);
    EXPECT_EQ(tileRequests(afterFirst), areaPaths(u"/t/B"_s));
    EXPECT_EQ(rowCount(outFile()), u"9"_s); // one frame in the file, not the two
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeB, 3, 4, 2));
    EXPECT_TRUE(storeText().contains("Version=/B\n"));
    EXPECT_TRUE(storeText().contains("Time=2026-10-15T21:10:00Z\n"));

    const qsizetype afterSecond = server_.requestCount();
    const Captured third = runCapture(job(u"run"_s, {u"--overwrite"_s}));
    ASSERT_EQ(third.rc, 0) << qPrintable(third.err);
    EXPECT_EQ(indexRequests(afterSecond), 1);
    EXPECT_EQ(tileRequests(afterSecond), QStringList()); // the frame is cached
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeB, 3, 4, 2));
}

// Acceptance 4: `export` alone exports exactly the stored frame without any request (the older frame was
// removed from the cache by the second run, T-0246: it must not leak into the file); without the store file
// it is exit 3.
TEST_F(CliSnapshot, ExportUsesTheStoredFrameWithoutARequestAndNeedsTheStoreFile) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    serveTiles("/B", kShadeB);
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    ASSERT_EQ(runCapture(job(u"run"_s, {u"--overwrite"_s})).rc, 0);
    const qsizetype before = server_.requestCount();

    const Captured exported = runCapture(job(u"export"_s, {u"--overwrite"_s}));
    ASSERT_EQ(exported.rc, 0) << qPrintable(exported.err);
    EXPECT_EQ(server_.requestCount(), before);
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeB, 3, 4, 2));
    // the time of the stored frame, read back from the file: the line after the name and the written suffix
    EXPECT_TRUE(lines(exported.out).contains(u"snapshot 2026-10-15 21:10 UTC"_s)) << qPrintable(exported.out);
    EXPECT_TRUE(exported.out.contains(u"  snapshot 2026-10-15 21:10 UTC\n"_s)) << qPrintable(exported.out);

    // the store file names the older frame: export takes that one, not the newest in the cache. T-0246: the
    // second run removed the older frame from the cache, so the export finds no tile (exit 5)
    ASSERT_TRUE(QFile::remove(storeFile()));
    {
        QFile f(storeFile());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("[Snapshot]\nVersion=/"
                "A2\nTime=2026-10-15T20:10:00Z\nDateOnly=0\nResolved=2026-10-15T20:18:04Z\n");
    }
    const Captured older = runCapture(job(u"export"_s, {u"--overwrite"_s}));
    EXPECT_EQ(older.rc, 5) << qPrintable(older.err);
    EXPECT_TRUE(older.err.contains(u"error: no tiles of Radar in the cache for this area"_s))
        << qPrintable(older.err);
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeB, 3, 4, 2)); // the file of the previous export stays

    ASSERT_TRUE(QFile::remove(storeFile()));
    const Captured none = runCapture(job(u"export"_s, {u"--overwrite"_s}));
    EXPECT_EQ(none.rc, 3);
    EXPECT_TRUE(none.err.contains(u"error: \"Radar\": no snapshot of this source was downloaded yet; run "
                                  "download or run first"_s))
        << qPrintable(none.err);
    EXPECT_EQ(server_.requestCount(), before);
}

// Acceptance 5: `estimate` takes no snapshot (no request, not even the index) and counts no tile as present
// although the cache holds the tiles of a downloaded frame: the figures are assumed (20 KB a tile).
TEST_F(CliSnapshot, EstimateSendsNothingAndCountsNoTileAsPresent) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2);
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    const qsizetype before = server_.requestCount();

    const Captured text = runCapture(job(u"estimate"_s));
    ASSERT_EQ(text.rc, 0) << qPrintable(text.err);
    EXPECT_EQ(server_.requestCount(), before);
    const QStringList out = lines(text.out);
    ASSERT_GE(out.size(), 3) << qPrintable(text.out);
    EXPECT_EQ(out[0], u"Radar"_s);
    EXPECT_EQ(out[1], u"snapshot: taken when downloading"_s);
    EXPECT_FALSE(text.out.contains(u"snapshot 2026"_s)); // no frame is named

    const Captured tsv = runCapture(job(u"estimate"_s, {u"--format=tsv"_s}));
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    const QStringList records = tsv.out.split(u'\n', Qt::SkipEmptyParts);
    // no snapshot record; per zoom: tiles, bbox tiles, share present 0, share marked 0, bytes tiles x 20480,
    // assumed 1, bytes on disk the same
    ASSERT_EQ(records.size(), 3) << qPrintable(tsv.out);
    const QString guid = kGuidRadarText;
    EXPECT_EQ(records[0], u"estimate\t%1\t1\t1\t1\t0.0000\t0.0000\t20480\t1\t20480"_s.arg(guid));
    EXPECT_EQ(records[1], u"estimate\t%1\t2\t2\t2\t0.0000\t0.0000\t40960\t1\t40960"_s.arg(guid));
    EXPECT_EQ(records[2], u"estimate\t%1\t3\t6\t6\t0.0000\t0.0000\t122880\t1\t122880"_s.arg(guid));
    EXPECT_EQ(server_.requestCount(), before);
}

// Acceptance 6, text: the snapshot line right after the source's name line, before its table, and the
// `written:` suffix.
TEST_F(CliSnapshot, RunTextNamesTheSnapshotAfterTheNameAndAtTheWrittenLine) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2);
    const Captured c = runCapture(job(u"run"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList out = lines(c.out);
    ASSERT_GE(out.size(), 3) << qPrintable(c.out);
    EXPECT_EQ(out[0], u"Radar"_s);
    EXPECT_EQ(out[1], u"snapshot 2026-10-15 20:10 UTC"_s);
    const QRegularExpression written(
        u"^written: .*base\\.mbtiles {2}9 tiles {2}\\S.*  snapshot 2026-10-15 20:10 UTC$"_s,
        QRegularExpression::MultilineOption);
    EXPECT_TRUE(written.match(c.out).hasMatch()) << qPrintable(c.out);
}

// Acceptance 6, TSV: the `snapshot` record before the estimate records of its source, the `written` record
// has the time in field 5; the static source's field 5 is empty and it gets no snapshot record.
TEST_F(CliSnapshot, TsvRunHasTheSnapshotRecordAndTheTimeInTheWrittenRecord) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2);
    serveStatic();
    const Captured c = runCapture(job(u"run"_s, {u"--source"_s, u"Template Map"_s, u"--format=tsv"_s}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList records = c.out.split(u'\n', Qt::SkipEmptyParts);
    const QString staticGuid = QString::fromLatin1(kGuidTemplate);

    qsizetype snapshotAt = -1;
    qsizetype firstRadarEstimate = -1;
    qsizetype lastStaticEstimate = -1;
    QStringList written;
    for (qsizetype i = 0; i < records.size(); ++i) {
        const QStringList f = records[i].split(u'\t');
        if (f[0] == u"snapshot"_s) {
            EXPECT_EQ(snapshotAt, -1); // exactly one
            snapshotAt = i;
            EXPECT_EQ(records[i], u"snapshot\t%1\t2026-10-15T20:10:00Z\t/A2"_s.arg(kGuidRadarText));
        } else if (f[0] == u"estimate"_s && f[1] == kGuidRadarText && firstRadarEstimate < 0) {
            firstRadarEstimate = i;
        } else if (f[0] == u"estimate"_s && f[1] == staticGuid) {
            lastStaticEstimate = i;
        } else if (f[0] == u"written"_s) {
            written.push_back(records[i]);
        }
    }
    ASSERT_GE(snapshotAt, 0) << qPrintable(c.out);
    ASSERT_GE(firstRadarEstimate, 0);
    EXPECT_EQ(firstRadarEstimate, snapshotAt + 1); // right before the estimate records of its source
    EXPECT_LT(lastStaticEstimate, snapshotAt);     // the static source (the base) is first and has none
    ASSERT_EQ(written.size(), 2) << qPrintable(c.out);
    const QStringList staticFields = written[0].split(u'\t');
    const QStringList radarFields = written[1].split(u'\t');
    ASSERT_EQ(staticFields.size(), 6) << qPrintable(written[0]);
    ASSERT_EQ(radarFields.size(), 6) << qPrintable(written[1]);
    EXPECT_EQ(staticFields[1], staticGuid);
    EXPECT_EQ(staticFields[5], QString());
    EXPECT_EQ(radarFields[1], kGuidRadarText);
    EXPECT_EQ(radarFields[5], u"2026-10-15T20:10:00Z"_s);
}

// Acceptance 7: the index does not answer usably: exit 4, `cannot take the snapshot of "Radar": <text>`, no
// tile request (the index only), no store file, no session, no output file. A 500 is a load error: legacy
// reports status 0 for a result without server respond (u_SimpleHttpDownloader.pas:113-121), spec 3.4 `no
// answer from`.
class CliSnapshotResolve : public CliSnapshot {
protected:
    void expectFailure(const HttpTestServer::Reply& reply, const QString& text) {
        server_.addReply("/index.json", reply);
        const Captured run = runCapture(job(u"run"_s));
        EXPECT_EQ(run.rc, 4) << qPrintable(run.err);
        EXPECT_TRUE(run.err.contains(u"error: cannot take the snapshot of \"Radar\": "_s + text + u"\n"_s))
            << qPrintable(run.err);
        EXPECT_EQ(requestPaths(), QStringList{u"/index.json"_s});
        EXPECT_FALSE(QFileInfo::exists(storeFile()));
        EXPECT_TRUE(sessionFiles().isEmpty());
        EXPECT_FALSE(QDir(dataDir() + u"/AutoSave"_s).exists());
        EXPECT_FALSE(QFileInfo::exists(outFile()));
    }
};

TEST_F(CliSnapshotResolve, ServerErrorAnswerIsNoAnswer) {
    expectFailure(statusReply(500, "Internal Server Error"), u"no answer from %1"_s.arg(indexUrl()));
}

TEST_F(CliSnapshotResolve, NotFoundAnswerNamesTheStatus) {
    expectFailure(statusReply(404, "Not Found"), u"HTTP 404 from %1"_s.arg(indexUrl()));
}

TEST_F(CliSnapshotResolve, AnArrayInsteadOfAnObjectNamesTheUrl) {
    expectFailure(jsonReply("[]"), u"%1: not a JSON object"_s.arg(indexUrl()));
}

TEST_F(CliSnapshotResolve, AnEmptyBodyIsRefused) {
    expectFailure(jsonReply(""), u"the answer of %1 is empty"_s.arg(indexUrl()));
}

// 3.5: the termination during the resolve (the index answers slowly) is exit 130, not exit 4: nothing is
// downloaded, no store file, no output.
TEST_F(CliSnapshot, TerminationDuringTheResolveIsExit130) {
    server_.addReply("/index.json", jsonReply(kIndexA, 3000));
    serveTiles("/A2", kShadeA2);
    const Captured c = runStoppedAfter(job(u"run"_s), 1);
    EXPECT_EQ(c.rc, 130) << qPrintable(c.err);
    EXPECT_FALSE(c.err.contains(u"cannot take the snapshot"_s)) << qPrintable(c.err);
    EXPECT_EQ(tileRequests(), QStringList());
    EXPECT_FALSE(QFileInfo::exists(storeFile()));
    EXPECT_FALSE(QFileInfo::exists(outFile()));
}

TEST_F(CliSnapshot, ARefusedStoreFileDirectoryIsExitFourBeforeAnyTileRequest) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2);
    {
        // a FILE where the directory Snapshots/ must be
        QFile f(dataDir() + u"/Snapshots"_s);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    const Captured c = runCapture(job(u"run"_s));
    EXPECT_EQ(c.rc, 4) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: cannot write "_s)) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"Snapshots"_s)) << qPrintable(c.err);
    EXPECT_EQ(tileRequests(), QStringList()); // before the first tile request
    EXPECT_FALSE(QFileInfo::exists(outFile()));
}

// Acceptance 8: the first run is stopped after some tiles of /A2 (server delay), then `run` with the index at
// /B: the saved session is of another snapshot and is not continued; with the index still at /A2 it is.
TEST_F(CliSnapshot, ASessionOfAnotherSnapshotIsDiscardedAndOfTheSameIsContinued) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2, 400);
    serveTiles("/B", kShadeB);
    const Captured first = runStoppedAfter(job(u"run"_s), 4); // the index and three tiles
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    ASSERT_FALSE(sessionFiles().isEmpty());
    const qsizetype afterFirst = server_.requestCount();
    EXPECT_GE(afterFirst, 4);

    const Captured second = runCapture(job(u"run"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"the saved session of \"Radar\" is of another snapshot (/A2); starting "
                                    "afresh"_s))
        << qPrintable(second.err);
    EXPECT_FALSE(second.err.contains(u"continuing the session of "_s)) << qPrintable(second.err);
    EXPECT_EQ(tileRequests(afterFirst), areaPaths(u"/t/B"_s)); // every B tile once, none of A2
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeB, 3, 4, 2));
    EXPECT_TRUE(sessionFiles().isEmpty());
}

TEST_F(CliSnapshot, ASessionOfTheSameSnapshotIsContinuedWithoutRepeatingTiles) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2, 400);
    const Captured first = runStoppedAfter(job(u"run"_s), 4);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    ASSERT_FALSE(sessionFiles().isEmpty());
    const qsizetype afterFirst = server_.requestCount();
    const QStringList firstPaths = requestPaths();
    const QString inFlight = firstPaths.last(); // its request was aborted: asked again

    const Captured second = runCapture(job(u"run"_s));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"continuing the session of "_s)) << qPrintable(second.err);
    EXPECT_FALSE(second.err.contains(u"another snapshot"_s)) << qPrintable(second.err);
    QStringList all = firstPaths + requestPaths(afterFirst);
    all.removeAll(u"/index.json"_s);
    all.sort();
    QStringList repeated;
    for (qsizetype i = 1; i < all.size(); ++i) {
        if (all[i] == all[i - 1]) {
            repeated.push_back(all[i]);
        }
    }
    EXPECT_EQ(repeated, QStringList{inFlight});
    all.removeDuplicates();
    EXPECT_EQ(all, areaPaths(u"/t/A2"_s));
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
}

// Acceptance 9 (u_DownloadSession.pas:252-254): the .sls saved by the interrupted run holds the snapshot in
// VersionDownload and VersionCheck and VersionCheckOther=0 (a legacy resume sees one frame only).
TEST_F(CliSnapshot, TheSavedSessionHoldsTheSnapshotAsItsVersion) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2, 400);
    const Captured first = runStoppedAfter(job(u"run"_s), 4);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const QStringList files = sessionFiles();
    ASSERT_FALSE(files.isEmpty());
    for (const QString& file : files) {
        SCOPED_TRACE(qPrintable(file));
        const auto ini = cg::core::ConfigProviderIni::open(file);
        ASSERT_TRUE(ini.has_value());
        const auto section = (*ini)->subItem(u"Session");
        ASSERT_TRUE(section != nullptr);
        EXPECT_EQ(section->readString(u"VersionDownload", u"none"_s), u"/A2"_s);
        EXPECT_EQ(section->readString(u"VersionCheck", u"none"_s), u"/A2"_s);
        EXPECT_FALSE(section->readBool(u"VersionCheckOther", true));
    }
}

// Acceptance 10 (S-14): a snapshot source on a cache that keeps no versions, or with invalid keys, is
// refused: the `sources` verdict and `run` / `estimate` / `export` exit 3 with the same text. A static source
// on the same cache is not refused.
TEST_F(CliSnapshot, RefusalS14ShowsInTheVerdictAndStopsEveryJobCommand) {
    struct Case {
        const char* name;
        QByteArray cacheLine;
        QByteArray indexType;
        QString text;
    };
    const std::vector<Case> cases = {
        {"sasFolderCache", "CacheType=2\r\n", "json-last",
         u"\"Radar\": a snapshot source needs a cache that keeps tile versions (SQLite 71); its cache "
         u"type is 2"_s},
        {"xmlType", "CacheType=71\r\n", "xml",
         u"\"Radar\": the snapshot keys are invalid: SnapshotIndexType=xml is not json-last, wms-default, "
         u"wmts-default, wmts-domain or none-refetch"_s},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        writeRadar(c.cacheLine, c.indexType);
        const Captured sources = runCapture(QStringList{u"sources"_s, u"--format=tsv"_s} + common());
        ASSERT_EQ(sources.rc, 0) << qPrintable(sources.err);
        QString verdict;
        for (const QString& line : sources.out.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList f = line.split(u'\t');
            if (f.value(1) == kGuidRadarText) {
                verdict = f.value(7);
            } else if (f.value(1) == QString::fromLatin1(kGuidTemplate)) {
                EXPECT_EQ(f.value(7), u"ok"_s); // the static source is not refused
            }
        }
        EXPECT_EQ(verdict, c.text);
        for (const QString& command : {u"run"_s, u"download"_s, u"estimate"_s, u"export"_s}) {
            SCOPED_TRACE(qPrintable(command));
            const Captured run = runCapture(job(command));
            EXPECT_EQ(run.rc, 3) << qPrintable(run.err);
            EXPECT_TRUE(run.err.contains(u"error: "_s + c.text + u"\n"_s)) << qPrintable(run.err);
        }
        EXPECT_EQ(server_.requestCount(), 0);
    }
}

// A static source is never resolved and gets no store file.
TEST_F(CliSnapshot, AStaticSourceIsNeverResolvedAndNeverGetsAStoreFile) {
    serveStatic();
    const Captured c =
        runCapture(QStringList{u"run"_s, u"--source"_s, u"Template Map"_s, u"--bbox"_s, u"10,30,100,60"_s,
                               u"--zooms"_s, u"1-3"_s, u"--out"_s, outFile()} +
                   common());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(server_.requestCount(), 9);
    EXPECT_FALSE(QDir(dataDir() + u"/Snapshots"_s).exists());
    EXPECT_FALSE(c.out.contains(u"snapshot"_s)) << qPrintable(c.out);
}

// ---------------------------------------------------------------------------------------------------------
// T-0244 (docs/specs/weather-sources.md 3.5, 4.3, 5.4; ticket acceptance 1-8): `resume` continues the frame
// the session names, `probe` resolves, `sources --format=tsv` fields 11 (refresh) and 12 (license). No FEAT
// tag (Features none). Legacy fact: the resume takes VersionDownload of the saved session
// (u_DownloadSession.pas:373-397); the port refuses an empty one (spec 4.3), legacy falls back to the base
// version (:373-378).
namespace {

class CliSnapshotResume : public CliSnapshot {
protected:
    // An interrupted `run` over frame /A2 (the index answers A for the first request, B after): the
    // `.sls` it printed. The store file holds /A2 and its time.
    QString interruptedSession() {
        serveIndex({kIndexA, kIndexB});
        serveTiles("/A2", kShadeA2, 400);
        const Captured first = runStoppedAfter(job(u"run"_s), 4);
        EXPECT_EQ(first.rc, 130) << qPrintable(first.err);
        const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(first.err);
        EXPECT_TRUE(match.hasMatch()) << qPrintable(first.err);
        return match.captured(1);
    }

    // A copy of `file` in `name` below the test root, `from` replaced by `to` in its text.
    QString copyWith(const QString& file, const QString& name, const QByteArray& from, const QByteArray& to) {
        QFile in(file);
        EXPECT_TRUE(in.open(QIODevice::ReadOnly)) << qPrintable(file);
        QByteArray text = in.readAll();
        EXPECT_TRUE(text.contains(from)) << text.constData();
        text.replace(from, to);
        const QString target = root_.filePath(name);
        QFile out(target);
        EXPECT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(text);
        return target;
    }

    QStringList resumeArgs(const QStringList& files, const QStringList& more = {}) const {
        return QStringList{u"resume"_s} + files + common() + more;
    }

    void writeStore(const QByteArray& version, const QByteArray& time) {
        ASSERT_TRUE(QDir().mkpath(dataDir() + u"/Snapshots"_s));
        QFile f(storeFile());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("[Snapshot]\nVersion=" + version + "\nTime=" + time + "\nDateOnly=0\nResolved=" + time +
                "\n");
    }

    QStringList totalLines(const QString& out) const {
        QStringList result;
        for (const QString& line : lines(out)) {
            if (line.startsWith(u"total "_s)) {
                result.push_back(line);
            }
        }
        return result;
    }
};

} // namespace

// Acceptance 1: the index now answers B, the resume asks no index and only the tiles of /A2; the time comes
// from the store file (its Version equals); the fill report counts the nine tiles of /A2.
TEST_F(CliSnapshotResume, ContinuesTheFrameOfTheSessionAndPrintsTheStoredTime) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    // the time text is the store file's, not derived from the version "/A2": mark it apart
    writeStore("/A2", "2026-10-15T20:11:00Z");
    const qsizetype before = server_.requestCount();
    const QStringList firstPaths = requestPaths();

    const Captured c = runCapture(resumeArgs({file}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(indexRequests(before), 0);
    const QStringList asked = requestPaths(before);
    ASSERT_FALSE(asked.isEmpty());
    for (const QString& path : asked) {
        EXPECT_TRUE(path.startsWith(u"/t/A2/"_s)) << qPrintable(path);
    }
    EXPECT_TRUE(lines(c.out).contains(u"Radar: snapshot 2026-10-15 20:11 UTC"_s)) << qPrintable(c.out);
    // before the fill report (the name line of the report comes after)
    EXPECT_LT(c.out.indexOf(u"Radar: snapshot"_s), c.out.indexOf(u"total "_s));
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 1) << qPrintable(c.out);
    EXPECT_TRUE(totals[0].startsWith(u"total 9 9 0 0 "_s)) << qPrintable(totals[0]);
    // every tile of /A2 was asked in one of the two runs
    QStringList all = firstPaths + asked;
    all.removeAll(u"/index.json"_s);
    all.removeDuplicates();
    all.sort();
    EXPECT_EQ(all, areaPaths(u"/t/A2"_s));
}

// The fill report after the resume counts the session's frame (applySnapshot): the cache holds the complete
// frame /B from an earlier run; /A2 lacks the tile (3,6,3) on the server (404), so it is not present in /A2
// although a lookup of any version would find /B's copy.
TEST_F(CliSnapshotResume, TheFillReportCountsOnlyTheFrameOfTheSession) {
    serveIndex({kIndexB, kIndexA});
    serveTiles("/B", kShadeB);
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    for (const QString& path : areaPaths()) {
        if (path == u"/3/6/3.png"_s) {
            continue;
        }
        const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
        HttpTestServer::Reply reply;
        reply.headers = {{"Content-Type", "image/png"}};
        reply.body = tilePng(kShadeA2, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
        reply.delayMs = 400;
        server_.addReply("/t/A2" + path.toLatin1(), std::move(reply));
    }
    const Captured first = runStoppedAfter(job(u"run"_s, {u"--overwrite"_s}), server_.requestCount() + 4);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(first.err);
    ASSERT_TRUE(match.hasMatch()) << qPrintable(first.err);

    const Captured c = runCapture(resumeArgs({match.captured(1)}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList totals = totalLines(c.out);
    ASSERT_EQ(totals.size(), 1) << qPrintable(c.out);
    EXPECT_TRUE(totals[0].startsWith(u"total 9 8 "_s)) << qPrintable(totals[0]); // 9 tiles, 8 present
}

// The store file of another snapshot is not the time of this one (Version differs): "/A2" is no time, so
// unknown; the store file is not rewritten by the resume.
TEST_F(CliSnapshotResume, AStoreFileOfAnotherVersionGivesNoTimeAndIsNotRewritten) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    writeStore("/B", "2026-10-15T21:10:00Z");
    const QByteArray stored = storeText();
    const Captured c = runCapture(resumeArgs({file}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_TRUE(lines(c.out).contains(u"Radar: snapshot unknown"_s)) << qPrintable(c.out);
    EXPECT_EQ(storeText(), stored);
}

// Acceptance 2: without the store file the version "/A2" is not a time: unknown, and the TSV `snapshot`
// record has the empty time field (before the fill records); the resume writes no store file.
TEST_F(CliSnapshotResume, WithoutTheStoreFileTheTimeIsUnknownAndNoFileIsWritten) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    ASSERT_TRUE(QFile::remove(storeFile()));
    const QString copy = copyWith(file, u"again.sls"_s, "VersionDownload=/A2", "VersionDownload=/A2");

    const Captured text = runCapture(resumeArgs({file}));
    ASSERT_EQ(text.rc, 0) << qPrintable(text.err);
    EXPECT_TRUE(lines(text.out).contains(u"Radar: snapshot unknown"_s)) << qPrintable(text.out);
    EXPECT_FALSE(QFileInfo::exists(storeFile()));

    const Captured tsv = runCapture(resumeArgs({copy}, {u"--format=tsv"_s}));
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    const QStringList records = tsv.out.split(u'\n', Qt::SkipEmptyParts);
    const qsizetype at = records.indexOf(u"snapshot\t%1\t\t/A2"_s.arg(kGuidRadarText));
    ASSERT_GE(at, 0) << qPrintable(tsv.out);
    for (qsizetype i = 0; i < records.size(); ++i) {
        if (records[i].startsWith(u"fill\t"_s)) {
            EXPECT_LT(at, i) << qPrintable(tsv.out);
        }
    }
    EXPECT_EQ(records.count(u"snapshot\t%1\t\t/A2"_s.arg(kGuidRadarText)), 1);
    EXPECT_FALSE(QFileInfo::exists(storeFile()));
}

// TSV with the store file: the time ISO in field 3 (the store file's), the version in field 4.
TEST_F(CliSnapshotResume, TsvSnapshotRecordHasTheStoredTimeAndTheVersion) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    writeStore("/A2", "2026-10-15T20:11:00Z");
    const Captured tsv = runCapture(resumeArgs({file}, {u"--format=tsv"_s}));
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    EXPECT_TRUE(tsv.out.split(u'\n', Qt::SkipEmptyParts)
                    .contains(u"snapshot\t%1\t2026-10-15T20:11:00Z\t/A2"_s.arg(kGuidRadarText)))
        << qPrintable(tsv.out);
    EXPECT_FALSE(tsv.out.contains(u"Radar: snapshot"_s)); // the text line is not in TSV mode
}

// Acceptance 3: a WMS rig (the EUMETView capabilities text served as the index, URL /t/{ver}/...): no store
// file, the version is a time and is parsed (spec 4.3: "WMS / GIBS versions are times").
TEST_F(CliSnapshotResume, AVersionThatIsATimeIsParsedWhenNoStoreFileHasIt) {
    QFile xml(cg::maptype::test::fixtureDir() +
              u"/app/snapshot/eumetview-mtg_fd-rgb_geocolour-capabilities.xml"_s);
    ASSERT_TRUE(xml.open(QIODevice::ReadOnly));
    HttpTestServer::Reply capabilities;
    capabilities.headers = {{"Content-Type", "text/xml"}};
    capabilities.body = xml.readAll();
    server_.addReply("/index.json", capabilities);
    const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
    writeZmp(maps(),
             {.folder = u"s1.zmp"_s,
              .guid = kGuidRadar,
              .name = "Radar",
              .urlBase = host + "/t/{ver}/{z}/{x}/{y}.png",
              .layer = true,
              .extra = "SnapshotIndex=" + host +
                       "/index.json\r\nSnapshotIndexType=wms-default\r\nSnapshotItem=rgb_geocolour\r\n"
                       "CacheType=71\r\n"});
    serveTiles("/2026-10-15T19:50:00Z", kShadeA2, 400);
    std::stop_source termination;
    Captured first;
    {
        std::jthread stopper([&] {
            const auto deadline = std::chrono::steady_clock::now() + 20s;
            while (server_.requestCount() < 4 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            termination.request_stop();
        });
        first = runCapture(job(u"run"_s), termination.get_token());
    }
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(first.err);
    ASSERT_TRUE(match.hasMatch()) << qPrintable(first.err);
    ASSERT_TRUE(QFile::remove(storeFile()));
    const qsizetype before = server_.requestCount();

    const Captured c = runCapture(resumeArgs({match.captured(1)}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_TRUE(lines(c.out).contains(u"Radar: snapshot 2026-10-15 19:50 UTC"_s)) << qPrintable(c.out);
    EXPECT_EQ(indexRequests(before), 0);
    for (const QString& path : requestPaths(before)) {
        EXPECT_TRUE(path.startsWith(u"/t/2026-10-15T19:50:00Z/"_s)) << qPrintable(path);
    }
    EXPECT_FALSE(QFileInfo::exists(storeFile()));
}

// Acceptance 4: a session whose VersionDownload is empty names no snapshot: exit 3 with the file in the text,
// no request, nothing downloaded.
TEST_F(CliSnapshotResume, ASessionWithoutAVersionIsRefused) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    const QString edited = copyWith(file, u"empty.sls"_s, "VersionDownload=/A2", "VersionDownload=");
    const qsizetype before = server_.requestCount();
    const Captured c = runCapture(resumeArgs({edited}));
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: %1: the session names no snapshot of \"Radar\"\n"_s.arg(edited)))
        << qPrintable(c.err);
    EXPECT_EQ(server_.requestCount(), before);
    EXPECT_TRUE(c.out.isEmpty()) << qPrintable(c.out);
}

// Acceptance 5: two files of one map that name /A2 and /B: exit 3 naming the SECOND file (in either order),
// no request. Two files of the same version are one snapshot: accepted, the snapshot line printed once.
TEST_F(CliSnapshotResume, TwoVersionsOfOneMapRefuseAtTheSecondFileAndTheSameVersionIsOne) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    const QString a2 = copyWith(file, u"a2.sls"_s, "VersionDownload=/A2", "VersionDownload=/A2");
    const QString b = copyWith(file, u"b.sls"_s, "VersionDownload=/A2", "VersionDownload=/B");
    const qsizetype before = server_.requestCount();

    const Captured ab = runCapture(resumeArgs({a2, b}));
    EXPECT_EQ(ab.rc, 3) << qPrintable(ab.err);
    EXPECT_TRUE(ab.err.contains(u"error: %1: the sessions name two snapshots of \"Radar\"\n"_s.arg(b)))
        << qPrintable(ab.err);
    const Captured ba = runCapture(resumeArgs({b, a2}));
    EXPECT_EQ(ba.rc, 3) << qPrintable(ba.err);
    EXPECT_TRUE(ba.err.contains(u"error: %1: the sessions name two snapshots of \"Radar\"\n"_s.arg(a2)))
        << qPrintable(ba.err);
    EXPECT_EQ(server_.requestCount(), before);

    const QString same = copyWith(file, u"same.sls"_s, "VersionDownload=/A2", "VersionDownload=/A2");
    const Captured twice = runCapture(resumeArgs({a2, same}));
    ASSERT_EQ(twice.rc, 0) << qPrintable(twice.err);
    EXPECT_EQ(lines(twice.out).count(u"Radar: snapshot 2026-10-15 20:10 UTC"_s), 1) << qPrintable(twice.out);
}

// S-14 at resume: the source was changed to a cache without versions after the session was saved; the
// refusal of `run` (same text) comes before anything else, with the file in front.
TEST_F(CliSnapshotResume, ASourceThatBecameInvalidIsRefusedWithTheS14Text) {
    const QString file = interruptedSession();
    ASSERT_FALSE(file.isEmpty());
    writeRadar("CacheType=2\r\n");
    const qsizetype before = server_.requestCount();
    const Captured c = runCapture(resumeArgs({file}));
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(c.err.contains(u"error: %1: \"Radar\": a snapshot source needs a cache that keeps tile "
                               "versions (SQLite 71); its cache type is 2\n"_s.arg(file)))
        << qPrintable(c.err);
    EXPECT_EQ(server_.requestCount(), before);
}

// Acceptance 8: the session of a static source resumes as before: no snapshot line or record.
TEST_F(CliSnapshotResume, AStaticSourceSessionResumesWithoutASnapshotLine) {
    serveStatic();
    const QStringList args = {u"run"_s,     u"--source"_s, u"Template Map"_s, u"--bbox"_s, u"10,30,100,60"_s,
                              u"--zooms"_s, u"1-3"_s,      u"--out"_s,        outFile()};
    const Captured first = runStoppedAfter(args + common(), 1);
    ASSERT_EQ(first.rc, 130) << qPrintable(first.err);
    const auto match = QRegularExpression(u"resume (\\S+final\\.sls)"_s).match(first.err);
    ASSERT_TRUE(match.hasMatch()) << qPrintable(first.err);
    const QString copy = copyWith(match.captured(1), u"static.sls"_s, "[Session]", "[Session]");

    const Captured text = runCapture(resumeArgs({match.captured(1)}));
    ASSERT_EQ(text.rc, 0) << qPrintable(text.err);
    EXPECT_FALSE(text.out.contains(u"snapshot"_s)) << qPrintable(text.out);
    const Captured tsv = runCapture(resumeArgs({copy}, {u"--format=tsv"_s}));
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    EXPECT_FALSE(tsv.out.contains(u"snapshot"_s)) << qPrintable(tsv.out);
    EXPECT_FALSE(QDir(dataDir() + u"/Snapshots"_s).exists());
}

// ---- probe (acceptance 6)

namespace {

class CliSnapshotProbe : public CliSnapshot {
protected:
    QStringList probeArgs() const {
        return QStringList{u"probe"_s, u"--filter"_s, u"Radar"_s, u"--at=100,40"_s, u"--probe-zoom=3"_s} +
               common();
    }
};

} // namespace

TEST_F(CliSnapshotProbe, ResolvesOnceAsksOneTileOfTheFrameAndWritesNoStoreFile) {
    serveIndex({kIndexA});
    serveTiles("/A2", kShadeA2);
    const Captured c = runCapture(probeArgs());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requestPaths(), (QStringList{u"/index.json"_s, u"/t/A2/3/6/3.png"_s}));
    const QStringList out = lines(c.out);
    ASSERT_EQ(out.size(), 2) << qPrintable(c.out);
    EXPECT_EQ(out[1],
              u"%1 Radar ok %2 image/png"_s.arg(kGuidRadarText).arg(tilePng(kShadeA2, 3, 6, 3).size()))
        << qPrintable(c.out);
    EXPECT_FALSE(QFileInfo::exists(storeFile()));
}

// The resolve's failure is that row's text (spec 3.4 in the prefix of 3.5): a 5xx has no server respond, 404
// does; the tile is not asked; the exit code is 4 (a failed row); no store file.
class CliSnapshotProbeFailure : public CliSnapshotProbe {
protected:
    void expectRow(int status, const char* reason, const QString& text) {
        server_.addReply("/index.json", statusReply(status, reason));
        const Captured run = runCapture(probeArgs());
        EXPECT_EQ(run.rc, 4) << qPrintable(run.err);
        const QStringList out = lines(run.out);
        ASSERT_EQ(out.size(), 2) << qPrintable(run.out);
        EXPECT_EQ(out[1], u"%1 Radar cannot take the snapshot of \"Radar\": %2"_s.arg(kGuidRadarText, text))
            << qPrintable(run.out);
        EXPECT_EQ(requestPaths(), QStringList{u"/index.json"_s});
        EXPECT_FALSE(QFileInfo::exists(storeFile()));
    }
};

TEST_F(CliSnapshotProbeFailure, ServerErrorIsNoAnswer) {
    expectRow(500, "Internal Server Error", u"no answer from %1"_s.arg(indexUrl()));
}

TEST_F(CliSnapshotProbeFailure, NotFoundNamesTheStatus) {
    expectRow(404, "Not Found", u"HTTP 404 from %1"_s.arg(indexUrl()));
}

// A termination during the resolve is the row `interrupted` and exit 130, not a failure text.
TEST_F(CliSnapshotProbe, TerminationDuringTheResolveIsAnInterruptedRow) {
    server_.addReply("/index.json", jsonReply(kIndexA, 3000));
    const Captured c = runStoppedAfter(probeArgs(), 1);
    EXPECT_EQ(c.rc, 130) << qPrintable(c.err);
    const QStringList out = lines(c.out);
    ASSERT_EQ(out.size(), 2) << qPrintable(c.out);
    EXPECT_EQ(out[1], u"%1 Radar interrupted"_s.arg(kGuidRadarText)) << qPrintable(c.out);
    EXPECT_EQ(tileRequests(), QStringList());
}

// S-14 joins the skip chain of probe (as the `sources` verdict): no request, a skipped row, exit 0.
TEST_F(CliSnapshotProbe, ASourceRefusedByS14IsSkippedWithoutARequest) {
    writeRadar("CacheType=2\r\n");
    const Captured c = runCapture(probeArgs());
    EXPECT_EQ(c.rc, 0) << qPrintable(c.err);
    const QStringList out = lines(c.out);
    ASSERT_EQ(out.size(), 2) << qPrintable(c.out);
    EXPECT_EQ(
        out[1],
        u"%1 Radar skipped: \"Radar\": a snapshot source needs a cache that keeps tile versions (SQLite 71); "
        u"its cache type is 2"_s.arg(kGuidRadarText))
        << qPrintable(c.out);
    EXPECT_EQ(server_.requestCount(), 0);
}

// A static source is probed without the index.
TEST_F(CliSnapshotProbe, AStaticSourceAsksNoIndex) {
    serveStatic();
    const Captured c = runCapture(
        QStringList{u"probe"_s, u"--filter"_s, u"Template"_s, u"--at=100,40"_s, u"--probe-zoom=3"_s} +
        common());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(requestPaths(), QStringList{u"/3/6/3.png"_s});
}

// ---- sources (acceptance 7)

// Fields 11 and 12 of the `source` records: the snapshot source says `snapshot` and its License (non-ASCII),
// the static one `static` and nothing; a source without License has the empty field; an invalid definition
// counts as `snapshot`. The text table has neither.
TEST_F(CliSnapshot, SourcesTsvHasTheRefreshAndTheLicenseFields) {
    struct Case {
        const char* name;
        QByteArray cacheLine;
        QByteArray indexType;
        QString refresh;
        QString license;
    };
    const std::vector<Case> cases = {
        {"licensed", "CacheType=71\r\nLicense=Данные RainViewer\r\n", "json-last", u"snapshot"_s,
         u"Данные RainViewer"_s},
        {"yearLicense", "CacheType=71\r\nLicense=Data {year}\r\n", "json-last", u"snapshot"_s,
         u"Data "_s + QString::number(QDateTime::currentDateTimeUtc().date().year())}, // T-0249
        {"noLicense", "CacheType=71\r\n", "json-last", u"snapshot"_s, QString()},
        {"invalidDefinition", "CacheType=71\r\n", "xml", u"snapshot"_s, QString()},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        writeRadar(c.cacheLine, c.indexType);
        const Captured tsv = runCapture(QStringList{u"sources"_s, u"--format=tsv"_s} + common());
        ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
        int seen = 0;
        for (const QString& line : tsv.out.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList f = line.split(u'\t');
            ASSERT_EQ(f.size(), 15) << qPrintable(line);
            if (f[1] == kGuidRadarText) {
                EXPECT_EQ(f[11], c.refresh);
                EXPECT_EQ(f[12], c.license);
                ++seen;
            } else if (f[1] == QString::fromLatin1(kGuidTemplate)) {
                EXPECT_EQ(f[11], u"static"_s);
                EXPECT_EQ(f[12], QString());
                ++seen;
            }
        }
        EXPECT_EQ(seen, 2);
        const Captured text = runCapture(QStringList{u"sources"_s} + common());
        ASSERT_EQ(text.rc, 0) << qPrintable(text.err);
        EXPECT_FALSE(text.out.contains(u"static"_s)) << qPrintable(text.out);
        EXPECT_FALSE(text.out.contains(u"RainViewer"_s)) << qPrintable(text.out);
        EXPECT_EQ(text.out.split(u'\n', Qt::SkipEmptyParts).size(), 3); // header and two sources
    }
}

// ---------------------------------------------------------------------------------------------------------
// T-0246 (docs/specs/weather-sources.md 9; ticket acceptance 1-5): after a completed download of a snapshot
// source the tiles of every other version in the job's area and zooms are deleted. No FEAT tag (Features
// none). Inherits Src/RegionProcess/Delete/u_ThreadDeleteTiles.pas ProcessRegion. The cache is read with
// SQL on its region files (SELECT v, count(*) FROM t GROUP BY v).
namespace {

class CliSnapshotPrune : public CliSnapshot {
protected:
    // version text ("" = the empty version) -> number of rows, over every SQLite file below the cache dir
    QMap<QString, int> versionCounts() const {
        QMap<QString, int> counts;
        QDirIterator it(root_.filePath(u"cache"_s), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            auto db = SqliteDb::open(it.next(), SqliteDb::OpenMode::ReadOnly);
            if (!db) {
                continue;
            }
            auto stmt = db->prepare("SELECT v, count(*) FROM t GROUP BY v");
            if (!stmt) {
                continue; // not a region file
            }
            for (;;) {
                auto step = stmt->step();
                if (!step || *step != SqliteStmt::StepResult::Row) {
                    break;
                }
                const QString v = stmt->columnText(0);
                counts[v == u"0"_s ? QString() : v] += stmt->columnInt(1);
            }
        }
        return counts;
    }

    // The first run took frame /A2; the index now answers B (frame /B) for the next run.
    void firstRunA2() {
        serveIndex({kIndexA, kIndexB});
        serveTiles("/A2", kShadeA2);
        serveTiles("/B", kShadeB);
        const Captured first = runCapture(job(u"run"_s));
        ASSERT_EQ(first.rc, 0) << qPrintable(first.err);
        ASSERT_EQ(versionCounts(), (QMap<QString, int>{{u"/A2"_s, 9}}));
    }
};

} // namespace

// Acceptance 1 and 5: the second run (frame /B) leaves only /B for every tile of the area; with -v stderr
// says how many tiles of the older frame went; the first run (nothing older) removed none.
TEST_F(CliSnapshotPrune, TheSecondRunRemovesTheOlderFrameAndVerboseCountsIt) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    serveTiles("/B", kShadeB);
    const Captured first = runCapture(job(u"run"_s, {u"-v"_s}));
    ASSERT_EQ(first.rc, 0) << qPrintable(first.err);
    EXPECT_TRUE(first.err.contains(u"removed 0 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(first.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/A2"_s, 9}}));

    const Captured second = runCapture(job(u"run"_s, {u"-v"_s, u"--overwrite"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"removed 9 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/B"_s, 9}}));
}

// Acceptance 5: without -v nothing is printed about the removal.
TEST_F(CliSnapshotPrune, WithoutVerboseNothingIsSaidAboutTheRemoval) {
    firstRunA2();
    const Captured second = runCapture(job(u"run"_s, {u"--overwrite"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_FALSE(second.err.contains(u"older snapshots"_s)) << qPrintable(second.err);
    EXPECT_FALSE(second.out.contains(u"older snapshots"_s)) << qPrintable(second.out);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/B"_s, 9}})); // it was removed, quietly
}

// Acceptance 2: the second run covers the zooms 1-2 only: the six tiles of zoom 3 keep frame /A2, the three
// tiles of zooms 1-2 hold /B only (3 tiles removed).
TEST_F(CliSnapshotPrune, TilesOutsideTheJobsZoomsStay) {
    firstRunA2();
    QStringList args = job(u"run"_s, {u"-v"_s, u"--overwrite"_s});
    args[args.indexOf(u"1-3"_s)] = u"1-2"_s;
    const Captured second = runCapture(args);
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"removed 3 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/A2"_s, 6}, {u"/B"_s, 3}}));
}

// Acceptance 3: the download of frame /B stalls (the server answers 500 for every tile, --stall-timeout 2):
// exit 4 and nothing of /A2 is removed, no word about the removal.
TEST_F(CliSnapshotPrune, ADownloadThatDidNotCompleteRemovesNothing) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    for (const QString& path : areaPaths()) {
        server_.addReply("/t/B" + path.toLatin1(), statusReply(500, "Internal Server Error"));
    }
    const Captured second =
        runCapture(job(u"run"_s, {u"-v"_s, u"--overwrite"_s, u"--stall-timeout"_s, u"2"_s}));
    EXPECT_EQ(second.rc, 4) << qPrintable(second.err);
    EXPECT_FALSE(second.err.contains(u"older snapshots"_s)) << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/A2"_s, 9}}));
}

// Acceptance 4: a static source gets no prune. The static "Template Map" shares the cache of "Radar"
// (NameInCache) so that a prune of it would find the /A2 rows: they stay, and nothing is said.
TEST_F(CliSnapshotPrune, AStaticSourceIsNeverPruned) {
    firstRunA2();
    const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
    writeZmp(maps(), {.folder = u"s2.zmp"_s,
                      .guid = kGuidTemplate,
                      .name = "Template Map",
                      .urlBase = host + "/{z}/{x}/{y}.png",
                      .nameInCache = "caches1", // the folder of "Radar" (cache + folder stem)
                      .extra = "CacheType=71\r\n"});
    serveStatic();
    const Captured c =
        runCapture(QStringList{u"run"_s, u"--source"_s, u"Template Map"_s, u"--bbox"_s, u"10,30,100,60"_s,
                               u"--zooms"_s, u"1-3"_s, u"--out"_s, outFile(), u"-v"_s, u"--overwrite"_s} +
                   common());
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_FALSE(c.err.contains(u"older snapshots"_s)) << qPrintable(c.err);
    const QMap<QString, int> counts = versionCounts();
    EXPECT_EQ(counts.value(u"/A2"_s), 9); // the rows of Radar's cache file are all still there
    EXPECT_EQ(counts, (QMap<QString, int>{{u"/A2"_s, 9}})); // Radar's cache file only: no row added or lost
}

// T-0351: the frame /B is byte-identical to /A2: the SQLite save skips every tile as the same as a stored one
// (legacy SkipIfSameAsPrev), so /B has no rows. The prune moves the /A2 tiles to /B instead of deleting them:
// the cache keeps all nine tiles (under /B), the export has them all and a third run requests no tile.
TEST_F(CliSnapshotPrune, IdenticalTilesOfTheNewFrameKeepTheOlderFrameAndTheExportHasEveryTile) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    serveTiles("/B", kShadeA2);
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    QFile::remove(outFile());
    // --strict: the prune runs before the fill report, so the moved tiles count as present (exit 0)
    const Captured second = runCapture(job(u"run"_s, {u"-v"_s, u"--overwrite"_s, u"--strict"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"removed 0 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/B"_s, 9}}));
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeA2, 3, 4, 2));
    const qsizetype before = server_.requestLog().size();
    QFile::remove(outFile());
    const Captured third = runCapture(job(u"run"_s, {u"--overwrite"_s}));
    ASSERT_EQ(third.rc, 0) << qPrintable(third.err);
    EXPECT_TRUE(tileRequests(before).isEmpty()) << qPrintable(tileRequests(before).join(u' '));
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/B"_s, 9}}));
}

// T-0351: the exact rule. A tile the new frame lacks (404, no TNE mark) is deleted with its old frame, it is
// not filled from the old frame; the other eight (identical, skipped by the save) move to the new frame.
TEST_F(CliSnapshotPrune, ATileTheNewFrameLacksIsDeletedWithItsOldFrameAndTheIdenticalOnesStay) {
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    const QStringList paths = areaPaths();
    for (qsizetype i = 1; i < paths.size(); ++i) { // the first tile is not served in /B: 404
        const QStringList parts = paths[i].split(u'/', Qt::SkipEmptyParts);
        HttpTestServer::Reply reply;
        reply.headers = {{"Content-Type", "image/png"}};
        reply.body = tilePng(kShadeA2, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
        server_.addReply("/t/B" + paths[i].toLatin1(), std::move(reply));
    }
    ASSERT_EQ(runCapture(job(u"run"_s)).rc, 0);
    QFile::remove(outFile());
    const Captured second = runCapture(job(u"run"_s, {u"-v"_s, u"--overwrite"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"removed 1 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/B"_s, 8}}));
    EXPECT_EQ(rowCount(outFile()), u"8"_s);
}

// T-0352: the mixed case on a real SQLite cache. The tiles of the empty version (a static source of the same
// cache, shade 100) are byte-identical to the new frame /B; the older frame /A2 (shade 40) has versioned
// rows. The save of /B is skipped as identical to the empty-version rows; the prune must move THAT row (shade
// 100, size and CRC of the refused data), not the newest listed /A2 tile (an old frame shown as current), and
// the empty-version row must go before the save (the save is skipped while it exists).
TEST_F(CliSnapshotPrune, TheIdenticalEmptyVersionRowIsMovedNotTheNewestListedOne) {
    const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
    writeZmp(maps(), {.folder = u"s2.zmp"_s,
                      .guid = kGuidTemplate,
                      .name = "Template Map",
                      .urlBase = host + "/{z}/{x}/{y}.png",
                      .nameInCache = "caches1", // the folder of "Radar" (cache + folder stem)
                      .extra = "CacheType=71\r\n"});
    serveStatic();
    const Captured stat =
        runCapture(QStringList{u"run"_s, u"--source"_s, u"Template Map"_s, u"--bbox"_s, u"10,30,100,60"_s,
                               u"--zooms"_s, u"1-3"_s, u"--out"_s, outFile()} +
                   common());
    ASSERT_EQ(stat.rc, 0) << qPrintable(stat.err);
    QFile::remove(outFile());
    serveIndex({kIndexA, kIndexB});
    serveTiles("/A2", kShadeA2);
    serveTiles("/B", 100); // the shade of the static tiles
    const Captured first = runCapture(job(u"run"_s));
    ASSERT_EQ(first.rc, 0) << qPrintable(first.err);
    ASSERT_EQ(versionCounts(), (QMap<QString, int>{{u"/A2"_s, 9}, {QString(), 9}}));
    QFile::remove(outFile());
    const Captured second = runCapture(job(u"run"_s, {u"-v"_s, u"--overwrite"_s, u"--strict"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"removed 9 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{u"/B"_s, 9}}));
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(100, 3, 4, 2));
}

// ---- T-0283: wmts-default and --time (docs/specs/weather-sources.md 3.2, app-cli.md 1.2, 1.7). No FEAT tag
// (Features none). The "Radar" source is rewritten as a WMTS source: its index is a capabilities document of
// one layer "radar.past" whose time runs 2020-01-01T00:00:00Z .. 2100-01-01T00:00:00Z every 3 h, default
// 2026-10-19T12:00:00.000Z; the tile URL carries TIME={ver} as a query.

namespace {

const QByteArray kWmtsCaps =
    "<?xml version=\"1.0\"?><Capabilities xmlns=\"http://www.opengis.net/wmts/1.0\" "
    "xmlns:ows=\"http://www.opengis.net/ows/1.1\"><Contents><Layer><ows:Identifier>radar.past</"
    "ows:Identifier>"
    "<Dimension><ows:Identifier>time</ows:Identifier><Default>2026-10-19T12:00:00.000Z</Default>"
    "<Value>2020-01-01T00:00:00Z/2100-01-01T00:00:00Z/PT10800S</Value></Dimension></Layer></Contents>"
    "</Capabilities>";

QString frameText(const QDateTime& time) {
    return time.toUTC().toString(u"yyyy-MM-dd'T'HH:mm:ss'Z'"_s);
}

// now + 24 h rounded down to the 3 h grid of the document (start at midnight)
QDateTime floored24(const QDateTime& now) {
    const QDateTime target = now.toUTC().addSecs(24 * 3600);
    return cg::core::platform::utcDateTime(target.date(), QTime(target.time().hour() / 3 * 3, 0));
}

class CliSnapshotWmts : public CliSnapshot {
protected:
    void SetUp() override {
        CliSnapshot::SetUp();
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(),
                 {.folder = u"s1.zmp"_s,
                  .guid = kGuidRadar,
                  .name = "Radar",
                  .urlBase = host + "/t/{z}/{x}/{y}.png?TIME={ver}",
                  .layer = true,
                  .extra = "SnapshotIndex=" + host +
                           "/caps.xml\r\nSnapshotIndexType=wmts-default\r\nSnapshotItem=radar.past\r\n"
                           "CacheType=71\r\nLicense=Credit {year}\r\n"});
        HttpTestServer::Reply caps;
        caps.headers = {{"Content-Type", "text/xml"}};
        caps.body = kWmtsCaps;
        server_.addReply("/caps.xml", std::move(caps));
    }

    // The nine tiles of the frame `time` (the text of TIME=).
    void serveFrame(const QByteArray& time, int shade) {
        for (const QString& path : areaPaths()) {
            const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
            HttpTestServer::Reply reply;
            reply.headers = {{"Content-Type", "image/png"}};
            reply.body = tilePng(shade, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
            server_.addReply("/t" + path.toLatin1() + "?TIME=" + time, std::move(reply));
        }
    }

    QStringList frameRequests(const QString& time, qsizetype from = 0) const {
        QStringList paths = requestPaths(from);
        paths.removeAll(u"/caps.xml"_s);
        paths.sort();
        QStringList expected;
        for (const QString& path : areaPaths()) {
            expected.push_back(u"/t"_s + path + u"?TIME="_s + time);
        }
        expected.sort();
        EXPECT_EQ(paths, expected);
        return paths;
    }

    QString storeValue(const char* key) const {
        for (const QByteArray& line : storeText().split('\n')) {
            if (line.startsWith(key)) {
                return QString::fromLatin1(line.mid(static_cast<qsizetype>(std::strlen(key))));
            }
        }
        ADD_FAILURE() << key;
        return {};
    }

    QString metadata(const char* name) const {
        return QString::fromUtf8(
            blob(outFile(), QByteArray("SELECT value FROM metadata WHERE name='") + name + "'"));
    }
};

} // namespace

// Acceptance 3: the default frame, then --time=+24h as a second snapshot; export names the frame and takes
// the year of it in the attribution.
TEST_F(CliSnapshotWmts, DefaultFrameThenAForecastFrame) {
    serveFrame("2026-10-19T12:00:00.000Z", 60);
    const Captured first = runCapture(job(u"download"_s));
    ASSERT_EQ(first.rc, 0) << qPrintable(first.err);
    frameRequests(u"2026-10-19T12:00:00.000Z"_s);
    EXPECT_EQ(storeValue("Version="), u"2026-10-19T12:00:00.000Z"_s);
    EXPECT_EQ(storeValue("Time="), u"2026-10-19T12:00:00Z"_s);

    const qsizetype afterFirst = server_.requestCount();
    const QDateTime before = QDateTime::currentDateTimeUtc();
    // the frame of +24h is not known before the run: a clock that crosses a 3 h step in between gives the
    // other one, so both are served
    const QDateTime plan = floored24(before);
    const QDateTime planNext = plan.addSecs(3 * 3600);
    serveFrame(frameText(plan).toLatin1(), 160);
    serveFrame(frameText(planNext).toLatin1(), 160);
    const Captured second = runCapture(job(u"download"_s, {u"--time=+24h"_s}));
    const QDateTime after = QDateTime::currentDateTimeUtc();
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    const QDateTime expected = floored24(after) == plan ? plan : planNext;
    EXPECT_TRUE(floored24(after) == plan || floored24(after) == planNext);
    EXPECT_EQ(storeValue("Version="), frameText(expected));
    EXPECT_EQ(storeValue("Time="), frameText(expected));
    frameRequests(frameText(expected), afterFirst);

    // export: the frame of the last download, in the description and, by its year, in the attribution
    const Captured exported = runCapture(job(u"export"_s));
    ASSERT_EQ(exported.rc, 0) << qPrintable(exported.err);
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(160, 3, 4, 2));
    EXPECT_EQ(metadata("description"),
              u"Radar, snapshot "_s + expected.toString(u"yyyy-MM-dd HH:mm"_s) + u" UTC"_s);
    EXPECT_EQ(metadata("attribution"), u"Credit "_s + QString::number(expected.date().year()));
}

// export uses the frame of the last download: --time is refused (usage error, exit 3), nothing is written.
TEST_F(CliSnapshotWmts, ExportRefusesTime) {
    const Captured c = runCapture(job(u"export"_s, {u"--time=+5h"_s}));
    EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
    EXPECT_TRUE(
        c.err.contains(u"error: --time is not accepted by export: export uses the frame stored by the "
                       u"last download\n"_s))
        << qPrintable(c.err);
    EXPECT_FALSE(QFileInfo::exists(outFile()));
}

TEST_F(CliSnapshotWmts, ATimeTheLayerDoesNotOfferIsRefusedBeforeAnyTile) {
    serveFrame("2026-10-19T12:00:00.000Z", 60);
    const std::vector<std::pair<QString, QString>> bad = {
        {u"2026-10-20T12:30:00Z"_s, u"off the 3 h grid"_s},
        {u"2101-01-01T00:00:00Z"_s, u"after the end"_s},
        {u"2019-12-31T21:00:00Z"_s, u"before the start"_s},
    };
    for (const auto& [value, why] : bad) {
        SCOPED_TRACE(why.toStdString());
        const Captured c = runCapture(job(u"download"_s, {u"--time="_s + value}));
        EXPECT_EQ(c.rc, 3) << qPrintable(c.err);
        EXPECT_TRUE(
            c.err.contains(u"error: --time %1 is not a time of \"Radar\" (range 2020-01-01T00:00:00Z.."
                           u"2100-01-01T00:00:00Z, step 3 h)\n"_s.arg(value)))
            << qPrintable(c.err);
    }
    for (const QString& path : requestPaths()) {
        EXPECT_EQ(path, u"/caps.xml"_s); // nothing but the index was asked for
    }
    EXPECT_FALSE(QFileInfo::exists(storeFile()));
}

// A source of another index type, or none, has no selectable time: refused at the start, no request.
TEST_F(CliSnapshot, TimeOnASourceWithoutSelectableTimeIsRefusedWithoutARequest) {
    const Captured json = runCapture(job(u"download"_s, {u"--time=+24h"_s}));
    EXPECT_EQ(json.rc, 3) << qPrintable(json.err);
    EXPECT_TRUE(json.err.contains(u"error: --time: \"Radar\" has no selectable time\n"_s))
        << qPrintable(json.err);
    for (const QString& command : {u"estimate"_s, u"run"_s}) {
        const Captured stat = runCapture(QStringList{command, u"--source"_s, u"Template Map"_s, u"--bbox"_s,
                                                     u"10,30,100,60"_s, u"--zooms"_s, u"1-3"_s, u"--out"_s,
                                                     outFile(), u"--time=2026-10-19T12:00:00Z"_s} +
                                         common());
        EXPECT_EQ(stat.rc, 3) << qPrintable(stat.err);
        EXPECT_TRUE(stat.err.contains(u"error: --time: \"Template Map\" has no selectable time\n"_s))
            << qPrintable(stat.err);
    }
    EXPECT_EQ(server_.requestCount(), 0);
}

// ---------------------------------------------------------------------------------------------------------
// T-0337 (docs/specs/weather-sources.md 3.2 SnapshotPick, none-refetch): end to end with the real clock. No
// FEAT tag (Features none).
namespace {

// The 3 h grid of kWmtsCaps (start at midnight): the frame nearest to `now`.
QDateTime nearestGrid3h(const QDateTime& now) {
    const QDateTime utc = now.toUTC();
    const qint64 sinceMidnight = utc.time().msecsSinceStartOfDay() / 1000;
    const qint64 slot = (sinceMidnight + 5400) / 10800; // 3 h = 10800 s; the half step (5400 s) goes up
    return cg::core::platform::utcDateTime(utc.date(), QTime(0, 0)).addSecs(slot * 10800);
}

class CliSnapshotNearestNow : public CliSnapshotWmts {
protected:
    void SetUp() override {
        CliSnapshotWmts::SetUp();
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(),
                 {.folder = u"s1.zmp"_s,
                  .guid = kGuidRadar,
                  .name = "Radar",
                  .urlBase = host + "/t/{z}/{x}/{y}.png?TIME={ver}",
                  .layer = true,
                  .extra = "SnapshotIndex=" + host +
                           "/caps.xml\r\nSnapshotIndexType=wmts-default\r\nSnapshotItem=radar.past\r\n"
                           "SnapshotPick=Nearest-Now\r\nCacheType=71\r\n"});
    }
};

class CliSnapshotNone : public CliSnapshotPrune {
protected:
    void SetUp() override {
        CliSnapshotPrune::SetUp();
        const QByteArray host = "http://127.0.0.1:" + QByteArray::number(server_.port());
        writeZmp(maps(),
                 {.folder = u"s1.zmp"_s,
                  .guid = kGuidRadar,
                  .name = "Radar",
                  .urlBase = host + "/{z}/{x}/{y}.png",
                  .layer = true,
                  .extra = "SnapshotIndex=" + host +
                           "/capabilities.xml\r\nSnapshotIndexType=None-Refetch\r\nCacheType=71\r\n"});
    }
};

} // namespace

// The frame nearest to the clock replaces the layer default (2026-10-19T12:00:00.000Z in the document).
TEST_F(CliSnapshotNearestNow, TakesTheFrameNearestToTheClockNotTheDefault) {
    const QDateTime before = QDateTime::currentDateTimeUtc();
    for (const QDateTime& frame : {nearestGrid3h(before), nearestGrid3h(before.addSecs(2))}) {
        serveFrame(frameText(frame).toLatin1(), 70);
    }
    const Captured c = runCapture(job(u"download"_s));
    const QDateTime after = QDateTime::currentDateTimeUtc();
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    const QString version = storeValue("Version=");
    EXPECT_TRUE(version == frameText(nearestGrid3h(before)) || version == frameText(nearestGrid3h(after)))
        << qPrintable(version);
    EXPECT_NE(version, u"2026-10-19T12:00:00.000Z"_s);
    EXPECT_EQ(storeValue("Time="), version);
    frameRequests(version);
}

// A layer without a time dimension: every download takes the instant of its start as the version, so the
// cache never serves the older picture; the index is not asked for; the older frame is pruned.
TEST_F(CliSnapshotNone, EveryDownloadFetchesFreshTilesAndTheOlderFrameIsPruned) {
    serveStatic(); // the first answer of every tile; the picture of the next run differs (a weather layer)
    for (const QString& path : areaPaths()) {
        const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
        HttpTestServer::Reply reply;
        reply.headers = {{"Content-Type", "image/png"}};
        reply.body = tilePng(180, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
        server_.addReply(path.toLatin1(), std::move(reply));
    }
    const Captured first = runCapture(job(u"download"_s));
    ASSERT_EQ(first.rc, 0) << qPrintable(first.err);
    EXPECT_EQ(tileRequests(), areaPaths());
    EXPECT_EQ(indexRequests() + requestPaths().count(u"/capabilities.xml"_s), 0);
    const QString v1 = QString::fromLatin1(storeText()).section(u"Version="_s, 1).section(u'\n', 0, 0);
    static const QRegularExpression instant(uR"(^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$)"_s);
    EXPECT_TRUE(instant.match(v1).hasMatch()) << qPrintable(v1);
    EXPECT_TRUE(storeText().contains(("Time=" + v1 + "\n").toLatin1())) << storeText().constData();

    std::this_thread::sleep_for(1100ms); // the version has the resolution of a second
    const qsizetype afterFirst = server_.requestCount();
    const Captured second = runCapture(job(u"download"_s, {u"-v"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_EQ(tileRequests(afterFirst), areaPaths()); // all nine again, no cached picture served
    const QString v2 = QString::fromLatin1(storeText()).section(u"Version="_s, 1).section(u'\n', 0, 0);
    EXPECT_NE(v2, v1);
    EXPECT_TRUE(second.err.contains(u"removed 9 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{v2, 9}}));

    // the TSV `refresh` field stays `snapshot` (field 11)
    const Captured tsv = runCapture(QStringList{u"sources"_s, u"--format=tsv"_s} + common());
    ASSERT_EQ(tsv.rc, 0) << qPrintable(tsv.err);
    int seen = 0;
    for (const QString& line : tsv.out.split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.split(u'\t');
        if (f.size() > 11 && f[1] == kGuidRadarText) {
            EXPECT_EQ(f[11], u"snapshot"_s);
            ++seen;
        }
    }
    EXPECT_EQ(seen, 1);
}

// T-0351: two runs with byte-identical pictures: the save of the second version is skipped for every tile,
// the cache must still hold all nine tiles (under the second version) and the export must have them.
TEST_F(CliSnapshotNone, TwoIdenticalRunsKeepEveryTile) {
    serveStatic();
    ASSERT_EQ(runCapture(job(u"download"_s)).rc, 0);
    std::this_thread::sleep_for(1100ms);
    const Captured second = runCapture(job(u"run"_s, {u"-v"_s, u"--strict"_s}));
    ASSERT_EQ(second.rc, 0) << qPrintable(second.err);
    EXPECT_TRUE(second.err.contains(u"removed 0 tiles of older snapshots of \"Radar\"\n"_s))
        << qPrintable(second.err);
    const QString v2 = QString::fromLatin1(storeText()).section(u"Version="_s, 1).section(u'\n', 0, 0);
    EXPECT_EQ(versionCounts(), (QMap<QString, int>{{v2, 9}}));
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
}

// ---- T-0356: the newest frame is not served yet -> one frame back
// -------------------------------------------

namespace {

// json-last index with frames /Old, /P, /N (oldest first); the ages are minutes before the clock of the test.
// `count` frames from the end: 3 = Old, P, N; 2 = P, N; 1 = N.
QByteArray ageIndex(int newestAgeMin, int count = 2) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QList<QPair<QByteArray, int>> frames = {
        {"/Old", newestAgeMin + 120}, {"/P", newestAgeMin + 60}, {"/N", newestAgeMin}};
    QByteArray entries;
    for (qsizetype i = frames.size() - count; i < frames.size(); ++i) {
        if (!entries.isEmpty()) {
            entries += ',';
        }
        entries += "{\"time\":" + QByteArray::number(now - qint64{frames[i].second} * 60) + ",\"path\":\"" +
                   frames[i].first + "\"}";
    }
    return R"({"radar":{"past":[)" + entries + "]}}";
}

class CliSnapshotFallback : public CliSnapshot {
protected:
    // every tile of the frame answers `status`
    void serveStatusTiles(const QByteArray& version, int status, const QByteArray& reason) {
        for (const QString& path : areaPaths()) {
            server_.addReply("/t" + version + path.toLatin1(), statusReply(status, reason));
        }
    }
    // the requests of the tiles of one frame
    qsizetype tilesOf(const QByteArray& version) const {
        qsizetype n = 0;
        for (const QString& path : requestPaths()) {
            n += path.startsWith(QString::fromLatin1("/t" + version + "/")) ? 1 : 0;
        }
        return n;
    }
    static QStringList withMaxErrors(QStringList more) {
        more << u"--max-connection-errors"_s << u"3"_s;
        return more;
    }
};

} // namespace

// The newest frame (10 min old) answers 502 for every tile: the run stops after three answers (not the legacy
// budget per tile), goes to the previous frame /P, downloads it, exits 0 and names the frame on stderr; the
// store file and the MBTiles carry /P.
TEST_F(CliSnapshotFallback, NewestFrameAnswering502IsReplacedByThePreviousFrame) {
    serveIndex({ageIndex(10)});
    serveStatusTiles("/N", 502, "Bad Gateway");
    serveTiles("/P", kShadeA2);
    const Captured c = runCapture(job(u"run"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(indexRequests(), 2);                          // the second one asks the frame before
    EXPECT_EQ(tilesOf("/P"), 9);                            // each tile once
    EXPECT_GE(tilesOf("/N"), 3);                            // the three answers that stop the run
    EXPECT_LE(tilesOf("/N"), 4);                            // not the retry budget per tile
    EXPECT_EQ(tileRequests().filter(u"/tOld"_s).size(), 0); // one step back only
    EXPECT_EQ(rowCount(outFile()), u"9"_s);
    EXPECT_EQ(tileZ3x4y2(outFile()), tilePng(kShadeA2, 3, 4, 2));
    EXPECT_TRUE(storeText().contains("Version=/P\n")) << storeText().constData();
    const QRegularExpression line(u"warning: the newest frame \\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d UTC of "
                                  u"\"Radar\" is not served yet; using the "
                                  u"previous frame \\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d UTC\n"_s);
    EXPECT_TRUE(c.err.contains(line)) << qPrintable(c.err);
    EXPECT_EQ(c.err.count(u"using the previous frame"_s), 1);
    EXPECT_FALSE(c.err.contains(u"session saved"_s));
    EXPECT_TRUE(sessionFiles().isEmpty());
}

// 404 on every tile of the newest frame: the same step back; the TSV run carries the later snapshot record
// (the frame of the run) and the written record names that frame's time.
TEST_F(CliSnapshotFallback, NewestFrameAnswering404IsReplacedAndTheTsvNamesThePreviousFrame) {
    const QByteArray index = ageIndex(10);
    serveIndex({index});
    serveStatusTiles("/N", 404, "Not Found");
    serveTiles("/P", kShadeA2);
    const Captured c = runCapture(job(u"run"_s, {u"--format=tsv"_s}));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(tilesOf("/P"), 9);
    const qint64 previousTime = QDateTime::currentSecsSinceEpoch() - 70 * 60;
    QStringList snapshots;
    QStringList written;
    for (const QString& record : c.out.split(u'\n', Qt::SkipEmptyParts)) {
        if (record.startsWith(u"snapshot\t"_s)) {
            snapshots.push_back(record);
        } else if (record.startsWith(u"written\t"_s)) {
            written.push_back(record);
        }
    }
    ASSERT_EQ(snapshots.size(), 2) << qPrintable(c.out);
    EXPECT_TRUE(snapshots[0].endsWith(u"\t/N"_s)) << qPrintable(snapshots[0]);
    EXPECT_TRUE(snapshots[1].endsWith(u"\t/P"_s)) << qPrintable(snapshots[1]);
    ASSERT_EQ(written.size(), 1);
    const QDateTime shown = QDateTime::fromString(written[0].split(u'\t')[5], Qt::ISODate);
    ASSERT_TRUE(shown.isValid()) << qPrintable(written[0]);
    EXPECT_NEAR(static_cast<double>(shown.toSecsSinceEpoch()), static_cast<double>(previousTime), 5.0);
    EXPECT_EQ(snapshots[1].split(u'\t')[2], written[0].split(u'\t')[5]);
}

// A frame 61 minutes old is not young: its 404s are the answer as before (no second index request, no tile of
// /P); at 59 minutes the same index falls back.
TEST_F(CliSnapshotFallback, TheNewestFrameMustBeYoungerThanSixtyMinutes) {
    serveIndex({ageIndex(61)});
    serveStatusTiles("/N", 404, "Not Found");
    serveTiles("/P", kShadeA2);
    const Captured old = runCapture(job(u"run"_s));
    EXPECT_NE(old.rc, 0);
    EXPECT_EQ(indexRequests(), 1);
    EXPECT_EQ(tilesOf("/P"), 0);
    EXPECT_FALSE(old.err.contains(u"previous frame"_s));

    server_.clearReplies();
    serveIndex({ageIndex(59)});
    serveStatusTiles("/N", 404, "Not Found");
    serveTiles("/P", kShadeA2);
    const Captured young = runCapture(job(u"run"_s));
    EXPECT_EQ(young.rc, 0) << qPrintable(young.err);
    EXPECT_EQ(tilesOf("/P"), 9);
}

// An old newest frame with 502: no fallback; the connection-error limit ends the run as before (exit 4).
TEST_F(CliSnapshotFallback, OldNewestFrameWith502IsNotReplaced) {
    serveIndex({ageIndex(120)});
    serveStatusTiles("/N", 502, "Bad Gateway");
    serveTiles("/P", kShadeA2);
    const Captured c = runCapture(job(u"run"_s, withMaxErrors({})));
    EXPECT_EQ(c.rc, 4) << qPrintable(c.err);
    EXPECT_EQ(tilesOf("/P"), 0);
    EXPECT_FALSE(c.err.contains(u"previous frame"_s));
}

// Some tiles of the newest frame are served: nothing falls back (two of nine tiles answer 404).
TEST_F(CliSnapshotFallback, ANewestFrameServedInPartIsKept) {
    serveIndex({ageIndex(10)});
    const QStringList paths = areaPaths();
    for (qsizetype i = 0; i < paths.size(); ++i) {
        if (i == 0 || i == 4) { // two tiles the provider does not have
            server_.addReply("/t/N" + paths[i].toLatin1(), statusReply(404, "Not Found"));
            continue;
        }
        const QStringList parts = paths[i].split(u'/', Qt::SkipEmptyParts);
        HttpTestServer::Reply reply;
        reply.headers = {{"Content-Type", "image/png"}};
        reply.body = tilePng(kShadeB, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
        server_.addReply("/t/N" + paths[i].toLatin1(), std::move(reply));
    }
    serveTiles("/P", kShadeA2);
    const Captured c = runCapture(job(u"run"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    EXPECT_EQ(tilesOf("/P"), 0);
    EXPECT_EQ(indexRequests(), 1);
    EXPECT_TRUE(storeText().contains("Version=/N\n"));
    EXPECT_EQ(rowCount(outFile()), u"7"_s);
    EXPECT_FALSE(c.err.contains(u"previous frame"_s));
}

// TL decision 1: 404 answers do not stop a run early. The first three tiles asked answer 404, the rest are
// served: the frame is served in part, so it is kept (no step back, the previous frame is never asked).
TEST_F(CliSnapshotFallback, ThreeLeadingNotFoundAnswersDoNotStopAServedFrame) {
    serveIndex({ageIndex(10)});
    const QStringList paths = areaPaths();
    for (qsizetype i = 0; i < paths.size(); ++i) {
        if (i < 3) { // the tiles the provider does not have
            server_.addReply("/t/N" + paths[i].toLatin1(), statusReply(404, "Not Found"));
            continue;
        }
        const QStringList parts = paths[i].split(u'/', Qt::SkipEmptyParts);
        HttpTestServer::Reply reply;
        reply.headers = {{"Content-Type", "image/png"}};
        reply.body = tilePng(kShadeB, parts[0].toInt(), parts[1].toInt(), parts[2].left(1).toInt());
        reply.delayMs = i == 3 ? 2500 : 0; // the poll (1 s) sees the three 404 answers and no stored tile
        server_.addReply("/t/N" + paths[i].toLatin1(), std::move(reply));
    }
    serveTiles("/P", kShadeA2);
    const Captured c = runCapture(job(u"run"_s));
    ASSERT_EQ(c.rc, 0) << qPrintable(c.err);
    // the precondition: the three 404 tiles were the first three asked, so the old rule would have stopped
    const QStringList asked = tileRequests();
    ASSERT_GE(asked.size(), 3);
    for (qsizetype i = 0; i < 3; ++i) {
        EXPECT_TRUE(asked[i].endsWith(paths[0]) || asked[i].endsWith(paths[1]) || asked[i].endsWith(paths[2]))
            << qPrintable(asked[i]);
    }
    ASSERT_GE(asked.size(), 4);
    EXPECT_TRUE(asked[3].endsWith(paths[3])) << qPrintable(asked[3]); // the slow tile
    EXPECT_EQ(tilesOf("/P"), 0);
    EXPECT_EQ(indexRequests(), 1);
    EXPECT_TRUE(storeText().contains("Version=/N\n"));
    EXPECT_EQ(rowCount(outFile()), u"6"_s);
    EXPECT_FALSE(c.err.contains(u"previous frame"_s));
}

// A server that closes the connection without an answer is a network failure, not a frame that is not out
// yet: no step back (the connection-error limit ends the run, exit 4).
TEST_F(CliSnapshotFallback, ConnectionsWithoutAnswerDoNotStepBack) {
    QFile ini(dataDir() + u"/chart-grabber.conf"_s); // one try, no reset pause: the failure shows at once
    ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
    ini.write("[Internet]\r\nDownloadTryCount=1\r\nSleepOnResetConnection=0\r\n[Internet_Proxy]"
              "\r\nUseIEProxySettings=0\r\n");
    ini.close();
    serveIndex({ageIndex(10)});
    for (const QString& path : areaPaths()) {
        HttpTestServer::Reply reply;
        reply.closeWithoutResponse = true;
        server_.addReply("/t/N" + path.toLatin1(), std::move(reply));
    }
    serveTiles("/P", kShadeA2);
    const Captured c = runCapture(job(u"run"_s, withMaxErrors({})));
    EXPECT_EQ(c.rc, 4) << qPrintable(c.err);
    EXPECT_EQ(tilesOf("/P"), 0);
    EXPECT_EQ(indexRequests(), 1);
    EXPECT_FALSE(c.err.contains(u"previous frame"_s));
}

// The index lists the newest frame only: nothing to step back to. The newest is downloaded again as before
// (all 404: exit 5, nothing exported) and stderr says why.
TEST_F(CliSnapshotFallback, NoPreviousFrameLeavesTheRunAsItWas) {
    serveIndex({ageIndex(10, 1)});
    serveStatusTiles("/N", 404, "Not Found");
    const Captured c = runCapture(job(u"run"_s));
    EXPECT_EQ(c.rc, 5) << qPrintable(c.err);
    EXPECT_TRUE(
        c.err.contains(u": the array at radar.past has no entry before the last); trying the newest again"_s))
        << qPrintable(c.err);
    EXPECT_EQ(indexRequests(), 2);
}

// One step only: the previous frame is not served either; the third frame is never tried (exit 5).
TEST_F(CliSnapshotFallback, OnlyOneStepBack) {
    serveIndex({ageIndex(10, 3)});
    serveStatusTiles("/N", 404, "Not Found");
    serveStatusTiles("/P", 404, "Not Found");
    serveTiles("/Old", kShadeB);
    const Captured c = runCapture(job(u"run"_s));
    EXPECT_EQ(c.rc, 5) << qPrintable(c.err);
    EXPECT_EQ(tilesOf("/Old"), 0);
    EXPECT_EQ(tilesOf("/P"), 9);
    EXPECT_EQ(c.err.count(u"using the previous frame"_s), 1);
}

// T-0360 item 5: a frame picked with --time is not "the newest" (a later frame is listed), however young it
// is (a forecast frame counts as young). All its tiles answer 502: no step back -- no "previous frame" line,
// the frame before is never asked. Removing `taken->newest` in fixSnapshots (cli_main.cpp) fails this test.
TEST_F(CliSnapshotWmts, AFrameChosenWithTimeIsNotReplacedByThePreviousFrame) {
    QFile ini(dataDir() + u"/chart-grabber.conf"_s); // no reset pause
    ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
    ini.write("[Internet]\r\nDownloadTryCount=1\r\nSleepOnResetConnection=0\r\n[Internet_Proxy]"
              "\r\nUseIEProxySettings=0\r\n");
    ini.close();
    // every tile of any frame of the grid answers 502, the frames before +24h are served: a step back would
    // succeed and show (rc is not asserted: the failed tiles alone do not set it)
    const QDateTime plan = floored24(QDateTime::currentDateTimeUtc());
    for (int step = -3; step <= 3; ++step) {
        const QByteArray time = frameText(plan.addSecs(step * 3 * 3600)).toLatin1();
        if (step < 0) {
            serveFrame(time, 60);
            continue;
        }
        for (const QString& path : areaPaths()) {
            server_.addReply("/t" + path.toLatin1() + "?TIME=" + time, statusReply(502, "Bad Gateway"));
        }
    }
    const Captured c = runCapture(job(u"download"_s, {u"--time=+24h"_s}));
    EXPECT_EQ(requestPaths().count(u"/caps.xml"_s), 1); // the index is asked once: no second pick
    EXPECT_FALSE(c.err.contains(u"previous frame"_s)) << qPrintable(c.err);
    for (const QString& path : requestPaths()) {
        for (int step = -3; step < 0; ++step) {
            EXPECT_FALSE(path.endsWith(u"TIME="_s + frameText(plan.addSecs(step * 3 * 3600))))
                << qPrintable(path);
        }
    }
}
