// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit): tests of snapshot_resolver, snapshot_store and refusalForSnapshot (T-0243,
// docs/specs/weather-sources.md 3.1, 3.4, 4.1, 4.2, 5.3; ticket acceptance 10-12). No FEAT tag (Features
// none). Legacy fact applySnapshot mirrors: the map settings dialog sets the version of the map's version
// request (frm_MapTypeEdit.pas:286); the request defaults to ShowOtherVersions = True
// (u_MapVersionRequestConfig.pas: 96-102). resolveSnapshot goes through net::simpleHttpRequest (port of
// u_SimpleHttpDownloader.pas:100-121: status of a result without server respond is 0, data only for an Ok
// result). Rig: the HttpTestServer, a real AppContext with a ZMP set written by cli_test_util.h.
#include "cg/app/app_context.h"
#include "cg/app/job_checks.h"
#include "cg/app/snapshot_resolver.h"
#include "cg/app/snapshot_store.h"
#include "cg/core/platform/qt_compat.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/map_version_request_config.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

#include "../maptype/maptype_test_util.h"
#include "../net/http_test_server.h"
#include "cli_test_util.h"

using namespace cg::app;
using namespace cg::app::test;
using namespace Qt::StringLiterals;
using cg::core::ErrorCode;
using cg::net::test::HttpTestServer;

namespace {

const QByteArray kIndexA =
    R"({"radar":{"past":[{"time":1792094400,"path":"/A"},{"time":1792095000,"path":"/A2"}]}})";

HttpTestServer::Reply jsonReply(const QByteArray& body, int status = 200, QByteArray reason = "OK") {
    HttpTestServer::Reply reply;
    reply.status = status;
    reply.reason = std::move(reason);
    reply.headers = {{"Content-Type", "application/json"}};
    reply.body = body;
    return reply;
}

SnapshotDefinition radarDefinition(const QString& url) {
    SnapshotDefinition d;
    d.indexUrl = url;
    d.type = SnapshotIndexType::JsonLast;
    d.item = u"radar.past"_s;
    d.versionField = u"path"_s;
    return d;
}

QByteArray guidOf(int n) {
    return QByteArray("{AB0001") + QByteArray::number(n + 10) + "-0000-0000-0000-0000000000A1}";
}

QByteArray slurp(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << qPrintable(path);
    return f.readAll();
}

QDateTime utc(int y, int mo, int d, int h, int mi, int s) {
    return cg::core::platform::utcDateTime(QDate(y, mo, d), QTime(h, mi, s));
}

class SnapshotResolverTest : public ::testing::Test {
protected:
    struct MapCase {
        const char* name;
        QByteArray extra; // more [PARAMS] lines
    };

    void SetUp() override {
        cg::maptype::test::ensureGuiApp();
        ASSERT_TRUE(root_.isValid());
        ASSERT_TRUE(server_.start());
        ASSERT_TRUE(QDir().mkpath(maps()));
        ASSERT_TRUE(QDir().mkpath(dataDir()));
        QFile ini(dataDir() + u"/chart-grabber.conf"_s); // no system proxy lookup
        ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
        ini.write("[Internet]\r\n[Internet_Proxy]\r\nUseIEProxySettings=0\r\n");
        ini.close();
        QFile mapsIni(maps() + u"/Maps.ini"_s);
        ASSERT_TRUE(mapsIni.open(QIODevice::WriteOnly));
        mapsIni.write(kMapsIni);
        mapsIni.close();

        const QByteArray keys =
            "SnapshotIndex=http://127.0.0.1:9/index.json\r\nSnapshotIndexType=json-last\r\n"
            "SnapshotItem=radar.past\r\n";
        const std::vector<MapCase> cases = {
            {"Static", ""},                                                      // 0
            {"Static Sas Cache", "CacheType=2\r\n"},                             // 1
            {"Snapshot Default Cache", keys},                                    // 2
            {"Snapshot Sqlite", keys + "CacheType=71\r\n"},                      // 3
            {"Snapshot Berkeley", keys + "CacheType=61\r\n"},                    // 4
            {"Snapshot Sas Cache", keys + "CacheType=2\r\n"},                    // 5
            {"Snapshot Other Cache", keys + "CacheType=1\r\n"},                  // 6
            {"Snapshot Bad Keys", "SnapshotIndex=ftp://x/\r\nCacheType=71\r\n"}, // 7
        };
        int n = 0;
        for (const MapCase& c : cases) {
            writeZmp(maps(), {.folder = u"m%1.zmp"_s.arg(n),
                              .guid = guidOf(n),
                              .name = QByteArray(c.name),
                              .urlBase = "http://127.0.0.1:9/{z}/{x}/{y}.png",
                              .extra = c.extra});
            ++n;
        }
        AppOptions options;
        options.arguments = {u"--datadir="_s + dataDir()};
        options.mapsDirs = {maps()};
        options.warn = [](const QString&) {};
        auto created = AppContext::create(options);
        ASSERT_TRUE(created.has_value());
        app_ = std::move(*created);
        ASSERT_TRUE(app_->loadMaps().has_value());
        ASSERT_EQ(app_->fullMapsSet()->count(), static_cast<int>(cases.size()));
    }

