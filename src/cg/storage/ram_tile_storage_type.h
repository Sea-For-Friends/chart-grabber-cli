// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageTypeInRAM.pas
//
// docs/specs/storage.md §2.14 (intro). One instance for the RAM backend: fixed abilities
// `CTileStorageReadWrite - [tsatScan]` (u_TileStorageTypeInRAM.pas:75) -- advertised exactly as
// legacy advertises it, even though `RamTileStorage::scanTiles` itself always works (legacy
// defect L-31, T-0021F3A review: `ScanTiles` has no access check of its own; the type's abilities
// are the only place "no scan" is enforced) -- plus VersionSupport::Stored,
// supportsMixedContentTypes == true, StorageClass::InMemory (so applyStorageConfigIni() never
// looks for a StorageConfig.ini, already landed behaviour). Reused here via the already-landed
// tileStorageTypeAbilities(TileStorageKind::Ram), which returns the identical row.
//
// build() ports BuildStorageInternal (:88-108): narrows abilities and constructs a RamTileStorage
// around the caller-supplied mem cache -- legacy forwards `ACacheTileInfo` straight through
// without creating or owning one itself (the cache is supplied by whatever builds the type, same
// convention as the filesystem type's memCache parameter); a null mem cache is RamTileStorage's
// own error (RamTileStorageMemCacheRequired), not checked here. `mainContentType` is already
// resolved by the caller (tile_storage_type.h).

#include "cg/storage/tile_storage_type.h"

namespace cg::storage {

// FEAT-STO-001, FEAT-STO-026. Ports TTileStorageTypeInRAM.
class RamTileStorageType final : public ITileStorageType {
public:
    RamTileStorageType(MapVersionFactoryPtr mapVersionFactory, core::PathConfigPtr basePath);

    [[nodiscard]] TileStorageTypeAbilities abilities() const override;
    [[nodiscard]] core::PathConfigPtr basePath() const override;
    [[nodiscard]] MapVersionFactoryPtr mapVersionFactory() const override;

    [[nodiscard]] core::Result<ITileStoragePtr>
    build(const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
          const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier,
          const QString& path, std::shared_ptr<TileInfoMemCache> cacheTileInfo) override;

private:
    TileStorageTypeAbilities abilities_;
    MapVersionFactoryPtr mapVersionFactory_;
    core::PathConfigPtr basePath_;
};

} // namespace cg::storage
