// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of the `sources` command with a *.source.json source (T-0224 acceptance 4). NEW code, no FEAT id:
// the expectations are the literals of docs/specs/map-source-json.md 6.1, 6.3 and the row format of
// tests/app/cli_main_test.cpp (GUID, kind, name, content type, EPSG, cache type, verdict).
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "../maptype/maptype_test_util.h"
#include "cli_test_util.h"

using namespace cg::app::test;
using namespace Qt::StringLiterals;

namespace {

QByteArray readFile(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return f.readAll();
}

void copyFixture(const QString& name, const QString& to) {
    QFile out(to);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(readFile(cg::maptype::test::fixtureDir() + u"/maptype/json/"_s + name));
}

QStringList commonArgs(const QString& root) {
    return {u"--maps"_s,
            root + u"/Maps"_s,
            u"--cache"_s,
            root + u"/cache"_s,
            u"--datadir="_s + root + u"/data"_s,
            u"--no-progress"_s};
}

QStringList rows(const QString& text) {
    QStringList out;
    for (const QString& line : text.split(u'\n', Qt::SkipEmptyParts)) {
        out.push_back(line.simplified());
    }
    return out;
}

TEST(CliSourceJson, SourcesListsJsonSourcesBesideZmps) {
    cg::maptype::test::ensureGuiApp();
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString maps = tmp.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(maps + u"/deep"_s));
    writeZmp(maps, {.folder = u"z.zmp"_s,
                    .guid = kGuidAlt,
                    .name = "Alt Path Map",
                    .urlBase = "http://127.0.0.1:9/{z}"});
    copyFixture(u"ex63.source.json"_s, maps + u"/ex63.source.json"_s);
    copyFixture(u"ex61.source.json"_s, maps + u"/deep/ex61.source.json"_s);
    const Captured c = runCapture(QStringList{u"sources"_s} + commonArgs(tmp.path()));
    EXPECT_EQ(c.rc, 0);
    EXPECT_EQ(c.err, QString());
    const QStringList r = rows(c.out);
    ASSERT_EQ(r.size(), 4) << qPrintable(c.out); // header + 3, sorted by name
    EXPECT_TRUE(r[0].startsWith(u"GUID kind name"_s)) << qPrintable(r[0]);
    EXPECT_TRUE(QRegularExpression(
                    u"^\\{AB0000A1-0000-0000-0000-0000000000A1\\} map Alt Path Map image/png 3857 \\d+ ok$"_s)
                    .match(r[1])
                    .hasMatch())
        << qPrintable(r[1]);
    EXPECT_TRUE(QRegularExpression(
                    u"^\\{51C1079B-B55D-4DF7-A410-ADC118F53AB1\\} map OpenTopoMap image/png 3857 \\d+ ok$"_s)
                    .match(r[3])
                    .hasMatch())
        << qPrintable(r[3]);
    EXPECT_TRUE(
        QRegularExpression(
            u"^\\{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777\\} layer OpenSeaMap Seamarks image/png 3857 \\d+ ok$"_s)
            .match(r[2])
            .hasMatch())
        << qPrintable(r[2]);
}

TEST(CliSourceJson, ABrokenJsonSourceIsReportedLikeABrokenZmpAndTheRestIsListed) {
    cg::maptype::test::ensureGuiApp();
    QTemporaryDir tmp;
    const QString maps = tmp.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(maps));
    copyFixture(u"ex63.source.json"_s, maps + u"/ok.source.json"_s);
    QFile bad(maps + u"/bad.source.json"_s);
    ASSERT_TRUE(bad.open(QIODevice::WriteOnly));
    bad.write(R"({"format":1,"colour":"red"})");
    bad.close();
    const Captured json = runCapture(QStringList{u"sources"_s} + commonArgs(tmp.path()));
    // the same report for a ZMP that breaks a rule of its own (no GUID)
    QTemporaryDir tmpZ;
    const QString mapsZ = tmpZ.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(mapsZ + u"/bad.zmp"_s));
    writeZmp(mapsZ, {.folder = u"z.zmp"_s,
                     .guid = kGuidAlt,
                     .name = "Alt Path Map",
                     .urlBase = "http://127.0.0.1:9/{z}"});
    QFile p(mapsZ + u"/bad.zmp/params.txt"_s);
    ASSERT_TRUE(p.open(QIODevice::WriteOnly));
    p.write("[PARAMS]\r\nname=x\r\n");
    p.close();
    const Captured zmp = runCapture(QStringList{u"sources"_s} + commonArgs(tmpZ.path()));
    EXPECT_EQ(json.rc, zmp.rc);
    EXPECT_TRUE(json.err.contains(u"There is an error "_s)) << qPrintable(json.err);
    EXPECT_TRUE(json.err.contains(u" in map bad.source.json"_s)) << qPrintable(json.err);
    EXPECT_TRUE(json.err.contains(u"colour"_s)) << qPrintable(json.err);
    EXPECT_TRUE(zmp.err.contains(u"There is an error "_s)) << qPrintable(zmp.err);
    EXPECT_TRUE(zmp.err.contains(u" in map bad.zmp"_s)) << qPrintable(zmp.err);
    const QStringList r = rows(json.out);
    ASSERT_EQ(r.size(), 2) << qPrintable(json.out);
    EXPECT_TRUE(r[1].contains(u"OpenTopoMap"_s)) << qPrintable(r[1]);
}

} // namespace
