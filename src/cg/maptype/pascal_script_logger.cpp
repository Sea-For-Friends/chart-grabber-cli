// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/pascal_script_logger.h"

#include "cg/core/platform/thread_os.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>

#include <mutex>
#include <stdexcept>

namespace cg::maptype {

namespace {

bool isDelimiter(QChar c) {
    return c == u'/' || c == u'\\';
}

// :89-96
QString uniqId(int appId) {
    return appId == 1 ? QString() : QStringLiteral(".") + QString::number(appId);
}

} // namespace

// :62-79. ExtractFileName and ChangeFileExt are done by hand: the legacy path is split on both
// delimiters and the extension is whatever follows the last dot of the name.
PascalScriptLogger::PascalScriptLogger(int appId, const QString& logsPath, const QString& zmpFileName) {
    qsizetype cut = zmpFileName.size();
    while (cut > 0 && !isDelimiter(zmpFileName.at(cut - 1))) {
        --cut;
    }
    QString name = zmpFileName.mid(cut);
    if (const qsizetype dot = name.lastIndexOf(u'.'); dot >= 0) {
        name.truncate(dot);
    }
    QString dir = logsPath;
    if (dir.isEmpty() || !isDelimiter(dir.back())) {
        dir += u'/';
    }
    fileName_ = dir + QStringLiteral("zmp/") + name + uniqId(appId) + QStringLiteral(".log");
}

// :98-111: the file stays open until the logger dies; it is created with truncation, unbuffered
// like the legacy TFileStream.
void PascalScriptLogger::lazyInit() {
    if (file_.isOpen()) {
        return;
    }
    const QString dir = QFileInfo(fileName_).absolutePath();
    if (!QDir(dir).exists() && !QDir().mkpath(dir)) {
        throw std::runtime_error(QStringLiteral("Cannot create directory %1").arg(dir).toStdString());
    }
    file_.setFileName(fileName_);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Unbuffered)) {
        throw std::runtime_error(file_.errorString().toStdString());
    }
}

// :121-137, record format :113-119
void PascalScriptLogger::write(const QByteArray& text) {
    if (text.isEmpty()) {
        return;
    }
    const std::lock_guard<core::Mutex> guard(mutex_);
    lazyInit();
    QByteArray record =
        QStringLiteral("[%1] %2: ")
            .arg(static_cast<qint64>(core::platform::currentThreadId()), 8, 10, QLatin1Char('0'))
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
            .toLatin1();
    record += text;
    record += "\r\n";
    if (file_.write(record) != record.size()) {
        throw std::runtime_error(file_.errorString().toStdString());
    }
}

} // namespace cg::maptype
