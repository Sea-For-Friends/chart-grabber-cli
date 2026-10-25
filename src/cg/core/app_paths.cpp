// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/app_paths.h"

#include "cg/core/platform/file_ops.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace cg::core {

namespace {

// T-0311 (a): the settings file and the old per-user directory name (D rows of this ticket).
constexpr QLatin1StringView kIniName{"chart-grabber.conf"};
constexpr QLatin1StringView kLegacyIniName{"SASPlanet.ini"};
constexpr QLatin1StringView kLegacyDirName{"SAS.Planet"};

enum class LegacyMove : unsigned char { NotNeeded, Moved, FellBack };

// One-time move of the old per-user directory: the new directory is absent and the old one exists ->
// atomic rename(2) (no copy-delete). On failure the old directory is used as it is. `note` gets the
// text of the one stderr line.
LegacyMove moveLegacyDir(QString& dir, const QString& legacyDir, QString& note) {
    if (QFileInfo::exists(dir) || !QFileInfo(legacyDir).isDir()) {
        return LegacyMove::NotNeeded;
    }
    QDir().mkpath(QFileInfo(dir).absolutePath());
    // The target is absent (checked above): a plain rename; a Windows rename that refuses an existing target
    // is right.
    if (platform::renamePath(legacyDir, dir)) {
        note += QStringLiteral("moved %1 to %2").arg(legacyDir, dir);
        return LegacyMove::Moved;
    }
    note += QStringLiteral("cannot move %1 to %2 (%3); using the old directory")
                .arg(legacyDir, dir, QString::fromLocal8Bit(std::strerror(errno)));
    dir = legacyDir;
    return LegacyMove::FellBack;
}

// T-0360: the tile cache and AutoSave lived in the roaming data folder on Windows. Moved once, like
// moveLegacyDir. On Windows the settings directory is the same local directory, so "the new directory exists"
// is not enough to refuse: it is occupied only when it holds something besides the settings file. An occupied
// directory is never merged: it is kept and the note says so.
void moveRoamingDir(const QString& dir, const QString& roamingDir, QString& note) {
    if (roamingDir.isEmpty() || QDir::cleanPath(roamingDir) == QDir::cleanPath(dir) ||
        !QFileInfo(roamingDir).isDir()) {
        return;
    }
    const QStringList present =
        QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    const bool occupied = std::ranges::any_of(
        present, [](const QString& name) { return name != kIniName && name != kLegacyIniName; });
    if (occupied) {
        note += QStringLiteral("not moving %1 to %2: the target is in use; %2 is used, %1 is left as it is")
                    .arg(roamingDir, dir);
        return;
    }
    QDir().mkpath(QFileInfo(dir).absolutePath());
    if (!QFileInfo::exists(dir)) {
        if (platform::renamePath(roamingDir, dir)) {
            note += QStringLiteral("moved %1 to %2").arg(roamingDir, dir);
        } else {
            // errno is read right after the failing call (T-0362).
            const int err = errno;
            note += QStringLiteral("cannot move %1 to %2 (%3); the cache starts empty")
                        .arg(roamingDir, dir, QString::fromLocal8Bit(std::strerror(err)));
        }
        return;
    }
    // Only the settings file is there: move the children one by one, the directory itself stays.
    // T-0362: a failure partway leaves the data split between the two folders, and a name found in both
    // is skipped; the note names them.
    QStringList movedNames;
    QStringList skippedNames;
    QStringList leftNames;
    int failErrno = 0;
    const QStringList children =
        QDir(roamingDir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString& name : children) {
        if (failErrno != 0) {
            leftNames.append(name);
        } else if (present.contains(name)) {
            skippedNames.append(name);
        } else if (platform::renamePath(QDir(roamingDir).filePath(name), QDir(dir).filePath(name))) {
            movedNames.append(name);
        } else {
            failErrno = errno;
            leftNames.append(name);
        }
    }
    if (failErrno != 0) {
        note +=
            QStringLiteral("cannot move %1 to %2 (%3); moved: %4; left in %1: %5")
                .arg(roamingDir, dir, QString::fromLocal8Bit(std::strerror(failErrno)),
                     movedNames.isEmpty() ? QStringLiteral("nothing") : movedNames.join(QLatin1String(", ")),
                     (leftNames + skippedNames).join(QLatin1String(", ")));
        return;
    }
    QDir().rmdir(roamingDir); // only when empty
    note += QStringLiteral("moved %1 to %2").arg(roamingDir, dir);
    if (!skippedNames.isEmpty()) {
        note += QStringLiteral(
                    "; not moved, the name is in both folders: %1; %2 is left with it, resolve by hand")
                    .arg(skippedNames.join(QLatin1String(", ")), roamingDir);
    }
}

} // namespace

