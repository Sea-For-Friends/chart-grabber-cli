// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/mbtiles_connection.h"

#include "cg/core/delphi_text.h"
#include "cg/core/log.h"
#include "cg/core/version.h"

#include <QFileInfo>
#include <QLatin1StringView>

#include <array>
#include <tuple>
#include <utility>

namespace cg::storage::sqlite {

namespace {

constexpr int kEmptyZoom = 255; // ports CEmptyZoomValue (:129)

// Ports InvertY (MB:131-134), used for both Y and (never, for MBTiles) Z: `Result := (1 shl Z) -
// Y - 1`. Reproduced bit-for-bit, including its two well-defined-in-Delphi/UB-in-C++ behaviors
// (review finding 1, legacy-defects.md L-47): the compiled `SHL r32, CL` instruction masks the
// shift count to 5 bits, and Delphi's overflow checks are off, so the subtraction wraps as
// unsigned 32-bit arithmetic. Reachable when the enumeration path
// (MbtilesEnumTilesStatement::result) feeds a `zoom_level` >= 31 straight from a foreign or
// corrupt .mbtiles file.
[[nodiscard]] int invertY(int y, quint8 z) {
    return static_cast<int>((1u << (z & 31u)) - static_cast<quint32>(y) - 1u);
}

// Ports IsLonLatRectEmpty(TDoubleRect) (u_GeoFunc.pas:498-500).
[[nodiscard]] bool isLonLatRectEmpty(const geo::LonLatRect& r) {
    return r.right <= r.left || r.bottom >= r.top;
}

// Ports StrToBounds (:136-155): "left,bottom,right,top", '.'-decimal, raises on anything else.
[[nodiscard]] core::Result<geo::LonLatRect> strToBounds(const QString& str) {
    if (str.isEmpty()) {
        return geo::LonLatRect{}; // DoubleRect(0, 0, 0, 0)
    }
    const QStringList parts = str.split(QLatin1Char(','));
    if (parts.size() != 4) {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("MBTiles: Invalid bounds value: \"%1\"").arg(str));
    }
    bool ok = true;
    const auto parseOne = [&ok](const QString& s) {
        double v = 0;
        ok = core::delphiTryStrToFloat(core::delphiTrim(s), v) && ok;
        return v;
    };
    geo::LonLatRect rect;
    rect.left = parseOne(parts[0]);
    rect.bottom = parseOne(parts[1]);
    rect.right = parseOne(parts[2]);
    rect.top = parseOne(parts[3]);
    if (!ok) {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("MBTiles: Invalid bounds value: \"%1\"").arg(str));
    }
    return rect;
}

QString formatFixed8(double v) {
    return QString::number(v, 'f', 8);
}

// Ports GetBoundsStr (:306-309): "%.8f,%.8f,%.8f,%.8f" of Left,Bottom,Right,Top.
QString boundsToStr(const geo::LonLatRect& r) {
    return QStringLiteral("%1,%2,%3,%4")
        .arg(formatFixed8(r.left), formatFixed8(r.bottom), formatFixed8(r.right), formatFixed8(r.top));
}

// Ports GetCenterStr (:311-317): "%.8f,%.8f,%d" of the bounds' midpoint and the given zoom.
QString centerToStr(const geo::LonLatRect& r, int zoom) {
    const geo::LonLat c = r.center();
    return QStringLiteral("%1,%2,%3").arg(formatFixed8(c.lon), formatFixed8(c.lat)).arg(zoom);
}

