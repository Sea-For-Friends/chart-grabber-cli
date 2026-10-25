// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Inherits: u_ThreadDownloadTiles.pas:357-390, 500-575 (one request, result
// classes). docs/specs/app-cli.md 3.13.
#include "cg/app/probe.h"

#include "cg/app/job_checks.h"
#include "cg/app/snapshot.h"
#include "cg/app/snapshot_resolver.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_request_result.h"
#include "cg/maptype/tile_request_task.h"
#include "cg/maptype/zmp_info.h"

#include <QCoreApplication>
#include <QUuid>

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Probe)
};

// The finish callback runs on a download thread, possibly after probeSources has given up: the state is
// shared.
struct Wait {
    std::mutex mutex;
    std::condition_variable_any changed;
    bool done = false;
    maptype::ITileRequestResultPtr result;
};

// The class of a download result and its text, one line.
QString kindText(const net::DownloadResult& r) {
    QString name;
    QString text = r.text;
    switch (r.kind) {
    case net::ResultKind::Ok:
        name = QStringLiteral("ok");
        break;
    case net::ResultKind::NotNecessary:
        name = QStringLiteral("not necessary");
        break;
    case net::ResultKind::Canceled:
        name = QStringLiteral("canceled");
        break;
    case net::ResultKind::ProxyError:
        name = QStringLiteral("proxy error");
        break;
    case net::ResultKind::Banned:
        name = QStringLiteral("banned");
        break;
    case net::ResultKind::BadContentType:
        name = QStringLiteral("bad content type");
        text += QStringLiteral(" ") + QString::fromLatin1(r.contentType); // :543 names the type
        break;
    case net::ResultKind::BadContentLength:
        name = QStringLiteral("bad content length");
        break;
    case net::ResultKind::BadContentEncoding:
        name = QStringLiteral("bad content encoding");
        break;
    case net::ResultKind::DataNotExists:
        name = QStringLiteral("not exists");
        break;
    case net::ResultKind::LoadError:
        name = QStringLiteral("load error");
        break;
    case net::ResultKind::NoConnectToServer:
        name = QStringLiteral("no connection");
        break;
    case net::ResultKind::UnknownError:
        name = QStringLiteral("unknown error");
        break;
    }
    text = text.simplified();
    return text.isEmpty() ? name : name + QStringLiteral(": ") + text;
}

// processResultAndCheckGotoNextTile (:500-575), as a row instead of a log line and a go-on flag.
ProbeRow classify(const maptype::ITileRequestResultPtr& result) {
    ProbeRow row;
    const auto* withDownload =
        dynamic_cast<const maptype::ITileRequestResultWithDownloadResult*>(result.get());
    if (withDownload != nullptr) {
        const net::DownloadResult& r = withDownload->downloadResult();
        if (r.kind == net::ResultKind::Ok) {
            if (const auto* error = dynamic_cast<const maptype::ITileRequestResultError*>(result.get())) {
                // :515-519 downloaded, but not saved
                row.result = QStringLiteral("not saved: ") + error->errorText().simplified();
                return row;
            }
            row.status = ProbeStatus::Passed;
            row.result =
                QStringLiteral("ok %1 %2").arg(r.data.size()).arg(QString::fromLatin1(r.contentType));
            return row;
        }
        row.result = kindText(r);
        return row;
    }
    // :572-573: no download result. A cancel is not a verdict on the server.
    if (dynamic_cast<const maptype::ITileRequestResultCanceled*>(result.get()) != nullptr) {
        row.result = QStringLiteral("canceled");
    } else if (const auto* error = dynamic_cast<const maptype::ITileRequestResultError*>(result.get())) {
        row.result = QStringLiteral("error: ") + error->errorText().simplified();
    } else {
        row.result = QStringLiteral("no download result");
    }
    return row;
}

