// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageTypeSQLiteFile.pas:73-133.
//
// One instance per SqliteFileFormat: ReadWrite for every format but OruxMaps (ReadOnly), versions
// ignored, no mixed content types, StorageClass::OneFile (so StorageConfig.ini is
// "<path>.StorageConfig.ini"). The contentTypeManager is the shared standard registry. build() is
// the six-parameter ITileStorageType::build().

#include "cg/storage/sqlite/sqlite_file_connection.h"
#include "cg/storage/tile_storage_type.h"

namespace cg::storage::sqlite {

// Ports TTileStorageTypeSQLiteFile.
class SqliteFileTileStorageType final : public ITileStorageType {
public:
    SqliteFileTileStorageType(SqliteFileFormat format, MapVersionFactoryPtr mapVersionFactory,
                              core::PathConfigPtr basePath);

    [[nodiscard]] TileStorageTypeAbilities abilities() const override;
    [[nodiscard]] core::PathConfigPtr basePath() const override;
    [[nodiscard]] MapVersionFactoryPtr mapVersionFactory() const override;

    [[nodiscard]] core::Result<ITileStoragePtr>
    build(const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
          const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier,
          const QString& path, std::shared_ptr<TileInfoMemCache> cacheTileInfo) override;

private:
    SqliteFileFormat format_;
    TileStorageTypeAbilities abilities_;
    MapVersionFactoryPtr mapVersionFactory_;
    core::PathConfigPtr basePath_;
};

} // namespace cg::storage::sqlite
