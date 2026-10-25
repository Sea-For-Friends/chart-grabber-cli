// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/job (new code; inherits Src/CmdLineArgProcessor/u_CmdLineArgProcessorAPI.pas:67-134,
// u_CmdLineArgProcessorHelpers.pas:124-143). No FEAT tag (T-0185: Features none).
#include "cg/app/job.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <vector>

using cg::app::AreaKind;
using cg::app::Command;
using cg::app::Job;
using cg::app::parseJob;
using cg::app::parseZoomList;

namespace {

using Zooms = std::vector<quint8>;

std::expected<Job, cg::app::CliError> parse(const QStringList& args) {
    return parseJob(QStringList{QStringLiteral("chart-grabber-cli")} + args);
}

Job mustParse(const QStringList& args) {
    auto job = parse(args);
    EXPECT_TRUE(job.has_value()) << (job ? "" : job.error().message.toStdString());
    return job ? *job : Job{};
}

// Exit code 2 and the message contains `needle`.
void expectUsageError(const QStringList& args, const QString& needle) {
    const auto job = parse(args);
    ASSERT_FALSE(job.has_value()) << args.join(u' ').toStdString();
    EXPECT_EQ(job.error().exitCode, 2);
    EXPECT_TRUE(job.error().message.contains(needle))
        << args.join(u' ').toStdString() << " -> " << job.error().message.toStdString();
}

QDateTime local(int y, int mo, int d, int h, int mi, int s) {
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s));
}

struct TempDir {
    QTemporaryDir dir;
    QString root() const { return QFileInfo(dir.path()).absoluteFilePath(); }
    QString write(const QString& name, const QByteArray& text) const {
        const QString path = root() + u'/' + name;
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(text);
        return path;
    }
};

// Current directory switched for a test, restored afterwards.
struct CurrentDir {
    QString saved = QDir::currentPath();
    explicit CurrentDir(const QString& path) { EXPECT_TRUE(QDir::setCurrent(path)); }
    ~CurrentDir() { QDir::setCurrent(saved); }
};

// A default locale that reads "24.5" as 245 (group separator): a toDouble without the C locale breaks.
struct GermanDefaultLocale {
    QLocale saved;
    GermanDefaultLocale() { QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany)); }
    ~GermanDefaultLocale() { QLocale::setDefault(saved); }
};

} // namespace

// ---- proof 1: every flag sets its field ----

TEST(JobFlags, JobAndDownloadAndCommonFlagsSetTheirFields) {
    TempDir tmp;
    CurrentDir cwd(tmp.root());
    GermanDefaultLocale german;
    const Job job = mustParse({
        "run",
        "--source={AB0000A1-0000-0000-0000-0000000000A1}",
        "--overlay=Метки",
        "--bbox=24.5,59.3,24.9,59.5",
        "--zooms=8-10,12",
        "--out=sub/base.mbtiles",
        "--overlay-out=sub/over.mbtiles",
        "--name=  Имя карты  ",
        "--description=\tОписание ",
        "--attribution= (c) OSM ",
        "--overwrite",
        "--threads=7",
        "--restart",
        "--replace",
        "--replace-if-size-differs",
        "--replace-older-than=2024-02-29",
        "--retry-missing",
        "--retry-missing-older-than=2023-12-31T23:59:58",
        "--autosave-min=11",
        "--stall-timeout=222",
        "--max-connection-errors=7",
        "--save-retries=4",
        "--strict",
        "--allow-webp",
        "--proxy=socks5://u:p@host:1080",
        "--assume-tile-kb=33",
        "--maps=mapsdir",
        "--cache=cachedir",
        "--no-progress",
        "--datadir=/ignored",
        "--portable",
    });
    EXPECT_EQ(job.command, Command::Run);
    EXPECT_EQ(job.source, QStringLiteral("{AB0000A1-0000-0000-0000-0000000000A1}"));
    EXPECT_EQ(job.overlay, QStringLiteral("Метки"));
    EXPECT_EQ(job.areaKind, AreaKind::BBox);
    // W,S,E,N = 24.5,59.3,24.9,59.5 -> left, bottom, right, top
    EXPECT_EQ(job.bbox.left, 24.5);
    EXPECT_EQ(job.bbox.bottom, 59.3);
    EXPECT_EQ(job.bbox.right, 24.9);
    EXPECT_EQ(job.bbox.top, 59.5);
    EXPECT_EQ(job.zooms, (Zooms{8, 9, 10, 12}));
    EXPECT_EQ(job.out, tmp.root() + QStringLiteral("/sub/base.mbtiles"));
    EXPECT_EQ(job.overlayOut, tmp.root() + QStringLiteral("/sub/over.mbtiles"));
    EXPECT_EQ(job.name, QStringLiteral("Имя карты")); // fr_ExportMBTiles.pas:331-344 Trim
    EXPECT_EQ(job.description, QStringLiteral("Описание"));
    EXPECT_EQ(job.attribution, QStringLiteral("(c) OSM"));
    EXPECT_TRUE(job.overwrite);
    EXPECT_EQ(job.threads, 7);
    EXPECT_TRUE(job.restart);
    EXPECT_TRUE(job.replace);
    EXPECT_TRUE(job.replaceIfSizeDiffers);
    EXPECT_EQ(job.replaceOlderThan, local(2024, 2, 29, 0, 0, 0));
    EXPECT_TRUE(job.retryMissing);
    EXPECT_EQ(job.retryMissingOlderThan, local(2023, 12, 31, 23, 59, 58));
    EXPECT_EQ(job.autosaveMin, 11);
    EXPECT_EQ(job.stallTimeoutSec, 222);
    EXPECT_EQ(job.maxConnectionErrors, 7);
    EXPECT_EQ(job.saveRetries, 4);
    EXPECT_TRUE(job.strict);
    EXPECT_TRUE(job.allowWebp);
    EXPECT_EQ(job.proxy, QStringLiteral("socks5://u:p@host:1080"));
    EXPECT_EQ(job.assumeTileKb, 33);
    EXPECT_EQ(job.mapsDirs, QStringList{tmp.root() + QStringLiteral("/mapsdir")});
    EXPECT_EQ(job.cacheDir, tmp.root() + QStringLiteral("/cachedir"));
    EXPECT_FALSE(job.progress);
    EXPECT_EQ(job.verbosity, 0);
}

