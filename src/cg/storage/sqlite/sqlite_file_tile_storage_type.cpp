// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_file_tile_storage_type.h"

#include "cg/storage/sqlite/sqlite_file_tile_storage.h"

namespace cg::storage::sqlite {

SqliteFileTileStorageType::SqliteFileTileStorageType(SqliteFileFormat format,
                                                     MapVersionFactoryPtr mapVersionFactory,
                                                     core::PathConfigPtr basePath)
    : format_(format),
      abilities_(tileStorageTypeAbilities(
          format == SqliteFileFormat::OruxMaps ? TileStorageKind::OruxMaps : TileStorageKind::SqliteFile)),
      mapVersionFactory_(std::move(mapVersionFactory)), basePath_(std::move(basePath)) {}

TileStorageTypeAbilities SqliteFileTileStorageType::abilities() const {
    return abilities_;
}

core::PathConfigPtr SqliteFileTileStorageType::basePath() const {
    return basePath_;
}

MapVersionFactoryPtr SqliteFileTileStorageType::mapVersionFactory() const {
    return mapVersionFactory_;
}

core::Result<ITileStoragePtr> SqliteFileTileStorageType::build(
    const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
    const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier, const QString& path,
    std::shared_ptr<TileInfoMemCache> cacheTileInfo) {
    const TileStorageAbilities resolvedAbilities =
        applyStorageConfigIni(abilities_.base, forceAbilities, abilities_.storageClass, path);

    ITileStoragePtr storage = std::make_shared<SqliteFileTileStorage>(
        abilities_, resolvedAbilities, mapVersionFactory_, std::move(projectionSet), std::move(tileNotifier),
        mainContentType, path, format_, std::move(cacheTileInfo));
    return storage;
}

} // namespace cg::storage::sqlite
