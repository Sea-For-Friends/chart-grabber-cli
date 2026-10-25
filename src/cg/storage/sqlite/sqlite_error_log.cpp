// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_error_log.h"

#include <QMutexLocker>

#include <utility>

namespace cg::storage::sqlite {

SqliteErrorLog::SqliteErrorLog(QString filename) : filename_(std::move(filename)) {}

SqliteErrorLog::~SqliteErrorLog() {
    close();
}

void SqliteErrorLog::logString(const QString& value) {
    const QMutexLocker lock(&mutex_);
    if (!file_.isOpen()) {
        if (filename_.isEmpty()) {
            return; // no filename known -- ports InternalOpenLog's "neither filename nor event"
        }
        file_.setFileName(filename_);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Append)) {
            return; // best-effort: legacy leaves FLogFile at 0 and every LogString is a no-op
        }
    }
    file_.write(value.toUtf8());
    file_.flush();
}

void SqliteErrorLog::close() {
    const QMutexLocker lock(&mutex_);
    if (file_.isOpen()) {
        file_.close();
    }
}

} // namespace cg::storage::sqlite
