// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_file_detect.h"

#include "cg/core/content_detector.h"
#include "cg/core/delphi_text.h"
#include "cg/storage/sqlite/oruxmaps_connection.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace cg::storage::sqlite {

namespace {

// Table name (lower case) -> sorted lower-case column names. A table with no row has no entry
// columns (GetTablesInfo reads them from the first row only).
using TablesInfo = QHash<QString, QStringList>;

// Ports QuotedStr.
[[nodiscard]] QString quotedStr(const QString& text) {
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Ports GetTablesInfo (:234-274).
[[nodiscard]] core::Result<TablesInfo> getTablesInfo(SqliteDb& db) {
    TablesInfo info;
    {
        auto stmt = db.prepare(QByteArrayView("SELECT name FROM sqlite_master WHERE type='table'"));
        if (!stmt) {
            return core::Unexpected(stmt.error());
        }
        for (;;) {
            auto step = stmt->step();
            if (!step) {
                return core::Unexpected(step.error());
            }
            if (*step != SqliteStmt::StepResult::Row) {
                break;
            }
            const QString key = core::delphiLowerCase(stmt->columnText(0));
            if (!key.isEmpty()) {
                info.insert(key, QStringList());
            }
        }
    }
    const QList<QString> keys = info.keys();
    for (const QString& key : keys) {
        auto stmt = db.prepare(("SELECT * FROM " + quotedStr(key) + " LIMIT 1").toUtf8());
        if (!stmt) {
            return core::Unexpected(stmt.error());
        }
        auto step = stmt->step();
        if (!step) {
            return core::Unexpected(step.error());
        }
        if (*step != SqliteStmt::StepResult::Row) {
            continue;
        }
        QStringList columns;
        for (int i = 0; i < stmt->columnCount(); ++i) {
            const QString name = core::delphiLowerCase(QString::fromUtf8(stmt->columnName(i)));
            if (!name.isEmpty()) {
                columns.append(name);
            }
        }
        if (!columns.isEmpty()) {
            columns.sort(); // for binary search
            info.insert(key, columns);
        }
    }
    return info;
}

// Ports IsContainsAll / IsContainsAny (:275-302); `columns` is sorted.
[[nodiscard]] bool containsAll(const QStringList& columns, std::initializer_list<const char16_t*> items) {
    return std::all_of(items.begin(), items.end(), [&columns](const char16_t* item) {
        return std::binary_search(columns.cbegin(), columns.cend(), QString::fromUtf16(item));
    });
}
[[nodiscard]] bool containsAny(const QStringList& columns, std::initializer_list<const char16_t*> items) {
    return std::any_of(items.begin(), items.end(), [&columns](const char16_t* item) {
        return std::binary_search(columns.cbegin(), columns.cend(), QString::fromUtf16(item));
    });
}

// Ports TryDetectCacheTypeCode (:315-370).
[[nodiscard]] std::optional<SqliteFileFormat> detectFormat(const QString& path, const TablesInfo& tables) {
    const QString ext = core::delphiLowerCase(QStringLiteral(".") + QFileInfo(path).suffix());
    const auto columnsOf = [&tables](const QString& table) -> const QStringList* {
        const auto it = tables.constFind(table);
        return it == tables.cend() ? nullptr : &*it;
    };
    const QStringList* tiles = columnsOf(QStringLiteral("tiles"));
    const QStringList* metadata = columnsOf(QStringLiteral("metadata"));
    const QStringList* info = columnsOf(QStringLiteral("info"));

    if (ext == u".mbtiles" && metadata != nullptr && containsAll(*metadata, {u"name", u"value"}) &&
        tiles != nullptr && containsAll(*tiles, {u"zoom_level", u"tile_column", u"tile_row", u"tile_data"})) {
        return SqliteFileFormat::MbTiles;
    }
    if (ext == u".rmaps" && info != nullptr && containsAll(*info, {u"minzoom", u"maxzoom"}) &&
        tiles != nullptr && containsAll(*tiles, {u"x", u"y", u"z", u"s", u"image"})) {
        return SqliteFileFormat::RMaps;
    }
    if (ext == u".sqlitedb" && tiles != nullptr && containsAll(*tiles, {u"x", u"y", u"z", u"s", u"image"})) {
        if (info != nullptr) {
            if (containsAny(*info, {u"zooms", u"center_x", u"center_y", u"provider"})) {
                return SqliteFileFormat::Locus;
            }
            if (containsAny(*info,
                            {u"ellipsoid", u"timecolumn", u"expireminutes", u"tilenumbering", u"tilesize"})) {
                return SqliteFileFormat::OsmAnd;
            }
        }
        return SqliteFileFormat::RMaps;
    }
    if (ext == u".db" && tiles != nullptr && containsAll(*tiles, {u"x", u"y", u"z", u"image"}) &&
        !oruxmapsSiblingXmlFile(path).isEmpty()) {
        return SqliteFileFormat::OruxMaps;
    }
    return std::nullopt;
}

// SuperObject's AsString for the value types the `sasgis` JSON can carry.
[[nodiscard]] QString jsonValueAsString(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    case QJsonValue::Double:
        return QString::number(value.toDouble(), 'g', 15);
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return {};
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return {};
}

// Ports TryParseSasGisJson (:376-389). SO() of text that is not JSON is nil in legacy and the next
// call on it raises; that is an error Result here (L-103).
[[nodiscard]] core::Result<void> parseSasGisJson(const QString& json, SqliteFileMetadata& metadata) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return core::fail(
            core::ErrorCode::ParseError,
            QStringLiteral("Invalid JSON in the 'sasgis' value: %1").arg(parseError.errorString()));
    }
    if (!doc.isObject()) {
        return {};
    }
    for (const QString& key : {QStringLiteral("epsg"), QStringLiteral("format")}) {
        const QString value = jsonValueAsString(doc.object().value(key));
        if (!value.isEmpty()) {
            metadata.insert(QStringLiteral("sasgis_") + key, value);
        }
    }
    return {};
}

} // namespace

