// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_date.h"

#include "cg/core/platform/qt_compat.h"

#include <cmath>

namespace cg::storage {

TileDate tileDateNow() {
    return QDateTime::currentDateTime();
}

double toDelphiDateTime(const TileDate& d) {
    // Delphi epoch 1899-12-30; the fractional part is the time of day, always positive in
    // SAS.Planet's range (tile dates are >= 1970). Do not port Delphi's negative-date sign quirk.
    //
    // An invalid `d` (default-constructed QDate/QTime) yields 0.0: intended, not accidental --
    // legacy's TTileInfoBasicNotExists reports LoadDate = 0 and BerkeleyDB writes that raw value,
    // so this is the byte-exact match for "no load date" on round-trip through storage. It is
    // asymmetric with fromDelphiDateTime(NaN) -> invalid on purpose: 0.0 is a valid legacy
    // encoding, NaN never occurs in a real record. See T-0021A review finding 6.
    static const QDate kEpoch(1899, 12, 30);
    return double(kEpoch.daysTo(d.date())) + double(d.time().msecsSinceStartOfDay()) / 86400000.0;
}

TileDate fromDelphiDateTime(double v) {
    if (std::isnan(v)) {
        return TileDate{}; // invalid
    }
    static const QDate kEpoch(1899, 12, 30);
    const double dayPart = std::floor(v);
    qint64 msecs = static_cast<qint64>(std::llround((v - dayPart) * 86400000.0));
    QDate date = kEpoch.addDays(static_cast<qint64>(dayPart));
    if (msecs >= 86400000) { // rounding carried into the next day
        msecs -= 86400000;
        date = date.addDays(1);
    }
    return TileDate(date, QTime::fromMSecsSinceStartOfDay(static_cast<int>(msecs)));
}

qint64 toLegacyUnixSeconds(const TileDate& d) { // DateTimeToUnix(x) with AInputIsUTC=true
    return core::platform::utcDateTime(d.date(), d.time()).toSecsSinceEpoch();
}

TileDate fromLegacyUnixSeconds(qint64 s) {
    const QDateTime utc = core::platform::utcFromSecsSinceEpoch(s);
    return TileDate(utc.date(), utc.time()); // re-read as a naive local wall clock
}

qint64 toPosixSeconds(const TileDate& d) {
    return d.toSecsSinceEpoch();
} // d is LocalTime

TileDate fromPosixSeconds(qint64 s) {
    return QDateTime::fromSecsSinceEpoch(s);
} // -> LocalTime

} // namespace cg::storage
