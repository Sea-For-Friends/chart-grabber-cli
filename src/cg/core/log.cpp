// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/log.h"

#include "cg/core/platform/terminal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <atomic>
#include <cstdio>
#include <utility>

namespace cg::core {

Q_LOGGING_CATEGORY(logCore, "cg.core")
Q_LOGGING_CATEGORY(logConfig, "cg.config")
Q_LOGGING_CATEGORY(logStorage, "cg.storage")
// T-0216: debug off by default -- with it on, libcurl's verbose trace (headers, binary TLS bytes) floods the
// terminal; legacy has no such trace (no CURLOPT_VERBOSE in the Pascal sources). Switch on with
// QT_LOGGING_RULES="cg.net.debug=true"; a rule beats this default severity.
Q_LOGGING_CATEGORY(logNet, "cg.net", QtInfoMsg)
Q_LOGGING_CATEGORY(logRender, "cg.render")
Q_LOGGING_CATEGORY(logMarks, "cg.marks")
Q_LOGGING_CATEGORY(logRegion, "cg.region")
Q_LOGGING_CATEGORY(logGps, "cg.gps")
Q_LOGGING_CATEGORY(logUi, "cg.ui")

namespace {

// The logger is the documented exception to "no global mutable state" (coding standard).
struct LogState {
    QMutex mutex;
    QFile file;
    QtMessageHandler previousHandler = nullptr;
    // Written by setMinimumLevel() and read by handler() on arbitrary threads (spec §3 "Log
    // (statics): thread-safe"; §2.7 "qCDebug/qCWarning are callable from any thread").
    std::atomic<QtMsgType> minimumLevel{QtDebugMsg};
    std::shared_ptr<LogRingBuffer> ring;
    bool installed = false;
};

LogState& state() {
    static LogState instance;
    return instance;
}

[[nodiscard]] int severity(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
        return 3;
    case QtFatalMsg:
        return 4;
    }
    return 0;
}

// Ports u_DebugLogger.pas:184-198,331-333: timestamp, category, message. Legacy wrote the Delphi
// class name where the port writes the logging category, and used CRLF where Linux uses LF.
[[nodiscard]] QString formatLine(const QMessageLogContext& context, const QString& message) {
    const QString category = QString::fromUtf8(context.category != nullptr ? context.category : "default");
    return QStringLiteral("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), category,
             message);
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    LogState& log = state();
    if (severity(type) < severity(log.minimumLevel.load(std::memory_order_relaxed))) {
        return;
    }
    // T-0025/T-0002B: formatLine() must run under log.mutex, not just the file write. It calls
    // QDateTime::currentDateTime().toString(), which goes through the C library's local-time
    // machinery (tzset/localtime); that machinery keeps process-wide state that is not safe under
    // concurrent first-touch, so two logger threads formatting a line at the same time raced on it
    // under ThreadSanitizer (malloc/free of the same buffer, log.cpp:74). Serializing formatting
    // alongside the file write removes the concurrent access without touching Qt/libc internals.
    // CORE-LOG-1 (T-0103): the stderr write is inside the mutex and is ONE write of the whole
    // line including its '\n' (stderr is unbuffered: one fwrite is one write(2)), so two threads
    // can neither interleave partial lines nor reach an unsynchronised sink.
    QString line;
    std::shared_ptr<LogRingBuffer> ring;
    {
        const QMutexLocker locker(&log.mutex);
        line = formatLine(context, message);
        const QByteArray bytes = line.toUtf8() + '\n';
        if (log.file.isOpen()) {
            log.file.write(bytes);
            log.file.flush();
        }
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stderr);
        std::fflush(stderr);
        ring = log.ring;
    }
    if (ring) {
        ring->append(line);
    }
    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

Result<void> Log::install(const QString& logDir, bool truncateOnStart) {
    LogState& log = state();
    const QMutexLocker locker(&log.mutex);
    if (log.file.isOpen()) {
        log.file.close();
    }
    if (!logDir.isEmpty()) {
        if (!QDir().mkpath(logDir)) {
            return fail(ErrorCode::IoError, QStringLiteral("cannot create log directory %1").arg(logDir));
        }
        log.file.setFileName(defaultLogFileName(logDir));
        const QIODevice::OpenMode mode =
            QIODevice::WriteOnly | (truncateOnStart ? QIODevice::Truncate : QIODevice::Append);
        if (!log.file.open(mode)) {
            return fail(
                ErrorCode::IoError,
                QStringLiteral("cannot open %1: %2").arg(log.file.fileName(), log.file.errorString()));
        }
    }
    if (!log.installed) {
        platform::prepareOutputStream(
            stderr); // T-0335: Windows text mode would write "\r\n"; no-op elsewhere
        log.previousHandler = qInstallMessageHandler(handler);
        log.installed = true;
    }
    return {};
}

void Log::shutdown() {
    LogState& log = state();
    const QMutexLocker locker(&log.mutex);
    if (log.file.isOpen()) {
        log.file.flush();
        log.file.close();
    }
    if (log.installed) {
        qInstallMessageHandler(log.previousHandler);
        log.previousHandler = nullptr;
        log.installed = false;
    }
    log.ring.reset();
}

void Log::setMinimumLevel(QtMsgType level) {
    state().minimumLevel.store(level, std::memory_order_relaxed);
}

QtMsgType Log::minimumLevel() {
    return state().minimumLevel.load(std::memory_order_relaxed);
}

void Log::setRingBuffer(std::shared_ptr<LogRingBuffer> buffer) {
    LogState& log = state();
    const QMutexLocker locker(&log.mutex);
    log.ring = std::move(buffer);
}

std::shared_ptr<LogRingBuffer> Log::ringBuffer() {
    LogState& log = state();
    const QMutexLocker locker(&log.mutex);
    return log.ring;
}

QString Log::defaultLogFileName(const QString& logDir) {
    return QDir(logDir).filePath(QStringLiteral("debug.txt"));
}

} // namespace cg::core