TEST(JobFlags, DefaultsWhenNothingIsGiven) {
    const Job job = mustParse({"download", "--source=a", "--bbox=1,2,3,4"});
    EXPECT_EQ(job.command, Command::Download);
    EXPECT_FALSE(job.overwrite);
    EXPECT_EQ(job.threads, 1);
    EXPECT_FALSE(job.restart);
    EXPECT_FALSE(job.replace);
    EXPECT_FALSE(job.replaceOlderThan.isValid());
    EXPECT_FALSE(job.retryMissingOlderThan.isValid());
    EXPECT_EQ(job.autosaveMin, 5);
    EXPECT_EQ(job.stallTimeoutSec, 300);
    EXPECT_EQ(job.maxConnectionErrors, 60);
    EXPECT_EQ(job.saveRetries, 3);
    EXPECT_EQ(job.assumeTileKb, 20);
    EXPECT_TRUE(job.progress);
    EXPECT_EQ(job.verbosity, 0);
    EXPECT_TRUE(job.zooms.empty());
}

TEST(JobFlags, PolygonAndRouteAreas) {
    TempDir tmp;
    CurrentDir cwd(tmp.root());
    const Job poly = mustParse({"estimate", "--overlay=o", "--polygon=a.hlg", "--zooms", "3"});
    EXPECT_EQ(poly.areaKind, AreaKind::PolygonFile);
    EXPECT_EQ(poly.polygonFile, tmp.root() + QStringLiteral("/a.hlg"));
    EXPECT_EQ(poly.zooms, (Zooms{3}));

    GermanDefaultLocale german;
    const Job route =
        mustParse({"estimate", "--source=s", "--route=r.gpx", "--route-name=Рейс 1", "--corridor-nm=1.5"});
    EXPECT_EQ(route.areaKind, AreaKind::Route);
    EXPECT_EQ(route.routeFile, tmp.root() + QStringLiteral("/r.gpx"));
    EXPECT_EQ(route.routeName, QStringLiteral("Рейс 1"));
    EXPECT_EQ(route.corridorNm, 1.5);
}

TEST(JobFlags, SourcesProbeAndResume) {
    GermanDefaultLocale german;
    const Job probe = mustParse({"probe", "--filter=Яндекс", "--at=24.5,-59.25", "--probe-zoom=15"});
    EXPECT_EQ(probe.command, Command::Probe);
    EXPECT_EQ(probe.filter, QStringLiteral("Яндекс"));
    EXPECT_EQ(probe.probeAt.lon, 24.5);
    EXPECT_EQ(probe.probeAt.lat, -59.25);
    EXPECT_EQ(probe.probeZoom, 15);
    const Job probeDefault = mustParse({"probe"});
    EXPECT_EQ(probeDefault.probeZoom, 10);
    EXPECT_EQ(probeDefault.probeAt.lon, 0.0);
    EXPECT_EQ(probeDefault.probeAt.lat, 0.0);

    const Job sources = mustParse({"sources", "--filter", "osm"});
    EXPECT_EQ(sources.command, Command::Sources);
    EXPECT_EQ(sources.filter, QStringLiteral("osm"));

    const Job resume = mustParse({"resume", "b.sls", "dir/a.sls", "--threads=2"});
    EXPECT_EQ(resume.command, Command::Resume);
    EXPECT_EQ(resume.slsFiles, (QStringList{"b.sls", "dir/a.sls"}));
    EXPECT_EQ(resume.threads, 2);
}

// ---- proof 2: the job file ----

