// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_storage_type_list_simple.h"

#include "cg/storage/cache_type.h"
#include "cg/storage/file_system_tile_storage_type.h"
#include "cg/storage/ram_tile_storage_type.h"
#include "cg/storage/sqlite/sqlite_file_tile_storage_type.h"
#include "cg/storage/sqlite/sqlite_region_tile_storage_type.h"

namespace cg::storage {

namespace {

// Legacy's TTileStorageTypeListItem.Create(guid, code, caption, type, canBeDefault, isChangeable);
// the guid, caption and the two flags are the cache-type table's.
ITileStorageTypeListItemPtr makeItem(CacheTypeId id, ITileStorageTypePtr type) {
    const CacheTypeDescriptor* d = cacheTypeById(id);
    return std::make_shared<TileStorageTypeListItem>(d->uuid, static_cast<int>(id),
                                                     QString::fromLatin1(d->displayName), std::move(type),
                                                     d->canBeDefault, d->canUseAsSubst);
}

std::vector<ITileStorageTypeListItemPtr> makeItems(const MapVersionFactoryPtr& versions,
                                                   const GlobalCacheConfig& config, core::Notifier& gc) {
    std::vector<ITileStorageTypeListItemPtr> list;
    // AddFileSystemTileStorageType x 9: SAS.Planet, GoogleMV, EarthSlicer, GlobalMapper x3, OsmAnd+, MOBAC,
    // TMS.
    for (const CacheTypeId id :
         {CacheTypeId::Sas, CacheTypeId::Gmv, CacheTypeId::Es, CacheTypeId::Gm, CacheTypeId::GmAux,
          CacheTypeId::GmBing, CacheTypeId::OsmAnd, CacheTypeId::Mobac, CacheTypeId::Tms}) {
        list.push_back(makeItem(id, std::make_shared<FileSystemTileStorageType>(
                                        codecForCacheType(id), versions, config.pathFor(id))));
    }
    // BerkeleyDB (6) and BerkeleyDB Versioned (61) go here in legacy: removed, T-0333 (owner 2026-10-21).
    list.push_back(makeItem(CacheTypeId::Sqlite, std::make_shared<sqlite::SqliteRegionTileStorageType>(
                                                     versions, config.pathFor(CacheTypeId::Sqlite), gc)));
    // GeoCacher, GoogleEarth, GoogleEarth Terrain and DBMS go here in legacy: not ported yet.
    list.push_back(makeItem(CacheTypeId::Ram, std::make_shared<RamTileStorageType>(versions, nullptr)));
    // AddSQLiteFileTileStorageType x 5 (OruxMaps has no path and is not offered as a default).
    const std::pair<CacheTypeId, sqlite::SqliteFileFormat> files[] = {
        {CacheTypeId::SqliteMbTiles, sqlite::SqliteFileFormat::MbTiles},
        {CacheTypeId::SqliteOsmAnd, sqlite::SqliteFileFormat::OsmAnd},
        {CacheTypeId::SqliteLocus, sqlite::SqliteFileFormat::Locus},
        {CacheTypeId::SqliteRMaps, sqlite::SqliteFileFormat::RMaps},
        {CacheTypeId::SqliteOruxMaps, sqlite::SqliteFileFormat::OruxMaps},
    };
    for (const auto& [id, format] : files) {
        list.push_back(makeItem(
            id, std::make_shared<sqlite::SqliteFileTileStorageType>(format, versions, config.pathFor(id))));
    }
    return list;
}

} // namespace

TileStorageTypeListSimple::TileStorageTypeListSimple(const MapVersionFactoryPtr& simpleVersionFactory,
                                                     const GlobalCacheConfig& cacheConfig,
                                                     core::Notifier& gcNotifier)
    : TileStorageTypeList(makeItems(simpleVersionFactory, cacheConfig, gcNotifier)) {}

} // namespace cg::storage