// Ports UpdateBoundsWithRect (:319-352): expands `bounds` in place to also cover `rect`; returns
// whether anything changed. A degenerate `rect` (e.g. width/height 0) changes nothing.
bool updateBoundsWithRect(geo::LonLatRect& bounds, const geo::LonLatRect& rect) {
    if (isLonLatRectEmpty(rect)) {
        return false;
    }
    if (isLonLatRectEmpty(bounds)) {
        bounds = rect;
        return true;
    }
    bool changed = false;
    if (rect.left < bounds.left) {
        bounds.left = rect.left;
        changed = true;
    }
    if (rect.right > bounds.right) {
        bounds.right = rect.right;
        changed = true;
    }
    if (rect.top > bounds.top) {
        bounds.top = rect.top;
        changed = true;
    }
    if (rect.bottom < bounds.bottom) {
        bounds.bottom = rect.bottom;
        changed = true;
    }
    return changed;
}

// Ports GetFormatStr (:248-258): the main content type's default extension without the dot,
// `jpeg -> jpg`, falling back to the full MIME string when it is none of png/jpg/webp/pbf.
QByteArray formatString(const core::ContentTypeInfo& mainContentType) {
    QByteArray ext = mainContentType.defaultExt.mid(1); // Copy(GetDefaultExt, 2): drop the leading '.'
    if (ext == "jpeg") {
        return "jpg";
    }
    if (ext == "png" || ext == "jpg" || ext == "webp" || ext == "pbf") {
        return ext;
    }
    return mainContentType.type;
}

// Ports TMetadataConnectionStatementMBTiles.ExecUpsert (:686-703). Unlike the base's seven leaf
// statement types (F1/F3's "a CACHED prepare failure degrades to a silent `false`"), this
// statement raises unconditionally on ANY CheckPrepared failure -- fresh or cached -- because it
// is not one of the flavour hooks that rework special-cased; it is MBTiles' own private helper,
// calling RaiseSQLite3Error itself exactly like legacy's ExecUpsert does. Used both by CreateTables
// (a fresh local statement, matching legacy's local VMetadataStmt) and by doUpdateMetadata (the
// member metadataUpsertStmt_).
core::Result<void> execUpsert(ConnectionStatement& stmt, sqlite::SqliteDb& db, const QString& name,
                              const QString& value) {
    auto prepared = stmt.ensurePrepared(db);
    if (!prepared) {
        return core::Unexpected(prepared.error());
    }
    if (!*prepared) {
        return core::fail(core::ErrorCode::IoError, db.lastError());
    }
    if (auto r = stmt.stmt().bindText(1, name); !r) {
        return core::Unexpected(r.error());
    }
    if (auto r = stmt.stmt().bindText(2, value); !r) {
        return core::Unexpected(r.error());
    }
    auto step = stmt.stmt().step();
    std::ignore = stmt.stmt().reset();
    if (!step) {
        return core::Unexpected(step.error());
    }
    if (*step != sqlite::SqliteStmt::StepResult::Done) {
        return core::fail(core::ErrorCode::IoError, db.lastError());
    }
    return {};
}

// --- the seven statement hooks + the metadata upsert/select --------------------------------

// Ports TTileDataConnectionStatementMBTiles (:446-467).
class MbtilesTileDataStatement final : public TileDataStatement {
public:
    using TileDataStatement::TileDataStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int y = xy.y();
        if (invertedY()) {
            y = invertY(y, zoom);
        }
        if (auto r = stmt().bindInt(1, zoom); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.x()); !r) {
            return r;
        }
        return stmt().bindInt(3, y);
    }
    QByteArray result() override { return blobColumn(0); }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral(
            "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?");
    }
};

// Ports TTileInfoConnectionStatementMBTiles (:469-492).
class MbtilesTileInfoStatement final : public TileInfoStatement {
public:
    using TileInfoStatement::TileInfoStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int y = xy.y();
        if (invertedY()) {
            y = invertY(y, zoom);
        }
        if (auto r = stmt().bindInt(1, zoom); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.x()); !r) {
            return r;
        }
        return stmt().bindInt(3, y);
    }
    quint32 result() override { return static_cast<quint32>(stmt().columnInt(0)); }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral(
            "SELECT length(tile_data) FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?");
    }
};

