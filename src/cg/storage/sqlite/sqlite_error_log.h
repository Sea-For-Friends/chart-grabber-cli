// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/{i_TileStorageSQLiteErrorLogger,u_TileStorageSQLiteErrorLogger}.pas
//
// TErrorLoggerToFile, collapsed: legacy supports either a fixed filename or a late-bound
// "ask the owner for a filename" callback (TErrorLoggerGetFileNameEvent), used only by
// TTileStorageSQLiteHolder (§2.16.4, T-0022B, out of this ticket's scope). This ticket ports the
// fixed-filename half only -- the class itself, not its one caller.

#include <QFile>
#include <QMutex>
#include <QString>

namespace cg::storage::sqlite {

// Appends whole lines to a log file, opened lazily on the first write (InternalOpenLog,
// u_TileStorageSQLiteErrorLogger.pas:97-119) and kept open until close() or destruction.
class SqliteErrorLog final {
public:
    explicit SqliteErrorLog(QString filename);
    ~SqliteErrorLog();

    SqliteErrorLog(const SqliteErrorLog&) = delete;
    SqliteErrorLog& operator=(const SqliteErrorLog&) = delete;

    // Writes `value` as-is (no separator added, matching LogString's raw WriteFile call) and
    // flushes. A failure to open/write is swallowed, exactly as legacy silently no-ops when
    // FLogFile stays 0 (this is a best-effort diagnostic log, not a Result-returning operation).
    void logString(const QString& value);

    void close();

private:
    QMutex mutex_;
    QString filename_;
    QFile file_;
};

} // namespace cg::storage::sqlite