TEST(JobFile, EveryKeySetsTheSameFieldAsItsFlag) {
    TempDir tmp;
    const QString file = tmp.write(QStringLiteral("job.ini"), "[Job]\n"
                                                              "Source=src\n"
                                                              "Overlay=ovl\n"
                                                              "BBox=24.5,59.3,24.9,59.5\n"
                                                              "Zooms=14,8-9,9\n"
                                                              "Time=2026-10-18T12:00:00Z\n"
                                                              "Out=out/base.mbtiles\n"
                                                              "OverlayOut=out/over.mbtiles\n"
                                                              "Name=Name1\n"
                                                              "Description=Desc1\n"
                                                              "Attribution=Attr1\n"
                                                              "Overwrite=true\n"
                                                              "[Download]\n"
                                                              "Threads=6\n"
                                                              "Restart=1\n"
                                                              "Replace=TRUE\n"
                                                              "ReplaceIfSizeDiffers=1\n"
                                                              "ReplaceOlderThan=2024-02-29T13:05:00\n"
                                                              "RetryMissing=true\n"
                                                              "RetryMissingOlderThan=2023-01-02\n"
                                                              "AutosaveMin=12\n"
                                                              "StallTimeout=123\n"
                                                              "MaxConnectionErrors=8\n"
                                                              "SaveRetries=9\n"
                                                              "Strict=1\n"
                                                              "AllowWebp=true\n"
                                                              "Proxy=http://h:3128\n"
                                                              "AssumeTileKb=44\n");
    const Job job = mustParse({"run", "--job", file});
    EXPECT_EQ(job.source, QStringLiteral("src"));
    EXPECT_EQ(job.overlay, QStringLiteral("ovl"));
    EXPECT_EQ(job.areaKind, AreaKind::BBox);
    EXPECT_EQ(job.bbox.left, 24.5);
    EXPECT_EQ(job.bbox.bottom, 59.3);
    EXPECT_EQ(job.bbox.right, 24.9);
    EXPECT_EQ(job.bbox.top, 59.5);
    EXPECT_EQ(job.zooms, (Zooms{8, 9, 14}));
    ASSERT_TRUE(job.time.has_value());
    EXPECT_FALSE(job.time->relative);
    EXPECT_EQ(job.time->instant.toString(Qt::ISODate).toStdString(), "2026-10-18T12:00:00Z");
    EXPECT_EQ(job.out, tmp.root() + QStringLiteral("/out/base.mbtiles"));
    EXPECT_EQ(job.overlayOut, tmp.root() + QStringLiteral("/out/over.mbtiles"));
    EXPECT_EQ(job.name, QStringLiteral("Name1"));
    EXPECT_EQ(job.description, QStringLiteral("Desc1"));
    EXPECT_EQ(job.attribution, QStringLiteral("Attr1"));
    EXPECT_TRUE(job.overwrite);
    EXPECT_EQ(job.threads, 6);
    EXPECT_TRUE(job.restart);
    EXPECT_TRUE(job.replace);
    EXPECT_TRUE(job.replaceIfSizeDiffers);
    EXPECT_EQ(job.replaceOlderThan, local(2024, 2, 29, 13, 5, 0));
    EXPECT_TRUE(job.retryMissing);
    EXPECT_EQ(job.retryMissingOlderThan, local(2023, 1, 2, 0, 0, 0));
    EXPECT_EQ(job.autosaveMin, 12);
    EXPECT_EQ(job.stallTimeoutSec, 123);
    EXPECT_EQ(job.maxConnectionErrors, 8);
    EXPECT_EQ(job.saveRetries, 9);
    EXPECT_TRUE(job.strict);
    EXPECT_TRUE(job.allowWebp);
    EXPECT_EQ(job.proxy, QStringLiteral("http://h:3128"));
    EXPECT_EQ(job.assumeTileKb, 44);
}

TEST(JobFile, PolygonAndRouteKeysAndRelativePath) {
    TempDir tmp;
    const QString poly = tmp.write(QStringLiteral("p.ini"), "[Job]\nSource=s\nPolygon=sub/a.hlg\n");
    const Job a = mustParse({"estimate", "--job", poly});
    EXPECT_EQ(a.areaKind, AreaKind::PolygonFile);
    EXPECT_EQ(a.polygonFile, tmp.root() + QStringLiteral("/sub/a.hlg"));

    const QString route = tmp.write(QStringLiteral("r.ini"),
                                    "[Job]\nSource=s\nRoute=tracks/r.gpx\nRouteName=Рейс\nCorridorNm=2.5\n");
    const Job b = mustParse({"estimate", "--job", route});
    EXPECT_EQ(b.areaKind, AreaKind::Route);
    EXPECT_EQ(b.routeFile, tmp.root() + QStringLiteral("/tracks/r.gpx"));
    EXPECT_EQ(b.routeName, QStringLiteral("Рейс"));
    EXPECT_EQ(b.corridorNm, 2.5);
}

// T-0357: the job file may have any name: .conf (the documented one) and .ini (older versions) both load.
TEST(JobFile, ConfAndIniExtensionsBothLoad) {
    TempDir tmp;
    for (const char* name : {"job.conf", "job.ini", "job"}) {
        const QString file =
            tmp.write(QString::fromLatin1(name), "[Job]\nSource=s\nBBox=1,2,3,4\nOut=o.mbtiles\n");
        EXPECT_EQ(mustParse({"export", "--job", file}).out, tmp.root() + QStringLiteral("/o.mbtiles"))
            << name;
    }
}

