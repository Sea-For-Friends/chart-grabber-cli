// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/rmaps_connection.h"

#include "cg/core/delphi_text.h"
#include "cg/core/log.h"

#include <QStringList>

#include <tuple>
#include <utility>

namespace cg::storage::sqlite {

namespace {

constexpr int kEmptyZoom = 255; // ports CEmptyZoomValue (:143)

// Ports InvertZ (:137-140): `Result := 17 - Z`. Z is Integer at every call site; BindParams'
// callers (FetchOne/FetchRectInfo/Insert/Delete) always pass a Byte AZoom widened to Integer, so
// 17 - Z never leaves int32 range there. The enumeration path is different: EnumTiles' Z comes
// straight from a foreign/corrupt file's `z` column (RMaps.pas:489, an arbitrary SQLite INTEGER,
// not the Byte the public API otherwise guarantees), so an extreme Z can make `17 - Z` overflow
// int32. Delphi's overflow checks are off, so legacy wraps as 32-bit two's-complement arithmetic;
// reproduced with the same unsigned-intermediate idiom mbtiles_connection.cpp's invertY() uses for
// its own out-of-range case (legacy-defects.md L-47 sibling; T-0022C2-review.md's "For C3" note).
[[nodiscard]] int invertZ(int z) {
    return static_cast<int>(17u - static_cast<quint32>(z));
}

// Ports SwapMinMaxZoom (:146-153).
void swapMinMaxZoom(int& minZoom, int& maxZoom) {
    std::swap(minZoom, maxZoom);
}

// Ports UpdateZooms (:155-175): inserts AZoom into AZooms' sorted position if not already present;
// returns whether anything was inserted.
bool updateZooms(int zoom, QList<int>& zooms) {
    qsizetype index = zooms.size();
    for (qsizetype i = 0; i < zooms.size(); ++i) {
        if (zooms[i] == zoom) {
            return false;
        }
        if (zooms[i] > zoom) {
            index = i;
            break;
        }
    }
    zooms.insert(index, zoom);
    return true;
}

// Ports StrToZooms (:177-190): ';'-separated integers, trimmed, inverted when AIsInvertedZ. Raises
// (StrToInt) on any non-integer element, uncaught -- mapped to an error Result the same way
// mbtiles_connection.cpp's strToBounds() maps StrToBounds' own raise. An empty AStr (SplitString's
// own 0-length result for an empty input, matching mbtiles_connection.cpp's strToBounds() early
// return for the same reason) yields an empty list without error -- reachable on every brand-new
// Locus file, whose `zooms` column is NULL until the first tile is written.
[[nodiscard]] core::Result<QList<int>> strToZooms(const QString& str, bool isInvertedZ) {
    if (str.isEmpty()) {
        return QList<int>{};
    }
    const QStringList parts = str.split(QLatin1Char(';'));
    QList<int> result;
    result.reserve(parts.size());
    for (const QString& part : parts) {
        int value = 0;
        if (!core::delphiTryStrToInt(core::delphiTrim(part), value)) {
            return core::fail(core::ErrorCode::ParseError,
                              QStringLiteral("RMaps: Invalid zooms value: \"%1\"").arg(str));
        }
        if (isInvertedZ) {
            value = invertZ(value);
        }
        result.push_back(value);
    }
    return result;
}

// Ports ZoomsToStr (:192-204).
[[nodiscard]] QString zoomsToStr(const QList<int>& zooms, bool isInvertedZ) {
    QStringList parts;
    parts.reserve(zooms.size());
    for (int z : zooms) {
        if (isInvertedZ) {
            z = invertZ(z);
        }
        parts.push_back(QString::number(z));
    }
    return parts.join(QLatin1Char(';'));
}

// --- the seven statement hooks, shared by all three formats ---------------------------------
// Y is never inverted in this family (the class doc comment); every constructor call below passes
// invertedY = false. Only Z inversion (per-connection, computed in finishOpen()) varies.

// Ports TTileDataConnectionStatementRMaps (:396-419).
class RmapsTileDataStatement final : public TileDataStatement {
public:
    using TileDataStatement::TileDataStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int z = zoom;
        if (invertedZ()) {
            z = invertZ(z);
        }
        if (auto r = stmt().bindInt(1, xy.x()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.y()); !r) {
            return r;
        }
        return stmt().bindInt(3, z);
    }
    QByteArray result() override { return blobColumn(0); }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT image FROM tiles WHERE x = ? AND y = ? AND z = ?");
    }
};

