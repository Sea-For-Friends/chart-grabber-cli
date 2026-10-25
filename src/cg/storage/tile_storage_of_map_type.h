// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageOfMapType.pas
//
// docs/specs/storage.md §2.22.3-§2.22.6. The only ITileStorage a map type holds: it owns no tiles, holds ONE
// current backend built from the map's cache-type code and path, replaces it when the code or the path
// changes at run time, and forwards every call to whatever backend is current (or answers "nothing").
// The tile notifier, the mem-cache and the state object survive every replacement.

#include "cg/core/content_type.h"
#include "cg/core/path_config.h"
#include "cg/core/sync.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/global_cache_config.h"
#include "cg/storage/simple_tile_storage_config.h"
#include "cg/storage/storage_state_proxy.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_type_list.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace cg::storage {

// FEAT-STO-001. Ports TTileStorageOfMapType.
//
// Lock order: storageMutex_ -> (the backend type's locks; the mem-cache lock; the state proxy's lock; the
// version changeable's lock). The backend is built, and the state notification fires (setTarget), INSIDE
// the non-recursive write lock of storageMutex_, as in legacy (L-c3). A state listener must not call the
// storage (any member of it, on the same thread): that deadlocks, as in legacy. The tile full-update
// notification fires after the lock is released.
//
// Lifetime: the subscriptions capture `this`; the owner must not destroy the object while a config it
// listens to can still fire on another thread (legacy: listeners without reference counting, L-c6).
class TileStorageOfMapType final : public ITileStorage {
public:
    // `cacheTileInfo` null: the map has no mem-cache. `mainContentType` null: no backend is ever built; it
    // is owned by the content type registry, which must outlive this object. The performance counter list
    // of legacy is dropped (core.md:92-94).
    TileStorageOfMapType(std::shared_ptr<GlobalCacheConfig> globalCacheConfig,
                         geo::ProjectionSetPtr projectionSet, ITileStorageTypeListPtr tileStorageTypeList,
                         std::shared_ptr<IMapVersionFactoryChangeableInternal> versionFactory,
                         std::shared_ptr<SimpleTileStorageConfig> config,
                         std::shared_ptr<TileInfoMemCache> cacheTileInfo,
                         const core::ContentTypeInfo* mainContentType);
    ~TileStorageOfMapType() override;

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
    void setRecordSkippedSaves(bool on) override;
    [[nodiscard]] std::map<std::tuple<quint8, int, int>, SkippedSave>
    takeSkippedSaves(const MapVersionInfoPtr& version) override;
    [[nodiscard]] core::Result<MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                            const MapVersionRequest& request) const override;
    [[nodiscard]] core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                             const MapVersionRequest& request,
                                                             std::stop_token stop) const override;
    [[nodiscard]] core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                          bool ignoreMultiVersionTiles) override;

    // D-165 (T-0275, L-44): the text of the last failed build (path, cache type, cause) while the current
    // backend is empty because of it; nullopt after a good build and when no build was attempted.
    [[nodiscard]] std::optional<QString> buildError() const;
    // The cache folder of the current backend (ends with '/'); empty before the first build.
    [[nodiscard]] QString path() const;

private:
    void onConfigChange();
    void onGlobalConfigChange();
    void onPathConfigChange();
    void storageLock();
    void storageUnlock();
    void doUpdateStorage();
    void updateActualPath();
    [[nodiscard]] ITileStoragePtr getStorage() const;
    // storageMutex_ held for writing.
    void buildStorage(const SimpleTileStorageConfigStaticPtr& config, CacheTypeId typeCode,
                      const QString& path);

    std::shared_ptr<GlobalCacheConfig> globalCacheConfig_;
    geo::ProjectionSetPtr projectionSet_;
    ITileStorageTypeListPtr tileStorageTypeList_;
    std::shared_ptr<IMapVersionFactoryChangeableInternal> versionFactory_;
    std::shared_ptr<SimpleTileStorageConfig> config_;
    const core::ContentTypeInfo* mainContentType_;
    std::shared_ptr<TileInfoMemCache> cacheTileInfo_;
    core::PathConfigPtr actualPath_;
    StorageStateProxy storageState_; // FStorageState + FStorageStateProxy: one object
    TilePyramidNotifierPtr tileNotifier_;
    std::atomic<int> storageLockCount_{0};
    std::atomic<bool> storageNeedUpdate_{false};
    CacheTypeId currentTypeCode_ = CacheTypeId::Default; // a new Delphi object is zeroed
    QString currentPath_;
    std::atomic<bool> recordSkipped_{false}; // T-0352: switched on by the run of a snapshot source
    std::mutex skippedMutex_;                // T-0351: guards skipped_
    std::vector<std::tuple<quint8, int, int, MapVersionInfoPtr, SkippedSave>> skipped_;
    ITileStoragePtr storage_;               // guarded by storageMutex_
    std::optional<QString> lastBuildError_; // guarded by storageMutex_
    mutable core::Mutex storageMutex_;
    core::Subscription configChanged_;
    core::Subscription globalConfigChanged_;
    core::Subscription pathChanged_;
};

} // namespace cg::storage