    cg::maptype::IMapTypePtr map(int n) const {
        auto m = app_->fullMapsSet()->mapTypeByGuid(QUuid::fromString(QString::fromLatin1(guidOf(n))));
        EXPECT_TRUE(m != nullptr) << n;
        return m;
    }

    QString maps() const { return root_.filePath(u"Maps"_s); }
    QString dataDir() const { return root_.filePath(u"data"_s); }
    QString url() const { return QString::fromLatin1(server_.url("/index.json")); }

    cg::core::Result<Snapshot> resolve(const std::stop_token& stop = {}) const {
        return resolveSnapshot(radarDefinition(url()), *map(3), *app_, stop);
    }

    // A server of its own for one answer: the scripted replies of one path are consumed per request, and a
    // failed request may be repeated by the downloader.
    struct OneAnswer {
        HttpTestServer server;
        QString url;
    };
    std::unique_ptr<OneAnswer> serveOnly(const HttpTestServer::Reply& reply) const {
        auto one = std::make_unique<OneAnswer>();
        EXPECT_TRUE(one->server.start());
        one->server.addReply("/index.json", reply);
        one->url = QString::fromLatin1(one->server.url("/index.json"));
        return one;
    }
    cg::core::Result<Snapshot> resolveAnswer(const OneAnswer& one) const {
        return resolveSnapshot(radarDefinition(one.url), *map(3), *app_, {});
    }

    static inline const QByteArray kMapsIni = "[Maps]\r\nLast=Static\r\n";
    QTemporaryDir root_;
    HttpTestServer server_;
    std::unique_ptr<AppContext> app_;
};

} // namespace

// Acceptance 11: applySnapshot sets the version of the request and turns the other versions off, for a
// request that has the legacy default ShowOtherVersions = True; a second call replaces the first; Maps.ini is
// not written (the program never saves it), not even at shutdown.
TEST_F(SnapshotResolverTest, ApplySnapshotSetsTheVersionHidesOtherVersionsAndWritesNothing) {
    const auto before = map(0)->versionRequest()->getStatic();
    EXPECT_TRUE(before->showOtherVersions);
    EXPECT_EQ(before->baseVersion->storeString(), QString());

    applySnapshot(*map(0), u"/A2"_s);
    const auto after = map(0)->versionRequest()->getStatic();
    ASSERT_TRUE(after->baseVersion != nullptr);
    EXPECT_EQ(after->baseVersion->storeString(), u"/A2"_s);
    EXPECT_FALSE(after->showOtherVersions);

    applySnapshot(*map(0), u"Город/2"_s);
    EXPECT_EQ(map(0)->versionRequest()->getStatic()->baseVersion->storeString(), u"Город/2"_s);
    EXPECT_FALSE(map(0)->versionRequest()->getStatic()->showOtherVersions);

    // the other maps keep their request
    EXPECT_TRUE(map(1)->versionRequest()->getStatic()->showOtherVersions);

    app_.reset();
    EXPECT_EQ(slurp(maps() + u"/Maps.ini"_s), kMapsIni);
}

