// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/ram_tile_storage_type.h"

#include "cg/storage/ram_tile_storage.h"

namespace cg::storage {

RamTileStorageType::RamTileStorageType(MapVersionFactoryPtr mapVersionFactory, core::PathConfigPtr basePath)
    : abilities_(tileStorageTypeAbilities(TileStorageKind::Ram)),
      mapVersionFactory_(std::move(mapVersionFactory)), basePath_(std::move(basePath)) {}

TileStorageTypeAbilities RamTileStorageType::abilities() const {
    return abilities_;
}

core::PathConfigPtr RamTileStorageType::basePath() const {
    return basePath_;
}

MapVersionFactoryPtr RamTileStorageType::mapVersionFactory() const {
    return mapVersionFactory_;
}

core::Result<ITileStoragePtr>
RamTileStorageType::build(const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
                          const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier,
                          const QString& path, std::shared_ptr<TileInfoMemCache> cacheTileInfo) {
    // StorageClass::InMemory: applyStorageConfigIni() never looks for a StorageConfig.ini file
    // (already landed, InMemoryStorageClassNeverLooksForAnIniFile), so `path` does not affect the
    // narrowed abilities. `path` itself is not forwarded to RamTileStorage -- legacy's own
    // BuildStorageInternal does not pass APath into TTileStorageInRAM.Create either
    // (u_TileStorageTypeInRAM.pas:98-107); the storage's path is always "" (T-0021F3a).
    const TileStorageAbilities resolvedAbilities =
        applyStorageConfigIni(abilities_.base, forceAbilities, abilities_.storageClass, path);

    // `cacheTileInfo` is forwarded as-is; a null cache is RamTileStorage's own error
    // (RamTileStorageMemCacheRequired), not re-checked here -- ticket's own instruction ("a null
    // mem cache is F3a's error, passed through").
    ITileStoragePtr storage = std::make_shared<RamTileStorage>(
        abilities_, resolvedAbilities, mapVersionFactory_, std::move(projectionSet), std::move(tileNotifier),
        mainContentType, std::move(cacheTileInfo));
    return storage;
}

} // namespace cg::storage
