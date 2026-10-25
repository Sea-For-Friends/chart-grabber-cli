// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_TileStorageTypeSQLite.pas (:76-136, TTileStorageTypeSQLite).
//
// docs/specs/storage.md §2.12, §2.16. The `cache_sqlite` type: tileStorageTypeAbilities(
// TileStorageKind::Sqlite3Native) -- ReadWrite, versions kept (legacy's constructor is always
// called with AIsVersioned = True, u_TileStorageTypeListSimple.pas:387), mixed content types,
// StorageClass::Folder; build() follows FileSystemTileStorageType::build().

#include "cg/core/notifier.h"
#include "cg/storage/tile_storage_type.h"

namespace cg::storage::sqlite {

// FEAT-STO-018.
class SqliteRegionTileStorageType final : public ITileStorageType {
public:
    SqliteRegionTileStorageType(MapVersionFactoryPtr mapVersionFactory, core::PathConfigPtr basePath,
                                core::Notifier& gcTick);

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
    core::Notifier&
        gcTick_; // the application's GC tick (the application notifier); handed to every storage built
};

} // namespace cg::storage::sqlite
