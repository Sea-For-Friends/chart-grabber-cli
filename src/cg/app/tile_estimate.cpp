// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/app/tile_estimate.h"

#include "cg/app/app_context.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/region/tiles_download.h"
#include "cg/storage/tile_storage.h"

#include <algorithm>
#include <cmath>

namespace cg::app {

namespace {

constexpr qint64 kSampleSize = 500; // spec 3.7: up to 500 tiles per zoom

struct Sample {
    qint64 looked = 0;
    qint64 present = 0;
    qint64 replaced = 0;
    qint64 retried = 0;
    qint64 markedMissing = 0;
    qint64 presentBytes = 0;
};

} // namespace

qint64 legacyTileCount(const geo::ProjectionPtr& projection, const geo::IGeometryLonLatPolygon& area,
                       const AppContext& app) {
    std::vector<geo::DoublePoint> scratch;
    const auto projected = app.projectedFactory()->createPolygonByLonLatPolygon(*projection, area, scratch);
    // A null projected polygon is an empty area: no tiles (legacy would crash in the count).
    return projected ? geo::TileIteratorDataProvider(projection, projected, 1).tilesTotal() : 0;
}

core::Result<std::vector<ZoomEstimate>>
estimateTiles(const maptype::IMapType& map, const geo::IGeometryLonLatPolygon& area,
              std::span<const quint8> zooms, const AppContext& app, int assumeTileKb, bool replace,
              const storage::TileDate& keepNewerThan, bool retryMissing,
              const storage::TileDate& retryMissingOlderThan, const std::stop_token& stop,
              bool assumeEmptyCache) {
    const geo::ProjectionSetPtr projectionSet = map.projectionSet();
    const auto tileStorage = map.tileStorage();
    const auto versionRequest = map.versionRequest()->getStatic();
    // The caller owns `area` for the whole call; the iterator keeps only the projected polygon.
    const std::shared_ptr<const geo::IGeometryLonLatPolygon> areaRef(std::shared_ptr<void>(), &area);

    std::vector<ZoomEstimate> result;
    std::vector<Sample> samples;
    qint64 allPresent = 0;
    qint64 allPresentBytes = 0;
    for (const quint8 zoom : zooms) {
        const geo::ProjectionPtr& projection = projectionSet->zoom(zoom);

        ZoomEstimate estimate;
        estimate.zoom = zoom;
        estimate.tiles = legacyTileCount(projection, area, app);
        const auto bbox = region::boundingBoxTileCount(zoom, map, &area, *app.projectedFactory());
        estimate.bboxTiles = bbox ? bbox->tilesCount : 0;

        Sample sample;
        if (estimate.tiles > 0 && !assumeEmptyCache) { // T-0243: no storage call, nothing counted as present
            const qint64 step =
                std::max<qint64>(1, static_cast<qint64>(std::ceil(static_cast<double>(estimate.tiles) /
                                                                  static_cast<double>(kSampleSize))));
            const geo::TileIteratorPtr iterator =
                app.tileIteratorFactory()->makeTileIterator(projection, areaRef);
            geo::PointI tile;
            qint64 index = 0;
            while (iterator->next(tile)) {
                if (stop.stop_requested()) {
                    return core::fail(core::ErrorCode::Cancelled, QStringLiteral("estimate stopped"));
                }
                if (index++ % step != 0) {
                    continue;
                }
                const auto info =
                    tileStorage->tileInfoEx(tile, zoom, *versionRequest, storage::LoadMode::WithoutData);
                if (!info) {
                    return std::unexpected(info.error());
                }
                ++sample.looked;
                if (info->isExists()) { // :320-321
                    ++sample.present;
                    sample.presentBytes += info->size;
                    // :322-332 replace requests it again, unless it is not older than the given date
                    if (replace && !(keepNewerThan.isValid() && info->loadDate.isValid() &&
                                     info->loadDate >= keepNewerThan)) {
                        ++sample.replaced;
                    }
                } else if (info->isExistsTne()) {
                    ++sample.markedMissing;
                    // :335-345 --retry-missing requests the marker again, unless it is not older than the
                    // date
                    if (retryMissing && !(retryMissingOlderThan.isValid() && info->loadDate.isValid() &&
                                          info->loadDate >= retryMissingOlderThan)) {
                        ++sample.retried;
                    }
                }
            }
        }
        if (sample.looked > 0) {
            estimate.sharePresent = static_cast<double>(sample.present) / static_cast<double>(sample.looked);
            estimate.shareMarkedMissing =
                static_cast<double>(sample.markedMissing) / static_cast<double>(sample.looked);
            estimate.shareReplaced =
                static_cast<double>(sample.replaced) / static_cast<double>(sample.looked);
            estimate.shareRetried = static_cast<double>(sample.retried) / static_cast<double>(sample.looked);
        }
        allPresent += sample.present;
        allPresentBytes += sample.presentBytes;
        samples.push_back(sample);
        result.push_back(estimate);
    }

    // GetLenEnd (frm_ProgressDownload.pas:395-408): mean size x tiles still to load.
    const bool noneAnywhere = allPresent == 0;
    for (std::size_t i = 0; i < result.size(); ++i) {
        ZoomEstimate& estimate = result[i];
        double mean = 0;
        if (noneAnywhere) {
            mean = assumeTileKb * 1024.0;
            estimate.assumed = true;
        } else if (samples[i].present > 0) {
            mean = static_cast<double>(samples[i].presentBytes) / static_cast<double>(samples[i].present);
        } else {
            mean = static_cast<double>(allPresentBytes) / static_cast<double>(allPresent);
        }
        estimate.bytesToDownload = std::llround(static_cast<double>(estimate.tiles) *
                                                (1.0 - estimate.sharePresent + estimate.shareReplaced -
                                                 estimate.shareMarkedMissing + estimate.shareRetried) *
                                                mean);
        estimate.bytesOnDisk =
            std::llround(static_cast<double>(estimate.tiles) * (1.0 - estimate.shareMarkedMissing) * mean);
    }
    return result;
}

bool anyTile(const std::vector<ZoomEstimate>& estimates) {
    return std::ranges::any_of(estimates, [](const ZoomEstimate& e) { return e.tiles != 0; });
}

QStringList alignColumns(const QList<QStringList>& rows) {
    QList<qsizetype> widths;
    for (const QStringList& row : rows) {
        for (qsizetype c = 0; c < row.size(); ++c) {
            if (c >= widths.size()) {
                widths.push_back(0);
            }
            widths[c] = std::max(widths[c], row[c].size());
        }
    }
    QStringList lines;
    for (const QStringList& row : rows) {
        QString line;
        for (qsizetype c = 0; c < row.size(); ++c) {
            if (c > 0) {
                line += QStringLiteral("  ");
            }
            line += row[c].rightJustified(widths[c]);
        }
        lines.push_back(line);
    }
    return lines;
}

QStringList estimateTable(const std::vector<ZoomEstimate>& estimates,
                          const geo::ValueToStringConverter& converter) {
    const auto percent = [](double share) {
        return QString::number(share * 100.0, 'f', 1) + QLatin1Char('%');
    };
    const auto bytes = [&](qint64 value, bool assumed) {
        QString text = QLatin1Char('~') + converter.dataSizeConvert(static_cast<double>(value) / 1024.0);
        return assumed ? text + QStringLiteral(" (assumed)") : text;
    };
    QList<QStringList> rows;
    rows.push_back({QStringLiteral("zoom"), QStringLiteral("SAS z"), QStringLiteral("tiles"),
                    QStringLiteral("bbox"), QStringLiteral("present"), QStringLiteral("marked missing"),
                    QStringLiteral("to download")});
    qint64 tiles = 0;
    qint64 bbox = 0;
    qint64 total = 0;
    bool assumed = false;
    for (const ZoomEstimate& e : estimates) {
        rows.push_back({QString::number(e.zoom), QString::number(e.zoom + 1), QString::number(e.tiles),
                        QString::number(e.bboxTiles), percent(e.sharePresent), percent(e.shareMarkedMissing),
                        bytes(e.bytesToDownload, e.assumed)});
        tiles += e.tiles;
        bbox += e.bboxTiles;
        total += e.bytesToDownload;
        assumed = assumed || e.assumed;
    }
    rows.push_back({QStringLiteral("total"), QString(), QString::number(tiles), QString::number(bbox),
                    QString(), QString(), bytes(total, assumed)});
    return alignColumns(rows);
}

} // namespace cg::app
