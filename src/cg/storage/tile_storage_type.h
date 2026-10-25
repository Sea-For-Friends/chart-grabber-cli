// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileStorageType.pas, i_TileStorageTypeConfig.pas, u_TileStorageTypeBase.pas
//
// docs/specs/storage.md §2.12 (:919-951). ITileStorageType is the "backend factory" every
// concrete `u_TileStorageType*.pas` subclass implements; i_TileStorageTypeConfig collapses into
// ITileStorageType::basePath(), its only member. No concrete backend lands in this ticket (Scope
// out); applyStorageConfigIni() ports the one piece of TTileStorageTypeBase.BuildStorage that is
// not itself abstract dispatch, so the eight backend tickets that will implement build() do not
// each re-open StorageConfig.ini themselves.
//
// TileInfoMemCache (the spec's build() parameter) is storage.md §2.9, landed in T-0021E1
// (src/cg/storage/tile_info_mem_cache.h). Forward-declared here as an opaque type passed only by
// shared_ptr -- ITileStorageType::build() only stores/forwards the shared_ptr, so no include is
// needed.

#include "cg/core/config_provider.h"
#include "cg/core/content_type.h"
#include "cg/core/path_config.h"
#include "cg/core/result.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_abilities.h"

#include <QString>

#include <memory>

namespace cg::storage {

class TileInfoMemCache; // storage.md §2.9, landed in T-0021E1 (see header comment)

// FEAT-STO-001. Ports ITileStorageType + ITileStorageTypeConfig (collapsed).
class ITileStorageType {
public:
    virtual ~ITileStorageType() = default;

    [[nodiscard]] virtual TileStorageTypeAbilities abilities() const = 0;
    [[nodiscard]] virtual core::PathConfigPtr basePath() const = 0;
    // Legacy `MapVersionFactory` (i_TileStorageType.pas:45-46 = u_TileStorageTypeBase.pas:209-212).
    [[nodiscard]] virtual MapVersionFactoryPtr mapVersionFactory() const = 0;

    // Legacy `BuildStorage` (i_TileStorageType.pas:48-55 = u_TileStorageTypeBase.pas:58-66), same
    // six parameters in the same order. `mainContentType` is already resolved by the caller
    // (u_MapType.pas:377/380); `tileNotifier` is caller-owned and shared across every build() the
    // caller makes so subscribers survive a backend swap (u_TileStorageOfMapType.pas:214,285,327)
    // -- build() must never construct its own notifier. `path` is used verbatim; the caller
    // guarantees a trailing '/' (IncludeTrailingPathDelimiter, u_TileStorageOfMapType.pas:317).
    [[nodiscard]] virtual core::Result<ITileStoragePtr>
    build(const TileStorageAbilities& forceAbilities, geo::ProjectionSetPtr projectionSet,
          const core::ContentTypeInfo& mainContentType, TilePyramidNotifierPtr tileNotifier,
          const QString& path /* trailing '/' guaranteed by the caller */,
          std::shared_ptr<TileInfoMemCache> cacheTileInfo) = 0;
};
using ITileStorageTypePtr = std::shared_ptr<ITileStorageType>;

// Ports TTileStorageTypeBase.GetStorageAbilitiesByConfig + the StorageConfig.ini-finding half of
// BuildStorage (u_TileStorageTypeBase.pas:88-141): looks beside `path` for StorageConfig.ini (the
// exact name depends on `storageClass` -- OneFile: "<path>.StorageConfig.ini"; Folder /
// InSeparateFiles: "<path>StorageConfig.ini", `path` already ending with a separator; InMemory /
// Other: none), reads its [Common] section and narrows `base` by it (the section's `IsReadOnly`
// additionally force-clears the three write bits, restrictedBy()'s own `forceReadOnly` contract),
// then narrows the result by `forced` -- a SECOND, plain pairwise AND with no additional
// read-only forcing. That is legacy's own two separate restriction passes, not one restrictedBy()
// call (u_TileStorageTypeBase.pas:98-124). Returns `base` (then narrowed by `forced`) unmodified
// by the ini pass when the file does not exist or has no [Common] section, matching legacy's
// `FileExists` guard.
//
// Legacy opens the file ONCE in BuildStorage and hands the provider to both GetStorageAbilitiesByConfig
// and BuildStorageInternal (u_TileStorageTypeBase.pas:162-175). openStorageConfigIni() is the
// file-finding half (nullptr when the class has no file or it cannot be read); the provider overload
// is the GetStorageAbilitiesByConfig half. The four-argument form is both, for types that need no
// other section.
[[nodiscard]] core::IConfigProviderPtr openStorageConfigIni(StorageClass storageClass, const QString& path);
[[nodiscard]] TileStorageAbilities applyStorageConfigIni(TileStorageAbilities base,
                                                         const TileStorageAbilities& forced,
                                                         const core::IConfigProviderPtr& storageConfig);
[[nodiscard]] TileStorageAbilities applyStorageConfigIni(TileStorageAbilities base,
                                                         const TileStorageAbilities& forced,
                                                         StorageClass storageClass, const QString& path);

} // namespace cg::storage
