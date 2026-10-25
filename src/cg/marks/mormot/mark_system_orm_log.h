// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// FEAT-GEO-034 (held open). Ports: Src/MarksDB/mORMot/u_MarkSystemORMLog.pas
// (docs/specs/marks.md §2.9.14).
//
// Legacy compiles the four log calls only under ORM_LOG_ENABLE, which MarkSystemORM.inc keeps
// switched off even in debug builds, so a shipped SAS.Planet writes no ORM log at all. Switched on,
// they go to mORMot's TSynLog: <log base>/marks/mormot*.txt, one file with the thread identified per
// line, levels SQL + Info + stack traces (everything with ORM_LOG_VERBOSE).
// Not ported: InitOrmLog (:45-67) -- the TSynLog file set-up; the port's lines go to the application
// log (cg/core/log.h) under the category below, with Qt's message format.

#include <QLoggingCategory>

namespace cg::marks {

// What Q_LOGGING_CATEGORY expands to, made inline so that this header needs no source file.
inline const QLoggingCategory& logMarksOrm() {
    static const QLoggingCategory category("cg.marks.orm");
    return category;
}

} // namespace cg::marks

// OrmLogInfo / OrmLogDebug / OrmLogCache (:78-91): printf-style, as qCInfo / qCDebug take it.
// OrmLogEnter (:69-76): mORMot logs entering and leaving the method; the port logs entering it.
#define ormLogInfo(...) qCInfo(cg::marks::logMarksOrm, __VA_ARGS__)
#define ormLogDebug(...) qCDebug(cg::marks::logMarksOrm, __VA_ARGS__)
#define ormLogCache(...) qCDebug(cg::marks::logMarksOrm, __VA_ARGS__)
#define ormLogEnter(method) qCDebug(cg::marks::logMarksOrm, "enter %s", method)