// Acceptance 12 (first half): the answer is parsed as the index of the definition; one request, GET.
TEST_F(SnapshotResolverTest, ResolveTakesTheLastFrameOfTheIndexWithOneGetRequest) {
    server_.addReply("/index.json", jsonReply(kIndexA));
    const auto taken = resolve();
    ASSERT_TRUE(taken.has_value()) << qPrintable(taken.error().what());
    EXPECT_EQ(taken->version, u"/A2"_s);
    EXPECT_EQ(taken->time, utc(2026, 10, 15, 20, 10, 0));
    EXPECT_FALSE(taken->dateOnly);
    const QList<QByteArray> log = server_.requestLog();
    ASSERT_EQ(log.size(), 1);
    EXPECT_TRUE(log[0].startsWith("GET /index.json HTTP/1.1\r\n")) << log[0].constData();
}

// Acceptance 12: the body limit is 8 MiB of the received data: exactly 8 MiB is read, one byte more is
// refused with the text of 3.4.
TEST_F(SnapshotResolverTest, ABodyOfEightMebibytesIsReadAndOneByteMoreIsRefused) {
    constexpr qsizetype kLimit = qsizetype{8} * 1024 * 1024;
    QByteArray exact = kIndexA;
    exact.append(QByteArray(kLimit - exact.size(), ' ')); // trailing white space is valid JSON
    ASSERT_EQ(exact.size(), kLimit);
    const auto fits = serveOnly(jsonReply(exact));
    const auto taken = resolveAnswer(*fits);
    ASSERT_TRUE(taken.has_value()) << qPrintable(taken.error().what());
    EXPECT_EQ(taken->version, u"/A2"_s);

    const auto tooBig = serveOnly(jsonReply(exact + ' '));
    const auto refused = resolveAnswer(*tooBig);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code(), ErrorCode::IoError);
    EXPECT_EQ(refused.error().what(), u"the answer of %1 is larger than 8 MiB"_s.arg(tooBig->url));
}

// Acceptance 12 and the texts of 3.4 "network": every refusal carries the URL. A 500 and a connection that is
// reset are results without server respond (status 0); a 404 has one.
TEST_F(SnapshotResolverTest, NetworkRefusalsHaveTheTextsOfTheSpec) {
    struct Case {
        const char* name;
        HttpTestServer::Reply reply;
        ErrorCode code;
        QString text;
    };
    HttpTestServer::Reply reset;
    reset.closeWithoutResponse = true;
    const std::vector<Case> cases = {
        {"500", jsonReply("no", 500, "Internal Server Error"), ErrorCode::IoError, u"no answer from %1"_s},
        {"reset", reset, ErrorCode::IoError, u"no answer from %1"_s},
        {"404", jsonReply("no", 404, "Not Found"), ErrorCode::IoError, u"HTTP 404 from %1"_s},
        {"empty", jsonReply(""), ErrorCode::IoError, u"the answer of %1 is empty"_s},
        {"array", jsonReply("[]"), ErrorCode::ParseError, u"%1: not a JSON object"_s},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        const auto one = serveOnly(c.reply);
        const auto taken = resolveAnswer(*one);
        ASSERT_FALSE(taken.has_value());
        EXPECT_EQ(taken.error().code(), c.code);
        EXPECT_EQ(taken.error().what(), c.text.arg(one->url));
    }
}

TEST_F(SnapshotResolverTest, ANoAnswerFromAClosedPortIsStatusZero) {
    quint16 port = 0;
    {
        QTcpServer closed;
        ASSERT_TRUE(closed.listen(QHostAddress::LocalHost, 0));
        port = closed.serverPort();
    } // closed again: nobody listens
    const QString closedUrl = u"http://127.0.0.1:%1/index.json"_s.arg(port);
    const auto taken = resolveSnapshot(radarDefinition(closedUrl), *map(3), *app_, {});
    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error().code(), ErrorCode::IoError);
    EXPECT_EQ(taken.error().what(), u"no answer from %1"_s.arg(closedUrl));
}

