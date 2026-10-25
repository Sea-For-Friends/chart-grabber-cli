// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Download/u_ProviderTilesDownload.pas:194-379 (headless half; the form :229-246 is
// ui)

#include "cg/region/provider_tiles_download.h"

#include "cg/core/config_provider_ini.h"
#include "cg/maptype/map_version_request_changeable.h"

#include <stdexcept>
#include <utility>

namespace cg::region {

ProviderTilesDownload::ProviderTilesDownload(
    std::stop_token appClosing, std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory,
    std::shared_ptr<const maptype::IMapTypeSet> fullMapsSet,
    std::shared_ptr<const geo::GeometryLonLatFactory> lonLatFactory,
    std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory,
    std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig,
    std::shared_ptr<net::DownloadInfo> downloadInfo)
    : appClosing_(std::move(appClosing)), tileIteratorFactory_(std::move(tileIteratorFactory)),
      fullMapsSet_(std::move(fullMapsSet)), lonLatFactory_(std::move(lonLatFactory)),
      projectedFactory_(std::move(projectedFactory)), downloadConfig_(std::move(downloadConfig)),
      downloadInfo_(std::move(downloadInfo)) {}

DownloadSessionRun ProviderTilesDownload::startSession(std::shared_ptr<IDownloadSession> session,
                                                       std::shared_ptr<IDownloadTaskProvider> taskProvider,
                                                       std::shared_ptr<net::DownloadInfo> info, bool paused) {
    DownloadSessionRun run;
    run.session = std::move(session);
    run.log = std::make_shared<core::LogRingBuffer>(5000, 0);         // :210
    run.cancelNotifier = std::make_shared<core::OperationNotifier>(); // :214-217
    const std::stop_token token = run.cancelNotifier->begin();        // :219
    run.progressInfo =
        std::make_shared<RegionProcessProgressInfoDownload>(run.log, run.session, paused); // :221-227

    // :229-246 the form is ui; absent here
    if (!token.stop_requested()) { // :248
        const storage::TileDate replaceTneOlderDate = run.session->replaceTneOlderDate();
        run.thread = std::make_unique<ThreadDownloadTiles>(
            run.session->workerIndex(), token, run.progressInfo, appClosing_, run.session->mapType(),
            run.session->versionForCheck(), run.session->versionForDownload(), std::move(taskProvider),
            downloadConfig_, std::move(info), run.session->replaceExistTiles(),
            run.session->checkExistTileSize(), run.session->checkExistTileDate(),
            run.session->checkTileDate(), run.session->secondLoadTne(), replaceTneOlderDate.isValid(),
            replaceTneOlderDate); // :250-268
        run.thread->start();      // :270
    }
    return run;
}

DownloadSessionRun ProviderTilesDownload::startBySls(const QString& fileName, bool startPaused) { // :274-322
    const auto ini = core::ConfigProviderIni::open(fileName);                                     // :282
    if (!ini) {
        throw std::runtime_error(ini.error().what().toStdString()); // the TMemIniFile constructor raises
    }
    const core::IConfigProviderPtr sessionSection = (*ini)->subItem(u"Session"); // :290; null -> load raises

    auto session = std::make_shared<DownloadSession>();                              // :292
    session->load(sessionSection, *fullMapsSet_, *downloadConfig_, *lonLatFactory_); // :294-299

    auto taskProvider = std::make_shared<DownloadTaskProvider>(
        session->mapType(), session->polygon(), tileIteratorFactory_, projectedFactory_,
        session->workersCount(), session->zoomArr(), session->zoom(), session->lastProcessedPoint(),
        session->lastProcessedCount()); // :301-312

    auto info =
        std::make_shared<net::DownloadInfo>(downloadInfo_, static_cast<quint64>(session->downloadedCount()),
                                            session->downloadedSize()); // :320
    return startSession(std::move(session), std::move(taskProvider), std::move(info),
                        startPaused); // :314-321
}

std::vector<DownloadSessionRun> ProviderTilesDownload::startProcess(const PolygonPtr& polygon,
                                                                    const TilesDownloadParams& params) {
    // unreachable in legacy (fr_TilesDownload.pas:531-534 validates the zooms, the map comes from the
    // selector :472-475, the polygon from the region); no register row
    if (!params.mapType || !polygon || params.zoomArray.empty()) {
        throw std::invalid_argument("ProviderTilesDownload::startProcess: no map, polygon or zoom");
    }
    const maptype::IMapTypePtr& map = params.mapType;
    const std::vector<quint8>& zoomArr = params.zoomArray;
    const int workersCount = params.splitCount;

    auto taskProvider =
        std::make_shared<DownloadTaskProvider>(map, polygon, tileIteratorFactory_, projectedFactory_,
                                               workersCount, zoomArr, zoomArr[0], geo::PointI{-1, -1},
                                               -1); // :340-349

    std::vector<DownloadSessionRun> runs;
    for (int i = 0; i < workersCount; ++i) { // :351
        const auto versionRequest = map->versionRequest()->getStatic();
        auto session = std::make_shared<DownloadSession>(
            map, versionRequest, versionRequest->baseVersion, zoomArr[0], zoomArr, polygon,
            params.isIgnoreTne, params.loadTneOlderDate, params.isReplace, params.isReplaceIfDifSize,
            params.isReplaceIfOlder, params.replaceDate, params.isCloseOnFinish,
            params.sessionAutosaveInterval, params.sessionAutosaveFilePrefix, workersCount, i); // :352-372

        runs.push_back(startSession(std::move(session), taskProvider,
                                    std::make_shared<net::DownloadInfo>(downloadInfo_),
                                    params.isStartPaused)); // :374-378
    }
    return runs;
}

} // namespace cg::region
