// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: c_CacheTypeCodes.pas, u_TileStorageTypeListSimple.pas (the registration table only --
// TTileStorageTypeListSimple's actual construction of storage-type objects belongs to the
// backend tickets that build them).
//
// docs/specs/storage.md §2.10.4 (:775-867). The 22-row cache-type registry: CacheTypeId maps 1:1
// onto c_CacheTypeCodes.pas's c_File_Cache_Id_* constants (kept numerically identical -- they are
// persisted in SASPlanet.ini's per-map `CacheType` key and in ZMP files). Every UUID, display
// name, `[PathToCache]` ini key and default folder is copied verbatim from
// u_TileStorageTypeListSimple.pas / c_CacheTypeCodes.pas, in legacy's registration order (the
// order of the "Map properties" cache-type combo box).

#include <QUuid>

#include <cstdint>
#include <memory>
#include <vector>

namespace cg::storage {

class ITileNameCodec;
using ITileNameCodecPtr = std::shared_ptr<const ITileNameCodec>;

// Numerically identical to c_CacheTypeCodes.pas's c_File_Cache_Id_* constants -- persisted, do
// not renumber.
enum class CacheTypeId : std::int8_t {
    Invalid = -1,
    Default = 0, // "use the map's DefCache" -- subst only, never a real storage
    Gmv = 1,
    Sas = 2,
    Es = 3,
    Gm = 4,
    GmAux = 41,
    GmBing = 42,
    Mobac = 43,
    OsmAnd = 44,
    Tms = 45,
    Ge = 5,
    GeTerrain = 51,
    Bdb = 6,
    BdbVersioned = 61,
    Dbms = 7,
    Sqlite = 71,
    SqliteMbTiles = 72,
    SqliteOsmAnd = 73,
    SqliteLocus = 74,
    SqliteRMaps = 75,
    SqliteOruxMaps = 76,
    GeoCacher = 8,
    Ram = 9,
};

// FEAT-STO-001, FEAT-STO-036.
struct CacheTypeDescriptor {
    CacheTypeId id;
    QUuid uuid;                // persisted; verbatim from u_TileStorageTypeListSimple.pas
    const char* displayName;   // never wrapped in tr() here -- only at the UI boundary
    const char* pathIdent;     // the [PathToCache] ini key; nullptr when there is none (Ram,
                               // SqliteOruxMaps)
    const char* defaultFolder; // relative to the global cache root; nullptr alongside pathIdent
    bool canBeDefault;         // TTileStorageTypeListItem's 5th ctor argument
    bool canUseAsSubst;        // its 6th argument
    bool supportedOnLinux;     // false for Ge, GeTerrain, GeoCacher, Dbms (ADR-0005 / ADR-0009)
                               // and Bdb, BdbVersioned (T-0333, owner 2026-10-21);
                               // Linux support, NOT the separate [VIEW] DefCache whitelist (see
                               // global_cache_config.h)
};

// In legacy registration order -- the order of the "Map properties" cache-type combo box.
[[nodiscard]] const std::vector<CacheTypeDescriptor>& cacheTypes();
[[nodiscard]] const CacheTypeDescriptor* cacheTypeById(CacheTypeId id); // nullptr when unknown

// Null for the 12 non-folder, non-region ids (GeoCacher, Ge, GeTerrain, Dbms, Bdb, BdbVersioned, Ram
// and the five SQLite-file formats): they have no folder-per-tile naming scheme. The 9 folder ids each get
// their own codec; Sqlite has the region codec (storage.md §2.10.2).
[[nodiscard]] ITileNameCodecPtr codecForCacheType(CacheTypeId id);

} // namespace cg::storage