// Ports TRectInfoConnectionStatementMBTiles (:494-539): the WHERE clause's tile_row comparison
// direction itself depends on invertedY (:499-502), not just the bound values.
class MbtilesRectInfoStatement final : public RectInfoStatement {
public:
    using RectInfoStatement::RectInfoStatement;

    core::Result<void> bindParams(geo::RectI rect, quint8 zoom) override {
        zoomForResult_ = zoom;
        int top = rect.top;
        int bottom = rect.bottom;
        if (invertedY()) {
            top = invertY(top, zoom);
            bottom = invertY(bottom, zoom);
        }
        if (auto r = stmt().bindInt(1, zoom); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, rect.left); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, rect.right); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(4, top); !r) {
            return r;
        }
        return stmt().bindInt(5, bottom);
    }
    void result(int& x, int& y, quint32& size) override {
        x = stmt().columnInt(0);
        y = stmt().columnInt(1);
        size = static_cast<quint32>(stmt().columnInt(2));
        if (invertedY()) {
            y = invertY(y, zoomForResult_);
        }
    }

protected:
    QByteArray sqlText() const override {
        return invertedY() ? QByteArrayLiteral("SELECT tile_column, tile_row, length(tile_data) FROM "
                                               "tiles WHERE zoom_level = ? AND tile_column >= ? AND "
                                               "tile_column < ? AND tile_row <= ? AND tile_row > ?")
                           : QByteArrayLiteral("SELECT tile_column, tile_row, length(tile_data) FROM "
                                               "tiles WHERE zoom_level = ? AND tile_column >= ? AND "
                                               "tile_column < ? AND tile_row >= ? AND tile_row < ?");
    }

private:
    quint8 zoomForResult_ = 0;
};

// Ports TEnumTilesConnectionStatementMBTiles (:541-559).
class MbtilesEnumTilesStatement final : public EnumTilesStatement {
public:
    using EnumTilesStatement::EnumTilesStatement;

    void result(int& x, int& y, quint8& z, QByteArray& blob) override {
        z = static_cast<quint8>(stmt().columnInt(0));
        x = stmt().columnInt(1);
        y = stmt().columnInt(2);
        blob = blobColumn(3);
        if (invertedY()) {
            y = invertY(y, z);
        }
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles");
    }
};

// Ports TInsertTileConnectionStatementMBTiles (:561-582).
class MbtilesInsertTileStatement final : public InsertTileStatement {
public:
    using InsertTileStatement::InsertTileStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom, const QByteArray& data) override {
        int y = xy.y();
        if (invertedY()) {
            y = invertY(y, zoom);
        }
        if (auto r = stmt().bindInt(1, zoom); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.x()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, y); !r) {
            return r;
        }
        return stmt().bindBlob(4, data);
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) "
                                 "VALUES (?,?,?,?)");
    }
};

// Ports TUpdateTileConnectionStatementMBTiles (:584-605): the blob binds FIRST (param 1), unlike
// every other statement here -- matches the SQL text's own parameter order.
class MbtilesUpdateTileStatement final : public UpdateTileStatement {
public:
    using UpdateTileStatement::UpdateTileStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom, const QByteArray& data) override {
        int y = xy.y();
        if (invertedY()) {
            y = invertY(y, zoom);
        }
        if (auto r = stmt().bindBlob(1, data); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, zoom); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, xy.x()); !r) {
            return r;
        }
        return stmt().bindInt(4, y);
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral(
            "UPDATE tiles SET tile_data = ? WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?");
    }
};

// Ports TDeleteTileConnectionStatementMBTiles (:607-625).
class MbtilesDeleteTileStatement final : public DeleteTileStatement {
public:
    using DeleteTileStatement::DeleteTileStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int y = xy.y();
        if (invertedY()) {
            y = invertY(y, zoom);
        }
        if (auto r = stmt().bindInt(1, zoom); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.x()); !r) {
            return r;
        }
        return stmt().bindInt(3, y);
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral(
            "DELETE FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?");
    }
};

