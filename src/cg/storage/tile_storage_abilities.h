// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileStorageAbilities.pas, u_TileStorageAbilities.pas

#include <cstdint>

namespace cg::storage {

// FEAT-STO-031
struct TileStorageAbilities {
    bool allowRead = false;
    bool allowScan = false;
    bool allowAdd = false;
    bool allowDelete = false;
    bool allowReplace = false;

    [[nodiscard]] bool operator==(const TileStorageAbilities&) const = default;

    // Ports TTileStorageAbilities.IsReadOnly: NOT add AND NOT delete AND NOT replace.
    // Note that it does not look at allowRead -- a no-access storage is also "read only".
    [[nodiscard]] constexpr bool isReadOnly() const noexcept {
        return !allowAdd && !allowDelete && !allowReplace;
    }

    [[nodiscard]] static constexpr TileStorageAbilities readOnly() {
        return {true, true, false, false, false};
    }
    [[nodiscard]] static constexpr TileStorageAbilities writeOnly() {
        return {false, false, true, true, true};
    }
    [[nodiscard]] static constexpr TileStorageAbilities readWrite() { return {true, true, true, true, true}; }
    [[nodiscard]] static constexpr TileStorageAbilities none() { return {}; }

    // Ports TTileStorageTypeBase.GetStorageAbilitiesByConfig: pairwise AND, plus the isReadOnly
    // override that force-clears the three write bits.
    [[nodiscard]] TileStorageAbilities restrictedBy(const TileStorageAbilities& other,
                                                    bool forceReadOnly) const;
};

enum class StorageClass : std::uint8_t { InMemory, OneFile, Folder, InSeparateFiles, Other };
enum class VersionSupport : std::uint8_t { Ignored, Stored, MultiVersions };

struct TileStorageTypeAbilities {
    TileStorageAbilities base{};
    VersionSupport version = VersionSupport::Ignored;
    bool supportsMixedContentTypes = false;
    StorageClass storageClass = StorageClass::Other;
};

// Tech Lead ruling 2026-09-19 (T-0021A rework): the per-type table the ticket's scope line names
// (storage.md :242-297), keyed by a coarse kind rather than the full `CacheTypeId` (that enum is
// T-0010/T-0021D scope, not landed yet -- one value per spec table row, folder-type and
// SQLiteFile-format variants collapsed exactly as the spec table collapses them).
enum class TileStorageKind : std::uint8_t {
    FolderSimple,  // all 9 folder types -- u_TileStorageTypeFileSystemSimple.pas
    Sqlite3Native, // u_TileStorageTypeSQLite.pas (always constructed AIsVersioned = True)
    SqliteFile,    // MBTiles / OsmAnd / Locus / RMaps -- u_TileStorageTypeSQLiteFile.pas
    OruxMaps,      // same file, AFormatId = sfOruxMaps
    Ram,           // u_TileStorageTypeInRAM.pas
    Archive,       // u_TileStorageTypeArchive.pas
};

// Ports each `u_TileStorageType*.pas` constructor's `TTileStorageTypeAbilities.Create(...)` call.
// `Archive`'s `base` is NOT fixed by legacy (`u_TileStorageTypeArchive.pas:82-107` takes
// `ATileStorageAbilities` as a caller-supplied constructor parameter); this returns
// `TileStorageAbilities::none()` for it and callers must supply the real value themselves --
// only `version`/`supportsMixedContentTypes`/`storageClass` are fixed for that row.
[[nodiscard]] TileStorageTypeAbilities tileStorageTypeAbilities(TileStorageKind kind);

} // namespace cg::storage