// Ports TTileInfoConnectionStatementRMaps (:421-444).
class RmapsTileInfoStatement final : public TileInfoStatement {
public:
    using TileInfoStatement::TileInfoStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int z = zoom;
        if (invertedZ()) {
            z = invertZ(z);
        }
        if (auto r = stmt().bindInt(1, xy.x()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.y()); !r) {
            return r;
        }
        return stmt().bindInt(3, z);
    }
    quint32 result() override { return static_cast<quint32>(stmt().columnInt(0)); }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT length(image) FROM tiles WHERE x = ? AND y = ? AND z = ?");
    }
};

// Ports TRectInfoConnectionStatementRMaps (:446-475).
class RmapsRectInfoStatement final : public RectInfoStatement {
public:
    using RectInfoStatement::RectInfoStatement;

    core::Result<void> bindParams(geo::RectI rect, quint8 zoom) override {
        int z = zoom;
        if (invertedZ()) {
            z = invertZ(z);
        }
        if (auto r = stmt().bindInt(1, rect.left); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, rect.right); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, rect.top); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(4, rect.bottom); !r) {
            return r;
        }
        return stmt().bindInt(5, z);
    }
    void result(int& x, int& y, quint32& size) override {
        x = stmt().columnInt(0);
        y = stmt().columnInt(1);
        size = static_cast<quint32>(stmt().columnInt(2));
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("SELECT x, y, length(image) FROM tiles WHERE x >= ? AND x < ? AND "
                                 "y >= ? AND y < ? AND z = ?");
    }
};

// Ports TEnumTilesConnectionStatementRMaps (:477-495).
class RmapsEnumTilesStatement final : public EnumTilesStatement {
public:
    using EnumTilesStatement::EnumTilesStatement;

    void result(int& x, int& y, quint8& z, QByteArray& blob) override {
        x = stmt().columnInt(0);
        y = stmt().columnInt(1);
        int zi = stmt().columnInt(2);
        blob = blobColumn(3);
        if (invertedZ()) {
            zi = invertZ(zi);
        }
        // ATileInfo.FZoom (legacy) truncates this full-width Integer to its own field width just
        // like the port's quint8 out-parameter does here -- InvertZ runs at full int precision
        // FIRST (above), matching legacy's assignment order (RMaps.pas:485-495).
        z = static_cast<quint8>(zi);
    }

protected:
    QByteArray sqlText() const override { return QByteArrayLiteral("SELECT x, y, z, image FROM tiles"); }
};

// Ports TInsertTileConnectionStatementRMaps (:497-518): `s` is always 0, a literal in the SQL text
// (a *layer* index in RMaps, not a size -- storage.md §2.17.2), never bound.
class RmapsInsertTileStatement final : public InsertTileStatement {
public:
    using InsertTileStatement::InsertTileStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom, const QByteArray& data) override {
        int z = zoom;
        if (invertedZ()) {
            z = invertZ(z);
        }
        if (auto r = stmt().bindInt(1, xy.x()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.y()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, z); !r) {
            return r;
        }
        return stmt().bindBlob(4, data);
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("INSERT INTO tiles (x, y, z, s, image) VALUES (?,?,?,0,?)");
    }
};

// Ports TUpdateTileConnectionStatementRMaps (:520-541).
class RmapsUpdateTileStatement final : public UpdateTileStatement {
public:
    using UpdateTileStatement::UpdateTileStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom, const QByteArray& data) override {
        int z = zoom;
        if (invertedZ()) {
            z = invertZ(z);
        }
        if (auto r = stmt().bindBlob(1, data); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.x()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(3, xy.y()); !r) {
            return r;
        }
        return stmt().bindInt(4, z);
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("UPDATE tiles SET image = ? WHERE x = ? AND y = ? AND z = ?");
    }
};

// Ports TDeleteTileConnectionStatementRMaps (:543-561).
class RmapsDeleteTileStatement final : public DeleteTileStatement {
public:
    using DeleteTileStatement::DeleteTileStatement;

    core::Result<void> bindParams(geo::PointI xy, quint8 zoom) override {
        int z = zoom;
        if (invertedZ()) {
            z = invertZ(z);
        }
        if (auto r = stmt().bindInt(1, xy.x()); !r) {
            return r;
        }
        if (auto r = stmt().bindInt(2, xy.y()); !r) {
            return r;
        }
        return stmt().bindInt(3, z);
    }

protected:
    QByteArray sqlText() const override {
        return QByteArrayLiteral("DELETE FROM tiles WHERE x = ? AND y = ? AND z = ?");
    }
};

