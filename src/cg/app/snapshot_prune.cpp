// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/app/snapshot_prune.h"

#include "cg/app/app_context.h"
#include "cg/app/tile_estimate.h"
#include "cg/core/hash.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/storage/tile_storage.h"

#include <algorithm>

namespace cg::app {

core::Result<qint64> pruneOtherSnapshots(const maptype::IMapType& map,
                                         const geo::IGeometryLonLatPolygon& area,
                                         std::span<const quint8> zooms, const AppContext& app,
                                         const std::stop_token& stop) {
    const geo::ProjectionSetPtr projectionSet = map.projectionSet();
    const auto tileStorage = map.tileStorage();
    const storage::MapVersionInfoPtr current = map.versionRequest()->getStatic()->baseVersion;
    if (!current) {
        return 0; // guard only: MapVersionRequestChangeable::getStatic never holds a null base version
    }
    // The caller owns `area` for the whole call; the iterator keeps only the projected polygon.
    const std::shared_ptr<const geo::IGeometryLonLatPolygon> areaRef(std::shared_ptr<void>(), &area);
    const storage::MapVersionRequest all{nullptr, true};

    // T-0351: the tiles whose save under the current version was skipped as byte-identical (this run)
    const auto unchanged = tileStorage->takeSkippedSaves(current);

    qint64 deleted = 0;
    for (const quint8 zoom : zooms) {
        // the same walk as fillReport (budget by the legacy count, T-0259)
        const qint64 budget = legacyTileCount(projectionSet->zoom(zoom), area, app);
        if (budget == 0) {
            continue;
        }
        const geo::TileIteratorPtr iterator =
            app.tileIteratorFactory()->makeTileIterator(projectionSet->zoom(zoom), areaRef, budget);
        geo::PointI tile;
        while (iterator->next(tile)) {
            if (stop.stop_requested()) { // u_ThreadDeleteTiles.pas:121-123
                return core::fail(core::ErrorCode::Cancelled, QStringLiteral("snapshot prune stopped"));
            }
            const auto versions = tileStorage->listVersions(tile, zoom, all);
            if (!versions) {
                return std::unexpected(versions.error());
            }
            const auto recorded = unchanged.find(std::tuple<quint8, int, int>{zoom, tile.x(), tile.y()});
            // T-0352: the list drops the rows of the empty version (GetListOfTileVersions passes
            // storeEmptyVersion False), so a recorded tile whose only stored row is a pre-T-0246 one under
            // the empty version has a null list: that row is a candidate too.
            if (!*versions && recorded == unchanged.end()) {
                continue;
            }
            // T-0351: the SQLite save skips a tile byte-identical to a stored one
            // (SaveFlag::SkipIfSameAsPrev, u_TileStorageSQLiteHandler.pas:317-347, any version of x,y), so
            // the current version may have no row although the new frame has the tile. For exactly the tiles
            // the download recorded as skipped, the stored row identical to the refused data is the tile of
            // the current snapshot: it is moved to the current version (read, delete the others, save: the
            // save is skipped while an identical row exists). T-0352: the record holds size and CRC-32 of
            // the refused data (the criterion of the skip); the candidates are the newest listed version
            // (the list is newest first, GetOrderByVersion_DESC, listTileVersions) and the row of the empty
            // version, and only the one whose size and CRC match is moved. A tile the new frame lacks (404,
            // no TNE) is not in the record, and a recorded tile without a matching candidate is not the
            // new frame's either: both go with their old frame.
            const bool hasCurrent = *versions && std::ranges::any_of(**versions, [&](const auto& v) {
                return v && v->isSame(*current);
            });
            bool move = false;
            storage::MapVersionInfoPtr newest; // the version of the moved row; null = the empty version
            storage::TileInfo moved;
            if (!hasCurrent && recorded != unchanged.end()) {
                std::vector<storage::MapVersionInfoPtr> candidates;
                if (*versions) {
                    const auto first =
                        std::ranges::find_if(**versions, [](const auto& v) { return v != nullptr; });
                    if (first != (*versions)->end()) {
                        candidates.push_back(*first);
                    }
                }
                candidates.push_back(nullptr);
                for (const storage::MapVersionInfoPtr& candidate : candidates) {
                    const auto info =
                        tileStorage->tileInfoEx(tile, zoom, storage::MapVersionRequest{candidate, false},
                                                storage::LoadMode::WithData);
                    if (!info) {
                        return std::unexpected(info.error());
                    }
                    // a row that vanished since the list has no data and cannot match
                    if (info->isExists() && info->data.size() == recorded->second.size &&
                        core::crc32(0, info->data.constData(), static_cast<std::size_t>(info->data.size())) ==
                            recorded->second.crc) {
                        moved = *info;
                        newest = candidate;
                        move = true;
                        break;
                    }
                }
            }
            const std::vector<storage::MapVersionInfoPtr> none;
            for (const storage::MapVersionInfoPtr& version : *versions ? **versions : none) {
                if (!version || version->isSame(*current)) {
                    continue;
                }
                const auto done = tileStorage->deleteTile(tile, zoom, version);
                if (!done) {
                    return std::unexpected(done.error());
                }
                if (*done && version != newest) { // u_ThreadDeleteTiles.pas:127-129; the moved one is no loss
                    ++deleted;
                }
            }
            if (move) {
                if (!newest) { // the empty-version row is the moved tile: it is no loss either; it goes
                               // BEFORE the save, which is skipped while this identical row exists
                    const auto gone = tileStorage->deleteTile(tile, zoom, nullptr);
                    if (!gone) {
                        return std::unexpected(gone.error());
                    }
                }
                const auto saved = tileStorage->saveTile(tile, zoom, current, moved.loadDate,
                                                         moved.contentType, moved.data, true);
                if (!saved || !*saved) { // put the tile back under its own version, then report
                    [[maybe_unused]] const auto restored = tileStorage->saveTile(
                        tile, zoom, newest, moved.loadDate, moved.contentType, moved.data, true);
                    return saved
                               ? core::fail(core::ErrorCode::IoError,
                                            QStringLiteral("the tile of the current snapshot was not saved"))
                               : std::unexpected(saved.error());
                }
            }
        }
    }
    return deleted;
}

} // namespace cg::app
