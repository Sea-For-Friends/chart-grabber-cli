// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider.h"

#include "cg/core/ini_value_codec.h"

#include <algorithm>

namespace cg::core {

QString IConfigProvider::readString(QStringView id, const QString& def) const {
    const QVariant raw = value(id);
    return raw.isValid() ? raw.toString() : def;
}

qint32 IConfigProvider::readInt(QStringView id, qint32 def) const {
    const QVariant raw = value(id);
    return raw.isValid() ? valuecodec::parseInt(raw.toString(), def) : def;
}

bool IConfigProvider::readBool(QStringView id, bool def) const {
    // Q14: ReadBool is ReadInteger(..., Ord(Default)) <> 0; "True"/"yes" do not parse.
    return readInt(id, def ? 1 : 0) != 0;
}

double IConfigProvider::readFloat(QStringView id, double def) const {
    const QVariant raw = value(id);
    return raw.isValid() ? valuecodec::parseFloat(raw.toString(), def) : def;
}

QDate IConfigProvider::readDate(QStringView id, QDate def) const {
    const QVariant raw = value(id);
    if (!raw.isValid()) {
        return def;
    }
    const QString text = raw.toString();
    return text.isEmpty() ? def : valuecodec::parseDate(text, def);
}

QTime IConfigProvider::readTime(QStringView id, QTime def) const {
    const QVariant raw = value(id);
    if (!raw.isValid()) {
        return def;
    }
    const QString text = raw.toString();
    return text.isEmpty() ? def : valuecodec::parseTime(text, def);
}

QDateTime IConfigProvider::readDateTime(QStringView id, const QDateTime& def) const {
    const QVariant raw = value(id);
    return raw.isValid() ? valuecodec::parseDateTime(raw.toString(), def) : def;
}

QUuid IConfigProvider::readUuid(QStringView id, const QUuid& def) const {
    // Ports u_ConfigProviderHelpers.pas:430-448 (ReadGUID): empty -> default; StringToGUID accepts
    // exactly "{8-4-4-4-12}" (38 characters, hex digits of either case, no trimming, no brace-less
    // form) and raises otherwise -> default. A successfully parsed null GUID is returned as is.
    const QString text = readString(id, QString());
    if (text.isEmpty()) {
        return def;
    }
    static constexpr qsizetype kDashes[] = {9, 14, 19, 24};
    if (text.size() != 38 || text[0] != u'{' || text[37] != u'}') {
        return def;
    }
    for (qsizetype i = 1; i < 37; ++i) {
        const bool dash = std::find(std::begin(kDashes), std::end(kDashes), i) != std::end(kDashes);
        if (dash ? text[i] != u'-' : !valuecodec::isHexDigits(QStringView(text).mid(i, 1))) {
            return def;
        }
    }
    return QUuid::fromString(text);
}

QStringList IConfigProvider::readStringList(QStringView id) const {
    // Ports u_ConfigProviderHelpers.pas:699- (Read/WriteStringListStatic): <id>_Count then
    // <id>_0.._(n-1), trimmed, empties skipped.
    const QString countId = id.toString() + QStringLiteral("_Count");
    const int count = readInt(countId, 0);
    QStringList result;
    for (int i = 0; i < count; ++i) {
        const QString itemId = id.toString() + QLatin1Char('_') + QString::number(i);
        const QString item = readString(itemId, QString()).trimmed();
        if (!item.isEmpty()) {
            result.append(item);
        }
    }
    return result;
}

} // namespace cg::core
