// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). docs/specs/app-cli.md 2.6. The platform is offscreen: a terminal program must
// not try xcb or wayland because a desktop variable happens to be set; plugin path variables are ignored
// likewise.
#include "cg/app/cli_main.h"
#include "cg/core/app_paths.h"
#include "cg/core/platform/file_ops.h"
#include "cg/core/platform/termination_signal.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <cstdio>

#include "git_commit.h"

namespace {

// T-0377, T-0382: a self-contained Linux bundle ships <exe dir>/fontroot (etc/fonts/fonts.conf plus DejaVu
// Sans) and ALWAYS renders with it. Its fontconfig (Debian 12, 2.14) must not read a newer host /etc/fonts or
// /usr/share/fontconfig (dozens of "Fontconfig warning" lines), and the clean images have no fonts at all.
// FONTCONFIG_SYSROOT prefixes EVERY path, the cache directory too, so the sysroot cannot be the bundle itself
// (the cache would be written into it). The sysroot is therefore a per-user directory with two symbolic links
// (etc, usr) into the bundle: <datadir>/fontroot when the command line has --datadir, else
// <cache>/chart-grabber/fontroot; when that cannot be used, $XDG_RUNTIME_DIR/chart-grabber-fontroot (private,
// mode 0700). fontconfig writes its cache to <sysroot>/var/cache/fontconfig, outside the bundle.
// FONTCONFIG_FILE is the sysroot-relative /etc/fonts/fonts.conf. A FONTCONFIG_FILE or FONTCONFIG_SYSROOT the
// user set, no bundled fontroot (the .deb has none; on Windows /proc/self/exe is absent) or no usable sysroot
// leave the environment as it is.

// Several CLIs may start at once and a running one reads the links: a link is never removed and re-made. A
// new one is made under a unique name in the same directory and renamed over the old one (atomic, the name
// always exists); if anything fails, a link that already points to the target (made by a concurrent start) is
// as good.
bool linkInto(const QString& dir, const QString& name, const QString& target) {
    const QString link = QDir(dir).filePath(name);
    if (QFileInfo(link).symLinkTarget() == target) {
        return true;
    }
    const QString tmp = link + QLatin1Char('.') + QString::number(QCoreApplication::applicationPid()) +
                        QLatin1Char('.') + QString::number(QRandomGenerator::global()->generate(), 16);
    if (QFile::link(target, tmp)) {
        if (!cg::core::platform::renamePath(tmp, link)) {
            QFile::remove(tmp);
        }
    }
    return QFileInfo(link).symLinkTarget() == target;
}

bool makeSysroot(const QString& sysroot, const QString& root, bool privateDir) {
    if (!QDir().mkpath(sysroot)) {
        return false;
    }
    if (privateDir) {
        QFile::setPermissions(sysroot,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    // T-0385: fontconfig 2.14 creates <sysroot>/var/cache/fontconfig itself and loses a race when several
    // CLIs start at once ("No writable cache directories"); QDir::mkpath tolerates an existing directory.
    if (!QDir().mkpath(QDir(sysroot).filePath(QStringLiteral("var/cache/fontconfig")))) {
        return false;
    }
    return linkInto(sysroot, QStringLiteral("etc"), root + QStringLiteral("/etc")) &&
           linkInto(sysroot, QStringLiteral("usr"), root + QStringLiteral("/usr"));
}

void useBundledFontconfig(const QStringList& arguments) {
    if (qEnvironmentVariableIsSet("FONTCONFIG_FILE") || qEnvironmentVariableIsSet("FONTCONFIG_SYSROOT")) {
        return;
    }
    const QString exe = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    if (exe.isEmpty()) {
        return;
    }
    const QString root = QFileInfo(exe).dir().filePath(QStringLiteral("fontroot"));
    if (!QFileInfo(root + QStringLiteral("/etc/fonts/fonts.conf")).isFile()) {
        return;
    }
    // With --datadir (read by the same function as AppPaths::detect) the sysroot is <datadir>/fontroot, so a
    // plugin that allows writes only inside its own directory sees nothing in ~/.cache; the directory is
    // created as the CLI does (QDir::mkpath); a relative path is relative to the working directory, as
    // everywhere else.
    bool hasDataDir = false;
    QString base = cg::core::AppPaths::explicitDataDirArgument(arguments, &hasDataDir);
    if (!hasDataDir || base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    QString sysroot;
    if (!base.isEmpty()) {
        sysroot = QDir(base).absoluteFilePath(QStringLiteral("fontroot"));
        if (!makeSysroot(sysroot, root, false)) {
            sysroot.clear();
        }
    }
    if (sysroot.isEmpty()) { // read-only or missing cache/data directory: a private directory in the runtime
                             // directory
        const QString run = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (run.isEmpty()) {
            return;
        }
        sysroot = QDir(run).absoluteFilePath(QStringLiteral("chart-grabber-fontroot"));
        if (!makeSysroot(sysroot, root, true)) {
            return;
        }
    }
    qputenv("FONTCONFIG_SYSROOT", QFile::encodeName(sysroot));
    qputenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(
        QStringLiteral("chart-grabber")); // one directory for both programs (Q-9)
    QCoreApplication::setApplicationVersion(QStringLiteral(CG_TILES_VERSION " (" CG_GIT_COMMIT ")"));
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // T-0377: Qt loads only the plugins of its own installation (qt.conf of a bundle, else the compiled-in
    // path): a QT_PLUGIN_PATH meant for another Qt (a host application's) would otherwise load that build's
    // offscreen plugin.
    qunsetenv("QT_PLUGIN_PATH");
    qunsetenv("QT_QPA_PLATFORM_PLUGIN_PATH");
    QStringList rawArguments; // QGuiApplication does not exist yet, so argv is read directly
    for (int i = 1; i < argc; ++i) {
        rawArguments.append(QString::fromLocal8Bit(argv[i]));
    }
    useBundledFontconfig(rawArguments);
    QGuiApplication app(argc, argv);
    cg::core::platform::TerminationSignal termination;
    const int rc = cg::app::runCli(app.arguments(), termination.token(),
                                   [&termination] { return termination.watchStdin(); });
    if (termination.stoppedByStdin()) { // T-0332: one line, also when the stop came before the job started
        std::fputs("chart-grabber-cli: stop requested on stdin; exit 143\n", stderr);
    }
    return termination.exitCode() != 0 ? termination.exitCode() : rc;
}
