// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/cache_type.h"

#include "cg/storage/codecs/es_tile_name_codec.h"
#include "cg/storage/codecs/gm1_tile_name_codec.h"
#include "cg/storage/codecs/gm2_tile_name_codec.h"
#include "cg/storage/codecs/gm3_tile_name_codec.h"
#include "cg/storage/codecs/gmv_tile_name_codec.h"
#include "cg/storage/codecs/mobac_tile_name_codec.h"
#include "cg/storage/codecs/osmand_tile_name_codec.h"
#include "cg/storage/codecs/region_tile_name_codec.h"
#include "cg/storage/codecs/sas_tile_name_codec.h"
#include "cg/storage/codecs/tms_tile_name_codec.h"

#include <QString>

namespace cg::storage {

namespace {

QUuid uuidOf(const char* braced) {
    return QUuid{QString::fromLatin1(braced)};
}

} // namespace

const std::vector<CacheTypeDescriptor>& cacheTypes() {
    // Table verbatim from u_TileStorageTypeListSimple.pas:104-133 (UUIDs), c_CacheTypeCodes.pas
    // (numeric ids, default folders) and the resourcestring block (display names); registration
    // order matches TTileStorageTypeListSimple.Create.
    static const std::vector<CacheTypeDescriptor> table = {
        {CacheTypeId::Sas, uuidOf("{BE87ACAB-7031-4F57-9C1D-FA62C709818F}"), "SAS.Planet", "SASC", "cache",
         true, true, true},
        {CacheTypeId::Gmv, uuidOf("{CB20D66C-FC79-4D1C-93A9-1C41A8D6B002}"), "GoogleMV", "GMVC", "cache_old",
         true, true, true},
        {CacheTypeId::Es, uuidOf("{F6056405-C25C-4573-AFAC-BC4F8DF52283}"), "EarthSlicer 1.95", "ESC",
         "cache_es", true, true, true},
        {CacheTypeId::Gm, uuidOf("{E6F98BC5-8684-42C9-92DE-3D994DA8C925}"), "GlobalMapper Tiles", "GMTiles",
         "cache_gmt", true, true, true},
        {CacheTypeId::GmAux, uuidOf("{4EF99AD6-D05E-4175-805C-DBBE08AC43B3}"), "GlobalMapper Aux", "GMTiles",
         "cache_gmt", true, true, true},
        {CacheTypeId::GmBing, uuidOf("{A65E31AC-7561-47FA-87B6-CE7F5603D5D1}"), "GlobalMapper Bing",
         "GMTiles", "cache_gmt", true, true, true},
        {CacheTypeId::OsmAnd, uuidOf("{A12465C1-2DB9-4309-8B48-A01E3BBDDE44}"), "OsmAnd+ Tiles", "GMTiles",
         "cache_gmt", true, true, true},
        {CacheTypeId::Mobac, uuidOf("{033B64B5-008B-4BAF-9EA9-B8176EA35433}"), "Mobile Atlas Creator (MOBAC)",
         "MOBACTiles", "cache_ma", true, true, true},
        {CacheTypeId::Tms, uuidOf("{55510DF4-7FAD-4476-93AD-454F8689A109}"), "Tile Map Service (TMS)",
         "TMSTiles", "cache_tms", true, true, true},
        // Rows 10/11 share one UUID on purpose: u_TileStorageTypeListSimple.pas passes
        // CTileStorageTypeBerkeleyDB to BOTH list items. CTileStorageTypeBerkeleyDBVersioned
        // ({CA3868AE-6762-4D17-B72F-6892E61E119B}) is declared in legacy and never used -- do not
        // "fix" this by giving BdbVersioned its own UUID; look storage types up by CacheTypeId.
        // T-0333 (owner 2026-10-21): the BerkeleyDB backend is removed; the rows stay because the
        // numbers 6 and 61 are persisted in configs and ZMPs. supportedOnLinux = false.
        {CacheTypeId::Bdb, uuidOf("{3DBF81CD-9356-40EB-9778-DE4D98E5BE61}"), "BerkeleyDB", "BDBCache",
         "cache_db", true, true, false},
        {CacheTypeId::BdbVersioned, uuidOf("{3DBF81CD-9356-40EB-9778-DE4D98E5BE61}"),
         "BerkeleyDB (Versioned)", "BDBVerCache", "cache_dbv", true, true, false},
        {CacheTypeId::Sqlite, uuidOf("{5E8ABF86-92B4-48FA-8F71-E94E22DD7831}"), "SQLite3", "SQLiteCache",
         "cache_sqlite", true, true, true},
        {CacheTypeId::GeoCacher, uuidOf("{F3163512-A190-426B-9D18-881AAD9DE61C}"), "GeoCacher", "GCCache",
         "cache_gc", false, false, false},
        {CacheTypeId::Ge, uuidOf("{71C83BAA-EEA0-45E1-833E-8CCC3A8D1A1A}"), "GoogleEarth", "GECache",
         "cache_ge", false, false, false},
        {CacheTypeId::GeTerrain, uuidOf("{C38B1837-A0E1-4139-89A3-3AB37C7ED702}"), "GoogleEarth Terrain",
         "GECache", "cache_ge", false, false, false},
        // Default folder is 'SASGIS_DBMS\$' verbatim from c_CacheTypeCodes.pas (a literal
        // backslash, not the forward slash docs/specs/storage.md :798 shows) -- legacy wins
        // (protocol rule 5); see docs/handoffs/T-0021D.md. The DBMS backend itself is out of
        // scope (ADR-0005 §7); this row exists only so the registry stays complete/verbatim.
        {CacheTypeId::Dbms, uuidOf("{5F9E2D54-A433-4853-B7EB-3EE218160263}"), "DBMS", "DBMSCache",
         "SASGIS_DBMS\\$", true, false, false},
        {CacheTypeId::Ram, uuidOf("{717034B7-B49E-4C89-BC75-002D0523E548}"), "RAM", nullptr, nullptr, true,
         true, true},
        {CacheTypeId::SqliteMbTiles, uuidOf("{8C217E83-8AA8-41A4-A01A-AB80D130C748}"), "MBTiles (SQLite3)",
         "MBTilesCache", "cache_mbtiles", true, false, true},
        {CacheTypeId::SqliteOsmAnd, uuidOf("{110AE260-747D-423B-90C6-D83F1CE40D3F}"), "OsmAnd (SQLite3)",
         "OsmAndCache", "cache_osmand", true, false, true},
        {CacheTypeId::SqliteLocus, uuidOf("{11EB8F68-5E2A-4CCA-BDA1-34E7B9FB0278}"), "Locus (SQLite3)",
         "LocusCache", "cache_locus", true, false, true},
        {CacheTypeId::SqliteRMaps, uuidOf("{57788698-C306-416D-B374-B7B8120A0F0E}"), "RMaps (SQLite3)",
         "RMaps", "cache_rmaps", true, false, true},
        {CacheTypeId::SqliteOruxMaps, uuidOf("{C50F37F3-8B2B-4F58-840A-772D004D4775}"), "OruxMaps (SQLite3)",
         nullptr, nullptr, false, false, true},
    };
    return table;
}

const CacheTypeDescriptor* cacheTypeById(CacheTypeId id) {
    for (const CacheTypeDescriptor& d : cacheTypes()) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

ITileNameCodecPtr codecForCacheType(CacheTypeId id) {
    switch (id) {
    case CacheTypeId::Sas:
        return std::make_shared<const SasTileNameCodec>();
    case CacheTypeId::Gmv:
        return std::make_shared<const GmvTileNameCodec>();
    case CacheTypeId::Es:
        return std::make_shared<const EsTileNameCodec>();
    case CacheTypeId::Gm:
        return std::make_shared<const Gm1TileNameCodec>();
    case CacheTypeId::GmAux:
        return std::make_shared<const Gm2TileNameCodec>();
    case CacheTypeId::GmBing:
        return std::make_shared<const Gm3TileNameCodec>();
    case CacheTypeId::Mobac:
        return std::make_shared<const MobacTileNameCodec>();
    case CacheTypeId::OsmAnd:
        return std::make_shared<const OsmandTileNameCodec>();
    case CacheTypeId::Tms:
        return std::make_shared<const TmsTileNameCodec>();
    case CacheTypeId::Sqlite:
        return std::make_shared<const RegionTileNameCodec>();
    default:
        return nullptr;
    }
}

} // namespace cg::storage
