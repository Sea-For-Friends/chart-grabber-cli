// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Export/MBTiles/u_StorageExportToMBTiles.pas
#include "cg/region/storage_export_to_mbtiles.h"

#include "cg/core/delphi_text.h"
#include "cg/core/ini_value_codec.h"
#include "cg/core/version.h"
#include "cg/region/log_region.h"

#include <QCryptographicHash>
#include <QFile>
#include <QStringList>

#include <array>

namespace cg::region {

using core::ErrorCode;
using storage::sqlite::SqliteDb;
using storage::sqlite::SqliteStmt;

namespace {

// u_GeoFunc.pas:498-501 IsLonLatRectEmpty
bool isLonLatRectEmpty(const geo::LonLatRect& r) {
    return (r.right <= r.left) || (r.bottom >= r.top);
}

// :168-169; %1 / %2 stand for the two %s of the Pascal text. QString::arg(a, b) expands both in
// one pass, so a "%2" inside the key or value is not expanded again.
constexpr QLatin1StringView kInsertMetadataSql("INSERT INTO metadata (name, value) VALUES (%1,%2)");
constexpr QLatin1StringView
    kInsertMetadataSqlReplace("INSERT OR REPLACE INTO metadata (name, value) VALUES (%1,%2)");

// :403-415
constexpr std::array<QByteArrayView, 3> kMbtilesDdl = {
    QByteArrayView("CREATE TABLE IF NOT EXISTS metadata (name text, value text)"),
    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS metadata_idx ON metadata (name)"),
    QByteArrayView(
        "CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer, "
        "tile_data blob)"),
};

constexpr QByteArrayView kInsertTilesSqlReplace(
    "INSERT OR REPLACE INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?)");
constexpr QByteArrayView kInsertTilesSqlIgnore(
    "INSERT OR IGNORE INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?)");

constexpr QByteArrayView
    kCreateIndexSql("CREATE INDEX IF NOT EXISTS tiles_idx on tiles (zoom_level, tile_column, tile_row)");

// u_MD5Func.pas:36-42 MD5Buffer (Md5Buf + BinToHex, upper-case hex); the only caller is :724.
QByteArray md5Buffer(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().toUpper();
}

// :551-615; cCRLF = #10 (:164)
constexpr std::array<QByteArrayView, 15> kTileMillDdl = {
    // grid_key
    QByteArrayView("CREATE TABLE IF NOT EXISTS grid_key (grid_id TEXT, key_name TEXT);"),
    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS grid_key_lookup ON grid_key (grid_id, key_name);"),

    // grid_utfgrid
    QByteArrayView("CREATE TABLE IF NOT EXISTS grid_utfgrid (grid_id TEXT, grid_utfgrid BLOB);"),
    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS grid_utfgrid_lookup ON grid_utfgrid (grid_id);"),

    // images
    QByteArrayView("CREATE TABLE IF NOT EXISTS images (tile_data blob, tile_id text);"),
    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS images_id ON images (tile_id);"),

    // keymap
    QByteArrayView("CREATE TABLE IF NOT EXISTS keymap (key_name TEXT, key_json TEXT);"),
    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS keymap_lookup ON keymap (key_name);"),

    // map
    QByteArrayView("CREATE TABLE IF NOT EXISTS map (\n"
                   "   zoom_level INTEGER,\n"
                   "   tile_column INTEGER,\n"
                   "   tile_row INTEGER,\n"
                   "   tile_id TEXT,\n"
                   "   grid_id TEXT\n"
                   ");"),

    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS map_index ON map (zoom_level, tile_column, tile_row);"),

    // metadata
    QByteArrayView("CREATE TABLE IF NOT EXISTS metadata (name text, value text);"),
    QByteArrayView("CREATE UNIQUE INDEX IF NOT EXISTS name ON metadata (name);"),

    // grid_data
    QByteArrayView("CREATE VIEW IF NOT EXISTS grid_data AS\n"
                   "    SELECT\n"
                   "        map.zoom_level AS zoom_level,\n"
                   "        map.tile_column AS tile_column,\n"
                   "        map.tile_row AS tile_row,\n"
                   "        keymap.key_name AS key_name,\n"
                   "        keymap.key_json AS key_json\n"
                   "    FROM map\n"
                   "    JOIN grid_key ON map.grid_id = grid_key.grid_id\n"
                   "    JOIN keymap ON grid_key.key_name = keymap.key_name;"),

    // grids
    QByteArrayView("CREATE VIEW IF NOT EXISTS grids AS\n"
                   "    SELECT\n"
                   "        map.zoom_level AS zoom_level,\n"
                   "        map.tile_column AS tile_column,\n"
                   "        map.tile_row AS tile_row,\n"
                   "        grid_utfgrid.grid_utfgrid AS grid\n"
                   "    FROM map\n"
                   "    JOIN grid_utfgrid ON grid_utfgrid.grid_id = map.grid_id;"),

    // tiles
    QByteArrayView("CREATE VIEW IF NOT EXISTS tiles AS\n"
                   "    SELECT\n"
                   "        map.zoom_level AS zoom_level,\n"
                   "        map.tile_column AS tile_column,\n"
                   "        map.tile_row AS tile_row,\n"
                   "        images.tile_data AS tile_data\n"
                   "    FROM map\n"
                   "    JOIN images ON images.tile_id = map.tile_id;"),
};

// :618-621
constexpr QByteArrayView
    kInsertImagesSqlReplace("INSERT OR REPLACE INTO images (tile_id, tile_data) VALUES (?,?)");
constexpr QByteArrayView
    kInsertImagesSqlIgnore("INSERT OR IGNORE INTO images (tile_id, tile_data) VALUES (?,?)");
constexpr QByteArrayView kInsertMapSqlReplace(
    "INSERT OR REPLACE INTO map (zoom_level, tile_column, tile_row, tile_id) VALUES (?,?,?,?)");
constexpr QByteArrayView kInsertMapSqlIgnore(
    "INSERT OR IGNORE INTO map (zoom_level, tile_column, tile_row, tile_id) VALUES (?,?,?,?)");

} // namespace

// ---- TSQLiteStorageMBTilesBase ---------------------------------------------------------------

SqliteStorageMbtilesBase::SqliteStorageMbtilesBase(QString exportPath, QString exportFileName, QString name,
                                                   QString description, QString attribution, bool isLayer,
                                                   QString imgFormat, bool useXYZScheme, bool forceDropTarget,
                                                   bool replaceTiles)
    : name_(std::move(name)), description_(std::move(description)), attribution_(std::move(attribution)),
      imgFormat_(std::move(imgFormat)), useXYZScheme_(useXYZScheme), exportPath_(std::move(exportPath)),
      exportFileName_(std::move(exportFileName)), forceDropTarget_(forceDropTarget),
      replaceTiles_(replaceTiles) {
    if (name_.isEmpty())
        name_ = QStringLiteral("Unnamed map");
    if (description_.isEmpty())
        description_ = QStringLiteral("Created by chart-grabber-cli ") +
                       QLatin1String(core::versionString()); // D row A, T-0311
    imgType_ = isLayer ? QStringLiteral("overlay") : QStringLiteral("baselayer");
    scheme_ = useXYZScheme_ ? QStringLiteral("xyz") : QStringLiteral("tms");
    // FSQLiteAvailable (:224, :374-376) is not ported: unreachable in legacy (system libsqlite3 is a build
    // dependency); no register row.
}

// Legacy has no destructor close: the handle is released WITHOUT committing.
SqliteStorageMbtilesBase::~SqliteStorageMbtilesBase() = default;

core::Result<void> SqliteStorageMbtilesBase::close() { // :227-233
    if (db_) {
        if (auto r = db_->exec("COMMIT TRANSACTION"); !r)
            return std::unexpected(r.error());
        db_.reset();
    }
    return {};
}

core::Result<void> SqliteStorageMbtilesBase::commitAndBeginTran() { // :235-241
    if (db_) {
        if (auto r = db_->exec("COMMIT TRANSACTION"); !r)
            return std::unexpected(r.error());
        if (auto r = db_->exec("BEGIN TRANSACTION"); !r)
            return std::unexpected(r.error());
    }
    return {};
}

core::Result<void> SqliteStorageMbtilesBase::readMetadata() { // :253-304
    existingMinZoom_ = -1;
    existingMaxZoom_ = -1;
    existingBounds_ = geo::LonLatRect{0, 0, 0, 0};

    if (!db_)
        return core::fail(
            ErrorCode::IoError,
            QStringLiteral(
                "MBTiles storage is not open")); // unreachable in legacy (u_ExportTaskToMBTiles.pas:221-243);
                                                 // no register row
    auto stmt = db_->prepare("SELECT name, value FROM metadata");
    if (!stmt)
        return std::unexpected(stmt.error());
    for (;;) {
        auto step = stmt->step();
        if (!step || *step != SqliteStmt::StepResult::Row) {
            // :266 loop ends on any non-ROW result without raising; as mbtiles_connection.cpp:480-488
            if (!step)
                qCWarning(logRegion) << "readMetadata: step failed:" << step.error().what();
            break;
        }
        const QString key = core::delphiLowerCase(core::delphiTrim(stmt->columnText(0)));
        const QString val = core::delphiLowerCase(core::delphiTrim(stmt->columnText(1)));

        if (key == QLatin1String("format")) {
            if (val != imgFormat_)
                return core::fail(ErrorCode::Unsupported,
                                  QStringLiteral("Invalid \"format\" value! Expected \"%1\" but found \"%2\"")
                                      .arg(imgFormat_, val));
        } else if (key == QLatin1String("type")) {
            if (val != imgType_)
                return core::fail(ErrorCode::Unsupported,
                                  QStringLiteral("Invalid \"type\" value! Expected \"%1\" but found \"%2\"")
                                      .arg(imgType_, val));
        } else if (key == QLatin1String("scheme")) {
            if (val != scheme_)
                return core::fail(ErrorCode::Unsupported,
                                  QStringLiteral("Invalid \"scheme\" value! Expected \"%1\" but found \"%2\"")
                                      .arg(scheme_, val));
        } else if (key == QLatin1String("minzoom")) {
            existingMinZoom_ = core::valuecodec::parseInt(val, -1);
        } else if (key == QLatin1String("maxzoom")) {
            existingMaxZoom_ = core::valuecodec::parseInt(val, -1);
        } else if (key == QLatin1String("bounds")) {
            const QStringList items = val.split(QLatin1Char(','), Qt::KeepEmptyParts);
            if (items.size() == 4) {
                geo::LonLatRect b;
                if (core::delphiTryStrToFloat(core::delphiTrim(items[0]), b.left) &&
                    core::delphiTryStrToFloat(core::delphiTrim(items[1]), b.bottom) &&
                    core::delphiTryStrToFloat(core::delphiTrim(items[2]), b.right) &&
                    core::delphiTryStrToFloat(core::delphiTrim(items[3]), b.top))
                    existingBounds_ = b;
            }
        }
    }
    return {};
}

core::Result<void>
SqliteStorageMbtilesBase::writeMetadata(const std::vector<std::pair<QString, QString>>& rows) { // :306-330
    const QString sql(forceDropTarget_ ? kInsertMetadataSql : kInsertMetadataSqlReplace);

    if (!db_)
        return core::fail(
            ErrorCode::IoError,
            QStringLiteral(
                "MBTiles storage is not open")); // unreachable in legacy (u_ExportTaskToMBTiles.pas:221-243);
                                                 // no register row
    if (auto r = db_->exec("BEGIN TRANSACTION"); !r)
        return std::unexpected(r.error());
    for (const auto& [key, val] : rows) {
        // WriteMetaKeyVal (:248-251): spliced between single quotes without escaping (LC-2).
        const QString text =
            sql.arg(QLatin1Char('\'') + key + QLatin1Char('\''), QLatin1Char('\'') + val + QLatin1Char('\''));
        if (auto r = db_->exec(text.toUtf8()); !r)
            return std::unexpected(r.error()); // no rollback: journal_mode = OFF (:323)
    }
    if (auto r = db_->exec("COMMIT TRANSACTION"); !r)
        return std::unexpected(r.error());
    return {};
}

QString SqliteStorageMbtilesBase::boundsStr(const geo::LonLatRect& r) const { // :332-340
    return QString::number(r.left, 'f', 8) + QLatin1Char(',') + QString::number(r.bottom, 'f', 8) +
           QLatin1Char(',') + QString::number(r.right, 'f', 8) + QLatin1Char(',') +
           QString::number(r.top, 'f', 8);
}

QString SqliteStorageMbtilesBase::centerStr(const geo::LonLatRect& r, quint8 minZoom) const { // :342-356
    const geo::LonLat c = r.center();
    return QString::number(c.lon, 'f', 8) + QLatin1Char(',') + QString::number(c.lat, 'f', 8) +
           QLatin1Char(',') + QString::number(minZoom);
}

geo::PointI SqliteStorageMbtilesBase::tileXY(geo::PointI tile, quint8 zoom) const { // :358-367
    geo::PointI result;
    result.setX(tile.x());
    if (useXYZScheme_)
        result.setY(tile.y());
    else
        result.setY((1 << zoom) - tile.y() - 1);
    return result;
}

core::Result<void> SqliteStorageMbtilesBase::openInternal(std::span<const QByteArrayView> tablesDdl,
                                                          bool deleteIfExists) { // :369-399
    if (auto r = close(); !r)
        return r;

    const QString fileName = exportPath_ + exportFileName_;

    if (deleteIfExists && QFile::exists(fileName)) {
        if (!QFile::remove(fileName))
            return core::fail(ErrorCode::IoError, QStringLiteral("Can't delete database: %1").arg(fileName));
    }

    auto db = SqliteDb::open(fileName, SqliteDb::OpenMode::ReadWriteCreate);
    if (!db)
        return std::unexpected(db.error());
    db_.emplace(std::move(*db));

    for (const QByteArrayView pragma :
         {QByteArrayView("PRAGMA locking_mode = EXCLUSIVE"), QByteArrayView("PRAGMA synchronous = OFF"),
          QByteArrayView("PRAGMA journal_mode = OFF"), QByteArrayView("PRAGMA temp_store = MEMORY")}) {
        if (auto r = db_->exec(pragma); !r)
            return std::unexpected(r.error());
    }
    for (const QByteArrayView ddl : tablesDdl) {
        if (auto r = db_->exec(ddl); !r)
            return std::unexpected(r.error());
    }
    return {};
}

// ---- TSQLiteStorageMBTilesClassic ------------------------------------------------------------

core::Result<void> SqliteStorageMbtilesClassic::open(const geo::LonLatRect& lonLatRect,
                                                     std::span<const quint8> zooms) { // :417-498
    if (auto r = openInternal(kMbtilesDdl, forceDropTarget_); !r)
        return r;

    // zooms is never empty: the only caller passes the non-empty zoom list of a started task
    // (u_ExportTaskToMBTiles.pas:221)
    quint8 minZoom = zooms.front();
    quint8 maxZoom = zooms.back();

    geo::LonLatRect bounds = lonLatRect;

    if (!forceDropTarget_) {
        if (auto r = readMetadata(); !r)
            return r;

        if (!isLonLatRectEmpty(existingBounds_))
            bounds = geo::LonLatRect::united(existingBounds_, lonLatRect);
        if (existingMinZoom_ != -1 && minZoom > existingMinZoom_)
            minZoom = static_cast<quint8>(existingMinZoom_);
        if (existingMaxZoom_ != -1 && maxZoom < existingMaxZoom_)
            maxZoom = static_cast<quint8>(existingMaxZoom_);
    }

    const std::vector<std::pair<QString, QString>> metadata = {
        {QStringLiteral("name"), name_},
        {QStringLiteral("type"), imgType_},
        {QStringLiteral("version"), QStringLiteral("1.3")},
        {QStringLiteral("description"), description_},
        {QStringLiteral("format"), imgFormat_},
        {QStringLiteral("bounds"), boundsStr(bounds)},
        {QStringLiteral("attribution"), attribution_},
        {QStringLiteral("minzoom"), QString::number(minZoom)},
        {QStringLiteral("maxzoom"), QString::number(maxZoom)},
        {QStringLiteral("center"), centerStr(lonLatRect, minZoom)}, // the NEW rect, LC-3 (:474 vs :465)
        {QStringLiteral("scheme"), scheme_},
    };
    if (auto r = writeMetadata(metadata); !r)
        return r;

    if (!db_)
        return core::fail(
            ErrorCode::IoError,
            QStringLiteral(
                "MBTiles storage is not open")); // unreachable in legacy (u_ExportTaskToMBTiles.pas:221-243);
                                                 // no register row
    auto stmt = db_->prepare(replaceTiles_ ? kInsertTilesSqlReplace : kInsertTilesSqlIgnore);
    if (!stmt)
        return std::unexpected(stmt.error());
    insertStmt_.emplace(std::move(*stmt));

    if (auto r = db_->exec("BEGIN TRANSACTION"); !r)
        return std::unexpected(r.error());
    return {};
}

core::Result<void> SqliteStorageMbtilesClassic::close() { // :500-517
    if (!db_)
        return {};

    insertStmt_.reset(); // ClearBindings + ClosePrepared

    if (auto r = db_->exec("COMMIT TRANSACTION"); !r)
        return std::unexpected(r.error());

    // create index at the very end to speed up insertion
    if (auto r = db_->exec(kCreateIndexSql); !r)
        return std::unexpected(r.error());

    db_.reset();
    return {};
}

core::Result<void> SqliteStorageMbtilesClassic::add(geo::PointI tile, quint8 zoom,
                                                    const QByteArray& data) { // :519-546
    if (!insertStmt_)
        return core::fail(
            ErrorCode::IoError,
            QStringLiteral(
                "MBTiles storage is not open")); // unreachable in legacy (u_ExportTaskToMBTiles.pas:221-243);
                                                 // no register row

    const geo::PointI t = tileXY(tile, zoom);

    if (auto r = insertStmt_->bindInt(1, zoom); !r)
        return r;
    if (auto r = insertStmt_->bindInt(2, t.x()); !r)
        return r;
    if (auto r = insertStmt_->bindInt(3, t.y()); !r)
        return r;
    // legacy BindBlob(AData.Buffer, AData.Size): the buffer of a zero-size tile is nil, which
    // sqlite3_bind_blob stores as NULL (u_BinaryData.pas:61-63; T-0179)
    if (auto r = data.isEmpty() ? insertStmt_->bindNull(4) : insertStmt_->bindBlob(4, data); !r)
        return r;

    auto step = insertStmt_->step();
    if (!step)
        return std::unexpected(step.error());
    return insertStmt_->reset();
}

// ---- TSQLiteStorageMBTilesTileMill -----------------------------------------------------------

core::Result<void> SqliteStorageMbtilesTileMill::open(const geo::LonLatRect& lonLatRect,
                                                      std::span<const quint8> zooms) { // :623-696
    if (auto r = openInternal(kTileMillDdl, forceDropTarget_); !r)
        return r;

    // zooms is never empty: the only caller passes the non-empty zoom list of a started task
    // (u_ExportTaskToMBTiles.pas:221)
    quint8 minZoom = zooms.front();
    quint8 maxZoom = zooms.back();

    geo::LonLatRect bounds = lonLatRect;

    if (!forceDropTarget_) {
        if (auto r = readMetadata(); !r)
            return r;

        if (!isLonLatRectEmpty(existingBounds_))
            bounds = geo::LonLatRect::united(existingBounds_, lonLatRect);
        if (existingMinZoom_ != -1 && minZoom > existingMinZoom_)
            minZoom = static_cast<quint8>(existingMinZoom_);
        if (existingMaxZoom_ != -1 && maxZoom < existingMaxZoom_)
            maxZoom = static_cast<quint8>(existingMaxZoom_);
    }

    const std::vector<std::pair<QString, QString>> metadata = {
        {QStringLiteral("name"), name_},
        {QStringLiteral("type"), imgType_},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("description"), description_},
        {QStringLiteral("format"), imgFormat_},
        {QStringLiteral("bounds"), boundsStr(bounds)},
        {QStringLiteral("attribution"), attribution_},
        {QStringLiteral("scheme"), scheme_},
        {QStringLiteral("minzoom"), QString::number(minZoom)},
        {QStringLiteral("maxzoom"), QString::number(maxZoom)},
        {QStringLiteral("center"), centerStr(lonLatRect, minZoom)}, // the NEW rect, LC-3 (:671 vs :666)
        {QStringLiteral("template"), QString()},
    };
    if (auto r = writeMetadata(metadata); !r)
        return r;

