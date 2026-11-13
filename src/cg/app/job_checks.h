// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: fr_ExportMBTiles.pas:550-573 (texts), :375-418 (via
// region::directTilesCopy); fr_TilesDownload.pas:428-431 (via region::allowDownload). Ports: nothing.
//
// Source resolution (GUID / file name / name) and the refusals that stop a job before its first request.
// findSource and job_area hold one refusal each; the one for a source with no tile at any zoom is the
// caller's, with the estimate (tile_estimate.h anyTile).

#include "cg/app/job.h"
#include "cg/app/snapshot.h"
#include "cg/maptype/map_type_set.h"

#include <QDateTime>
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
    bool asOverlay = false; // the flag, not the ZMP, decides the slot
    QString outFile;        // export and run only
    QString name;           // MBTiles name: --name for the base, else the source's name without apostrophes
    QString attribution;    // MBTiles attribution: --attribution, else the source's License text
    std::optional<Snapshot> snapshot; // set by the snapshot step of the command
    // job.zooms without the zooms above the source's MaxZoom; 0-based, sorted
    std::vector<quint8> zooms;
    // The snapshot is the newest frame of its index and younger than kYoungFrameSeconds at the start of the
    // run: the download stops when its tiles are not served and the run goes one frame back
    bool frameFallback = false;
};

// Errors have exit code 3.
[[nodiscard]] std::expected<maptype::IMapTypePtr, CliError> findSource(const maptype::IMapTypeSet& maps,
                                                                       const QString& id);

// The first refusal that applies to exporting this map (tile type, projection, format, zooms).
[[nodiscard]] std::optional<QString> refusalForExport(const maptype::IMapType& map, const Job& job,
                                                      bool asOverlay);

[[nodiscard]] std::optional<QString> refusalForDownload(const maptype::IMapType& map);

// The storage facade of the source could not build its backend. The text names the source and the cause;
// nullopt for any other ITileStorage.
[[nodiscard]] std::optional<QString> refusalForCacheBuild(const storage::ITileStorage& storage,
                                                          const QString& sourceName);

// Invalid snapshot keys, an invalid MaxZoom (every source), or a snapshot source on a cache that keeps no
// tile versions.
[[nodiscard]] std::optional<QString> refusalForSnapshot(const maptype::IMapType& map);

// The area's bounding tile rectangle at a chosen zoom holds more than 2^31-1 tiles, so the legacy 32-bit
// count would wrap. The count itself stays legacy (tile_count.cpp).
[[nodiscard]] std::optional<QString> refusalForAreaSize(const maptype::IMapType& map,
                                                        const geo::IGeometryLonLatPolygon& area,
                                                        std::span<const quint8> zooms);

// The TermsOfUseWarning URL of the map's params.txt [PARAMS], trimmed; nullopt when the key is absent or
// empty, or a provider on the way is null. A key of this project (legacy ignores it).
[[nodiscard]] std::optional<QString> termsOfUseUrl(const maptype::IMapType& map);

// A key of this project (legacy ignores it): true when `NavigationWarning` of the map's params.txt
// [PARAMS], trimmed, is `1` or `true` (ASCII case-insensitive); anything else, absent, or a null provider
// on the way gives false.
[[nodiscard]] bool navigationWarning(const maptype::IMapType& map);

// The source refusals (--source, --overlay) and the output file names. The warnings (slot disagreement,
// apostrophe removed from a derived name) go to `warn`. Commands without sources give an empty vector.
[[nodiscard]] std::expected<std::vector<ResolvedSource>, CliError>
resolveJob(const Job& job, const AppContext& app, const std::function<void(const QString&)>& warn);

// Which cached tiles and TNE markers a download requests again. The user's flags, and for a snapshot whose
// model run changed (Snapshot::referenceSeen) every tile and marker loaded before the run was first seen.
// DownloadRunner::run and the estimate printed before a download both read it, so the table counts what the
// download then asks for. An invalid date = no date.
struct TileRetryRule {
    bool replace = false;
    QDateTime replaceOlderThan;
    bool retryMissing = false;
    QDateTime retryMissingOlderThan;
};
[[nodiscard]] TileRetryRule tileRetryRule(const Job& job, const std::optional<Snapshot>& snapshot);

} // namespace cg::app
