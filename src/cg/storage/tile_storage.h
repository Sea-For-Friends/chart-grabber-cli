// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileStorage.pas
//
// docs/specs/storage.md §2.6 (:428-495). The root interface every backend implements. Every
// method is synchronous and blocking (survey 03 §6, idioms §8): cancellation is layered by
// callers (tileRectInfo takes a std::stop_token) so the byte-level backend logic stays
// unit-testable without an event loop.
//
// Deviation (already flagged by T-0021A, not re-derived here): the tile coordinate type is
// geo::PointI, not geo::TilePoint, and the tile rect type is geo::RectI, not geo::TileRect --
// geo slice A (T-0016) landed those names; see tile_info.h / tile_rect_info.h.

#include "cg/core/changeable.h"
#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection_set.h"
#include "cg/storage/map_version.h"
#include "cg/storage/storage_state.h"
#include "cg/storage/tile_info.h"
#include "cg/storage/tile_pyramid_notifier.h"
#include "cg/storage/tile_rect_info.h"
#include "cg/storage/tile_storage_abilities.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QtGlobal>

#include <map>
#include <memory>
#include <set>
#include <stop_token>
#include <tuple>

namespace cg::storage {

// FEAT-STO-001. Ports ITileStorage.
class ITileStorage {
public:
    virtual ~ITileStorage() = default;

    [[nodiscard]] virtual TileStorageTypeAbilities typeAbilities() const = 0;
    [[nodiscard]] virtual StorageStatePtr state() const = 0;
    // Ports GetState / property State (IStorageStateChangeble): subscribe through changed(); the
    // current snapshot is state() (legacy State.GetStatic). Read-only on purpose -- the
    // IStorageStateChangeble half, not the IStorageStateInternal setters -- and a plain
    // core::Changeable so a facade (TTileStorageOfMapType's FStorageStateProxy) can forward it.
    [[nodiscard]] virtual core::Changeable& stateChangeable() = 0;
    [[nodiscard]] virtual const geo::IProjectionSet& projectionSet() const = 0;
    [[nodiscard]] virtual TilePyramidNotifier& tileNotifier() = 0;

    // Ports GetTileFileName: a *display* path -- the file or region a tile lives in. Folder
    // caches return the tile file, region caches the .sdb/.sqlitedb, RAM returns "". It is a UI
    // affordance ("copy tile path"); nothing is ever opened through it.
    [[nodiscard]] virtual QString tileFileName(geo::PointI xy, quint8 zoom,
                                               const MapVersionInfoPtr& version) const = 0;

    // FEAT-STO-001. Single-version lookup. Must consult the mem-cache BEFORE checking
    // state()->readAccess (u_TileStorageFileSystem.pas:GetTileInfo, first statement) -- reproduce
    // the order of each backend, the RAM storage depends on it (the SQLite single-file storage tests
    // the access first). Where legacy answers nil (folder / SQLite single-file without read access,
    // archive always) the port answers a default TileInfo, type Unknown, and never caches it.
    [[nodiscard]] virtual core::Result<TileInfo>
    tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr& version, LoadMode mode) const = 0;

    // Ports GetTileInfoEx: the caller may ask for "this version, or the newest". A
    // version-ignoring backend forwards to tileInfo(xy, zoom, nullptr, mode)
    // (u_TileStorageFileSystem.pas:GetTileInfoEx).
    [[nodiscard]] virtual core::Result<TileInfo>
    tileInfoEx(geo::PointI xy, quint8 zoom, const MapVersionRequest& request, LoadMode mode) const = 0;

    [[nodiscard]] virtual core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                        const MapVersionInfoPtr& version) = 0;

    // Ports SaveTile. Empty contentType OR null data => save a TNE. `overwrite` is passed by
    // callers as state()->replaceAccess, not a constant (u_TileDownloadResultSaverStuped.pas:312).
    // Returns false (not an error) when the tile exists and `overwrite` is false.
    [[nodiscard]] virtual core::Result<bool> saveTile(geo::PointI xy, quint8 zoom,
                                                      const MapVersionInfoPtr& version, TileDate loadDate,
                                                      QByteArrayView contentType, const QByteArray& data,
                                                      bool overwrite) = 0;

    // T-0351 (new): the tiles (zoom, x, y) whose saves under `version` were refused although they carried
    // data, i.e. skipped as byte-identical to a stored version (SaveFlag::SkipIfSameAsPrev,
    // u_TileStorageSQLiteHandler.pas:317-347); the record is emptied of them. T-0352: with each tile the
    // size and CRC-32 of the refused data, the criterion of that skip, so that the caller can tell which
    // stored row was the identical one. Default: none (only the facade of a map records, and only after
    // setRecordSkippedSaves(true)).
    struct SkippedSave {
        qint64 size = 0;
        quint32 crc = 0;
        friend bool operator==(const SkippedSave&, const SkippedSave&) = default;
    };
    // T-0352 (option C): nothing is recorded unless the run switches it on (only a snapshot source needs
    // the record); default: no-op, like takeSkippedSaves.
    virtual void setRecordSkippedSaves(bool /*on*/) {}
    [[nodiscard]] virtual std::map<std::tuple<quint8, int, int>, SkippedSave>
    takeSkippedSaves(const MapVersionInfoPtr& /*version*/) {
        return {};
    }

    // FEAT-STO-032. Default (u_TileStorageAbstract.pas): an empty list.
    [[nodiscard]] virtual core::Result<MapVersionList>
    listVersions(geo::PointI xy, quint8 zoom, const MapVersionRequest& request) const = 0;

    // FEAT-STO-035. `stop` replaces the legacy (AOperationID, INotifierOperation) pair (idioms 8:
    // "storage interfaces stay synchronous and take a std::stop_token").
    [[nodiscard]] virtual core::Result<TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                     const MapVersionRequest& request,
                                                                     std::stop_token stop) const = 0;

    // FEAT-STO-035. Default (u_TileStorageAbstract.pas): null -- scanning not allowed.
    [[nodiscard]] virtual core::Result<EnumTileInfoPtr> scanTiles(bool ignoreTne,
                                                                  bool ignoreMultiVersionTiles) = 0;
};
using ITileStoragePtr = std::shared_ptr<ITileStorage>;

} // namespace cg::storage
