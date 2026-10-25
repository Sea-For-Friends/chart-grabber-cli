// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_region_tile_storage_type.h"

#include "cg/storage/sqlite/sqlite_region_tile_storage.h"

namespace cg::storage::sqlite {

SqliteRegionTileStorageType::SqliteRegionTileStorageType(MapVersionFactoryPtr mapVersionFactory,
                                                         core::PathConfigPtr basePath, core::Notifier& gcTick)
    : abilities_(tileStorageTypeAbilities(TileStorageKind::Sqlite3Native)),
      mapVersionFactory_(std::move(mapVersionFactory)), basePath_(std::move(basePath)), gcTick_(gcTick) {}

TileStorageTypeAbilities SqliteRegionTileStorageType::abilities() const {
    return abilities_;
}

core::PathConfigPtr SqliteRegionTileStorageType::basePath() const {
    return basePath_;
}

MapVersionFactoryPtr SqliteRegionTileStorageType::mapVersionFactory() const {
    return mapVersionFactory_;
}

core::Result<ITileStoragePtr> SqliteRegionTileStorageType::build(
    const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
    const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier, const QString& path,
    std::shared_ptr<TileInfoMemCache> cacheTileInfo) {
    const TileStorageAbilities resolvedAbilities =
        applyStorageConfigIni(abilities_.base, forceAbilities, abilities_.storageClass, path);

    ITileStoragePtr storage = std::make_shared<SqliteRegionTileStorage>(
        abilities_, resolvedAbilities, mapVersionFactory_, std::move(projectionSet), std::move(tileNotifier),
        path, mainContentType, /*isVersioned=*/true, std::move(cacheTileInfo), gcTick_);
    return storage;
}

} // namespace cg::storage::sqlite