// Ports the upsert half of TMetadataConnectionStatementMBTiles (:627-643, FText at :640-642).
class MbtilesMetadataUpsertStatement final : public ConnectionStatement {
public:
    MbtilesMetadataUpsertStatement() : ConnectionStatement(false, false) {}

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("INSERT INTO metadata (name, value) VALUES (?,?) "
                                 "ON CONFLICT (name) DO UPDATE SET value = excluded.value");
    }
};

// Ports FSelectStmt (:635-638): bounds/center/maxzoom/minzoom.
class MbtilesMetadataSelectStatement final : public ConnectionStatement {
public:
    MbtilesMetadataSelectStatement() : ConnectionStatement(false, false) {}

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT name, value FROM metadata WHERE name in (\"bounds\", \"center\", "
                                 "\"maxzoom\", \"minzoom\")");
    }
};

} // namespace

// --- MbtilesConnection -----------------------------------------------------------------------

MbtilesConnection::MbtilesConnection(ConstructKey, bool isReadOnly, QString fileName,
                                     core::ContentTypeInfo mainContentType,
                                     const core::ContentTypeRegistry& contentTypeManager,
                                     geo::ProjectionSetPtr projectionSet)
    : SqliteFileConnection(isReadOnly, std::move(fileName), mainContentType, contentTypeManager,
                           std::move(projectionSet)),
      isReadOnly_(isReadOnly), mainContentType_(std::move(mainContentType)), bounds_{}, minZoom_(kEmptyZoom),
      maxZoom_(kEmptyZoom) {
    // Placeholder invertedY: the real value depends on the `scheme` metadata key, which is not
    // known until after initialize() has run fetchMetadata() -- see the header's class doc
    // comment. Their SQL text does not depend on invertedY (only RectInfo's does, and it is not
    // built here), so initialize()'s eager prepare of these two succeeds/fails identically
    // regardless of this placeholder; finishOpen() rebuilds both with the real value.
    tileDataStmt_ = std::make_unique<MbtilesTileDataStatement>(/*invertedY=*/true, /*invertedZ=*/false);
    tileInfoStmt_ = std::make_unique<MbtilesTileInfoStatement>(/*invertedY=*/true, /*invertedZ=*/false);
}

core::Result<std::unique_ptr<MbtilesConnection>>
MbtilesConnection::open(bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
                        const core::ContentTypeRegistry& contentTypeManager,
                        geo::ProjectionSetPtr projectionSet, SqliteFileInfoPtr existingInfo) {
    // ConstructKey{} (review finding 4): only open() can name `ConstructKey` (private), so this is
    // the only call site in the codebase that can construct a MbtilesConnection.
    auto connection = std::make_unique<MbtilesConnection>(ConstructKey{}, isReadOnly, std::move(fileName),
                                                          std::move(mainContentType), contentTypeManager,
                                                          std::move(projectionSet));
    if (auto r = connection->initialize(std::move(existingInfo)); !r) {
        return core::Unexpected(r.error());
    }
    if (auto r = connection->finishOpen(); !r) {
        return core::Unexpected(r.error());
    }
    return connection;
}

