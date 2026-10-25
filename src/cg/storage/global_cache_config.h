// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GlobalCacheConfig.pas, u_GlobalCacheConfig.pas
//
// docs/specs/storage.md §2.10.4's "Path composition" (:846-867). The `[PathToCache]` per-cache-
// type roots plus `[VIEW] DefCache`. Each PathConfig is chained off the caller-supplied global
// cache root (ports `ACacheGlobalPath`, itself `TPathConfig.Create('PrimaryPath', './',
// <data dir>)` -- built one level up, u_GlobalState.pas:482, out of this ticket's scope).

#include "cg/core/config_element_complex.h"
#include "cg/core/path_config.h"
#include "cg/storage/cache_type.h"

#include <QtGlobal>

#include <map>

namespace cg::storage {

// FEAT-STO-036.
class GlobalCacheConfig final : public core::ConfigElementComplex {
public:
    explicit GlobalCacheConfig(core::PathConfigPtr cacheGlobalPath);
    ~GlobalCacheConfig() override;

    // The per-cache-type root, chained under the global cache root; nullptr for Ram and
    // SqliteOruxMaps (no [PathToCache] entry). Ports the 16 GetXxxPath accessors of
    // i_GlobalCacheConfig.pas, collapsed via CacheTypeDescriptor::pathIdent: types that share one
    // ini key (GMTiles: Gm/GmAux/GmBing/OsmAnd; GECache: Ge/GeTerrain) share one PathConfig
    // instance here too, exactly as legacy's single FGMTilesPath/FGECachePath field does --
    // setting one moves all its cache types together.
    [[nodiscard]] core::PathConfigPtr pathFor(CacheTypeId id) const;

    [[nodiscard]] CacheTypeId defCache() const;
    // Ports SetDefCache's whitelist (u_GlobalCacheConfig.pas:255-286): silently ignored (no
    // change, no notification) when `id` is not one of the 16 listed ids: Gmv, Sas, Es, Gm,
    // GmAux, Mobac, Tms, Dbms, Sqlite, Ram, Bdb, BdbVersioned, SqliteMbTiles, SqliteOsmAnd,
    // SqliteLocus, SqliteRMaps.
    //
    // NOTE the whitelist is NOT "every id except Ge/GeTerrain/GeoCacher/SqliteOruxMaps" --
    // docs/specs/storage.md :864-865 undercounts it: GmBing and OsmAnd (ids 42, 44) are ALSO
    // rejected by legacy even though they are ordinary, Linux-supported folder types. Legacy wins
    // (protocol rule 5); see docs/handoffs/T-0021D.md.
    void setDefCache(CacheTypeId id);

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;

private:
    void setDefCacheLocked(CacheTypeId id); // the "...Locked()" split (core.md 2.16): doReadConfig
                                            // calls this while already inside the write lock.

    core::PathConfigPtr cacheGlobalPath_;
    std::map<CacheTypeId, core::PathConfigPtr> pathByType_;
    CacheTypeId defCache_ = CacheTypeId::Sqlite; // c_File_Cache_Id_SQLite = 71
};

} // namespace cg::storage