// Ports the UPDATE half of TMetadataConnectionStatementRMaps (:565-585, FText at :579-582):
// Locus's `info` table carries an extra `zooms` column.
class RmapsMetadataUpdateStatement final : public ConnectionStatement {
public:
    explicit RmapsMetadataUpdateStatement(bool isLocus)
        : ConnectionStatement(false, false), isLocus_(isLocus) {}

protected:
    QByteArray sqlText() const override {
        return isLocus_ ? QByteArrayLiteral("UPDATE info SET minzoom = ?, maxzoom = ?, zooms = ?")
                        : QByteArrayLiteral("UPDATE info SET minzoom = ?, maxzoom = ?");
    }

private:
    bool isLocus_;
};

// Ports FSelectStmt (:576, :580, :583).
class RmapsMetadataSelectStatement final : public ConnectionStatement {
public:
    explicit RmapsMetadataSelectStatement(bool isLocus)
        : ConnectionStatement(false, false), isLocus_(isLocus) {}

protected:
    QByteArray sqlText() const override {
        return isLocus_ ? QByteArrayLiteral("SELECT minzoom, maxzoom, zooms from info LIMIT 1")
                        : QByteArrayLiteral("SELECT minzoom, maxzoom from info LIMIT 1");
    }

private:
    bool isLocus_;
};

} // namespace

// --- RmapsConnection ---------------------------------------------------------------------------

RmapsConnection::RmapsConnection(ConstructKey, bool isReadOnly, QString fileName,
                                 core::ContentTypeInfo mainContentType,
                                 const core::ContentTypeRegistry& contentTypeManager,
                                 geo::ProjectionSetPtr projectionSet, SqliteFileFormat format)
    : SqliteFileConnection(isReadOnly, std::move(fileName), mainContentType, contentTypeManager,
                           std::move(projectionSet)),
      isReadOnly_(isReadOnly), mainContentType_(std::move(mainContentType)), format_(format),
      minZoom_(kEmptyZoom), maxZoom_(kEmptyZoom) {
    // Placeholder invertedZ (see the header's class doc comment): the real value depends on
    // `format_`/metadata not yet read. Their SQL text does not depend on invertedZ, so
    // initialize()'s eager prepare of these two succeeds/fails identically regardless of this
    // placeholder; finishOpen() rebuilds both with the real value.
    tileDataStmt_ = std::make_unique<RmapsTileDataStatement>(/*invertedY=*/false, /*invertedZ=*/true);
    tileInfoStmt_ = std::make_unique<RmapsTileInfoStatement>(/*invertedY=*/false, /*invertedZ=*/true);
}

core::Result<std::unique_ptr<RmapsConnection>>
RmapsConnection::open(bool isReadOnly, QString fileName, core::ContentTypeInfo mainContentType,
                      const core::ContentTypeRegistry& contentTypeManager,
                      geo::ProjectionSetPtr projectionSet, SqliteFileInfoPtr existingInfo,
                      SqliteFileFormat format) {
    auto connection = std::make_unique<RmapsConnection>(ConstructKey{}, isReadOnly, std::move(fileName),
                                                        std::move(mainContentType), contentTypeManager,
                                                        std::move(projectionSet), format);
    if (auto r = connection->initialize(std::move(existingInfo)); !r) {
        return core::Unexpected(r.error());
    }
    if (auto r = connection->finishOpen(); !r) {
        return core::Unexpected(r.error());
    }
    return connection;
}