TEST(JobFile, RelativePathOfTheFileIsNotTheCurrentDirectory) {
    TempDir tmp, other;
    const QString file =
        tmp.write(QStringLiteral("job.ini"), "[Job]\nSource=s\nBBox=1,2,3,4\nOut=o.mbtiles\n");
    CurrentDir cwd(other.root());
    EXPECT_EQ(mustParse({"export", "--job", file}).out, tmp.root() + QStringLiteral("/o.mbtiles"));
    // A flag path resolves against the current directory.
    EXPECT_EQ(mustParse({"export", "--job", file, "--out=f.mbtiles"}).out,
              other.root() + QStringLiteral("/f.mbtiles"));
}

TEST(JobFile, FlagBeatsFileForTheSameKey) {
    TempDir tmp;
    const QString file = tmp.write(QStringLiteral("job.ini"),
                                   "[Job]\nSource=fromfile\nBBox=1,2,3,4\nZooms=1-2\n[Download]\nThreads=3\n"
                                   "Proxy=http://file:1\nAutosaveMin=8\n");
    const Job job =
        mustParse({"download", "--job", file, "--threads=9", "--zooms", "5", "--source=fromflag"});
    EXPECT_EQ(job.threads, 9);
    EXPECT_EQ(job.zooms, (Zooms{5}));
    EXPECT_EQ(job.source, QStringLiteral("fromflag"));
    // Keys not overridden stay from the file.
    EXPECT_EQ(job.proxy, QStringLiteral("http://file:1"));
    EXPECT_EQ(job.autosaveMin, 8);
    // An area flag replaces the area of the file (flags override the file; the ticket's order
    // file-then-flags).
    const QString poly = tmp.write(QStringLiteral("p.ini"), "[Job]\nSource=s\nPolygon=a.hlg\n");
    const Job area = mustParse({"estimate", "--job", poly, "--bbox=1,2,3,4"});
    EXPECT_EQ(area.areaKind, AreaKind::BBox);
    EXPECT_TRUE(area.polygonFile.isEmpty());
    // The flag comes first on the command line: the order of the arguments does not matter.
    EXPECT_EQ(mustParse({"download", "--threads=9", "--job", file}).threads, 9);
}

// ---- proof 3: zoom lists ----

TEST(JobZooms, BothFormsAndNormalisation) {
    const std::vector<std::pair<QString, Zooms>> good = {
        {"8-10,12", {8, 9, 10, 12}},
        {"14,8-9,9", {8, 9, 14}},
        {"0", {0}},
        {"23", {23}},
        {"5,5,5", {5}},
        {"12,3", {3, 12}},
        {"7-7", {7}},
    };
    for (const auto& [text, expected] : good) {
        const auto zooms = parseZoomList(text);
        ASSERT_TRUE(zooms.has_value()) << text.toStdString();
        EXPECT_EQ(*zooms, expected) << text.toStdString();
    }
    const auto all = parseZoomList(QStringLiteral("0-23"));
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 24U);
    EXPECT_EQ(all->front(), 0);
    EXPECT_EQ(all->back(), 23);

    EXPECT_EQ(mustParse({"estimate", "--source=s", "--bbox=1,2,3,4", "--zooms=8-10,12"}).zooms,
              (Zooms{8, 9, 10, 12}));
    EXPECT_EQ(mustParse({"estimate", "--source=s", "--bbox=1,2,3,4", "--zooms", "8-10,12"}).zooms,
              (Zooms{8, 9, 10, 12}));
    EXPECT_EQ(mustParse({"estimate", "--source=s", "--bbox=1,2,3,4", "--zooms=14,8-9,9"}).zooms,
              (Zooms{8, 9, 14}));
}

TEST(JobZooms, BadFormsAreUsageErrorsNamingTheOption) {
    for (const QString bad : {"", "a", "5-3", "24", "-1", "8-24", "3,,4", "1-2-3", "8-"}) {
        EXPECT_FALSE(parseZoomList(bad).has_value()) << bad.toStdString();
        expectUsageError({"estimate", "--source=s", "--bbox=1,2,3,4", "--zooms=" + bad}, "--zooms");
    }
    // Same through the job file.
    TempDir tmp;
    const QString file = tmp.write(QStringLiteral("j.ini"), "[Job]\nSource=s\nBBox=1,2,3,4\nZooms=5-3\n");
    expectUsageError({"estimate", "--job", file}, "--zooms");
}

