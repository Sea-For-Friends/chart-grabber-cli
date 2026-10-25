// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0366 (TL ruling 2026-10-24: a port extension, no legacy behaviour to match): a *.source.json source whose
// script does not load shows its reason in `sources` and `probe`; a getUrl that throws stays a failure of
// its tile. NEW code, no FEAT id; the expected texts are literals of the QJSEngine messages and the line
// number of the script text.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "../maptype/maptype_test_util.h"
#include "cli_test_util.h"

using namespace cg::app::test;
using namespace Qt::StringLiterals;

namespace {

void writeJsonSource(const QString& maps, const QString& script) {
    const QJsonObject source{{u"format"_s, 1},
                             {u"guid"_s, u"{00000000-0000-4000-8000-000000000366}"_s},
                             {u"name"_s, u"Broken script"_s},
                             {u"url"_s, u"http://127.0.0.1:9/{z}/{x}/{y}.png"_s},
                             {u"contentType"_s, u"image/png"_s},
                             {u"ext"_s, u".png"_s},
                             {u"nameInCache"_s, u"broken_script"_s},
                             {u"allowHttp"_s, true},
                             {u"script"_s, QJsonObject{{u"code"_s, script}}}};
    QFile out(maps + u"/broken.source.json"_s);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(QJsonDocument(source).toJson());
}

struct Row {
    const char* name;
    const char* script;
    const char* sourcesVerdict; // the text after the cache column of `sources`
    const char* probeResult;    // the text of `probe`
};

} // namespace

TEST(CliJsScriptVerdict, ASourceWhoseScriptDoesNotLoadShowsWhyInSourcesAndProbe) {
    cg::maptype::test::ensureGuiApp();
    const Row rows[] = {
        {"syntax error on line 2", "function getUrl(tile, ctx) {\n  return ;;; }}\n",
         "the download subsystem of this map is disabled: Expected token `;' at line 2",
         "skipped: the download subsystem of this map is disabled: Expected token `;' at line 2"},
        {"no getUrl", "var x = 1;\n",
         "the download subsystem of this map is disabled: function getUrl(tile, ctx) is not defined",
         "skipped: the download subsystem of this map is disabled: function getUrl(tile, ctx) is not "
         "defined"},
        {"the script of docs/user/examples/sources/broken/script-error/jssyntax.source.json",
         "function getUrl(tile, ctx) { return ctx.urlBase + tile.x + \".png\" ",
         "the download subsystem of this map is disabled: Expected token `;' at line 1",
         "skipped: the download subsystem of this map is disabled: Expected token `;' at line 1"},
        {"top-level code throws on line 3", "var a = 1;\nvar b = 2;\nthrow Error('boom');\n",
         "the download subsystem of this map is disabled: boom at line 3",
         "skipped: the download subsystem of this map is disabled: boom at line 3"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        QTemporaryDir tmp;
        ASSERT_TRUE(tmp.isValid());
        const QString maps = tmp.filePath(u"Maps"_s);
        ASSERT_TRUE(QDir().mkpath(maps));
        writeJsonSource(maps, QString::fromUtf8(row.script));
        const QStringList common{u"--maps"_s,
                                 maps,
                                 u"--cache"_s,
                                 tmp.filePath(u"cache"_s),
                                 u"--datadir="_s + tmp.filePath(u"data"_s),
                                 u"--no-progress"_s};
        const Captured sources = runCapture(QStringList{u"sources"_s} + common);
        EXPECT_EQ(sources.rc, 0);
        EXPECT_TRUE(sources.out.simplified().endsWith(QString::fromUtf8(row.sourcesVerdict)))
            << qPrintable(sources.out);
        const Captured probe =
            runCapture(QStringList{u"probe"_s, u"--at=10,10"_s, u"--probe-zoom=3"_s} + common);
        EXPECT_TRUE(probe.out.simplified().endsWith(QString::fromUtf8(row.probeResult)))
            << qPrintable(probe.out);
        EXPECT_EQ(probe.out.count(u"Tile does not exist"_s), 0) << qPrintable(probe.out);
        // `download` refuses to start with the same text
        const Captured download = runCapture(QStringList{u"download"_s, u"--source"_s, u"Broken script"_s,
                                                         u"--bbox=10,10,11,11"_s, u"--zooms=3"_s} +
                                             common);
        EXPECT_NE(download.rc, 0);
        EXPECT_TRUE(download.err.contains(QString::fromUtf8(row.sourcesVerdict))) << qPrintable(download.err);
    }
}

TEST(CliJsScriptVerdict, AGetUrlThatThrowsAtTheFirstTileIsAFailureOfThatTileOnly) {
    cg::maptype::test::ensureGuiApp();
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString maps = tmp.filePath(u"Maps"_s);
    ASSERT_TRUE(QDir().mkpath(maps));
    writeJsonSource(maps, u"function getUrl(tile, ctx) {\n  throw Error('no tile today');\n}\n"_s);
    const QStringList common{u"--maps"_s,
                             maps,
                             u"--cache"_s,
                             tmp.filePath(u"cache"_s),
                             u"--datadir="_s + tmp.filePath(u"data"_s),
                             u"--no-progress"_s};
    const Captured sources = runCapture(QStringList{u"sources"_s} + common);
    EXPECT_EQ(sources.rc, 0);
    EXPECT_TRUE(sources.out.simplified().endsWith(u" ok"_s)) << qPrintable(sources.out);
    const Captured probe = runCapture(QStringList{u"probe"_s, u"--at=10,10"_s, u"--probe-zoom=3"_s} + common);
    EXPECT_TRUE(probe.out.simplified().endsWith(u"error: no tile today at line 2"_s))
        << qPrintable(probe.out);
}