core::Result<void> RmapsConnection::createTables() {
    // Ports CreateTables (:294-341).
    const int epsg = projectionSet()->zoom(0)->projectionType()->projectionEpsg();
    const QString json = QStringLiteral("{\"epsg\":%1,\"format\":\"%2\"}")
                             .arg(epsg)
                             .arg(QString::fromUtf8(mainContentType_.type));

    if (format_ == SqliteFileFormat::OsmAnd) {
        const QString ellipsoid = (epsg == 3395) ? QStringLiteral("1") : QStringLiteral("0");
        if (auto r = db().exec(
                QByteArrayView("CREATE TABLE info (minzoom int, maxzoom int, ellipsoid int, timecolumn text, "
                               "expireminutes text, tilenumbering text, tilesize int, sasgis text)"));
            !r) {
            return core::Unexpected(r.error());
        }
        const QString sql = QStringLiteral("INSERT INTO info (ellipsoid, timecolumn, expireminutes, "
                                           "tilenumbering, tilesize, sasgis) VALUES (%1, \"no\", 0, "
                                           "\"BigPlanet\", 256, '%2')")
                                .arg(ellipsoid, json);
        if (auto r = db().exec(sql.toUtf8()); !r) {
            return core::Unexpected(r.error());
        }
    } else if (format_ == SqliteFileFormat::Locus) {
        if (auto r = db().exec(QByteArrayView(
                "CREATE TABLE info (minzoom int, maxzoom int, center_x double, center_y double, "
                "zooms text, provider int, sasgis text)"));
            !r) {
            return core::Unexpected(r.error());
        }
        const QString sql = QStringLiteral("INSERT INTO info (sasgis) VALUES ('%1')").arg(json);
        if (auto r = db().exec(sql.toUtf8()); !r) {
            return core::Unexpected(r.error());
        }
    } else if (format_ == SqliteFileFormat::RMaps) {
        if (auto r = db().exec(QByteArrayView("CREATE TABLE info (minzoom int, maxzoom int, sasgis text)"));
            !r) {
            return core::Unexpected(r.error());
        }
        const QString sql = QStringLiteral("INSERT INTO info (sasgis) VALUES ('%1')").arg(json);
        if (auto r = db().exec(sql.toUtf8()); !r) {
            return core::Unexpected(r.error());
        }
    } else {
        // Ports the `else raise` of the CreateTables case statement (:335), reached only when a
        // fresh file is being created (the constructor's own, later, identical check --
        // finishOpen() below -- covers every open of an EXISTING file, matching legacy's two
        // raise sites).
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("RMaps: Unsupported FormatID = %1").arg(static_cast<int>(format_)));
    }

    if (auto r = db().exec(QByteArrayView(
            "CREATE TABLE tiles (x int, y int, z int, s int, image blob, PRIMARY KEY (x,y,z,s))"));
        !r) {
        return core::Unexpected(r.error());
    }
    return {};
}

// Ports FetchMetadata (:343-369).
core::Result<void> RmapsConnection::fetchMetadata() {
    auto prepared = db().prepare(QByteArrayView("SELECT * FROM info LIMIT 1"));
    if (!prepared) {
        return core::Unexpected(prepared.error()); // ports PrepareStatement's raise, uncaught
    }
    sqlite::SqliteStmt stmt = std::move(*prepared);
    auto step = stmt.step();
    if (step && *step == sqlite::SqliteStmt::StepResult::Row) {
        const int count = stmt.columnCount();
        for (int i = 0; i < count; ++i) {
            const QString name = core::delphiLowerCase(QString::fromUtf8(stmt.columnName(i)));
            const QString value = stmt.isNull(i) ? QString() : stmt.columnText(i);
            fileInfo()->addOrSetMetadataValue(name, value);
        }
    } else if (!step) {
        qCWarning(core::logStorage) << "RmapsConnection::fetchMetadata: step failed:" << step.error().what();
    }
    return {};
}

// Ports UpdateMetadata (:371-394).
core::Result<void> RmapsConnection::updateMetadata(geo::PointI /*xy*/, quint8 zoom) {
    // AXY is unused, matching legacy's own UpdateMetadata, which never reads it either.
    bool doUpdate = false;
    const int z = static_cast<int>(zoom);

    if (minZoom_ == kEmptyZoom || minZoom_ > z) {
        minZoom_ = z;
        doUpdate = true;
    }
    if (maxZoom_ == kEmptyZoom || maxZoom_ < z) {
        maxZoom_ = z;
        doUpdate = true;
    }
    if (format_ == SqliteFileFormat::Locus && updateZooms(z, zooms_)) {
        doUpdate = true;
    }

    if (!doUpdate) {
        return {};
    }
    return execMetadataUpdate();
}

