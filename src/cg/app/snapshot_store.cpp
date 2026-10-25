// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). See snapshot_store.h. docs/specs/weather-sources.md section 4.2.
#include "cg/app/snapshot_store.h"

#include "cg/core/config_provider_ini.h"
#include "cg/core/platform/file_ops.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <set>

namespace cg::app {

namespace {

// T-0357: the store file is <guid>.conf; <guid>.ini is the name of the versions before.
QString storeFile(const QString& dir, const QString& guid) {
    return dir + QLatin1Char('/') + guid + QStringLiteral(".conf");
}

QString oldStoreFile(const QString& dir, const QString& guid) {
    return dir + QLatin1Char('/') + guid + QStringLiteral(".ini");
}

// T-0357: only <guid>.ini exists -> one atomic rename to <guid>.conf and one stderr note; both exist -> the
// .conf wins, the .ini stays and one note per file and process says so. A failed rename reads the old file.
// Returns the file to read.
QString fileToRead(const QString& dir, const QString& guid) {
    QString path = storeFile(dir, guid);
    QString old = oldStoreFile(dir, guid);
    const bool hasOld = QFileInfo::exists(old);
    if (QFileInfo::exists(path)) {
        static std::mutex mutex;
        static std::set<QString> told;
        if (hasOld && [&] {
                const std::lock_guard lock(mutex);
                return told.insert(old).second;
            }()) {
            QTextStream(stderr) << "note: " << old << " and " << path << " both exist; " << path
                                << " is used, " << old << " is left alone" << Qt::endl;
        }
        return path;
    }
    if (!hasOld) {
        return path;
    }
    if (core::platform::renamePath(old, path)) {
        QTextStream(stderr) << "note: renamed " << old << " to " << path << Qt::endl;
        return path;
    }
    const int err = errno;
    if (QFileInfo::exists(path)) {
        return path; // another process renamed it first
    }
    QTextStream(stderr) << "note: cannot rename " << old << " to " << path << " ("
                        << QString::fromLocal8Bit(std::strerror(err)) << "); it is still read" << Qt::endl;
    return old;
}

} // namespace

core::Result<void> saveSnapshot(const QString& dir, const QString& guid, const Snapshot& s,
                                const QDateTime& resolvedUtc) {
    const QString path = storeFile(dir, guid);
    if (!QDir().mkpath(dir)) {
        return core::fail(core::ErrorCode::IoError, QStringLiteral("cannot create %1").arg(dir));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return core::fail(core::ErrorCode::IoError, file.errorString());
    }
    const QString text =
        QStringLiteral("[Snapshot]\nVersion=%1\nTime=%2\nDateOnly=%3\nResolved=%4\n")
            .arg(s.version, snapshotTimeIso(s), s.dateOnly ? QStringLiteral("1") : QStringLiteral("0"),
                 resolvedUtc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")));
    if (file.write(text.toUtf8()) < 0 || !file.commit()) {
        return core::fail(core::ErrorCode::IoError, file.errorString());
    }
    return {};
}

std::optional<Snapshot> loadSnapshot(const QString& dir, const QString& guid) {
    const QString path = fileToRead(dir, guid);
    if (!QFileInfo::exists(path)) {
        return std::nullopt;
    }
    const auto ini = core::ConfigProviderIni::open(path);
    if (!ini) {
        return std::nullopt;
    }
    const core::IConfigProviderPtr section = (*ini)->subItem(u"Snapshot");
    if (!section) {
        return std::nullopt;
    }
    const QString version = section->readString(u"Version", {});
    if (version.isEmpty()) {
        return std::nullopt;
    }
    Snapshot s;
    s.version = version;
    const QString time = section->readString(u"Time", {}).trimmed();
    if (auto parsed = snapshotFromText(QStringLiteral("x"), time); parsed && !time.isEmpty()) {
        s.time = parsed->time;
        s.dateOnly = parsed->dateOnly;
    }
    return s;
}

} // namespace cg::app