// T-0283: --time (the forms themselves are tested in snapshot_test.cpp, parseSnapshotTime).
TEST(JobTime, FlagFormsAndUsageErrors) {
    const QStringList base = {"download", "--source=s", "--bbox=1,2,3,4", "--zooms=3"};
    EXPECT_FALSE(mustParse(base).time.has_value()); // absent = latest
    const Job relative = mustParse(base + QStringList{"--time=+24h"});
    ASSERT_TRUE(relative.time.has_value());
    EXPECT_TRUE(relative.time->relative);
    EXPECT_EQ(relative.time->hours, 24);
    const Job spaced = mustParse(base + QStringList{"--time", " 2026-10-18T12:00:00Z "}); // trimmed
    ASSERT_TRUE(spaced.time.has_value());
    EXPECT_EQ(spaced.time->instant.toString(Qt::ISODate).toStdString(), "2026-10-18T12:00:00Z");
    expectUsageError(base + QStringList{"--time=tomorrow"}, "--time");
    expectUsageError(base + QStringList{"--time=tomorrow"}, "\"tomorrow\" is not a time such as");
    TempDir tmp;
    const QString file =
        tmp.write(QStringLiteral("t.ini"), "[Job]\nSource=s\nBBox=1,2,3,4\nZooms=3\nTime=+1.5h\n");
    expectUsageError({"download", "--job", file}, "--time");
}

// ---- proof 4: usage errors ----

TEST(JobErrors, CommandLine) {
    expectUsageError({}, "command");
    expectUsageError({"frobnicate"}, "frobnicate");
    expectUsageError({"sources", "--no-such-flag"}, "no-such-flag");
    expectUsageError({"sources", "extra"}, "extra");
    const auto flagClash = parse({"estimate", "--source=s", "--bbox=1,2,3,4", "--polygon=a.hlg"});
    ASSERT_FALSE(flagClash.has_value());
    EXPECT_EQ(flagClash.error().exitCode, 3); // spec 1.7 S-3: a refusal, not a usage error
    EXPECT_TRUE(flagClash.error().message.contains(QStringLiteral("--polygon")));
    expectUsageError({"estimate", "--source=s", "--route=r.gpx"}, "--corridor-nm");
    expectUsageError({"estimate", "--source=s", "--bbox=1,2,3,4", "--corridor-nm=2"}, "--corridor-nm");
    expectUsageError({"export", "--source=s", "--bbox=1,2,3,4"}, "--out");
    expectUsageError({"run", "--source=s", "--bbox=1,2,3,4"}, "--out");
    expectUsageError({"resume"}, "resume");
    expectUsageError({"estimate", "--bbox=1,2,3,4"}, "--source");
    expectUsageError({"estimate", "--source=s"}, "--bbox");
    expectUsageError({"sources", "-v", "-q"}, "-q");
    expectUsageError({"sources", "-q", "-v"}, "-v");
    expectUsageError({"download", "--source=s", "--bbox=1,2,3,4", "--threads=0"}, "--threads");
    expectUsageError({"download", "--source=s", "--bbox=1,2,3,4", "--threads=65"}, "--threads");
    expectUsageError({"download", "--source=s", "--bbox=1,2,3,4", "--threads=x"}, "--threads");
    expectUsageError({"download", "--source=s", "--bbox=1,2,3,4", "--threads=-1"}, "--threads");
    expectUsageError({"estimate", "--source=s", "--route=r.gpx", "--corridor-nm=0"}, "--corridor-nm");
    expectUsageError({"estimate", "--source=s", "--route=r.gpx", "--corridor-nm=-2"}, "--corridor-nm");
    expectUsageError({"estimate", "--source=s", "--route=r.gpx", "--corridor-nm=a"}, "--corridor-nm");
}

TEST(JobErrors, ThreadsBoundariesAreAccepted) {
    EXPECT_EQ(mustParse({"download", "--source=s", "--bbox=1,2,3,4", "--threads=1"}).threads, 1);
    EXPECT_EQ(mustParse({"download", "--source=s", "--bbox=1,2,3,4", "--threads=64"}).threads, 64);
}

TEST(JobErrors, BBoxMustBeFourDecimalNumbers) {
    for (const QString bad : {"1,2,3", "1,2,3,4,5", "a,2,3,4", "1;2;3;4", "", "1,2,3,"}) {
        expectUsageError({"estimate", "--source=s", "--bbox=" + bad}, "--bbox");
    }
    // The decimal point is '.', a comma cannot be one (u_CmdLineArgProcessorHelpers.pas:124-143 str2r).
    expectUsageError({"estimate", "--source=s", "--bbox=24,5,59,3,24,9,59,5"}, "--bbox");
}

TEST(JobErrors, ProbeZoomRange) {
    EXPECT_EQ(mustParse({"probe", "--probe-zoom=23"}).probeZoom, 23);
    EXPECT_EQ(mustParse({"probe", "--probe-zoom=0"}).probeZoom, 0);
    expectUsageError({"probe", "--probe-zoom=24"}, "--probe-zoom");
    expectUsageError({"probe", "--probe-zoom=-1"}, "--probe-zoom");
}

TEST(JobErrors, BBoxUsesTheCLocale) {
    GermanDefaultLocale german;
    const Job job = mustParse({"estimate", "--source=s", "--bbox=24.5,59.3,24.9,59.5"});
    EXPECT_EQ(job.bbox.left, 24.5);
    EXPECT_EQ(job.bbox.right, 24.9);
}

TEST(JobErrors, JobFileThatDoesNotExist) {
    TempDir tmp;
    const QString missing = tmp.root() + QStringLiteral("/nope.ini");
    expectUsageError({"sources", "--job", missing}, QStringLiteral("--job: cannot read \"%1\"").arg(missing));
}

