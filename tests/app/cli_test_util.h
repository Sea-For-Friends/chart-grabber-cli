// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// T-0193: helpers shared by cli_main_test.cpp and cli_end_to_end_test.cpp (no production code).
// runCapture runs cg::app::runCli with file descriptors 1 and 2 redirected to temporary files, because
// Console writes to the process's stdout / stderr.
#include "cg/app/cli_main.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cstdio>
#include <optional>
#include <stop_token>
#include <unistd.h>

namespace cg::app::test {

struct Captured {
    int rc = -1;
    QString out;
    QString err;
};

namespace detail {

inline QString slurp(std::FILE* file) {
    std::fflush(file);
    std::rewind(file);
    QByteArray all;
    char buffer[1024];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        all.append(buffer, static_cast<qsizetype>(n));
    }
    return QString::fromUtf8(all);
}

// Points fd 1 and 2 at the two files for its lifetime.
class FdRedirect {
public:
    FdRedirect(std::FILE* out, std::FILE* err) {
        std::fflush(stdout);
        std::fflush(stderr);
        savedOut_ = ::dup(1);
        savedErr_ = ::dup(2);
        ::dup2(fileno(out), 1);
        ::dup2(fileno(err), 2);
    }
    ~FdRedirect() {
        std::fflush(stdout);
        std::fflush(stderr);
        ::dup2(savedOut_, 1);
        ::dup2(savedErr_, 2);
        ::close(savedOut_);
        ::close(savedErr_);
    }
    FdRedirect(const FdRedirect&) = delete;
    FdRedirect& operator=(const FdRedirect&) = delete;

private:
    int savedOut_ = -1;
    int savedErr_ = -1;
};

} // namespace detail

// arguments[0] is added here.
inline Captured runCapture(const QStringList& arguments, const std::stop_token& termination = {},
                           const StdinWatcher& watchStdin = {}) {
    std::FILE* out = std::tmpfile();
    std::FILE* err = std::tmpfile();
    Captured captured;
    {
        const detail::FdRedirect redirect(out, err);
        captured.rc =
            runCli(QStringList{QStringLiteral("chart-grabber-cli")} + arguments, termination, watchStdin);
    }
    captured.out = detail::slurp(out);
    captured.err = detail::slurp(err);
    std::fclose(out);
    std::fclose(err);
    return captured;
}

// One ZMP folder with the [PARAMS] a download needs. `script` empty = template only.
struct ZmpSpec {
    QString folder;
    QByteArray guid;
    QByteArray name;
    QByteArray urlBase{};
    QByteArray contentType = "image/png";
    QByteArray ext = ".png";
    QByteArray script{};
    bool layer = false;
    std::optional<QByteArray> nameInCache{}; // T-0275: unset = "cache" + the folder stem
    QByteArray extra{}; // more [PARAMS] lines, each ending in CRLF (T-0198: CacheType, MemCacheCapacity)
};

inline void writeZmp(const QString& mapsDir, const ZmpSpec& spec) {
    QByteArray params =
        "[PARAMS]\r\nGUID=" + spec.guid + "\r\nname=" + spec.name + "\r\nNameInCache=" +
        spec.nameInCache.value_or("cache" + spec.folder.left(spec.folder.indexOf(u'.')).toLatin1()) +
        "\r\nDefURLBase=" + spec.urlBase + "\r\nContentType=" + spec.contentType + "\r\nExt=" + spec.ext +
        "\r\nprojection=1\r\nsradiusa=6378137\r\nsradiusb=6356752\r\n";
    if (spec.layer) {
        params += "asLayer=1\r\n";
    }
    params += spec.extra;
    const QString dir = QDir(mapsDir).filePath(spec.folder);
    ASSERT_TRUE(QDir().mkpath(dir));
    QFile paramsFile(dir + QStringLiteral("/params.txt"));
    ASSERT_TRUE(paramsFile.open(QIODevice::WriteOnly));
    paramsFile.write(params);
    if (!spec.script.isEmpty()) {
        QFile scriptFile(dir + QStringLiteral("/GetUrlScript.txt"));
        ASSERT_TRUE(scriptFile.open(QIODevice::WriteOnly));
        scriptFile.write(spec.script);
    }
}

inline const QByteArray kGuidAlt = "{AB0000A1-0000-0000-0000-0000000000A1}";
inline const QByteArray kGuidWebp = "{AB0000B2-0000-0000-0000-0000000000B2}";
inline const QByteArray kGuidTemplate = "{AB0000C3-0000-0000-0000-0000000000C3}";
inline const QByteArray kGuidScripted = "{AB0000D4-0000-0000-0000-0000000000D4}";

// The four maps of the tests. Folder order (alt, webp, template, scripted) differs from the order of their
// names (Alt Path Map, Scripted Layer, Template Map, WebP Map). Every URL points at 127.0.0.1:<port>.
//  - "Template Map": template only, /{z}/{x}/{y}.png
//  - "Scripted Layer": an asLayer ZMP whose DefURLBase is a WRONG path; only GetUrlScript.txt builds the
//  right
//    URL (getz is zoom + 1 in a script, {z} is the zoom)
//  - "Alt Path Map": template only, /alt/{z}/{x}/{y}.png
//  - "WebP Map": stored as .webp
inline void writeZmpSet(const QString& mapsDir, quint16 port) {
    const QByteArray host = "http://127.0.0.1:" + QByteArray::number(port);
    writeZmp(mapsDir, {.folder = QStringLiteral("w1.zmp"),
                       .guid = kGuidAlt,
                       .name = "Alt Path Map",
                       .urlBase = host + "/alt/{z}/{x}/{y}.png"});
    writeZmp(mapsDir, {.folder = QStringLiteral("w2.zmp"),
                       .guid = kGuidWebp,
                       .name = "WebP Map",
                       .urlBase = host + "/{z}/{x}/{y}.webp",
                       .ext = ".webp"});
    writeZmp(mapsDir, {.folder = QStringLiteral("w3.zmp"),
                       .guid = kGuidTemplate,
                       .name = "Template Map",
                       .urlBase = host + "/{z}/{x}/{y}.png"});
    writeZmp(
        mapsDir,
        {.folder = QStringLiteral("w4.zmp"),
         .guid = kGuidScripted,
         .name = "Scripted Layer",
         .urlBase = host + "/wrong/{z}/{x}/{y}.png",
         .script = "begin ResultURL := '" + host +
                   "/' + inttostr(GetZ - 1) + '/' + inttostr(GetX) + '/' + inttostr(GetY) + '.png'; end.",
         .layer = true});
}

} // namespace cg::app::test
