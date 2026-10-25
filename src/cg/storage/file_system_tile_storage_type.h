// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageTypeFileSystemSimple.pas
//
// docs/specs/storage.md §2.13 (intro). One instance per folder-cache codec (T-0021B/C's nine
// ITileNameCodec implementations): fixed abilities ReadWrite / VersionSupport::Ignored /
// supportsMixedContentTypes == false / StorageClass::InSeparateFiles, exactly
// TTileStorageTypeFileSystemSimple.Create's TTileStorageTypeAbilities.Create(...) call
// (u_TileStorageTypeFileSystemSimple.pas:73-91) -- reused here via the already-landed
// tileStorageTypeAbilities(TileStorageKind::FolderSimple) (tile_storage_abilities.cpp), which
// returns the identical row.
//
// build() ports BuildStorageInternal (:93-122): applies applyStorageConfigIni() (already landed,
// tile_storage_type.cpp) to narrow the abilities, then constructs a FileSystemTileStorage with the
// codec, the caller's projection set and tile notifier, and the caller-supplied mem cache.
// `mainContentType` is already resolved by the caller (tile_storage_type.h); `path` is used
// verbatim, the caller guarantees a trailing '/' (T-0021F3b rework -- see tile_storage_type.h).

#include "cg/storage/tile_name_codec.h"
#include "cg/storage/tile_storage_type.h"

namespace cg::storage {

// FEAT-STO-001. Ports TTileStorageTypeFileSystemSimple.
class FileSystemTileStorageType final : public ITileStorageType {
public:
    // `codec` ports FNameGenerator + FTileNameParser (merged, tile_name_codec.h); `basePath` is
    // the collapsed ITileStorageTypeConfig (its only member, storage.md §2.12 header comment).
    FileSystemTileStorageType(ITileNameCodecPtr codec, MapVersionFactoryPtr mapVersionFactory,
                              core::PathConfigPtr basePath);

    [[nodiscard]] TileStorageTypeAbilities abilities() const override;
    [[nodiscard]] core::PathConfigPtr basePath() const override;
    [[nodiscard]] MapVersionFactoryPtr mapVersionFactory() const override;

    [[nodiscard]] core::Result<ITileStoragePtr>
    build(const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
          const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier,
          const QString& path, std::shared_ptr<TileInfoMemCache> cacheTileInfo) override;

private:
    TileStorageTypeAbilities abilities_;
    ITileNameCodecPtr codec_;
    MapVersionFactoryPtr mapVersionFactory_;
    core::PathConfigPtr basePath_;
};

} // namespace cg::storage