core::Result<void> MbtilesConnection::createTables() {
    // https://github.com/mapbox/mbtiles-spec/blob/master/1.3/spec.md
    static constexpr std::array<QByteArrayView, 4> kCreate = {
        QByteArrayView("CREATE TABLE metadata (name text, value text)"),
        QByteArrayView("CREATE UNIQUE INDEX metadata_idx ON metadata (name)"),
        QByteArrayView("CREATE TABLE tiles (zoom_level integer, tile_column integer, tile_row integer, "
                       "tile_data blob)"),
        QByteArrayView("CREATE UNIQUE INDEX tiles_idx on tiles (zoom_level, tile_column, tile_row)"),
    };
    for (const QByteArrayView sql : kCreate) {
        if (auto r = db().exec(sql); !r) {
            return core::Unexpected(r.error());
        }
    }

    // A fresh local statement, matching legacy's local VMetadataStmt (:262, :272-281) -- the
    // member metadataUpsertStmt_ does not exist yet at this point (it is built in finishOpen(),
    // after createTables()/fetchMetadata() have both already run inside initialize()).
    MbtilesMetadataUpsertStatement upsert;
    const QString name = QFileInfo(fileInfo()->fileName()).completeBaseName();
    if (auto r = execUpsert(upsert, db(), QStringLiteral("name"), name); !r) {
        return r;
    }
    // D row A, T-0311: legacy writes 'Created by SAS.Planet'
    // (u_TileStorageSQLiteFileConnectionMBTiles.pas:275).
    if (auto r = execUpsert(upsert, db(), QStringLiteral("description"),
                            QStringLiteral("Created by chart-grabber-cli ") +
                                QLatin1String(core::versionString()));
        !r) {
        return r;
    }
    if (auto r = execUpsert(upsert, db(), QStringLiteral("format"),
                            QString::fromUtf8(formatString(mainContentType_)));
        !r) {
        return r;
    }
    if (auto r = execUpsert(upsert, db(), QStringLiteral("scheme"), QStringLiteral("tms")); !r) {
        return r;
    }
    const int epsg = projectionSet()->zoom(0)->projectionType()->projectionEpsg();
    if (auto r = execUpsert(upsert, db(), QStringLiteral("crs"), QStringLiteral("EPSG:%1").arg(epsg)); !r) {
        return r;
    }
    return {};
}

core::Result<void> MbtilesConnection::fetchMetadata() {
    auto prepared = db().prepare(QByteArrayView("SELECT name, value FROM metadata"));
    if (!prepared) {
        return core::Unexpected(prepared.error()); // ports PrepareStatement's raise, uncaught
    }
    sqlite::SqliteStmt stmt = std::move(*prepared);
    for (;;) {
        auto step = stmt.step();
        if (!step || *step != sqlite::SqliteStmt::StepResult::Row) {
            if (!step) {
                qCWarning(core::logStorage)
                    << "MbtilesConnection::fetchMetadata: step failed:" << step.error().what();
            }
            break; // ports the while-loop's own exit: any non-ROW outcome, no raise either way
        }
        fileInfo()->addOrSetMetadataValue(core::delphiLowerCase(stmt.columnText(0)), stmt.columnText(1));
    }
    return {};
}

core::Result<void> MbtilesConnection::doUpdateMetadata(const QString& name, const QString& value) {
    if (auto r = execUpsert(*metadataUpsertStmt_, db(), name, value); !r) {
        return r;
    }
    fileInfo()->addOrSetMetadataValue(name, value);
    return {};
}

core::Result<std::array<QString, 4>> MbtilesConnection::execSelect() {
    auto prepared = metadataSelectStmt_->ensurePrepared(db());
    if (!prepared) {
        return core::Unexpected(prepared.error());
    }
    if (!*prepared) {
        return core::fail(core::ErrorCode::IoError, db().lastError());
    }

    std::array<QString, 4> result;
    sqlite::SqliteStmt& stmt = metadataSelectStmt_->stmt();
    for (;;) {
        auto step = stmt.step();
        if (!step || *step != sqlite::SqliteStmt::StepResult::Row) {
            if (!step) {
                qCWarning(core::logStorage)
                    << "MbtilesConnection::execSelect: step failed:" << step.error().what();
            }
            break;
        }
        const QString name = core::delphiLowerCase(stmt.columnText(0));
        int index = -1;
        if (name == QLatin1String("bounds")) {
            index = 0;
        } else if (name == QLatin1String("center")) {
            index = 1;
        } else if (name == QLatin1String("maxzoom")) {
            index = 2;
        } else if (name == QLatin1String("minzoom")) {
            index = 3;
        }
        if (index >= 0) { // the WHERE clause already restricts to these four names
            result[static_cast<std::size_t>(index)] = stmt.columnText(1);
        }
    }
    std::ignore = stmt.reset();
    return result;
}