// Ports TMetadataConnectionStatementRMaps.ExecUpdate (:593-726).
core::Result<void> RmapsConnection::execMetadataUpdate() {
    // Ports the unconditional `if not Self.CheckPrepared or not FSelectStmt.CheckPrepared then
    // RaiseSQLite3Error` (:624-628) -- unlike the base's seven flavour-hook statement types, this
    // private helper raises on ANY CheckPrepared failure, fresh or cached, the same convention
    // mbtiles_connection.cpp's execUpsert() documents for its own analogous private helper.
    auto preparedUpdate = metadataUpdateStmt_->ensurePrepared(db());
    if (!preparedUpdate) {
        return core::Unexpected(preparedUpdate.error());
    }
    if (!*preparedUpdate) {
        return core::fail(core::ErrorCode::IoError, db().lastError());
    }
    auto preparedSelect = metadataSelectStmt_->ensurePrepared(db());
    if (!preparedSelect) {
        return core::Unexpected(preparedSelect.error());
    }
    if (!*preparedSelect) {
        return core::fail(core::ErrorCode::IoError, db().lastError());
    }

    const bool isLocus = format_ == SqliteFileFormat::Locus;
    int dbMinZoom = kEmptyZoom;
    int dbMaxZoom = kEmptyZoom;
    QString dbZoomsStr;
    QList<int> dbZooms;

    sqlite::SqliteStmt& selectStmt = metadataSelectStmt_->stmt();
    auto step = selectStmt.step();
    if (step && *step == sqlite::SqliteStmt::StepResult::Row) {
        // Ports the nested GetZoomValue (:599-609): NULL -> CEmptyZoomValue, else ColumnInt
        // inverted when FIsInvertedZ.
        const auto zoomValue = [&](int col) {
            if (selectStmt.isNull(col)) {
                return kEmptyZoom;
            }
            const int v = selectStmt.columnInt(col);
            return invertedZ_ ? invertZ(v) : v;
        };
        dbMinZoom = zoomValue(0);
        dbMaxZoom = zoomValue(1);
        if (isLocus) {
            swapMinMaxZoom(dbMinZoom, dbMaxZoom);
            dbZoomsStr = selectStmt.columnText(2);
            auto parsed = strToZooms(dbZoomsStr, invertedZ_);
            if (!parsed) {
                std::ignore = selectStmt.reset();
                return core::Unexpected(parsed.error());
            }
            dbZooms = *parsed;
        }
    } else if (!step) {
        qCWarning(core::logStorage) << "RmapsConnection::execMetadataUpdate: select step failed:"
                                    << step.error().what();
    }
    std::ignore = selectStmt.reset();

    // Ports the "compare and sync local values with values from db" block (:652-676).
    bool doUpdate = false;
    if (dbMinZoom == kEmptyZoom || dbMinZoom > minZoom_) {
        doUpdate = true;
    } else if (dbMinZoom < minZoom_) {
        minZoom_ = dbMinZoom;
    }
    if (dbMaxZoom == kEmptyZoom || dbMaxZoom < maxZoom_) {
        doUpdate = true;
    } else if (dbMaxZoom > maxZoom_) {
        maxZoom_ = dbMaxZoom;
    }

    QString zoomsMerged;
    if (isLocus) {
        for (const int z : dbZooms) {
            updateZooms(z, zooms_);
        }
        zoomsMerged = zoomsToStr(zooms_, invertedZ_);
        if (zoomsMerged != dbZoomsStr) {
            doUpdate = true;
        }
    }

    if (!doUpdate) {
        // "ok, other process/thread already update all data in db" (:678-681).
        return {};
    }

    // Ports "do update db" (:683-690).
    const int writeMin = invertedZ_ ? invertZ(minZoom_) : minZoom_;
    const int writeMax = invertedZ_ ? invertZ(maxZoom_) : maxZoom_;

    sqlite::SqliteStmt& updateStmt = metadataUpdateStmt_->stmt();
    core::Result<void> bound;
    if (isLocus) {
        // Ports the Locus bind (:692-698): the UPDATE's param 1 (the `minzoom` column) is bound
        // from writeMax and param 2 (`maxzoom`) from writeMin -- legacy's write-side swap, mirror
        // image of swapMinMaxZoom() above.
        bound = updateStmt.bindInt(1, writeMax);
        if (bound) {
            bound = updateStmt.bindInt(2, writeMin);
        }
        if (bound) {
            bound = updateStmt.bindText(3, zoomsMerged);
        }
    } else {
        bound = updateStmt.bindInt(1, writeMin);
        if (bound) {
            bound = updateStmt.bindInt(2, writeMax);
        }
    }
    if (!bound) {
        return core::Unexpected(bound.error());
    }

    auto updateStep = updateStmt.step();
    std::ignore = updateStmt.reset();
    if (!updateStep) {
        return core::Unexpected(updateStep.error());
    }
    if (*updateStep != sqlite::SqliteStmt::StepResult::Done) {
        return core::fail(core::ErrorCode::IoError, db().lastError());
    }

    // Ports "sync local info with db state" (:717-725).
    if (isLocus) {
        fileInfo()->addOrSetMetadataValue(QStringLiteral("minzoom"), QString::number(writeMax));
        fileInfo()->addOrSetMetadataValue(QStringLiteral("maxzoom"), QString::number(writeMin));
        fileInfo()->addOrSetMetadataValue(QStringLiteral("zooms"), zoomsMerged);
    } else {
        fileInfo()->addOrSetMetadataValue(QStringLiteral("minzoom"), QString::number(writeMin));
        fileInfo()->addOrSetMetadataValue(QStringLiteral("maxzoom"), QString::number(writeMax));
    }
    return {};
}

