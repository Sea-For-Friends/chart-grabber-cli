// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-011 (load-date semantics feeding TNE/tile-info); docs/specs/storage.md
// §2.1, §2.4.3
#include "cg/storage/tile_date.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using cg::storage::fromDelphiDateTime;
using cg::storage::fromLegacyUnixSeconds;
using cg::storage::fromPosixSeconds;
using cg::storage::TileDate;
using cg::storage::tileDateNow;
using cg::storage::toDelphiDateTime;
using cg::storage::toLegacyUnixSeconds;
using cg::storage::toPosixSeconds;

namespace {

TEST(TileDate, TileDateNowIsValidAndLocalTimeSpec) {
    const TileDate now = tileDateNow();
    EXPECT_TRUE(now.isValid());
    EXPECT_EQ(now.timeSpec(), Qt::LocalTime);
}

TEST(TileDate, ToDelphiDateTimeAtEpochIsZero) {
    const TileDate d(QDate(1899, 12, 30), QTime(0, 0, 0));
    EXPECT_DOUBLE_EQ(toDelphiDateTime(d), 0.0);
}

TEST(TileDate, ToDelphiDateTimeUnixEpochIs25569) {
    // The well-known Delphi TDateTime value for 1970-01-01.
    const TileDate d(QDate(1970, 1, 1), QTime(0, 0, 0));
    EXPECT_DOUBLE_EQ(toDelphiDateTime(d), 25569.0);
}

TEST(TileDate, ToDelphiDateTimeHasFractionalDayComponent) {
    const TileDate d(QDate(2000, 1, 1), QTime(12, 0, 0));
    const double v = toDelphiDateTime(d);
    EXPECT_DOUBLE_EQ(v, 36526.5); // 2000-01-01 is TDateTime 36526; noon is exactly half a day
}

TEST(TileDate, FromDelphiDateTimeZeroIsEpochMidnight) {
    const TileDate d = fromDelphiDateTime(0.0);
    EXPECT_EQ(d.date(), QDate(1899, 12, 30));
    EXPECT_EQ(d.time(), QTime(0, 0, 0));
}

TEST(TileDate, FromDelphiDateTimeNaNIsInvalid) {
    const TileDate d = fromDelphiDateTime(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(d.isValid());
}

TEST(TileDate, FromDelphiDateTimeRoundTripsWithToDelphiDateTime) {
    const TileDate d(QDate(2020, 6, 15), QTime(9, 30, 45));
    const double v = toDelphiDateTime(d);
    const TileDate back = fromDelphiDateTime(v);
    EXPECT_EQ(back, d);
}

TEST(TileDate, FromDelphiDateTimeRoundingCarriesIntoNextDay) {
    // Crafted so that the fractional-day * 86'400'000 rounds up to exactly one full day.
    const TileDate d = fromDelphiDateTime(100.99999999537037);
    EXPECT_EQ(d.date(), QDate(1899, 12, 30).addDays(101));
    EXPECT_EQ(d.time(), QTime(0, 0, 0));
}

TEST(TileDate, ToLegacyUnixSecondsMatchesMeasuredFixture) {
    // tests/fixtures/cache/README.md: the cache_sqlite fixture's row has d = 1789643597, measured
    // to decode (as a naive-local-read-as-UTC value) to 2026-09-17 11:13:17.
    const TileDate d(QDate(2026, 9, 17), QTime(11, 13, 17));
    EXPECT_EQ(toLegacyUnixSeconds(d), 1789643597);
}

TEST(TileDate, FromLegacyUnixSecondsMatchesMeasuredFixture) {
    const TileDate d = fromLegacyUnixSeconds(1789643597);
    EXPECT_EQ(d.date(), QDate(2026, 9, 17));
    EXPECT_EQ(d.time(), QTime(11, 13, 17));
    EXPECT_EQ(d.timeSpec(), Qt::LocalTime);
}

TEST(TileDate, FromLegacyUnixSecondsRoundTripsWithToLegacyUnixSeconds) {
    const qint64 s = 1789643597;
    EXPECT_EQ(toLegacyUnixSeconds(fromLegacyUnixSeconds(s)), s);
}

TEST(TileDate, ToPosixSecondsTreatsInputAsLocalTime) {
    const TileDate d = fromPosixSeconds(1700000000);
    EXPECT_EQ(d.timeSpec(), Qt::LocalTime);
    EXPECT_EQ(toPosixSeconds(d), 1700000000);
}

TEST(TileDate, FromPosixSecondsRoundTripsWithToPosixSeconds) {
    const TileDate d(QDate(2023, 11, 14), QTime(18, 20, 0));
    const qint64 s = toPosixSeconds(d);
    EXPECT_EQ(fromPosixSeconds(s), d);
}

TEST(TileDate, DefaultConstructedTileDateIsInvalid) {
    // §2.4.3's "no load date" is represented by an invalid TileDate. Deliberately NOT asserting
    // `invalid < valid` here: QDateTime::operator< orders invalid before valid only from Qt 6.8's
    // three-way comparison, while this project's floor is Qt 6.5 (CMakeLists.txt:31) -- see
    // predicates.cpp's tileOlderThan, which checks isValid() explicitly instead of relying on
    // that ordering (T-0021A review finding 3).
    const TileDate invalid;
    EXPECT_FALSE(invalid.isValid());
}

TEST(TileDate, ToDelphiDateTimeOfInvalidTileDateIsZero) {
    // Intended, not accidental: legacy's TTileInfoBasicNotExists reports LoadDate = 0 and
    // BerkeleyDB writes that raw value. See tile_date.cpp's toDelphiDateTime comment and T-0021A
    // review finding 6.
    const TileDate invalid;
    EXPECT_DOUBLE_EQ(toDelphiDateTime(invalid), 0.0);
}

} // namespace
