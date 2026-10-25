// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: portability (Qt 6.4 floor, ADR-0018; no FEAT). The helpers must give the same QDateTime on every
// Qt: timeSpec() == Qt::UTC and ISO text with "Z" (QTimeZone::utc() would give "+00:00").
#include "cg/core/platform/qt_compat.h"

#include <QDateTime>
#include <QRegularExpression>

#include <gtest/gtest.h>

using namespace cg::core::platform;

TEST(QtCompat, UtcDateTimeIsUtcSpecWithZ) {
    const QDateTime dt = utcDateTime(QDate(2026, 10, 15), QTime(20, 10, 7));
    EXPECT_EQ(dt.timeSpec(), Qt::UTC);
    EXPECT_EQ(dt.toString(Qt::ISODate), QStringLiteral("2026-10-15T20:10:07Z"));
    EXPECT_EQ(dt.toSecsSinceEpoch(), 1'792'095'007);
}

TEST(QtCompat, UtcFromMSecsAndSecs) {
    const QDateTime ms = utcFromMSecsSinceEpoch(1'700'000'000'123);
    EXPECT_EQ(ms.timeSpec(), Qt::UTC);
    EXPECT_EQ(ms.toString(Qt::ISODateWithMs), QStringLiteral("2023-11-14T22:13:20.123Z"));
    const QDateTime s = utcFromSecsSinceEpoch(100);
    EXPECT_EQ(s.timeSpec(), Qt::UTC);
    EXPECT_EQ(s.toString(Qt::ISODate), QStringLiteral("1970-01-01T00:01:40Z"));
}

TEST(QtCompat, MatchViewMatchesSubjectView) {
    const QRegularExpression re(QStringLiteral("^z(\\d+)/(\\d+)$"));
    const QString text = QStringLiteral("z12/345");
    const QRegularExpressionMatch m = matchView(re, QStringView(text));
    ASSERT_TRUE(m.hasMatch());
    EXPECT_EQ(m.captured(2), QStringLiteral("345"));
    EXPECT_FALSE(matchView(re, QStringView(QStringLiteral("x"))).hasMatch());
}
