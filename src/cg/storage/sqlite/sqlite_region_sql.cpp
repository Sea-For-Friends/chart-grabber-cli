// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_sql.h"

namespace cg::storage::sqlite {

namespace {

// Ports QuotedStrA: wraps in single quotes, doubling any embedded single quote.
QByteArray quoteSqlText(QByteArrayView s) {
    QByteArray out;
    out.reserve(s.size() + 2);
    out.append('\'');
    for (const char c : s) {
        if (c == '\'') {
            out.append('\'');
        }
        out.append(c);
    }
    out.append('\'');
    return out;
}

// The requested-version parse/quote step shared by versionFieldIsEqual and
// versionFieldIsEqualOrMax (ParseSQLiteDBVersion, u_TileStorageSQLiteFunc.pas:94-121).
struct ParsedVersion {
    QByteArray sql; // the literal to put on the SQL right-hand side, already quoted if needed
    bool isInt = false;
};

ParsedVersion parseVersionForSql(VersionColMode mode, QByteArrayView requestedVersion) {
    if (requestedVersion.isEmpty()) {
        return ParsedVersion{defaultVersion(mode), true};
    }

    qint64 parsed = 0;
    if (localTryStrToInt64(requestedVersion, &parsed)) {
        QByteArray raw(requestedVersion.data(), requestedVersion.size());
        return ParsedVersion{mode == VersionColMode::Text ? quoteSqlText(raw) : raw, true};
    }

    QByteArray raw(requestedVersion.data(), requestedVersion.size());
    return ParsedVersion{quoteSqlText(raw), false};
}

} // namespace

// Deviation D-31 (docs/parity/deviations.md): uses Qt's `toLongLong` grammar, not legacy
// `TryStrToInt64A`'s (source not in the legacy tree; disputed, no oracle).
bool localTryStrToInt64(QByteArrayView s, qint64* value) {
    if (s.isEmpty()) {
        return false;
    }
    const char first = s[0];
    if (first == 'x' || first == 'X' || first == '$') {
        return false;
    }

    bool ok = false;
    const qint64 parsed = QByteArray(s.data(), s.size()).toLongLong(&ok, 10);
    if (ok && value != nullptr) {
        *value = parsed;
    }
    return ok;
}

QByteArray defaultVersion(VersionColMode mode) {
    return mode == VersionColMode::Int ? QByteArray("0") : QByteArray("''");
}

QByteArray versionFieldIsEqual(VersionColMode mode, QByteArrayView requestedVersion) {
    const ParsedVersion v = parseVersionForSql(mode, requestedVersion);

    QByteArray lhs;
    switch (mode) {
    case VersionColMode::Int:
        lhs = v.isInt ? QByteArray("v") : QByteArray("cast(v as TEXT)");
        break;
    case VersionColMode::Text:
        lhs = v.isInt ? QByteArray("cast(v as TEXT)") : QByteArray("v");
        break;
    }
    return lhs + "=" + v.sql;
}

QByteArray requestedVersionToDb(VersionColMode mode, QByteArrayView requestedVersion) {
    return parseVersionForSql(mode, requestedVersion).sql;
}

namespace {

QByteArray getOrderByVersion(VersionColMode mode, bool desc) {
    const QByteArray order = desc ? QByteArray(" DESC") : QByteArray(" ASC");
    if (mode == VersionColMode::Text) {
        return QByteArray("v COLLATE LOGICALSORTING") + order;
    }
    return QByteArray("v") + order;
}

} // namespace

QByteArray getOrderByVersionAsc(VersionColMode mode) {
    return getOrderByVersion(mode, false);
}

QByteArray getOrderByVersionDesc(VersionColMode mode) {
    return getOrderByVersion(mode, true);
}

QByteArray versionFieldIsEqualOrMax(VersionColMode mode, QByteArrayView requestedVersion) {
    const ParsedVersion v = parseVersionForSql(mode, requestedVersion);

    // Legacy quirk (see header comment): NOT the same rule as versionFieldIsEqual's left-hand side.
    const QByteArray lhs =
        (mode == VersionColMode::Int && !v.isInt) ? QByteArray("cast(v as TEXT)") : QByteArray("v");

    const QByteArray maxExpr =
        "(SELECT v FROM t WHERE x=? AND y=? ORDER BY " + getOrderByVersionDesc(mode) + " LIMIT 1)";

    return lhs + " IN (" + v.sql + "," + maxExpr + ")";
}

QByteArray getCheckOtherVersionsBySizeSqlText() {
    return QByteArray("?<>COALESCE((SELECT s FROM t WHERE x=? AND y=? AND s=?),-1)");
}

QByteArray getCheckOtherVersionsByCrc32SqlText() {
    return QByteArray("?<>COALESCE((SELECT h FROM t WHERE x=? AND y=? AND h=?),-1)");
}

} // namespace cg::storage::sqlite