Result<AppPaths> AppPaths::detect(const QStringList& arguments, const QString& applicationDirOverride,
                                  const QString& roamingDataDirOverride) {
    AppPaths paths;
    paths.applicationDir =
        applicationDirOverride.isEmpty() ? QCoreApplication::applicationDirPath() : applicationDirOverride;

    // ADR-0011: "--datadir" is an explicit override (legacy CLI switches preserved), independent
    // of the portable-vs-XDG auto-detection below, and takes precedence over "--portable".
    QString explicitDataDir;
    bool sawDataDirFlag = false;
    static const QString kDataDirPrefix = QStringLiteral("--datadir=");
    static const QString kDataDirFlag = QStringLiteral("--datadir");
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString& arg = arguments.at(i);
        if (arg.startsWith(kDataDirPrefix)) {
            explicitDataDir = arg.mid(kDataDirPrefix.size());
            sawDataDirFlag = true;
        } else if (arg == kDataDirFlag) {
            // T-0360: the space form "--datadir DIR", as parseJob (QCommandLineParser) accepts it; a missing
            // value is the same error as an empty one.
            explicitDataDir = i + 1 < arguments.size() ? arguments.at(i + 1) : QString();
            sawDataDirFlag = true;
            ++i;
        }
    }

    if (sawDataDirFlag) {
        // finding 7: reject an empty override rather than silently falling through to auto-detect.
        if (explicitDataDir.isEmpty()) {
            return fail(ErrorCode::ParseError, QStringLiteral("--datadir= requires a non-empty path"));
        }
        paths.portable = false;
        paths.configDir = explicitDataDir;
        paths.dataDir = explicitDataDir;
        paths.cacheDir = explicitDataDir;
    } else {
        const bool portableFlag = arguments.contains(QStringLiteral("--portable"));
        const bool hasIniNextToExe = QFile::exists(QDir(paths.applicationDir).filePath(kIniName));
        const bool hasPortableMarker =
            QFile::exists(QDir(paths.applicationDir).filePath(QStringLiteral("portable.txt")));
        paths.portable = portableFlag || hasIniNextToExe || hasPortableMarker;

        if (paths.portable) {
            paths.configDir = paths.applicationDir;
            paths.dataDir = paths.applicationDir;
            paths.cacheDir = paths.applicationDir;
        } else {
            // Tile caches are user data, not a cache in the XDG sense: ~/.cache is routinely wiped
            // and would destroy hundreds of GB of downloaded tiles (ADR-0011).
            paths.configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
            // T-0360: the local (not roaming) data location; on Linux it is the same directory as
            // AppDataLocation.
            paths.dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

            // T-0311 (a): the directories were called SAS.Planet before; both moves are reported by
            // one stderr line.
            QString note;
            const auto legacyOf = [](const QString& dir) {
                return QFileInfo(dir).absolutePath() + QLatin1Char('/') + kLegacyDirName;
            };
            const QString legacyConfig = legacyOf(paths.configDir);
            const QString legacyData = legacyOf(paths.dataDir);
            const LegacyMove configMove = moveLegacyDir(paths.configDir, legacyConfig, note);
            if (configMove == LegacyMove::Moved) {
                // The settings file inside the moved directory gets the new name, atomically.
                const QString oldIni = QDir(paths.configDir).filePath(kLegacyIniName);
                const QString newIni = QDir(paths.configDir).filePath(kIniName);
                // newIni is absent (checked here): a rename that refuses an existing target loses nothing
                // (T-0335).
                if (QFileInfo::exists(oldIni) && !QFileInfo::exists(newIni) &&
                    !platform::renamePath(oldIni, newIni)) {
                    note += QStringLiteral("; cannot rename %1 (%2); it is still read")
                                .arg(oldIni, QString::fromLocal8Bit(std::strerror(errno)));
                    paths.legacyIni = true;
                }
            } else if (configMove == LegacyMove::FellBack) {
                paths.legacyIni = true;
            }
            // T-0360: Windows only (AppDataLocation is the roaming folder there; on Linux it equals dataDir
            // and nothing is moved).
            const QString roamingDataDir =
                roamingDataDirOverride.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                    : roamingDataDirOverride;
            QString dataNote;
            moveLegacyDir(paths.dataDir, legacyData, dataNote);
            moveRoamingDir(paths.dataDir, roamingDataDir, dataNote);
            if (!dataNote.isEmpty()) {
                note += (note.isEmpty() ? QString() : QStringLiteral("; ")) + dataNote;
            }
            if (!note.isEmpty()) {
                QTextStream(stderr) << "note: " << note << Qt::endl;
            }
            paths.cacheDir = paths.dataDir;
        }
    }

    const QDir maker;
    for (const QString& dir : {paths.configDir, paths.dataDir, paths.cacheDir}) {
        if (!dir.isEmpty() && !maker.mkpath(dir)) {
            return fail(ErrorCode::IoError, QStringLiteral("cannot create directory %1").arg(dir));
        }
    }
    return paths;
}

QString AppPaths::mainConfigFilePath() const {
    // D-6: a constant, not the legacy first-dot-of-the-exe-name derivation (u_SASMainConfigProvider.pas:83);
    // T-0311 (a): "chart-grabber.conf" instead of "SASPlanet.ini". The old name is read only in a
    // directory that came from the old SAS.Planet directory and could not be renamed.
    if (legacyIni && !QFileInfo::exists(QDir(configDir).filePath(kIniName))) {
        return QDir(configDir).filePath(kLegacyIniName);
    }
    return QDir(configDir).filePath(kIniName);
}

} // namespace cg::core