// Acceptance 12: a termination that fired before the call is Cancelled, not "no answer" (the downloader
// reports status 0 for it too), and nothing was sent.
TEST_F(SnapshotResolverTest, AStoppedTokenIsCancelledAndSendsNothing) {
    server_.addReply("/index.json", jsonReply(kIndexA));
    std::stop_source source;
    source.request_stop();
    const auto taken = resolve(source.get_token());
    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error().code(), ErrorCode::Cancelled);
    EXPECT_EQ(server_.requestCount(), 0);
}

// 5.3 / 4.1: the refusal of a snapshot source. A static source is never refused, whatever its cache; a
// snapshot source needs the versioned storage (the effective default is SQLite, the shipped definitions say
// 71); invalid keys carry the detail of snapshotDefinition.
TEST_F(SnapshotResolverTest, RefusalForSnapshotCoversCacheAndKeys) {
    const QString noVersions =
        u"a snapshot source needs a cache that keeps tile versions (SQLite 71); its cache type is %1"_s;
    EXPECT_EQ(refusalForSnapshot(*map(0)), std::nullopt); // static, default cache
    EXPECT_EQ(refusalForSnapshot(*map(1)), std::nullopt); // static on a cache without versions
    EXPECT_EQ(refusalForSnapshot(*map(2)), std::nullopt); // snapshot, default cache = SQLite
    EXPECT_EQ(refusalForSnapshot(*map(3)), std::nullopt); // 71
    // 61: T-0333 (owner 2026-10-21) removed the BerkeleyDB backend; no storage, no versions -> refused
    EXPECT_EQ(refusalForSnapshot(*map(4)), u"\"Snapshot Berkeley\": "_s + noVersions.arg(61));
    EXPECT_EQ(refusalForSnapshot(*map(5)), u"\"Snapshot Sas Cache\": "_s + noVersions.arg(2));
    EXPECT_EQ(refusalForSnapshot(*map(6)), u"\"Snapshot Other Cache\": "_s + noVersions.arg(1));
    EXPECT_EQ(
        refusalForSnapshot(*map(7)),
        u"\"Snapshot Bad Keys\": the snapshot keys are invalid: SnapshotIndex=ftp://x/ is not an http(s) URL"_s);
}

// snapshot_store.cpp (4.2)
// ------------------------------------------------------------------------------------

class SnapshotStoreTest : public ::testing::Test {
protected:
    QString dir() const { return root_.filePath(u"data/Snapshots"_s); }
    QString file() const { return dir() + u"/{G}.conf"_s; }
    QTemporaryDir root_;
};

// The exact text of the spec: LF, no BOM, the five lines; the resolve time is written in UTC whatever the
// zone of the argument (21:04:05 at +03:00 is 18:04:05Z); the directory is created with its parents.
TEST_F(SnapshotStoreTest, SaveWritesTheFileOfTheSpecInUtc) {
    Snapshot s;
    s.version = u"/v2/radar/7b6bac246650"_s;
    s.time = utc(2026, 10, 15, 20, 10, 0);
    const QDateTime resolved(QDate(2026, 10, 15), QTime(21, 4, 5), QTimeZone(3 * 3600));
    ASSERT_TRUE(saveSnapshot(dir(), u"{G}"_s, s, resolved).has_value());
    EXPECT_EQ(slurp(file()),
              QByteArray("[Snapshot]\nVersion=/v2/radar/7b6bac246650\nTime=2026-10-15T20:10:00Z\n"
                         "DateOnly=0\nResolved=2026-10-15T18:04:05Z\n"));
    EXPECT_FALSE(QFileInfo::exists(file() + u".part"_s));
}

