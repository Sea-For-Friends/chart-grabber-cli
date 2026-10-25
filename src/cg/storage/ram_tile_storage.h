// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageInRAM.pas (Create, GetTileFileName, GetTileInfo, GetTileInfoEx,
// GetTileRectInfo, DeleteTile, SaveTile, ScanTiles). `u_TileStorageTypeInRAM.pas` is T-0021F3b
// (the storage TYPE that builds one of these), out of scope here.
//
// docs/specs/storage.md §2.14 (:1092-1100). A `TileInfoMemCache` with an `ITileStorage` face and
// no persistence: `storagePath` is always "", one content type, versions ARE honoured
// (`VersionSupport::Stored`, T-0021F-storage) because the mem-cache keys on the version's store
// string. There is no separate `fsMutex_`-style lock here -- `TileInfoMemCache` already guards
// itself (tile_info_mem_cache.h), and legacy's `TTileStorageInRAM` takes no lock of its own either.

#include "cg/core/content_type.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_info_mem_cache.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_abilities.h"
#include "cg/storage/tile_storage_base.h"

#include <QString>

#include <memory>
#include <stdexcept>

namespace cg::storage {

// Ports the `raise ETileStorageInRAM.Create(rsMemCacheNotAssigned)` check
// (u_TileStorageInRAM.pas:138-140) -- a genuine, user-facing runtime exception in legacy (not a
// Delphi `Assert`), so the port throws too rather than `Q_ASSERT`: coding standard "Errors"
// reserves exceptions for "programmer errors and unrecoverable I/O setup", and a null mem-cache
// reaching this constructor is exactly that (a config error, not a data-dependent failure a
// `Result` would carry).
class RamTileStorageMemCacheRequired : public std::invalid_argument {
public:
    RamTileStorageMemCacheRequired()
        : std::invalid_argument("Can't initialize in-memory tile storage. Check your config!") {}
};

// FEAT-STO-026. Ports TTileStorageInRAM.
class RamTileStorage final : public ITileStorage {
public:
    // The first five parameters forward straight into TileStorageBase (with an always-empty
    // storagePath -- legacy `Create` passes `''` to `inherited Create`, u_TileStorageInRAM.pas:148),
    // matching FileSystemTileStorage's constructor convention. `mainContentType` is what
    // `saveTile`'s content-type compatibility check (content_type_compat.h) validates an incoming
    // save against, ports `FMainContentType`. `memCache` must be non-null: throws
    // RamTileStorageMemCacheRequired otherwise (see above); this class IS its mem cache, not a
    // cache in front of one, so it is never optional the way `FileSystemTileStorage::memCache_` is.
    RamTileStorage(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                   MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                   TilePyramidNotifierPtr tileNotifier, core::ContentTypeInfo mainContentType,
                   std::shared_ptr<TileInfoMemCache> memCache);

    // Ports `TTileStorageInRAM.Destroy` (u_TileStorageInRAM.pas:158-162):
    // `FTileInfoMemCache.OnTileInfoUpdate := nil`. The mem cache is caller-owned and outlives this
    // storage (it is built by the map type and may be rebuilt into a new RamTileStorage later), so
    // the callback installed in the constructor (which captures `this`) MUST be cleared here or a
    // later add()/TTL-evict on that cache calls into a destroyed object.
    ~RamTileStorage() override;

    [[nodiscard]] TileStorageTypeAbilities typeAbilities() const override;
    [[nodiscard]] StorageStatePtr state() const override;
    [[nodiscard]] core::Changeable& stateChangeable() override;
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override;
    [[nodiscard]] TilePyramidNotifier& tileNotifier() override;

    // Ports GetTileFileName: ignores every parameter, always ":: RAM" (u_TileStorageInRAM.pas:
    // 164-171, `':: ' + rsRAM`) -- a UI display string, nothing is ever opened through it.
    [[nodiscard]] QString tileFileName(geo::PointI xy, quint8 zoom,
                                       const MapVersionInfoPtr& version) const override;

    // Ports GetTileInfo (u_TileStorageInRAM.pas:173-184): a bare mem-cache lookup with NO access
    // check at all -- unlike FileSystemTileStorage, legacy's RAM backend never consults
    // StorageStateInternal.ReadAccess here. `updateTtl` is `false` (legacy passes the mem-cache's
    // `Get` a literal `False`, unlike FileSystemTileStorage's front-cache use of `Get`, which
    // passes `true`): this class IS the storage, so a read must not keep an entry alive past its
    // configured TTL the way a "cache in front of a backend" read is meant to.
    [[nodiscard]] core::Result<TileInfo>
    tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version, LoadMode mode) const override;
    // Ports GetTileInfoEx (u_TileStorageInRAM.pas:186-203): extracts `request.baseVersion` (legacy:
    // `AVersionInfo.BaseVersion`) and forwards to tileInfo -- versions ARE honoured here, unlike
    // FileSystemTileStorage's tileInfoEx, which always forwards `nullptr`.
    [[nodiscard]] core::Result<TileInfo>
    tileInfoEx(geo::PointI xy, quint8 zoom, const MapVersionRequest& request, LoadMode mode) const override;

    // Ports DeleteTile (u_TileStorageInRAM.pas:318-333): gated on `state()->deleteAccess`.
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                const MapVersionInfoPtr& version) override;

    // Ports SaveTile (u_TileStorageInRAM.pas:278-316): gated on `state()->addAccess`; the
    // content-type compatibility check (content_type_compat.h) only runs when BOTH `contentType`
    // and `data` are non-empty/non-null (legacy: `Assigned(AContentType) and Assigned(AData)`) --
    // narrower than FileSystemTileStorage's own gate, which checks whenever `contentType` alone is
    // non-empty; each backend ports its own legacy order (protocol rule 5).
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version,
                                              TileDate loadDate, QByteArrayView contentType,
                                              const QByteArray& data, bool overwrite) override;

    // FEAT-STO-032 default (u_TileStorageAbstract.pas): TTileStorageInRAM never overrides
    // GetListOfTileVersions, so this is always the empty list, same as FileSystemTileStorage.
    [[nodiscard]] core::Result<MapVersionList> listVersions(geo::PointI xy, quint8 zoom,
                                                            const MapVersionRequest& request) const override;

    // Ports GetTileRectInfo (u_TileStorageInRAM.pas:205-276): gated on `state()->readAccess`, the
    // 2048 cap and a degenerate rect after clamping both give a
    // successful null result (legacy `Result := nil`; spec §2.6 note 6), and `stop_requested()` maps to
    // `fail(Cancelled, ...)` (the project- wide `std::stop_token` idiom F2 already uses -- legacy's own
    // `Result := nil; Exit` on cancellation is not reproduced literally, since that would make "cancelled"
    // and "access denied" indistinguishable).
    [[nodiscard]] core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                             const MapVersionRequest& request,
                                                             std::stop_token stop) const override;

    // Ports ScanTiles (u_TileStorageInRAM.pas:335-340): `Result := FTileInfoMemCache.GetEnum(...)`,
    // unconditional, no access gate at all (`TTileStorageAbstract.ScanTiles`'s own base is not
    // called either). The RAM type's abilities fix `allowScan = false`
    // (`u_TileStorageTypeInRAM.pas:75`), so "no scan" is what the RAM storage TYPE advertises to
    // callers that consult abilities -- it is not something `scanTiles` itself enforces.
    [[nodiscard]] core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                          bool ignoreMultiVersionTiles) override;

private:
    TileStorageBase base_;
    core::ContentTypeInfo mainContentType_;
    std::shared_ptr<TileInfoMemCache> memCache_; // never null after construction
};

} // namespace cg::storage
