// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_storage_abilities.h"

namespace cg::storage {

TileStorageAbilities TileStorageAbilities::restrictedBy(const TileStorageAbilities& other,
                                                        bool forceReadOnly) const {
    return TileStorageAbilities{
        allowRead && other.allowRead,
        allowScan && other.allowScan,
        allowAdd && other.allowAdd && !forceReadOnly,
        allowDelete && other.allowDelete && !forceReadOnly,
        allowReplace && other.allowReplace && !forceReadOnly,
    };
}

TileStorageTypeAbilities tileStorageTypeAbilities(TileStorageKind kind) {
    switch (kind) {
    case TileStorageKind::FolderSimple:
        return {TileStorageAbilities::readWrite(), VersionSupport::Ignored, false,
                StorageClass::InSeparateFiles};
    case TileStorageKind::Sqlite3Native:
        // u_TileStorageTypeSQLite.pas, which u_TileStorageTypeListSimple.pas always constructs with
        // IsVersioned = True. (T-0333: the BerkeleyDb / BerkeleyDbVersioned kinds that shared this
        // row were removed with the backend, owner 2026-10-21.)
        return {TileStorageAbilities::readWrite(), VersionSupport::MultiVersions, true, StorageClass::Folder};
    case TileStorageKind::SqliteFile:
        return {TileStorageAbilities::readWrite(), VersionSupport::Ignored, false, StorageClass::OneFile};
    case TileStorageKind::OruxMaps:
        return {TileStorageAbilities::readOnly(), VersionSupport::Ignored, false, StorageClass::OneFile};
    case TileStorageKind::Ram: {
        TileStorageAbilities ram = TileStorageAbilities::readWrite();
        ram.allowScan = false; // CTileStorageReadWrite - [tsatScan]
        return {ram, VersionSupport::Stored, true, StorageClass::InMemory};
    }
    case TileStorageKind::Archive:
        // base is caller-supplied in legacy (u_TileStorageTypeArchive.pas:82-107); leave it
        // unset here.
        return {TileStorageAbilities::none(), VersionSupport::Ignored, true, StorageClass::OneFile};
    }
    return {};
}

} // namespace cg::storage
