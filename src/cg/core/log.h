// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_LogSimple.pas, Src/Debug/u_DebugLogger.pas, Src/Debug/DebugLog.inc
//
// The application-wide logging facade. Legacy compiled a whole logger subsystem down to a fake
// consumer unless the user asked for a log file; the port does the same with one Qt message
// handler, one category per module, and an optional file sink next to the settings.

#include "cg/core/log_ring_buffer.h"
#include "cg/core/result.h"

#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::core {

// One QLoggingCategory per module.
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logConfig)
Q_DECLARE_LOGGING_CATEGORY(logStorage)
Q_DECLARE_LOGGING_CATEGORY(logNet)
Q_DECLARE_LOGGING_CATEGORY(logRender)
Q_DECLARE_LOGGING_CATEGORY(logMarks)
Q_DECLARE_LOGGING_CATEGORY(logRegion)
Q_DECLARE_LOGGING_CATEGORY(logGps)
Q_DECLARE_LOGGING_CATEGORY(logUi)

class Log {
public:
    // Installs the Qt message handler. Call once from main() before any logging.
    // `logDir` empty -> stderr only; `logDir` set -> also appends to <logDir>/debug.txt.
    // FEAT-UI-185
    static Result<void> install(const QString& logDir, bool truncateOnStart = true);
    // Flushes and closes the file, restores the previous handler. Contract (T-0103): from then on
    // new messages go to that previous handler (Qt's default if none); a message already inside
    // our handler on another thread completes normally (its state lives to process exit), on stderr only once
    // the file is closed. install() and shutdown() may race with logging threads; the app calls install() as
    // the first statement of main() and shutdown() last, after every thread is joined.
    static void shutdown();

    // Ports Src/Debug/DebugLog.inc: below this level nothing is written at all.
    static void setMinimumLevel(QtMsgType level);
    [[nodiscard]] static QtMsgType minimumLevel();

    // Ports i_LogSimpleProvider.pas: a bounded in-memory tail for the download progress dialog.
    static void setRingBuffer(std::shared_ptr<LogRingBuffer> buffer);
    [[nodiscard]] static std::shared_ptr<LogRingBuffer> ringBuffer();

    [[nodiscard]] static QString defaultLogFileName(const QString& logDir); // <logDir>/debug.txt
};

} // namespace cg::core