    if (!db_)
        return core::fail(
            ErrorCode::IoError,
            QStringLiteral(
                "MBTiles storage is not open")); // unreachable in legacy (u_ExportTaskToMBTiles.pas:221-243);
                                                 // no register row

    // map FIRST, then images; the second is not prepared when the first fails (:687-689)
    auto mapStmt = db_->prepare(replaceTiles_ ? kInsertMapSqlReplace : kInsertMapSqlIgnore);
    if (!mapStmt)
        return std::unexpected(mapStmt.error());
    insertMapStmt_.emplace(std::move(*mapStmt));

    auto imagesStmt = db_->prepare(replaceTiles_ ? kInsertImagesSqlReplace : kInsertImagesSqlIgnore);
    if (!imagesStmt)
        return std::unexpected(imagesStmt.error());
    insertImagesStmt_.emplace(std::move(*imagesStmt));

    if (auto r = db_->exec("BEGIN TRANSACTION"); !r)
        return std::unexpected(r.error());
    return {};
}

core::Result<void> SqliteStorageMbtilesTileMill::close() { // :698-709
    // ClearBindings + ClosePrepared of both statements
    insertMapStmt_.reset();
    insertImagesStmt_.reset();

    return SqliteStorageMbtilesBase::close();
}

core::Result<void> SqliteStorageMbtilesTileMill::add(geo::PointI tile, quint8 zoom,
                                                     const QByteArray& data) { // :711-757
    if (!insertMapStmt_ || !insertImagesStmt_)
        return core::fail(
            ErrorCode::IoError,
            QStringLiteral(
                "MBTiles storage is not open")); // unreachable in legacy (u_ExportTaskToMBTiles.pas:221-243);
                                                 // no register row

    const QString tileId = QString::fromLatin1(md5Buffer(data));

    // insert data into 'images' table
    if (auto r = insertImagesStmt_->bindText(1, tileId); !r)
        return r;
    // legacy BindBlob(AData.Buffer, AData.Size): the buffer of a zero-size tile is nil, which
    // sqlite3_bind_blob stores as NULL (u_BinaryData.pas:61-63; T-0179)
    if (auto r = data.isEmpty() ? insertImagesStmt_->bindNull(2) : insertImagesStmt_->bindBlob(2, data); !r)
        return r;
    if (auto step = insertImagesStmt_->step(); !step)
        return std::unexpected(step.error());
    if (auto r = insertImagesStmt_->reset(); !r)
        return r;

    // insert data into 'map' table
    const geo::PointI t = tileXY(tile, zoom);

    if (auto r = insertMapStmt_->bindInt(1, zoom); !r)
        return r;
    if (auto r = insertMapStmt_->bindInt(2, t.x()); !r)
        return r;
    if (auto r = insertMapStmt_->bindInt(3, t.y()); !r)
        return r;
    if (auto r = insertMapStmt_->bindText(4, tileId); !r)
        return r;
    if (auto step = insertMapStmt_->step(); !step)
        return std::unexpected(step.error());
    return insertMapStmt_->reset();
}

} // namespace cg::region
