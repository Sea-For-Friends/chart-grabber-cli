// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: T-0264 cli/git_commit.cmake (docs/specs/app-cli.md 2.6): commit precedence and the -dirty scope; no
// FEAT. The script is run with `cmake -P` on throwaway directories and repositories.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "fixture_path.h"

namespace {

struct ProcResult {
    int rc = -1;
    QString out;
};

ProcResult runIn(const QString& program, const QStringList& args, const QString& dir = QString()) {
    QProcess p;
    if (!dir.isEmpty()) {
        p.setWorkingDirectory(dir);
    }
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished();
        return {};
    }
    return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1,
            QString::fromUtf8(p.readAll()).trimmed()};
}

QString git(const QString& dir, const QStringList& args) {
    QStringList all{QStringLiteral("-c"), QStringLiteral("user.name=t"),
                    QStringLiteral("-c"), QStringLiteral("user.email=t@example.invalid"),
                    QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false"),
                    QStringLiteral("-C"), dir};
    all += args;
    const ProcResult r = runIn(QStringLiteral("git"), all);
    EXPECT_EQ(r.rc, 0) << r.out.toStdString();
    return r.out;
}

void write(const QString& path, const QString& text) {
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text.toUtf8());
}

// Runs cli/git_commit.cmake; returns the commit text between the quotes of the generated #define, or the
// failure.
ProcResult script(const QString& src, const QString& outDir, const QString& override_ = QString()) {
    const QString repo = QDir(cgFixturesDir()).absoluteFilePath(QStringLiteral("../.."));
    QStringList args{QStringLiteral("-DSRC=") + src,
                     QStringLiteral("-DOUT=") + outDir + QStringLiteral("/git_commit.h")};
    if (!override_.isEmpty()) {
        args << QStringLiteral("-DOVERRIDE=") + override_;
    }
    args << QStringLiteral("-P") << QDir::cleanPath(repo + QStringLiteral("/cli/git_commit.cmake"));
    ProcResult r = runIn(QStringLiteral("cmake"), args);
    if (r.rc != 0) {
        return r;
    }
    QFile f(outDir + QStringLiteral("/git_commit.h"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {-2, {}};
    }
    const QString t = QString::fromUtf8(f.readAll());
    const qsizetype a = t.indexOf(QLatin1Char('"'));
    const qsizetype b = t.lastIndexOf(QLatin1Char('"'));
    return {0, t.mid(a + 1, b - a - 1)};
}

// A repository with files under every build-input tree and one outside (docs).
QString makeRepo(QTemporaryDir& tmp, const QString& name) {
    const QString dir = tmp.filePath(name);
    QDir().mkpath(dir);
    git(dir, {QStringLiteral("init"), QStringLiteral("-q")});
    write(dir + QStringLiteral("/CMakeLists.txt"), QStringLiteral("a\n"));
    write(dir + QStringLiteral("/src/a.cpp"), QStringLiteral("a\n"));
    write(dir + QStringLiteral("/docs/d.md"), QStringLiteral("a\n"));
    write(dir + QStringLiteral("/.gitignore"), QStringLiteral("a\n"));
    git(dir, {QStringLiteral("add"), QStringLiteral(".")});
    git(dir, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("c")});
    return dir;
}

const QString kStamp = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

} // namespace

TEST(GitCommitScript, CleanRepositoryPrintsHeadWithoutDirty) {
    QTemporaryDir tmp;
    const QString repo = makeRepo(tmp, QStringLiteral("r"));
    const QString head = git(repo, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).left(12);
    const ProcResult r = script(repo, tmp.path());
    EXPECT_EQ(r.rc, 0);
    EXPECT_EQ(r.out, head);
}

