// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageFileSystem.pas (Create, GetTileFileName, GetTileInfo, GetTileInfoEx,
// DeleteTile, SaveTile, GetTileInfoByPath), the load half of u_FileSystemTools.pas
// (UpdateTileInfoByFile / CreateDirIfNotExists -- the date half is core/platform/file_times.h).
//
// docs/specs/storage.md §2.13 (:952-1046). One instance per folder cache: one content type
// (`supportsMixedContentTypes == false`, FEAT-STO-012), one tile-name codec (any of the nine
// folder layouts landed by T-0021B/C), `storagePath` ending with '/'. Versions are ignored
// (`TileStorageKind::FolderSimple` -> `VersionSupport::Ignored`): every method accepts a version
// argument only because ITileStorage requires one and forwards `nullptr` to the mem-cache/codec.
//
// §2.13.4 (`scanTiles`/`tileRectInfo`, FEAT-STO-035) is T-0021F2; `file_system_tile_storage_type.h`
// and the RAM backend are T-0021F3. Both out-of-scope ITileStorage methods below return
// `ErrorCode::Unsupported` -- never a stub that pretends to work.

#include "cg/core/content_type.h"
#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_name_codec.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_abilities.h"
#include "cg/storage/tile_storage_base.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace cg::storage {

// FEAT-STO-002..013. Ports TTileStorageFileSystem.
class FileSystemTileStorage final : public ITileStorage {
public:
    // The first five parameters forward straight into TileStorageBase, in the same order as its
    // own constructor -- storage.md §2.13's pseudocode elides `mapVersionFactory`/`tileNotifier`
    // (abbreviated before T-0021D's TileStorageBase landed with them as required constructor
    // arguments); ticket T-0021F1 restores them so this type actually builds. `resolvedAbilities`
    // is the already-narrowed ceiling (StorageConfig.ini + caller `forced`, ports
    // u_TileStorageTypeBase.pas:BuildStorage), matching TileStorageBase's own contract, NOT the
    // raw forced input.
    // `mainContentType` carries type + defaultExt (with the dot) + kind together, so the two can
    // never disagree the way separate MIME + extension parameters could (T-0021F1 review finding
    // 1). `tileFileExt_` is derived from `mainContentType.defaultExt`, matching legacy's `FFileExt
    // := FMainContentType.GetDefaultExt` (u_TileStorageFileSystem.pas:180).
    FileSystemTileStorage(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                          MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                          TilePyramidNotifierPtr tileNotifier, QString storagePath,
                          core::ContentTypeInfo mainContentType, ITileNameCodecPtr codec,
                          std::shared_ptr<TileInfoMemCache> memCache);

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

    // FEAT-STO-032 default (u_TileStorageAbstract.pas): FolderSimple ignores versions, so this
    // always returns the empty list.
    [[nodiscard]] core::Result<MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                            const MapVersionRequest& request) const override;

    // T-0021F2.
    [[nodiscard]] core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                             const MapVersionRequest& request,
                                                             std::stop_token stop) const override;

    // T-0021F2.
    [[nodiscard]] core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                          bool ignoreMultiVersionTiles) override;

private:
    // storage.md §2.13.1: tile = storagePath + codec->addExt(basePath, tileFileExt); tne =
    // storagePath + codec->addExt(basePath, ".tne"). `basePath` is computed once and shared, since
    // a codec's addExt() can be format-specific (OsmandTileNameCodec's ".jpg.tile" vs ".tne").
    [[nodiscard]] QString basePath(geo::PointI xy, quint8 zoom) const;
    [[nodiscard]] QString tilePathFor(const QString& base) const;
    [[nodiscard]] QString tnePathFor(const QString& base) const;

    // Ports TFileSystemTools.UpdateTileInfoByFile, called twice (tile then tne) -- ports
    // GetTileInfoByPath's order of operations. Assumes the caller already holds fsMutex_ (core.md
    // 2.16 "public / ...Locked()" split): called under a ReadLock from tileInfo(), and directly
    // (no separate lock) from inside saveTile()'s own WriteLock, because legacy's FFsLock is a
    // recursive critical section that lets SaveTile call GetTileInfo reentrantly while holding the
    // write lock -- fsMutex_ is deliberately non-recursive (tech-lead-rulings.md), so the
    // reentrant call is replaced by calling this helper directly instead of re-entering the public
    // API.
    [[nodiscard]] core::Result<TileInfo> probeLocked(const QString& tilePath, const QString& tnePath,
                                                     bool withData) const;

    TileStorageBase base_;
    core::ContentTypeInfo mainContentType_;
    QString tileFileExt_; // cached QString form of mainContentType_.defaultExt, for codec_->addExt()
    ITileNameCodecPtr codec_;
    std::shared_ptr<TileInfoMemCache> memCache_; // may be null (legacy Assigned() checks)
    mutable core::Mutex fsMutex_;                // see spec §3.2
};

} // namespace cg::storage