core::Result<void> RmapsConnection::finishOpen() {
    // Ports the constructor's body (:222-285, minus statement construction, which is split
    // between the constructor and here -- see the header's class doc comment).
    if (format_ == SqliteFileFormat::OsmAnd) {
        auto tileNumbering = fileInfo()->metadataValue(QStringLiteral("tilenumbering"));
        invertedZ_ =
            tileNumbering.has_value() && core::delphiSameText(*tileNumbering, QStringLiteral("BigPlanet"));
    } else if (format_ == SqliteFileFormat::Locus || format_ == SqliteFileFormat::RMaps) {
        invertedZ_ = true;
    } else {
        // Ports the `else raise` of the constructor's own format case (:241) -- reached on every
        // open (not only a brand-new file, unlike createTables()'s identical check above),
        // matching legacy's second raise site for an existing file.
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("RMaps: Unsupported FormatID = %1").arg(static_cast<int>(format_)));
    }

    const auto parseZoom = [this](const QString& key) {
        auto v = fileInfo()->metadataValue(key);
        int n = 0;
        return v && core::delphiTryStrToInt(*v, n) ? n : kEmptyZoom;
    };
    minZoom_ = parseZoom(QStringLiteral("minzoom"));
    maxZoom_ = parseZoom(QStringLiteral("maxzoom"));

    if (invertedZ_) {
        if (minZoom_ != kEmptyZoom) {
            minZoom_ = invertZ(minZoom_);
        }
        if (maxZoom_ != kEmptyZoom) {
            maxZoom_ = invertZ(maxZoom_);
        }
    }

    if (format_ == SqliteFileFormat::Locus) {
        swapMinMaxZoom(minZoom_, maxZoom_);
        if (auto zoomsStr = fileInfo()->metadataValue(QStringLiteral("zooms"))) {
            auto parsed = strToZooms(*zoomsStr, invertedZ_);
            if (!parsed) {
                return core::Unexpected(parsed.error());
            }
            zooms_ = *parsed;
        }
    }

    // Read access (:268-272): rebuild tileData/tileInfo now that invertedZ_ is finally known (see
    // the header's class doc comment), plus rectInfo/enumTiles for the first time. Gated on
    // isEnabled() for the same reason mbtiles_connection.cpp's finishOpen() gates its own rebuild:
    // a connection whose first prepare already failed must not re-attempt it here, or a cached
    // failure would be misreported as a fresh one.
    if (isEnabled()) {
        tileDataStmt_ = std::make_unique<RmapsTileDataStatement>(false, invertedZ_);
        tileInfoStmt_ = std::make_unique<RmapsTileInfoStatement>(false, invertedZ_);
        if (auto r = tileDataStmt_->ensurePrepared(db()); !r) {
            return core::Unexpected(r.error());
        }
        if (auto r = tileInfoStmt_->ensurePrepared(db()); !r) {
            return core::Unexpected(r.error());
        }
    }
    rectInfoStmt_ = std::make_unique<RmapsRectInfoStatement>(false, invertedZ_);
    enumTilesStmt_ = std::make_unique<RmapsEnumTilesStatement>(false, invertedZ_);

    // Write access (:275-281): only when !isReadOnly_.
    if (!isReadOnly_) {
        insertStmt_ = std::make_unique<RmapsInsertTileStatement>(false, invertedZ_);
        updateStmt_ = std::make_unique<RmapsUpdateTileStatement>(false, invertedZ_);
        deleteStmt_ = std::make_unique<RmapsDeleteTileStatement>(false, invertedZ_);
        const bool isLocus = format_ == SqliteFileFormat::Locus;
        metadataUpdateStmt_ = std::make_unique<RmapsMetadataUpdateStatement>(isLocus);
        metadataSelectStmt_ = std::make_unique<RmapsMetadataSelectStatement>(isLocus);
    }

    return {};
}

} // namespace cg::storage::sqlite
