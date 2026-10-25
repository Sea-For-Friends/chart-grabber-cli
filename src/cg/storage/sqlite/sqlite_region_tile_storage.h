// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_TileStorageSQLite.pas (:165-562, TTileStorageSQLite) and the
// per-operation dispatch of u_TileStorageSQLiteHelper.pas (:274-528: handler lookup by region file,
// delete/list/get/rect/save).
//
// docs/specs/storage.md §2.16 (intro), §3.1 rule 3 (notify outside every lock). The ITileStorage
// face of the native `cache_sqlite`: each (x, y, zoom) maps to a region file (region codec +
// ".sqlitedb"), the operation runs on that file's handler through SqliteRegionPool. Versions are
// stored (the type is always built versioned, u_TileStorageTypeListSimple.pas:387).
//
// Idle close (spec §3.1 rule 5): no timer here. As in legacy (the helper registers a TTL listener
// on the AGCNotifier the storage type was built with, u_TileStorageSQLiteHelper.pas:151-196), the
// storage subscribes one listener to the application's GC tick (the core::Notifier of the
// GarbageCollectorThread, handed in through the type's constructor); each tick calls
// SqliteRegionPool::gcTick(), whose 180 s gate runs sweepIdle() (files idle for kIdleCloseMs). The
// subscription is dropped in the destructor before the pool goes, and the listener holds only a weak
// reference to the pool, so a tick in flight cannot touch a dead pool.

#include "cg/core/content_type.h"
#include "cg/core/notifier.h"
#include "cg/core/result.h"
#include "cg/storage/sqlite/sqlite_region_handler.h"
#include "cg/storage/sqlite/sqlite_region_pool.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_name_codec.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_base.h"

#include <QString>

#include <memory>

namespace cg::storage::sqlite {

// FEAT-STO-018. Ports TTileStorageSQLite.
class SqliteRegionTileStorage final : public ITileStorage {
public:
    // Parameters as FileSystemTileStorage's; `isVersioned` ports AIsVersioned (FUseVersion of the
    // helper). `storagePath` ends with '/'. `memCache` may be null. `gcTick` is the application's GC
    // notifier (the application notifier); the storage only subscribes, so the notifier may die first.
    SqliteRegionTileStorage(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                            MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                            TilePyramidNotifierPtr tileNotifier, QString storagePath,
                            core::ContentTypeInfo mainContentType, bool isVersioned,
                            std::shared_ptr<TileInfoMemCache> memCache, core::Notifier& gcTick);
    ~SqliteRegionTileStorage() override;

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

    // Legacy flags: SkipIfSameAsPrev always, KeepExisting unless `overwrite`.
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              TileDate loadDate, QByteArrayView contentType,
                                              const QByteArray& data, bool overwrite) override;
    // The backend-specific form (spec §2.16.3): the flags are not on ITileStorage.
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              TileDate loadDate, QByteArrayView contentType,
                                              const QByteArray& data, SaveFlags flags);

    [[nodiscard]] core::Result<MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                            const MapVersionRequest& request) const override;
    [[nodiscard]] core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                             const MapVersionRequest& request,
                                                             std::stop_token stop) const override;
    [[nodiscard]] core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                          bool ignoreMultiVersionTiles) override;

private:
    [[nodiscard]] QString dbPath(geo::PointI xy, quint8 zoom) const;
    [[nodiscard]] core::Result<TileInfo> tileInfoInternal(geo::PointI xy, quint8 zoom,
                                                          const MapVersionInfoPtr& version, LoadMode mode,
                                                          bool showOtherVersions) const;

    TileStorageBase base_;
    core::ContentTypeInfo mainContentType_;
    ITileNameCodecPtr codec_; // the region codec
    std::shared_ptr<TileInfoMemCache> memCache_;
    std::shared_ptr<SqliteRegionPool> pool_;
    core::Subscription gcSub_; // last: dropped first, before the pool is destroyed
};

} // namespace cg::storage::sqlite