TEST(GitCommitScript, DirtyCountsBuildInputsOnly) {
    QTemporaryDir tmp;
    const QString repo = makeRepo(tmp, QStringLiteral("r"));
    const QString head = git(repo, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).left(12);
    write(repo + QStringLiteral("/.gitignore"), QStringLiteral("changed\n"));
    write(repo + QStringLiteral("/docs/d.md"), QStringLiteral("changed\n"));
    write(repo + QStringLiteral("/untracked.txt"), QStringLiteral("x\n"));
    EXPECT_EQ(script(repo, tmp.path()).out, head);
    write(repo + QStringLiteral("/src/a.cpp"), QStringLiteral("changed\n"));
    EXPECT_EQ(script(repo, tmp.path()).out, head + QStringLiteral("-dirty"));
}

TEST(GitCommitScript, OverrideIsWrittenVerbatimAndBeatsGit) {
    QTemporaryDir tmp;
    const QString repo = makeRepo(tmp, QStringLiteral("r"));
    EXPECT_EQ(script(repo, tmp.path(), QStringLiteral("0123456789ab")).out, QStringLiteral("0123456789ab"));
    EXPECT_EQ(script(repo, tmp.path(), QStringLiteral("0123456789ab-dirty")).out,
              QStringLiteral("0123456789ab-dirty"));
}

TEST(GitCommitScript, MalformedOverrideFails) {
    QTemporaryDir tmp;
    for (const QString& bad : {QStringLiteral("xyz"), QStringLiteral("0123456789AB"),
                               QStringLiteral("0123456789abc"), QStringLiteral("0123456789ab-dirt")}) {
        const ProcResult r = script(tmp.path(), tmp.path(), bad);
        EXPECT_NE(r.rc, 0) << bad.toStdString();
        EXPECT_TRUE(r.out.contains(QStringLiteral("CHART_GRABBER_COMMIT must be"))) << r.out.toStdString();
    }
}

TEST(GitCommitScript, ArchiveStampUsedWithoutGit) {
    QTemporaryDir tmp;
    const QString src = tmp.filePath(QStringLiteral("a"));
    write(src + QStringLiteral("/cli/archive_commit.txt"), kStamp + QLatin1Char('\n'));
    EXPECT_EQ(script(src, tmp.path()).out, QStringLiteral("0123456789ab"));
}

TEST(GitCommitScript, ArchiveInsideOtherRepositoryIgnoresOuterHead) {
    QTemporaryDir tmp;
    const QString outer = makeRepo(tmp, QStringLiteral("outer"));
    const QString src = outer + QStringLiteral("/sub");
    write(src + QStringLiteral("/cli/archive_commit.txt"), kStamp + QLatin1Char('\n'));
    // T-0347: the outer repository is really read by the script (its own top gives its HEAD, also with MSYS
    // git), so the stamp below wins because SRC is not the top, not because git failed.
    const QString outerHead = git(outer, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).left(12);
    EXPECT_EQ(script(outer, tmp.path()).out, outerHead);
    EXPECT_EQ(script(src, tmp.path()).out, QStringLiteral("0123456789ab"));
}

TEST(GitCommitScript, UnexpandedOrMissingStampGivesUnknown) {
    QTemporaryDir tmp;
    const QString none = tmp.filePath(QStringLiteral("none"));
    QDir().mkpath(none);
    EXPECT_EQ(script(none, tmp.path()).out, QStringLiteral("unknown"));
    const QString raw = tmp.filePath(QStringLiteral("raw"));
    write(raw + QStringLiteral("/cli/archive_commit.txt"), QStringLiteral("$Format:%H$\n"));
    EXPECT_EQ(script(raw, tmp.path()).out, QStringLiteral("unknown"));
}

TEST(GitCommitScript, CommittedStampFileIsTheExportSubstPlaceholder) {
    const QString repo = QDir(cgFixturesDir()).absoluteFilePath(QStringLiteral("../.."));
    QFile f(repo + QStringLiteral("/cli/archive_commit.txt"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString::fromUtf8(f.readAll()), QStringLiteral("$Format:%H$\n"));
    QFile a(repo + QStringLiteral("/.gitattributes"));
    ASSERT_TRUE(a.open(QIODevice::ReadOnly));
    EXPECT_TRUE(
        QString::fromUtf8(a.readAll()).contains(QStringLiteral("cli/archive_commit.txt export-subst")));
}