TEST(JobErrors, JobFile) {
    TempDir tmp;
    const QString jobKey = tmp.write(QStringLiteral("a.ini"), "[Job]\nSource=s\nBogusKey=1\n");
    expectUsageError({"sources", "--job", jobKey},
                     QStringLiteral("unknown key \"BogusKey\" in [Job] of ") + jobKey);
    const QString dlKey = tmp.write(QStringLiteral("b.ini"), "[Download]\nWorkers=1\n");
    expectUsageError({"sources", "--job", dlKey},
                     QStringLiteral("unknown key \"Workers\" in [Download] of ") + dlKey);
    // A key of [Download] in [Job] is unknown too.
    const QString wrong = tmp.write(QStringLiteral("c.ini"), "[Job]\nThreads=2\n");
    expectUsageError({"sources", "--job", wrong}, "unknown key \"Threads\" in [Job]");
    const QString twoAreas =
        tmp.write(QStringLiteral("d.ini"), "[Job]\nSource=s\nBBox=1,2,3,4\nPolygon=a.hlg\n");
    {
        const auto clash = parse({"estimate", "--job", twoAreas});
        ASSERT_FALSE(clash.has_value());
        EXPECT_EQ(clash.error().exitCode, 3); // spec 1.7 S-3: a refusal, not a usage error
        EXPECT_TRUE(clash.error().message.contains(QStringLiteral("--polygon")))
            << clash.error().message.toStdString();
    }
    const QString badValue = tmp.write(QStringLiteral("e.ini"), "[Download]\nThreads=65\n");
    expectUsageError({"sources", "--job", badValue}, "--threads");
    const QString badDate = tmp.write(QStringLiteral("f.ini"), "[Download]\nReplaceOlderThan=29.02.2024\n");
    expectUsageError({"sources", "--job", badDate}, "--replace-older-than");
    // The semantic checks run after the file: a file without an area fails for estimate.
    const QString noArea = tmp.write(QStringLiteral("g.ini"), "[Job]\nSource=s\n");
    expectUsageError({"estimate", "--job", noArea}, "--bbox");
    // ... and a flag completes it.
    EXPECT_EQ(mustParse({"estimate", "--job", noArea, "--bbox=1,2,3,4"}).areaKind, AreaKind::BBox);
}

// ---- proof 5: help / version / verbosity ----

TEST(JobCommands, HelpVersionVerbosity) {
    EXPECT_EQ(mustParse({"--help"}).command, Command::Help);
    EXPECT_EQ(mustParse({"-h"}).command, Command::Help);
    EXPECT_EQ(mustParse({"estimate", "--help"}).command, Command::Help);
    EXPECT_EQ(mustParse({"--version"}).command, Command::Version);

    const Job verbose = mustParse({"sources", "-v"});
    EXPECT_EQ(verbose.command, Command::Sources);
    EXPECT_EQ(verbose.verbosity, 1);
    const Job quiet = mustParse({"sources", "-q"});
    EXPECT_EQ(quiet.verbosity, -1);
    EXPECT_EQ(mustParse({"sources"}).verbosity, 0);
    EXPECT_FALSE(mustParse({"sources", "--no-progress"}).progress);
}

// T-0332: opt-in switch, off by default
TEST(JobCommands, StopOnStdinIsAnOptInSwitch) {
    EXPECT_FALSE(mustParse({"sources"}).stopOnStdin);
    EXPECT_TRUE(mustParse({"sources", "--stop-on-stdin"}).stopOnStdin);
    EXPECT_TRUE(cg::app::helpText().contains(QStringLiteral("--stop-on-stdin")));
}

TEST(JobCommands, EveryCommandWord) {
    EXPECT_EQ(mustParse({"sources"}).command, Command::Sources);
    EXPECT_EQ(mustParse({"probe"}).command, Command::Probe);
    EXPECT_EQ(mustParse({"resume", "a.sls"}).command, Command::Resume);
    EXPECT_EQ(mustParse({"estimate", "--source=s", "--bbox=1,2,3,4"}).command, Command::Estimate);
    EXPECT_EQ(mustParse({"export", "--source=s", "--bbox=1,2,3,4", "--out=o"}).command, Command::Export);
}

// ---- proof 6: dates ----

TEST(JobDates, ValidAndInvalid) {
    const QStringList base = {"download", "--source=s", "--bbox=1,2,3,4"};
    EXPECT_EQ(mustParse(base + QStringList{"--replace-older-than=2024-02-29"}).replaceOlderThan,
              local(2024, 2, 29, 0, 0, 0));
    EXPECT_EQ(mustParse(base + QStringList{"--replace-older-than=2024-02-29T13:05:00"}).replaceOlderThan,
              local(2024, 2, 29, 13, 5, 0));
    EXPECT_EQ(
        mustParse(base + QStringList{"--retry-missing-older-than=2024-02-29T13:05:07"}).retryMissingOlderThan,
        local(2024, 2, 29, 13, 5, 7));
    for (const QString bad : {"2023-02-29", "29.02.2024", "2024-02-29T25:00:00", "2024-02-29T13:05",
                              "2024-2-29", "2024-02-29T1:05:00", "2024-02-29T13:5:00", "2024-02-29T13:05:0",
                              "", "2024-02-29 13:05:00", "yesterday"}) {
        expectUsageError(base + QStringList{"--replace-older-than=" + bad}, "--replace-older-than");
        expectUsageError(base + QStringList{"--retry-missing-older-than=" + bad},
                         "--retry-missing-older-than");
    }
}