// FEAT-STO-022
core::Result<std::optional<SqliteFileFormat>> detectSqliteFileFormat(const QString& path) {
    auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadOnly);
    if (!db) {
        return core::Unexpected(db.error());
    }
    const auto tables = getTablesInfo(*db);
    if (!tables) {
        return core::Unexpected(tables.error());
    }
    return detectFormat(path, *tables);
}

// Ports ReadMetadata (:371-486), minus the OruxMaps branch.
core::Result<SqliteFileMetadata> readSqliteFileMetadata(SqliteDb& db, SqliteFileFormat format) {
    SqliteFileMetadata metadata;
    if (format == SqliteFileFormat::MbTiles) {
        auto stmt = db.prepare(QByteArrayView("SELECT name, value FROM metadata"));
        if (!stmt) {
            return core::Unexpected(stmt.error());
        }
        for (;;) {
            auto step = stmt->step();
            if (!step) {
                return core::Unexpected(step.error());
            }
            if (*step != SqliteStmt::StepResult::Row) {
                break;
            }
            const QString key = core::delphiLowerCase(stmt->columnText(0));
            if (!key.isEmpty()) {
                metadata.insert(key, stmt->columnText(1));
            }
        }
    } else if (format != SqliteFileFormat::OruxMaps) {
        auto stmt = db.prepare(QByteArrayView("SELECT * FROM info LIMIT 1"));
        if (!stmt) {
            return core::Unexpected(stmt.error());
        }
        auto step = stmt->step();
        if (!step) {
            return core::Unexpected(step.error());
        }
        if (*step == SqliteStmt::StepResult::Row) {
            for (int i = 0; i < stmt->columnCount(); ++i) {
                const QString key = core::delphiLowerCase(QString::fromUtf8(stmt->columnName(i)));
                if (!key.isEmpty()) {
                    metadata.insert(key, stmt->columnText(i));
                }
            }
        }
    }
    if (metadata.contains(QStringLiteral("sasgis"))) {
        const QString json = metadata.value(QStringLiteral("sasgis")); // a copy: the call inserts
        if (auto parsed = parseSasGisJson(json, metadata); !parsed) {
            return core::Unexpected(parsed.error());
        }
    }
    return metadata;
}

namespace {

[[nodiscard]] core::Result<int> strToInt(const QString& text) {
    int value = 0;
    if (!core::delphiTryStrToInt(text, value)) {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("'%1' is not a valid integer value").arg(text));
    }
    return value;
}

[[nodiscard]] QString valueOf(const SqliteFileMetadata& metadata, const QString& key) {
    return metadata.value(key);
}

} // namespace

// Projection chain of :770-782.
core::Result<int> sqliteFileProjectionEpsg(const SqliteFileMetadata& metadata) {
    if (const QString epsg = valueOf(metadata, QStringLiteral("sasgis_epsg")); !epsg.isEmpty()) {
        return strToInt(epsg);
    }
    if (QString crs = valueOf(metadata, QStringLiteral("crs")); !crs.isEmpty()) {
        const qsizetype colon = crs.indexOf(QLatin1Char(':'));
        if (colon >= 0) {
            crs = crs.mid(colon + 1);
        }
        return strToInt(crs);
    }
    if (valueOf(metadata, QStringLiteral("ellipsoid")) == u"1") {
        return 3395;
    }
    return 3857;
}

// Content-type chain of :784-805 and TryDetectContentType (:487-527).
core::Result<SqliteFileContentType> sqliteFileContentType(SqliteDb& db, SqliteFileFormat format,
                                                          const SqliteFileMetadata& metadata,
                                                          const core::ContentTypeRegistry& registry) {
    SqliteFileContentType result;
    result.type = QByteArrayLiteral("image/jpg");

    if (const QString sasgis = valueOf(metadata, QStringLiteral("sasgis_format")); !sasgis.isEmpty()) {
        result.type = sasgis.toUtf8();
    } else if (const QString fmt = valueOf(metadata, QStringLiteral("format")); !fmt.isEmpty()) {
        if (fmt.contains(QLatin1Char('/'))) {
            result.type = fmt.toUtf8();
        } else if (const core::ContentTypeInfo* info =
                       registry.infoByExt((QStringLiteral(".") + fmt).toUtf8());
                   info != nullptr) {
            result.type = info->type;
        } else {
            result.type.clear(); // unsupported content-type
            result.ext = QStringLiteral(".") + fmt;
        }
    } else {
        const QByteArrayView sql = format == SqliteFileFormat::MbTiles
                                       ? QByteArrayView("SELECT tile_data FROM tiles LIMIT 1")
                                       : QByteArrayView("SELECT image FROM tiles LIMIT 1");
        auto stmt = db.prepare(sql);
        if (!stmt) {
            return core::Unexpected(stmt.error());
        }
        auto step = stmt->step();
        if (!step) {
            return core::Unexpected(step.error());
        }
        if (*step == SqliteStmt::StepResult::Row) {
            const QByteArray detected = core::detectContentType(stmt->columnBlob(0));
            if (!detected.isEmpty()) {
                result.type = detected;
            }
        }
    }

    if (!result.type.isEmpty()) {
        result.isBitmap = registry.isBitmapType(result.type);
        const core::ContentTypeInfo* info = registry.infoByType(result.type);
        result.ext = info != nullptr ? QString::fromLatin1(info->defaultExt) : QString();
    }
    return result;
}

} // namespace cg::storage::sqlite