TEST_F(SnapshotStoreTest, DateOnlyUnknownTimeAndNonAsciiVersionAreWrittenAsTheyAre) {
    Snapshot day;
    day.version = u"2026-10-15"_s;
    day.time = utc(2026, 10, 15, 0, 0, 0);
    day.dateOnly = true;
    ASSERT_TRUE(saveSnapshot(dir(), u"{G}"_s, day, utc(2026, 10, 16, 1, 2, 3)).has_value());
    EXPECT_EQ(slurp(file()), QByteArray("[Snapshot]\nVersion=2026-10-15\nTime=2026-10-15\nDateOnly=1\n"
                                        "Resolved=2026-10-16T01:02:03Z\n"));

    Snapshot unknown;
    unknown.version = u"Город/1"_s;
    ASSERT_TRUE(saveSnapshot(dir(), u"{G}"_s, unknown, utc(2026, 10, 16, 1, 2, 3)).has_value());
    EXPECT_EQ(slurp(file()),
              QByteArray("[Snapshot]\nVersion=\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4/1\nTime=\n"
                         "DateOnly=0\nResolved=2026-10-16T01:02:03Z\n")); // replaced, UTF-8
}

TEST_F(SnapshotStoreTest, SaveFailuresAreIoErrorsThatNameTheirPath) {
    Snapshot s;
    s.version = u"/A2"_s;
    s.time = utc(2026, 10, 15, 20, 10, 0);
    // a FILE where the directory must be
    ASSERT_TRUE(QDir().mkpath(root_.filePath(u"data"_s)));
    QFile blocker(dir());
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const auto noDir = saveSnapshot(dir(), u"{G}"_s, s, utc(2026, 10, 15, 20, 18, 4));
    ASSERT_FALSE(noDir.has_value());
    EXPECT_EQ(noDir.error().code(), ErrorCode::IoError);
    EXPECT_TRUE(noDir.error().what().contains(dir())) << qPrintable(noDir.error().what());

    // a DIRECTORY where the file must be
    ASSERT_TRUE(QFile::remove(dir()));
    ASSERT_TRUE(QDir().mkpath(file()));
    const auto noFile = saveSnapshot(dir(), u"{G}"_s, s, utc(2026, 10, 15, 20, 18, 4));
    ASSERT_FALSE(noFile.has_value());
    EXPECT_EQ(noFile.error().code(), ErrorCode::IoError);
    EXPECT_FALSE(noFile.error().what().isEmpty());
}

TEST_F(SnapshotStoreTest, LoadReadsBackWhatSaveWrote) {
    Snapshot s;
    s.version = u"Город/A=1"_s;
    s.time = utc(2026, 10, 15, 20, 10, 0);
    ASSERT_TRUE(saveSnapshot(dir(), u"{G}"_s, s, utc(2026, 10, 15, 20, 18, 4)).has_value());
    const auto loaded = loadSnapshot(dir(), u"{G}"_s);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->version, u"Город/A=1"_s);
    EXPECT_EQ(loaded->time, utc(2026, 10, 15, 20, 10, 0));
    EXPECT_FALSE(loaded->dateOnly);

    Snapshot day;
    day.version = u"2026-10-15"_s;
    day.time = utc(2026, 10, 15, 0, 0, 0);
    day.dateOnly = true;
    ASSERT_TRUE(saveSnapshot(dir(), u"{H}"_s, day, utc(2026, 10, 15, 20, 18, 4)).has_value());
    const auto loadedDay = loadSnapshot(dir(), u"{H}"_s);
    ASSERT_TRUE(loadedDay.has_value());
    EXPECT_EQ(loadedDay->time, utc(2026, 10, 15, 0, 0, 0));
    EXPECT_TRUE(loadedDay->dateOnly);

    EXPECT_FALSE(loadSnapshot(dir(), u"{G}x"_s).has_value()); // another source: no file
}

