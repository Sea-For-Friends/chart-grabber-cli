// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageTypeListSimple.pas (TTileStorageTypeListSimple)
//
// Builds the list of every storage type the port has, in legacy registration order (the order of
// the cache-type combo box), on top of the cache-type table of cache_type.h (GUID, caption, flags):
// nine file-system flavours, SQLite region (71), RAM (9) and the five single-file SQLite formats (72-76).
//
// Legacy also registers BerkeleyDB (6), BerkeleyDB Versioned (61) (removed, T-0333, owner
// 2026-10-21), GeoCacher (8), GoogleEarth (5), GoogleEarth Terrain (51) and DBMS (7);
// those backends do not exist in the port, so the list has no item for them (itemByCode()
// returns nullptr). The commented-out tar archive types are not registered in legacy either.
//
// `gcNotifier` is the application's GC tick (GarbageCollectorThread's notifier) and must outlive
// the list: the SQLite region type keeps a reference to it.

#include "cg/core/notifier.h"
#include "cg/storage/global_cache_config.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage_type_list.h"

#include <memory>

namespace cg::storage {

class TileStorageTypeListSimple final : public TileStorageTypeList {
public:
    // FEAT-STO-001. `simpleVersionFactory` is legacy's AMapVersionFactoryList.GetSimpleVersionFactory,
    // shared by every type. The content-type manager and archive factory of legacy's constructor are
    // not needed by the landed types.
    TileStorageTypeListSimple(const MapVersionFactoryPtr& simpleVersionFactory,
                              const GlobalCacheConfig& cacheConfig, core::Notifier& gcNotifier);
};

} // namespace cg::storage