// ---- help text ----

TEST(JobHelp, ListsEveryOptionAndTheNotes) {
    const QString text = cg::app::helpText();
    for (const char* flag : {"--job=FILE",
                             "--source=ID",
                             "--overlay=ID",
                             "--bbox=W,S,E,N",
                             "--polygon=FILE",
                             "--route=FILE",
                             "--route-name=NAME",
                             "--corridor-nm=N",
                             "--zooms=LIST",
                             "--out=PATH",
                             "--overlay-out=FILE",
                             "--name=TEXT",
                             "--description=TEXT",
                             "--attribution=TEXT",
                             "--overwrite",
                             "--threads=N",
                             "--restart",
                             "--replace ",
                             "--replace-if-size-differs",
                             "--replace-older-than=DATE",
                             "--retry-missing ",
                             "--retry-missing-older-than=DATE",
                             "--autosave-min=N",
                             "--stall-timeout=SEC",
                             "--max-connection-errors=N",
                             "--save-retries=N",
                             "--strict",
                             "--allow-webp",
                             "--proxy=URL",
                             "--assume-tile-kb=N",
                             "--maps=DIR",
                             "--cache=DIR",
                             "--filter=TEXT",
                             "--at=LON,LAT",
                             "--probe-zoom=Z",
                             "-v ",
                             "-q ",
                             "--no-progress",
                             "-h, --help",
                             "--version",
                             "--datadir=DIR",
                             "--portable"}) {
        EXPECT_TRUE(text.contains(QLatin1String(flag))) << flag;
    }
    EXPECT_TRUE(text.contains("Zoom levels are 0-based (OpenCPN, MBTiles); SAS.Planet shows them 1-based."));
    EXPECT_TRUE(text.contains("--corridor-nm is the total width of the corridor."));
    EXPECT_TRUE(text.contains("Examples:"));
    for (const char* word : {"sources", "estimate", "download", "export", "run", "resume", "probe"}) {
        EXPECT_TRUE(text.contains(QStringLiteral("chart-grabber-cli %1 ").arg(QLatin1String(word)))) << word;
    }
    // Group order: job, download, common.
    EXPECT_LT(text.indexOf("--source=ID"), text.indexOf("--threads=N"));
    EXPECT_LT(text.indexOf("--threads=N"), text.indexOf("--maps=DIR"));
}

// ---- T-0202 proof 1: --format and --progress ----

using namespace Qt::StringLiterals;

TEST(JobFormatAndProgress, BothSpellingsOfTheValueParseAndDefaultsAreTextAndAuto) {
    using cg::app::OutputFormat;
    using cg::app::ProgressMode;
    const Job defaults = mustParse({"sources"});
    EXPECT_EQ(defaults.format, OutputFormat::Text);
    EXPECT_EQ(defaults.progressMode, ProgressMode::Auto);
    EXPECT_TRUE(defaults.progress);

    EXPECT_EQ(mustParse({"sources", "--format=tsv"}).format, OutputFormat::Tsv);
    EXPECT_EQ(mustParse({"sources", "--format", "tsv"}).format, OutputFormat::Tsv);
    EXPECT_EQ(mustParse({"sources", "--format=text"}).format, OutputFormat::Text);
    EXPECT_EQ(mustParse({"sources", "--progress=lines"}).progressMode, ProgressMode::Lines);
    EXPECT_EQ(mustParse({"sources", "--progress", "lines"}).progressMode, ProgressMode::Lines);
    EXPECT_TRUE(mustParse({"sources", "--progress=lines"}).progress);
    EXPECT_EQ(mustParse({"sources", "--progress=auto"}).progressMode, ProgressMode::Auto);
}

TEST(JobFormatAndProgress, OtherValuesAreUsageErrorsWithTheSpecMessages) {
    expectUsageError({"sources", "--format=xml"}, u"--format: expected text or tsv"_s);
    expectUsageError({"sources", "--progress=verbose"}, u"--progress: expected auto, lines or off"_s);
    // exactly once: the option prefix is added by the caller of the setter
    EXPECT_EQ(parse({"sources", "--format=xml"}).error().message, u"--format: expected text or tsv"_s);
    EXPECT_EQ(parse({"sources", "--progress=verbose"}).error().message,
              u"--progress: expected auto, lines or off"_s);
}

