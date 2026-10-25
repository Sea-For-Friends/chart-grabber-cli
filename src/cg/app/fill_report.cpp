// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/app/fill_report.h"

#include "cg/app/app_context.h"
#include "cg/app/tile_estimate.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/storage/tile_storage.h"

namespace cg::app {

core::Result<std::vector<ZoomFill>> fillReport(const maptype::IMapType& map,
                                               const geo::IGeometryLonLatPolygon& area,
                                               std::span<const quint8> zooms, const AppContext& app,
                                               const std::stop_token& stop) {
    const geo::ProjectionSetPtr projectionSet = map.projectionSet();
    const auto tileStorage = map.tileStorage();
    const auto versionRequest = map.versionRequest()->getStatic();
    // The caller owns `area` for the whole call; the iterator keeps only the projected polygon.
    const std::shared_ptr<const geo::IGeometryLonLatPolygon> areaRef(std::shared_ptr<void>(), &area);

    std::vector<ZoomFill> result;
    for (const quint8 zoom : zooms) {
        ZoomFill fill;
        fill.zoom = zoom;
        // The download walk is budgeted by the legacy count (u_DownloadTaskProvider.pas:208-229): a zoom
        // whose count is 0 (area of at most 1 pixel) is not walked at all (T-0259).
        const qint64 budget = legacyTileCount(projectionSet->zoom(zoom), area, app);
        if (budget == 0) {
            result.push_back(fill);
            continue;
        }
        const geo::TileIteratorPtr iterator =
            app.tileIteratorFactory()->makeTileIterator(projectionSet->zoom(zoom), areaRef, budget);
        geo::PointI tile;
        while (iterator->next(tile)) {
            if (stop.stop_requested()) {
                return core::fail(core::ErrorCode::Cancelled, QStringLiteral("fill report stopped"));
            }
            const auto info =
                tileStorage->tileInfoEx(tile, zoom, *versionRequest, storage::LoadMode::WithoutData);
            if (!info) {
                return std::unexpected(info.error());
            }
            ++fill.tiles;
            if (info->isExists()) { // u_ThreadDownloadTiles.pas:320-321
                ++fill.present;
                fill.bytes += info->size;
            } else if (info->isExistsTne()) {
                ++fill.markedMissing;
            } else {
                ++fill.missing;
            }
        }
        result.push_back(fill);
    }
    return result;
}

QStringList fillTable(const std::vector<ZoomFill>& fills, const geo::ValueToStringConverter& converter) {
    const auto bytes = [&](qint64 value) {
        return converter.dataSizeConvert(static_cast<double>(value) / 1024.0);
    };
    QList<QStringList> rows;
    rows.push_back({QStringLiteral("zoom"), QStringLiteral("tiles"), QStringLiteral("present"),
                    QStringLiteral("marked missing"), QStringLiteral("missing"), QStringLiteral("size")});
    ZoomFill total;
    for (const ZoomFill& f : fills) {
        rows.push_back({QString::number(f.zoom), QString::number(f.tiles), QString::number(f.present),
                        QString::number(f.markedMissing), QString::number(f.missing), bytes(f.bytes)});
        total.tiles += f.tiles;
        total.present += f.present;
        total.markedMissing += f.markedMissing;
        total.missing += f.missing;
        total.bytes += f.bytes;
    }
    rows.push_back({QStringLiteral("total"), QString::number(total.tiles), QString::number(total.present),
                    QString::number(total.markedMissing), QString::number(total.missing),
                    bytes(total.bytes)});
    return alignColumns(rows);
}

} // namespace cg::app