// T-0357: a <guid>.ini of an older version is renamed to <guid>.conf once and used; with both files the .conf
// wins and the .ini stays.
TEST_F(SnapshotStoreTest, OldIniIsRenamedOnceAndConfWinsWhenBothExist) {
    ASSERT_TRUE(QDir().mkpath(dir()));
    const auto put = [](const QString& path, const char* version) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("[Snapshot]\nVersion=") + version + "\nTime=2026-10-15T20:10:00Z\n");
    };
    const QString old = dir() + u"/{G}.ini"_s;
    put(old, "old");
    const auto first = loadSnapshot(dir(), u"{G}"_s);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->version, u"old"_s);
    EXPECT_FALSE(QFileInfo::exists(old));
    EXPECT_TRUE(QFileInfo::exists(file()));
    const auto second = loadSnapshot(dir(), u"{G}"_s); // renamed once: the second read uses the .conf
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->version, u"old"_s);

    put(old, "stale"); // both present
    const auto both = loadSnapshot(dir(), u"{G}"_s);
    ASSERT_TRUE(both.has_value());
    EXPECT_EQ(both->version, u"old"_s);
    EXPECT_TRUE(QFileInfo::exists(old));
    QFile left(old);
    ASSERT_TRUE(left.open(QIODevice::ReadOnly));
    EXPECT_TRUE(left.readAll().contains("stale"));
}

// T-0357: when the rename fails (a read-only directory) the .ini is still read and nothing is created.
TEST_F(SnapshotStoreTest, OldIniIsStillReadWhenTheRenameFails) {
    ASSERT_TRUE(QDir().mkpath(dir()));
    const QString old = dir() + u"/{G}.ini"_s;
    {
        QFile f(old);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("[Snapshot]\nVersion=old\nTime=2026-10-15T20:10:00Z\n");
    }
    const QFile::Permissions was = QFile::permissions(dir());
    ASSERT_TRUE(QFile::setPermissions(dir(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    if (QFile::exists(dir() + u"/probe"_s) || QFile(dir() + u"/probe"_s).open(QIODevice::WriteOnly)) {
        QFile::setPermissions(dir(), was);
        GTEST_SKIP() << "directory permissions are not enforced (root)";
    }
    const auto got = loadSnapshot(dir(), u"{G}"_s);
    QFile::setPermissions(dir(), was);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->version, u"old"_s);
    EXPECT_TRUE(QFileInfo::exists(old));
    EXPECT_FALSE(QFileInfo::exists(file()));
}

// "No snapshot": no file, no [Snapshot] section, a missing or empty Version. An unreadable time does not hide
// the version (the time stays unknown).
TEST_F(SnapshotStoreTest, LoadWithoutUsableVersionIsNoSnapshotAndABadTimeStaysUnknown) {
    ASSERT_TRUE(QDir().mkpath(dir()));
    const auto put = [&](const QByteArray& text) {
        QFile f(file());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    };
    EXPECT_FALSE(loadSnapshot(dir(), u"{G}"_s).has_value()); // no file
    put("[Other]\nVersion=/A2\nTime=2026-10-15T20:10:00Z\n");
    EXPECT_FALSE(loadSnapshot(dir(), u"{G}"_s).has_value()); // no section
    put("[Snapshot]\nTime=2026-10-15T20:10:00Z\n");
    EXPECT_FALSE(loadSnapshot(dir(), u"{G}"_s).has_value()); // no Version
    put("[Snapshot]\nVersion=\nTime=2026-10-15T20:10:00Z\n");
    EXPECT_FALSE(loadSnapshot(dir(), u"{G}"_s).has_value()); // empty Version
    put("[Snapshot]\r\nVersion=/A2\r\nTime=not a time\r\n");
    const auto bad = loadSnapshot(dir(), u"{G}"_s);
    ASSERT_TRUE(bad.has_value());
    EXPECT_EQ(bad->version, u"/A2"_s);
    EXPECT_FALSE(bad->time.isValid());
    put("[Snapshot]\nVersion=/A2\n");
    const auto noTime = loadSnapshot(dir(), u"{G}"_s);
    ASSERT_TRUE(noTime.has_value());
    EXPECT_FALSE(noTime->time.isValid());
}
