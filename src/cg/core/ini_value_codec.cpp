// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/ini_value_codec.h"

#include <QtGlobal>
#include <QtNumeric>

#include <algorithm>
#include <cmath>

namespace cg::core::valuecodec {

// Delphi StrToIntDef after TCustomIniFile.ReadInteger has rewritten a leading 0x as $ (Q13).
qint32 parseInt(const QString& text, qint32 def) {
    QString str = text;
    if (str.size() > 2 && str.at(0) == u'0' && (str.at(1) == u'x' || str.at(1) == u'X')) {
        str = QLatin1Char('$') + str.sliced(2);
    }
    if (str.isEmpty()) {
        return def;
    }
    qsizetype pos = 0;
    bool negative = false;
    if (str.at(pos) == u'+' || str.at(pos) == u'-') {
        negative = str.at(pos) == u'-';
        ++pos;
    }
    if (pos >= str.size()) {
        return def;
    }
    bool ok = false;
    if (str.at(pos) == u'$') {
        const QString digits = str.sliced(pos + 1);
        if (digits.isEmpty()) {
            return def;
        }
        const qulonglong value = digits.toULongLong(&ok, 16);
        if (!ok || value > 0xFFFFFFFFULL) {
            return def; // Delphi's Val overflows the Integer range and StrToIntDef falls back
        }
        const auto wrapped = static_cast<qint32>(static_cast<quint32>(value));
        return negative ? -wrapped : wrapped;
    }
    const qlonglong value = str.toLongLong(&ok, 10);
    if (!ok || value < -2147483648LL || value > 2147483647LL) {
        return def;
    }
    return static_cast<qint32>(value);
}

double parseFloat(const QString& text, double def) {
    QString str = text;
    if (str.isEmpty()) {
        return def;
    }
    if (str == QLatin1StringView("NAN")) {
        return qQNaN();
    }
    const qsizetype comma = str.indexOf(u','); // Q16: the FIRST comma only
    if (comma >= 0) {
        str.replace(comma, 1, u'.');
    }
    bool ok = false;
    const double value = str.toDouble(&ok);
    // QString::toDouble(), unlike Delphi StrToFloat, also accepts "inf"/"infinity"/"nan" (any
    // case). Only the exact-text "NAN" case above is a legacy-recognised spelling; every other
    // non-finite parse falls back to `def`, exactly as StrToFloat's EConvertError would.
    return (ok && std::isfinite(value)) ? value : def;
}

QDate parseDate(const QString& text, QDate def) {
    const QStringList parts = text.split(u'.');
    if (parts.size() != 3) {
        return def;
    }
    bool okDay = false;
    bool okMonth = false;
    bool okYear = false;
    const int day = parts.at(0).toInt(&okDay);
    const int month = parts.at(1).toInt(&okMonth);
    int year = parts.at(2).toInt(&okYear);
    if (!okDay || !okMonth || !okYear) {
        return def;
    }
    if (parts.at(2).size() <= 2) {
        // TwoDigitYearCenturyWindow = 50 (u_ConfigDataProviderByIniFileSection.pas:122).
        const int base = QDate::currentDate().year() - 50;
        year += (base / 100) * 100;
        if (year < base) {
            year += 100;
        }
    }
    const QDate date(year, month, day);
    return date.isValid() ? date : def;
}

QTime parseTime(const QString& text, QTime def) {
    const QStringList parts = text.split(u':');
    if (parts.size() < 2 || parts.size() > 3) {
        return def;
    }
    bool okHour = false;
    bool okMinute = false;
    bool okSecond = true;
    const int hour = parts.at(0).toInt(&okHour);
    const int minute = parts.at(1).toInt(&okMinute);
    const int second = parts.size() == 3 ? parts.at(2).toInt(&okSecond) : 0;
    if (!okHour || !okMinute || !okSecond) {
        return def;
    }
    const QTime time(hour, minute, second);
    return time.isValid() ? time : def;
}

QDateTime parseDateTime(const QString& text, const QDateTime& def) {
    if (text.isEmpty()) {
        return def;
    }
    const qsizetype space = text.indexOf(u' ');
    if (space < 0) {
        const QDate dateOnly = parseDate(text, QDate());
        if (dateOnly.isValid()) {
            return QDateTime(dateOnly, QTime(0, 0));
        }
        const QTime timeOnly = parseTime(text, QTime());
        return timeOnly.isValid() ? QDateTime(QDate(1899, 12, 30), timeOnly) : def;
    }
    const QDate date = parseDate(text.first(space), QDate());
    const QTime time = parseTime(text.sliced(space + 1).trimmed(), QTime());
    return date.isValid() && time.isValid() ? QDateTime(date, time) : def;
}

QString formatFloat(double value) {
    if (std::isnan(value)) {
        return QStringLiteral("NAN");
    }
    if (std::isinf(value)) {
        return value > 0 ? QStringLiteral("INF") : QStringLiteral("-INF");
    }
    return QString::number(value, 'g', 15); // Delphi FloatToStr, 15 significant digits, '.'
}

QString formatDate(QDate value) {
    return value.toString(QStringLiteral("dd.MM.yyyy"));
}

QString formatTime(QTime value) {
    return value.toString(QStringLiteral("HH:mm:ss"));
}

QString formatDateTime(const QDateTime& value) {
    return value.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

bool isHexDigits(QStringView text) {
    return std::all_of(text.cbegin(), text.cend(), [](QChar ch) {
        return (ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f') || (ch >= u'A' && ch <= u'F');
    });
}

} // namespace cg::core::valuecodec
