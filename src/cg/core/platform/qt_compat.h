// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (Qt 6.4 floor portability helpers, ADR-0018).
// Qt 6.4 floor (ADR-0018): QTimeZone::UTC is Qt 6.5, the Qt::TimeSpec overloads are deprecated from Qt 6.9.
// These give the same QDateTime on every version: timeSpec() == Qt::UTC, ISO text with "Z". The only place
// that tests QT_VERSION (coding standard: version/platform code lives under core/platform/).

#include <QDateTime>
#include <QRegularExpression>
#include <QStringView>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QTimeZone>
#endif

namespace cg::core::platform {

[[nodiscard]] inline QDateTime utcDateTime(QDate date, QTime time) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QDateTime(date, time, QTimeZone::UTC);
#else
    return QDateTime(date, time, Qt::UTC);
#endif
}

[[nodiscard]] inline QDateTime utcFromMSecsSinceEpoch(qint64 msecs) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC);
#else
    return QDateTime::fromMSecsSinceEpoch(msecs, Qt::UTC);
#endif
}

[[nodiscard]] inline QDateTime utcFromSecsSinceEpoch(qint64 secs) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC);
#else
    return QDateTime::fromSecsSinceEpoch(secs, Qt::UTC);
#endif
}

// QRegularExpression::match(QStringView) is deprecated from Qt 6.8 in favour of matchView (new in 6.5).
[[nodiscard]] inline QRegularExpressionMatch matchView(const QRegularExpression& re, QStringView subject) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return re.matchView(subject);
#else
    return re.match(subject);
#endif
}

} // namespace cg::core::platform
