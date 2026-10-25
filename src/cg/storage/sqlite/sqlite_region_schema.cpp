// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_schema.h"

#include "cg/core/content_type.h"
#include "cg/core/log.h"
#include "cg/storage/sqlite/sqlite_error_log.h"

#include <QDateTime>
#include <QFile>
#include <QStringList>

#include <utility>

namespace cg::storage::sqlite {

namespace {

// Ports TStringListStatic.CreateFromFile / TStringList.LoadFromFile's "one statement per line"
// splitting (spec §2.16.1 "Override-file rules"): a statement may not span lines, and a file
// ending in a line terminator does not produce a phantom trailing empty statement. The files this
// module reads are user-authored plain-ASCII/UTF-8 SQL (the §7.4 fixture, or a user's own
// s.sql/t.sql/e.sql), so UTF-8 decoding is exact for every realistic case.
QList<QByteArray> splitStatements(const QString& text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar(u'\r'), QChar(u'\n'));
    QStringList lines = normalized.split(QChar(u'\n'), Qt::KeepEmptyParts);
    if (!lines.isEmpty() && lines.constLast().isEmpty()) {
        lines.removeLast(); // trailing '\n' does not add a phantom last statement
    }
    QList<QByteArray> statements;
    statements.reserve(lines.size());
    for (const QString& line : std::as_const(lines)) {
        statements.push_back(line.toUtf8());
    }
    return statements;
}

QList<QByteArray> loadStatementFile(const QString& path, QList<QByteArray> defaultValue) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return defaultValue;
    }
    const QString text = QString::fromUtf8(file.readAll());
    return splitStatements(text);
}

} // namespace

SqliteRegionSchema::SqliteRegionSchema(const QString& storagePath, QByteArray mainContentType,
                                       MapVersionFactoryPtr mapVersionFactory, bool logging)
    : mainContentType_(std::move(mainContentType)), mapVersionFactory_(std::move(mapVersionFactory)),
      logging_(logging) {
    // Ports InternalLoadParams (u_TileStorageSQLiteHolder.pas:186-226), the built-in defaults of
    // spec §2.16.1's table.
    sessionSql_ = loadStatementFile(storagePath + QStringLiteral("s.sql"),
                                    {QByteArrayLiteral("PRAGMA encoding = \"UTF-8\""),
                                     QByteArrayLiteral("PRAGMA cache_size = 2000"),
                                     QByteArrayLiteral("PRAGMA main.journal_mode = WAL"),
                                     QByteArrayLiteral("PRAGMA synchronous = NORMAL")});

    newTableSql_ = loadStatementFile(
        storagePath + QStringLiteral("t.sql"),
        {QByteArrayLiteral("CREATE TABLE IF NOT EXISTS t (x INTEGER NOT NULL,y INTEGER NOT NULL,"
                           "v INTEGER DEFAULT 0 NOT NULL,c TEXT,s INTEGER DEFAULT 0 NOT NULL,"
                           "h INTEGER DEFAULT 0 NOT NULL,d INTEGER NOT NULL,b BLOB,"
                           "constraint PK_TB primary key (x,y,v))"),
         QByteArrayLiteral("CREATE INDEX IF NOT EXISTS t_v_idx on t (v)")});

    establishedSql_ = loadStatementFile(storagePath + QStringLiteral("e.sql"), {}); // no default

    if (logging_) {
        errorLog_ = std::make_unique<SqliteErrorLog>(storagePath + QStringLiteral("sqlite.log"));
    }
}

SqliteRegionSchema::~SqliteRegionSchema() = default;

QByteArray SqliteRegionSchema::contentTypeToDb(QByteArrayView contentType) const {
    if (contentType.isEmpty()) {
        return QByteArray();
    }
    QByteArray asBytes(contentType.data(), contentType.size());
    if (asBytes.compare(mainContentType_, Qt::CaseInsensitive) == 0) {
        return QByteArray();
    }
    return asBytes;
}

QByteArray SqliteRegionSchema::contentTypeFromDb(QByteArrayView contentTypeFromDb) const {
    if (contentTypeFromDb.isEmpty()) {
        return mainContentType_;
    }
    // T-0022B2 review finding 4: FContentTypeManager.GetInfo(c) -- nil for an unknown type, the
    // canonical info for a known one.
    const core::ContentTypeInfo* info = core::ContentTypeRegistry::standard().infoByType(contentTypeFromDb);
    return info ? info->type : QByteArray();
}

MapVersionInfoPtr SqliteRegionSchema::versionInfo(const QString& versionStr) const {
    return mapVersionFactory_->byStoreString(versionStr);
}

void SqliteRegionSchema::logError(LogKind kind, const QString& message) const {
    const char* kindText = nullptr;
    switch (kind) {
    case LogKind::Init:
        kindText = "init";
        break;
    case LogKind::Delete:
        kindText = "delete";
        break;
    case LogKind::Select:
        kindText = "select";
        break;
    case LogKind::Replace:
        kindText = "replace";
        break;
    case LogKind::GetVer:
        kindText = "getver";
        break;
    case LogKind::GetMap:
        kindText = "fillmap";
        break;
    }

    qCWarning(core::logStorage) << "sqlite region" << kindText << message;

    if (!logging_) {
        return;
    }

    // Ports LogError's VLogText assembly (u_TileStorageSQLiteHolder.pas:279-291). Delphi's
    // `DateTimeToString` counts the run length of a `z` format specifier and prints milliseconds
    // with 3 digits for any run longer than 1 -- so `'zzzz'` gives `.mmm`, the same 3 digits as
    // `'zzz'`, not 4 (T-0022B2 review finding 5); no invented extra padding digit.
    const QDateTime now = QDateTime::currentDateTime();
    const QString timestamp = now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString line = timestamp + QChar(u'\t') + QChar(u'(') + QString::fromLatin1(kindText) +
                         QChar(u')') + QChar(u'\t') + message + QStringLiteral("\r\n");
    errorLog_->logString(line);
}

} // namespace cg::storage::sqlite