core::Result<void> MbtilesConnection::updateMetadata(geo::PointI xy, quint8 zoom) {
    // "this function executed inside sql transaction" (:368-369).
    int doUpdate = 0;
    bool doUpdateCenter = false;

    if (!projectionSet()->checkZoom(zoom)) {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("MBTiles: Invalid zoom value: %1").arg(static_cast<int>(zoom)));
    }

    if (minZoom_ == kEmptyZoom || minZoom_ > zoom) {
        minZoom_ = zoom;
        ++doUpdate;
    }
    if (maxZoom_ == kEmptyZoom || maxZoom_ < zoom) {
        maxZoom_ = zoom;
        ++doUpdate;
    }

    const geo::LonLatRect tileRect = projectionSet()->zoom(zoom)->tilePos2LonLatRect(xy);
    if (updateBoundsWithRect(bounds_, tileRect)) {
        ++doUpdate;
    }

    if (doUpdate == 0) {
        return {};
    }

    auto selected = execSelect();
    if (!selected) {
        return core::Unexpected(selected.error());
    }
    const std::array<QString, 4>& str = *selected; // [0]=bounds [1]=center [2]=maxzoom [3]=minzoom

    // minzoom
    {
        int dbMinZoom = 0;
        if (!core::delphiTryStrToInt(str[3], dbMinZoom)) {
            dbMinZoom = kEmptyZoom;
        }
        if (dbMinZoom == kEmptyZoom || dbMinZoom > minZoom_) {
            if (auto r = doUpdateMetadata(QStringLiteral("minzoom"), QString::number(minZoom_)); !r) {
                return r;
            }
            doUpdateCenter = true;
        } else if (dbMinZoom < minZoom_) {
            minZoom_ = dbMinZoom;
        }
    }

    // maxzoom
    {
        int dbMaxZoom = 0;
        if (!core::delphiTryStrToInt(str[2], dbMaxZoom)) {
            dbMaxZoom = kEmptyZoom;
        }
        if (dbMaxZoom == kEmptyZoom || dbMaxZoom < maxZoom_) {
            if (auto r = doUpdateMetadata(QStringLiteral("maxzoom"), QString::number(maxZoom_)); !r) {
                return r;
            }
        } else if (dbMaxZoom > maxZoom_) {
            maxZoom_ = dbMaxZoom;
        }
    }

    // bounds
    {
        auto dbBounds = strToBounds(str[0]);
        if (!dbBounds) {
            return core::Unexpected(dbBounds.error());
        }
        if (isLonLatRectEmpty(*dbBounds)) {
            if (auto r = doUpdateMetadata(QStringLiteral("bounds"), boundsToStr(bounds_)); !r) {
                return r;
            }
            doUpdateCenter = true;
        } else if (updateBoundsWithRect(*dbBounds, bounds_)) {
            if (auto r = doUpdateMetadata(QStringLiteral("bounds"), boundsToStr(*dbBounds)); !r) {
                return r;
            }
            bounds_ = *dbBounds;
            doUpdateCenter = true;
        }
    }

    // center
    if (doUpdateCenter) {
        // MB:311 declares GetCenterStr's zoom parameter `ACenterZoom: Byte`; MB:437 passes
        // FMinZoom (Integer) into it, so a negative minZoom_ (a foreign/corrupt file's `minzoom`
        // row, MB:407-409) is truncated to a Byte -- e.g. -1 becomes 255 (review finding 2).
        const QString centerStr = centerToStr(bounds_, static_cast<quint8>(minZoom_));
        if (centerStr != str[1]) {
            if (auto r = doUpdateMetadata(QStringLiteral("center"), centerStr); !r) {
                return r;
            }
        }
    }

    return {};
}

