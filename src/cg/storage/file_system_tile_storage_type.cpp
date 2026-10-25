// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/file_system_tile_storage_type.h"

#include "cg/storage/file_system_tile_storage.h"

namespace cg::storage {

FileSystemTileStorageType::FileSystemTileStorageType(ITileNameCodecPtr codec,
                                                     MapVersionFactoryPtr mapVersionFactory,
                                                     core::PathConfigPtr basePath)
    : abilities_(tileStorageTypeAbilities(TileStorageKind::FolderSimple)), codec_(std::move(codec)),
      mapVersionFactory_(std::move(mapVersionFactory)), basePath_(std::move(basePath)) {}

TileStorageTypeAbilities FileSystemTileStorageType::abilities() const {
    return abilities_;
}

core::PathConfigPtr FileSystemTileStorageType::basePath() const {
    return basePath_;
}

MapVersionFactoryPtr FileSystemTileStorageType::mapVersionFactory() const {
    return mapVersionFactory_;
}

core::Result<ITileStoragePtr> FileSystemTileStorageType::build(
    const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
    const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier, const QString& path,
    std::shared_ptr<TileInfoMemCache> cacheTileInfo) {
    // u_TileStorageTypeBase.pas: Folder/InSeparateFiles storage classes read "<path>StorageConfig.ini"
    // with `path` already ending in a separator -- the caller guarantees that (tile_storage_type.h).
    const TileStorageAbilities resolvedAbilities =
        applyStorageConfigIni(abilities_.base, forceAbilities, abilities_.storageClass, path);

    ITileStoragePtr storage = std::make_shared<FileSystemTileStorage>(
        abilities_, resolvedAbilities, mapVersionFactory_, std::move(projectionSet), std::move(tileNotifier),
        path, mainContentType, codec_, std::move(cacheTileInfo));
    return storage;
}

} // namespace cg::storage