// The request of processTask :357-390 for one tile, and the wait for its result.
ProbeRow probeOne(const maptype::IMapType& map, const Job& job, const std::stop_token& stop,
                  std::chrono::milliseconds timeout) {
    ProbeRow row;
    try {
        const geo::ProjectionSetPtr projectionSet = map.projectionSet();
        quint8 zoom = static_cast<quint8>(job.probeZoom); // 0..23 by the option's range
        projectionSet->validateZoom(zoom);
        const geo::ProjectionPtr projection = projectionSet->zoom(zoom);
        if (!projection->projectionType()->checkLonLatPos(job.probeAt)) {
            row.result = QStringLiteral("--at is outside the projection");
            return row;
        }
        const geo::PointI tile =
            geo::roundPoint(projection->lonLat2TilePosFloat(job.probeAt), geo::PointRounding::ToTopLeft);

        auto wait = std::make_shared<Wait>();
        const auto notifier = std::make_shared<maptype::TileRequestTaskFinishNotifier>(
            [wait](const maptype::ITileRequestTask&, const maptype::ITileRequestResultPtr& result) {
                {
                    const std::scoped_lock lock(wait->mutex);
                    wait->result = result;
                    wait->done = true;
                }
                wait->changed.notify_all();
            });
        std::stop_source cancel; // both cancel arguments are one token (:432-433)
        const auto subsystem = map.tileDownloadSubsystem();
        const auto task =
            subsystem->getRequestTask(cancel.get_token(), cancel.get_token(), notifier, tile, zoom,
                                      map.versionRequest()->getStatic()->baseVersion, false);
        if (!task) {
            row.result =
                QStringLiteral("downloading is disabled for this map or the tile is outside its grid");
            return row;
        }
        subsystem->download(task); // may finish the task on this thread
        std::unique_lock lock(wait->mutex);
        const bool finished = wait->changed.wait_for(lock, stop, timeout, [&wait] { return wait->done; });
        lock.unlock(); // request_stop may run the finish callback on this thread
        notifier->setEnabled(false);
        if (!finished) {
            cancel.request_stop();
            row.result = stop.stop_requested() ? QStringLiteral("interrupted")
                                               : QStringLiteral("timeout: no result in %1 s")
                                                     .arg(static_cast<double>(timeout.count()) / 1000.0);
            return row;
        }
        return classify(wait->result);
    } catch (const std::exception& e) {
        row.result = QStringLiteral("error: ") + QString::fromUtf8(e.what()).simplified();
        return row;
    }
}

} // namespace

QString sourceName(const maptype::IMapType& map) {
    return map.guiConfig()->name()->value();
}

QString guidText(const maptype::IMapType& map) {
    return map.zmp()->guid().toString(QUuid::WithBraces).toUpper();
}

std::vector<maptype::IMapTypePtr> matchingSources(const AppContext& app, const QString& filterText) {
    const maptype::IMapTypeSetPtr maps = app.fullMapsSet();
    const QString filter = filterText.toLower();
    std::vector<maptype::IMapTypePtr> found;
    for (int i = 0; i < maps->count(); ++i) {
        const maptype::IMapTypePtr map = maps->item(i);
        if (filter.isEmpty() || sourceName(*map).toLower().contains(filter) ||
            map->zmp()->fileName().toLower().contains(filter) || guidText(*map).toLower().contains(filter)) {
            found.push_back(map);
        }
    }
    std::ranges::sort(found, [](const maptype::IMapTypePtr& a, const maptype::IMapTypePtr& b) {
        const int byName = QString::compare(sourceName(*a), sourceName(*b), Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : guidText(*a) < guidText(*b);
    });
    return found;
}

std::vector<ProbeRow> probeSources(const AppContext& app, const Job& job, const std::stop_token& stop,
                                   std::chrono::milliseconds timeout) {
    Job verdictJob = job;
    verdictJob.zooms.clear(); // the verdict is about the map, not about a zoom list (as `sources`)
    std::vector<ProbeRow> rows;
    for (const maptype::IMapTypePtr& map : matchingSources(app, job.filter)) {
        if (stop.stop_requested()) {
            break;
        }
        ProbeRow row;
        const bool layer = map->zmp()->isLayer();
        std::optional<QString> reason = refusalForExport(*map, verdictJob, layer);
        if (!reason) {
            reason = refusalForDownload(*map);
        }
        if (!reason) {
            reason = refusalForSnapshot(*map); // S-14 as in `sources` (T-0244)
        }
        if (reason) {
            row.status = ProbeStatus::Skipped;
            row.result = QStringLiteral("skipped: ") + reason->simplified();
        } else {
            // T-0244 (weather-sources.md 3.5): a snapshot source resolves first; the store file is not
            // written. Unreachable error: refusalForSnapshot above refused an invalid definition.
            const auto definition = snapshotDefinition(*map);
            std::optional<QString> failure;
            if (definition && definition->has_value()) {
                const auto taken = resolveSnapshot(**definition, *map, app, stop);
                if (!taken) {
                    failure = taken.error().code() == core::ErrorCode::Cancelled
                                  ? QStringLiteral("interrupted") // as probeOne's cancel path
                                  : Tr::tr("cannot take the snapshot of \"%1\": %2")
                                        .arg(sourceName(*map), taken.error().what());
                } else {
                    applySnapshot(*map, taken->version);
                }
            }
            if (failure) {
                row.result = *failure;
            } else {
                row = probeOne(*map, job, stop, timeout);
            }
        }
        row.guid = guidText(*map);
        row.name = sourceName(*map);
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace cg::app