core::Result<void> MbtilesConnection::finishOpen() {
    // Ports the derived constructor's body that runs after `inherited Create` (:172-228, minus
    // statement construction, which is split between the constructor and here -- see the header's
    // class doc comment).
    if (auto format = fileInfo()->metadataValue(QStringLiteral("format"))) {
        const bool extMatches =
            core::delphiSameText(QLatin1Char('.') + *format, QString::fromUtf8(mainContentType_.defaultExt));
        const bool typeMatches = core::delphiSameText(*format, QString::fromUtf8(mainContentType_.type));
        if (!extMatches && !typeMatches) {
            return core::fail(
                core::ErrorCode::ParseError,
                QStringLiteral("MBTiles: The detected tile format \"%1\" does not match the provided "
                               "content type \"%2\"")
                    .arg(*format, QString::fromUtf8(mainContentType_.type)));
        }
    }

    if (auto bounds = fileInfo()->metadataValue(QStringLiteral("bounds"))) {
        auto parsed = strToBounds(*bounds);
        if (!parsed) {
            return core::Unexpected(parsed.error());
        }
        bounds_ = *parsed;
    } else {
        bounds_ = geo::LonLatRect{};
    }

    const auto parseZoom = [this](const QString& key) {
        auto v = fileInfo()->metadataValue(key);
        int n = 0;
        return v && core::delphiTryStrToInt(*v, n) ? n : kEmptyZoom;
    };
    minZoom_ = parseZoom(QStringLiteral("minzoom"));
    maxZoom_ = parseZoom(QStringLiteral("maxzoom"));

    bool invertedY = true; // ":203-207" -- missing or not exactly "xyz" (case-insensitive) => inverted
    if (auto scheme = fileInfo()->metadataValue(QStringLiteral("scheme"))) {
        invertedY = !core::delphiSameText(*scheme, QStringLiteral("xyz"));
    }

    // Read access (:212-215): rebuild tileData/tileInfo now that invertedY is finally known (see
    // the header's class doc comment), plus rectInfo/enumTiles for the first time. If the
    // connection was never enabled (the base's eager prepare of the placeholder objects already
    // failed), leave tileData/tileInfo alone -- every operation checks isEnabled() before ever
    // touching them, so their invertedY correctness is moot and re-attempting the prepare here
    // would misreport a cached failure as a fresh one.
    if (isEnabled()) {
        tileDataStmt_ = std::make_unique<MbtilesTileDataStatement>(invertedY, false);
        tileInfoStmt_ = std::make_unique<MbtilesTileInfoStatement>(invertedY, false);
        if (auto r = tileDataStmt_->ensurePrepared(db()); !r) {
            return core::Unexpected(r.error());
        }
        if (auto r = tileInfoStmt_->ensurePrepared(db()); !r) {
            return core::Unexpected(r.error());
        }
    }
    rectInfoStmt_ = std::make_unique<MbtilesRectInfoStatement>(invertedY, false);
    enumTilesStmt_ = std::make_unique<MbtilesEnumTilesStatement>(invertedY, false);

    // Write access (:218-224): only when !isReadOnly_.
    if (!isReadOnly_) {
        insertStmt_ = std::make_unique<MbtilesInsertTileStatement>(invertedY, false);
        updateStmt_ = std::make_unique<MbtilesUpdateTileStatement>(invertedY, false);
        deleteStmt_ = std::make_unique<MbtilesDeleteTileStatement>(invertedY, false);
        metadataUpsertStmt_ = std::make_unique<MbtilesMetadataUpsertStatement>();
        metadataSelectStmt_ = std::make_unique<MbtilesMetadataSelectStatement>();
    }

    return {};
}

} // namespace cg::storage::sqlite
