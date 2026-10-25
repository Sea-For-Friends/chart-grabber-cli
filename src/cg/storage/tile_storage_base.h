// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_TileStorageAbstract.pas (the shared, non-abstract half: constructor plumbing and
// NotifyTileUpdate; GetTileFileName/GetTileInfo/GetTileInfoEx/GetTileRectInfo/DeleteTile/SaveTile
// stay `virtual; abstract` in legacy and are each backend's own job).
//
// docs/specs/storage.md §2.6.3. A small non-virtual helper holding what every backend shares: the
// type abilities, the storage state, the tile-pyramid notifier, the projection set, the storage
// path and the map-version factory. Concrete backends hold this as a MEMBER ("prefer it as a
// member") and implement ITileStorage themselves -- it exists only because eight backends would
// otherwise repeat these eleven lines each. No backend lands in this ticket (Scope out), so
// nothing constructs one yet.
//
// The two legacy virtual-but-not-abstract defaults (GetListOfTileVersions -> empty list,
// ScanTiles -> nil) are not methods here: they carry no state, so a backend that wants the
// default just returns `MapVersionList{}` / `EnumTileInfoPtr{}` directly from its own override.

#include "cg/geo/geo_types.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/map_version.h"
#include "cg/storage/storage_state.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_storage_abilities.h"

#include <QString>
#include <QtGlobal>

namespace cg::storage {

class TileStorageBase {
public:
    // `resolvedAbilities`: the FINAL TileStorageAbilities ceiling for this instance -- already
    // narrowed by StorageConfig.ini and the caller's forced abilities
    // (tile_storage_type.h::applyStorageConfigIni(), ports u_TileStorageTypeBase.pas
    // GetStorageAbilitiesByConfig). Despite the legacy parameter's name (AStorageForceAbilities),
    // TTileStorageAbstract.Create receives the already-resolved value, not the raw forced input;
    // see u_TileStorageTypeBase.pas:BuildStorage.
    // `tileNotifier`: caller-supplied and shared, never default-constructed here -- ports
    // u_TileStorageAbstract.pas:121-160 (ATileNotifier is a required constructor argument, just
    // stored). The future TileStorageOfMapType-equivalent facade owns ONE TilePyramidNotifier and
    // passes that same instance into every BuildStorage/TileStorageBase it creates across a
    // cache-type/path swap (u_TileStorageOfMapType.pas:214,281-285), so subscribers registered
    // before the swap keep receiving updates from the new backend.
    TileStorageBase(TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
                    MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
                    TilePyramidNotifierPtr tileNotifier, QString storagePath);

    [[nodiscard]] const TileStorageTypeAbilities& typeAbilities() const noexcept { return typeAbilities_; }
    [[nodiscard]] StorageStateHolder& state() noexcept { return state_; }
    [[nodiscard]] const StorageStateHolder& state() const noexcept { return state_; }
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const noexcept { return *projectionSet_; }
    [[nodiscard]] TilePyramidNotifier& tileNotifier() noexcept { return *tileNotifier_; }
    [[nodiscard]] const QString& storagePath() const noexcept { return storagePath_; }
    [[nodiscard]] const MapVersionFactoryPtr& mapVersionFactory() const noexcept {
        return mapVersionFactory_;
    }

    // Ports TTileStorageAbstract.NotifyTileUpdate: called after every successful saveTile/
    // deleteTile, outside the backend's own lock (spec §3.1 rule 3).
    void notifyTileUpdate(geo::PointI tile, quint8 zoom, const MapVersionInfoPtr& version);

private:
    TileStorageTypeAbilities typeAbilities_;
    StorageStateHolder state_;
    geo::ProjectionSetPtr projectionSet_;
    TilePyramidNotifierPtr tileNotifier_;
    QString storagePath_;
    MapVersionFactoryPtr mapVersionFactory_;
};

} // namespace cg::storage