TEST(JobFormatAndProgress, ProgressOffEqualsNoProgressAndNoProgressBeatsAuto) {
    using cg::app::ProgressMode;
    const Job alias = mustParse({"sources", "--no-progress"});
    const Job off = mustParse({"sources", "--progress=off"});
    EXPECT_EQ(alias.progressMode, ProgressMode::Off);
    EXPECT_EQ(off.progressMode, alias.progressMode);
    EXPECT_FALSE(alias.progress);
    EXPECT_FALSE(off.progress);
    // the explicit alias wins whatever the order of the flags
    EXPECT_EQ(mustParse({"sources", "--no-progress", "--progress=auto"}).progressMode, ProgressMode::Off);
    EXPECT_FALSE(mustParse({"sources", "--no-progress", "--progress=lines"}).progress);
}

TEST(JobFormatAndProgress, HelpListsBothOptions) {
    const QString help = cg::app::helpText();
    EXPECT_TRUE(help.contains(u"--format=FORMAT"_s)) << help.toStdString();
    EXPECT_TRUE(help.contains(u"--progress=MODE"_s)) << help.toStdString();
}

// T-0291 (app-cli.md 1.6.1, 1.6.3): --maps is repeatable and required in `create`, --user-maps is gone. No
// FEAT.
TEST(JobMaps, RepeatedFlagKeepsEveryFolderInTheOrderGiven) {
    const TempDir tmp;
    CurrentDir cwd(tmp.root());
    // absolute names are built from the temporary folder: "/abs/a" is not absolute on Windows (no drive
    // letter)
    const QString a = tmp.root() + u"/abs/a"_s;
    const QString c = tmp.root() + u"/abs/c"_s;
    const QString x = tmp.root() + u"/x"_s;
    const Job job = mustParse({u"sources"_s, u"--maps"_s, a, u"--maps=rel/b"_s, u"--maps"_s, c});
    EXPECT_EQ(job.mapsDirs, (QStringList{a, tmp.root() + u"/rel/b"_s, c}));
    // the same folder twice stays twice here; AppContext::create makes it once
    const QString xOption = u"--maps="_s + x;
    EXPECT_EQ(mustParse({u"sources"_s, xOption, xOption}).mapsDirs, (QStringList{x, x}));
    // no --maps still parses: the refusal is in AppContext::create
    EXPECT_TRUE(mustParse({"sources"}).mapsDirs.isEmpty());
}

TEST(JobMaps, EmptyValueIsAUsageErrorEvenAfterAGoodFolder) {
    expectUsageError({"sources", "--maps="}, u"--maps: an empty folder name"_s);
    expectUsageError({"sources", "--maps", "/good", "--maps="}, u"--maps: an empty folder name"_s);
}

TEST(JobMaps, UserMapsOptionAndJobKeyAreUnknown) {
    expectUsageError({"sources", "--user-maps", "X"}, u"user-maps"_s);
    const TempDir tmp;
    const QString file = tmp.write(u"j.ini"_s, "[Job]\r\nUserMaps=X\r\n");
    expectUsageError({"sources", "--job", file}, u"unknown key \"UserMaps\" in [Job] of "_s + file);
    // --maps is not a job-file key either
    const QString maps = tmp.write(u"m.ini"_s, "[Job]\r\nMaps=X\r\n");
    expectUsageError({"sources", "--job", maps}, u"unknown key \"Maps\" in [Job] of "_s + maps);
}

TEST(JobMaps, HelpDescribesMapsAndSaysSourcesAreNotBuiltIn) {
    const QString help = cg::app::helpText();
    EXPECT_TRUE(help.contains(u"--maps=DIR"_s)) << help.toStdString();
    EXPECT_TRUE(help.contains(u"folder of map-source definitions (*.zmp, *.source.json); required; repeat "
                              u"for more folders, a later "
                              "folder wins on the same GUID"_s))
        << help.toStdString();
    EXPECT_TRUE(help.contains(u"Map sources are not built in: every command except -h and --version needs at "
                              "least one --maps folder."_s));
    EXPECT_TRUE(help.contains(u"--maps=./map-sources --source=\"OpenStreetMap\""_s));
    EXPECT_TRUE(help.contains(u"run --job=passage.conf --maps=./map-sources --threads=4"_s));
    EXPECT_FALSE(help.contains(u"user-maps"_s));
}

// T-0360 item 2: a backslash in a job-file path (Windows) must not leave a mixed-slash path: the same form
// as a flag path (forward slashes, T-0335). On Linux a backslash is a file name character, so the check is
// about what the platform calls a separator.
TEST(JobFile, OutPathOfTheFileUsesForwardSlashes) {
    TempDir tmp;
    const QString file =
        tmp.write(QStringLiteral("job.ini"),
                  "[Job]\nSource=s\nBBox=1,2,3,4\nOut=charts\\x.mbtiles\nOverlayOut=a\\b.mbtiles\n");
    const Job job = mustParse({"export", "--job", file});
    const bool backslashSeparates = QDir::separator() == QLatin1Char('\\'); // Windows
    EXPECT_EQ(job.out, tmp.root() + (backslashSeparates ? QStringLiteral("/charts/x.mbtiles")
                                                        : QStringLiteral("/charts\\x.mbtiles")));
    EXPECT_EQ(job.overlayOut, tmp.root() + (backslashSeparates ? QStringLiteral("/a/b.mbtiles")
                                                               : QStringLiteral("/a\\b.mbtiles")));
}
