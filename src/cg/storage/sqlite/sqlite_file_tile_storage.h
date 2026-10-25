// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLiteFile/u_TileStorageSQLiteFile.pas:133-389,
//        u_TileStorageSQLiteFileConnectionBuilder.pas:81-178 (Create, MakeNewConnection),
//        u_EnumTileInfoBySQLiteFile.pas.
//
// docs/specs/storage.md §2.17 (intro), §2.6, §3.1. One ITileStorage over one single-file SQLite
// cache (MBTiles, OsmAnd, Locus, RMaps, OruxMaps): every method acquires a connection from the
// pool, runs one operation under the storage's read or write lock, and releases it. Versions are
// ignored. The format dispatch (MakeNewConnection) is a private builder defined in the .cpp.

#include "cg/core/content_type.h"
#include "cg/core/sync.h"
#include "cg/storage/sqlite/sqlite_file_connection.h"
#include "cg/storage/sqlite/sqlite_file_connection_pool.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_base.h"

#include <QString>

#include <memory>

namespace cg::storage::sqlite {

class SqliteFileConnectionBuilder;

// FEAT-STO-019, FEAT-STO-020, FEAT-STO-021. Ports TTileStorageSQLiteFile.
class SqliteFileTileStorage final : public ITileStorage {
public:
    // `memCache` may be null (legacy Assigned() checks). `path` is passed through prepareFileName().
    SqliteFileTileStorage(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                          MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                          TilePyramidNotifierPtr tileNotifier, core::ContentTypeInfo mainContentType,
                          const QString& path, SqliteFileFormat format,
                          std::shared_ptr<TileInfoMemCache> memCache);
    ~SqliteFileTileStorage() override;

    [[nodiscard]] TileStorageTypeAbilities typeAbilities() const override;
    [[nodiscard]] StorageStatePtr state() const override;
    [[nodiscard]] core::Changeable& stateChangeable() override;
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override;
    [[nodiscard]] TilePyramidNotifier& tileNotifier() override;

    [[nodiscard]] QString tileFileName(geo::PointI xy, quint8 zoom,
                                       const MapVersionInfoPtr& version) const override;
    [[nodiscard]] core::Result<TileInfo>
    tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version, LoadMode mode) const override;
    [[nodiscard]] core::Result<TileInfo>
    tileInfoEx(geo::PointI xy, quint8 zoom, const MapVersionRequest& request, LoadMode mode) const override;
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                const MapVersionInfoPtr& version) override;
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              TileDate loadDate, QByteArrayView contentType,
                                              const QByteArray& data, bool overwrite) override;
    [[nodiscard]] core::Result<MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                            const MapVersionRequest& request) const override;
    [[nodiscard]] core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                             const MapVersionRequest& request,
                                                             std::stop_token stop) const override;
    [[nodiscard]] core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                          bool ignoreMultiVersionTiles) override;

private:
    std::shared_ptr<TileStorageBase> base_; // shared with the builder (it flips the access flags)
    core::ContentTypeInfo mainContentType_;
    std::shared_ptr<TileInfoMemCache> memCache_;
    QString fileName_;
    std::shared_ptr<SqliteFileConnectionBuilder> builder_;
    mutable SqliteFileConnectionPool pool_;
    std::shared_ptr<core::Mutex> lock_; // ports FLock (read/write); shared with scan enumerators
};

} // namespace cg::storage::sqlite
