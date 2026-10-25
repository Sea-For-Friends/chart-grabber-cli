// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: fr_ExportMBTiles.pas:550-573 (texts), :375-418 (via
// region::directTilesCopy); fr_TilesDownload.pas:428-431 (via region::allowDownload); region-export.md
// section 9. Ports: nothing. docs/specs/app-cli.md 1.4, 1.5, 1.7, 3.6, 5.2.
//
// Source resolution (GUID / file name / name) and the refusals S-1 ... S-11 that stop a job before its first
// request. S-2 is findSource; S-12 is job_area's; S-13 (no tile at any zoom) is the caller's, with the
// estimate (tile_estimate.h anyTile).

#include "cg/app/job.h"
#include "cg/app/snapshot.h"
#include "cg/maptype/map_type_set.h"

#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cg::geo {
class IGeometryLonLatPolygon;
} // namespace cg::geo

namespace cg::storage {
class ITileStorage;
} // namespace cg::storage

namespace cg::app {

class AppContext;

struct ResolvedSource {
    maptype::IMapTypePtr map;
    bool asOverlay = false; // the flag, not the ZMP, decides the slot (app-cli.md 1.4)
    QString outFile;        // export and run only
    QString name;           // MBTiles name: --name for the base, else the source's name without apostrophes
    QString attribution;    // MBTiles attribution: --attribution, else the source's License text (T-0222)
    std::optional<Snapshot>
        snapshot; // set by the snapshot step of the command (T-0243, weather-sources.md 3.5)
    // job.zooms without the zooms above the source's MaxZoom (T-0245, weather-sources.md 6); 0-based, sorted
    std::vector<quint8> zooms;
    // T-0356: the snapshot is the newest frame of its index and younger than kYoungFrameSeconds at the start
    // of the run: the download stops when its tiles are not served and the run goes one frame back
    bool frameFallback = false;
};

// app-cli.md 1.4. Errors have exit code 3.
[[nodiscard]] std::expected<maptype::IMapTypePtr, CliError> findSource(const maptype::IMapTypeSet& maps,
                                                                       const QString& id);

// S-4 ... S-7: the first applicable reason.
[[nodiscard]] std::optional<QString> refusalForExport(const maptype::IMapType& map, const Job& job,
                                                      bool asOverlay);

// S-8, S-9.
[[nodiscard]] std::optional<QString> refusalForDownload(const maptype::IMapType& map);

// S-15 (T-0275, app-cli.md 1.7; legacy L-44): the storage facade of the source could not build its backend.
// The text names the source and the cause; nullopt for any other ITileStorage.
[[nodiscard]] std::optional<QString> refusalForCacheBuild(const storage::ITileStorage& storage,
                                                          const QString& sourceName);

// S-14 (T-0243, weather-sources.md 5.3): invalid snapshot keys, an invalid MaxZoom (T-0245; every source), or
// a snapshot source on a cache that keeps no tile versions.
[[nodiscard]] std::optional<QString> refusalForSnapshot(const maptype::IMapType& map);

// S-16 (T-0277, app-cli.md 1.7; legacy L-18): the area's bounding tile rectangle at a chosen zoom holds more
// than 2^31-1 tiles, so the legacy 32-bit count would wrap. The count itself stays legacy (tile_count.cpp).
[[nodiscard]] std::optional<QString> refusalForAreaSize(const maptype::IMapType& map,
                                                        const geo::IGeometryLonLatPolygon& area,
                                                        std::span<const quint8> zooms);

// app-cli.md 1.6.2: the TermsOfUseWarning URL of the map's params.txt [PARAMS], trimmed; nullopt when the key
// is absent or empty, or a provider on the way is null. A key of this project (legacy ignores it).
[[nodiscard]] std::optional<QString> termsOfUseUrl(const maptype::IMapType& map);

// app-cli.md 1.6.5, a key of this project (legacy ignores it): true when `NavigationWarning` of the map's
// params.txt [PARAMS], trimmed, is `1` or `true` (ASCII case-insensitive); anything else, absent, or a null
// provider on the way gives false.
[[nodiscard]] bool navigationWarning(const maptype::IMapType& map);

// S-1, S-3, S-2 (--source, --overlay), S-4 ... S-11 and the output names of app-cli.md 1.5. The warnings
// (slot disagreement, apostrophe removed from a derived name) go to `warn`. Commands without sources give an
// empty vector.
[[nodiscard]] std::expected<std::vector<ResolvedSource>, CliError>
resolveJob(const Job& job, const AppContext& app, const std::function<void(const QString&)>& warn);

} // namespace cg::app
