// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Export/MBTiles/u_ExportTaskToMBTiles.pas
#include "cg/region/export_task_to_mbtiles.h"

#include "cg/region/log_region.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <utility>
#include <vector>

namespace cg::region {

namespace {

// ExtractFilePath: everything up to and including the last separator (:138).
// A bare file name gives '' as in legacy; processRegion then fails like Delphi's ForceDirectories('')
// (EInOutError, SCannotCreateDir = "Unable to create directory" in the Delphi RTL; the RTL text could not
// be taken from the legacy tree, it is the RTL's own resource string). The bare name CAN arrive
// (fr_ExportMBTiles.pas:554 accepts what IsValidFileName, u_FileSystemFunc.pas:80-102, accepts).
QString extractFilePath(const QString& path) {
    if (!path.contains(u'/')) {
        return {};
    }
    QString dir = QFileInfo(path).path();
    if (!dir.endsWith(u'/')) {
        dir += u'/';
    }
    return dir;
}

} // namespace

ExportTaskToMbtiles::ExportTaskToMbtiles(std::shared_ptr<IRegionProcessProgressInfoInternal> progressInfo,
                                         const QString& exportPath,
                                         std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory,
                                         std::shared_ptr<const geo::IGeometryLonLatPolygon> polygon,
                                         std::span<const quint8> zoomArr,
                                         std::shared_ptr<storage::ITileStorage> tileStorage,
                                         std::shared_ptr<const storage::MapVersionRequest> mapVersion,
                                         bool directTilesCopy, bool useXYZScheme,
                                         bool makeTileMillCompatibility, const QString& name,
                                         const QString& description, const QString& attribution, bool isLayer,
                                         const QString& imgFormat, bool forceDropTarget, bool replaceTiles)
    : ExportTaskAbstract(std::move(progressInfo), std::move(polygon), zoomArr,
                         std::move(tileIteratorFactory)),
      tileStorage_(std::move(tileStorage)), mapVersion_(std::move(mapVersion)),
      directTilesCopy_(directTilesCopy), exportPath_(extractFilePath(exportPath)) { // :138
    const QString exportFileName = QFileInfo(exportPath).fileName();                // :139
    if (makeTileMillCompatibility) {                                                // :123-127, :146
        sqliteStorage_ = std::make_unique<SqliteStorageMbtilesTileMill>(
            exportPath_, exportFileName, name, description, attribution, isLayer, imgFormat, useXYZScheme,
            forceDropTarget, replaceTiles); // :148-160
    } else {
        sqliteStorage_ = std::make_unique<SqliteStorageMbtilesClassic>(
            exportPath_, exportFileName, name, description, attribution, isLayer, imgFormat, useXYZScheme,
            forceDropTarget, replaceTiles);
    }
}

core::Result<void> ExportTaskToMbtiles::processRegion() {
    if (auto r = ExportTaskAbstract::processRegion(); !r) { // :186 inherited
        return r;
    }

    const bool doDirectCopy = directTilesCopy_ && tileStorage_ != nullptr; // :188
    if (!doDirectCopy) {
        // DC-2: the recompose branch (:190-193, :207-212, :241, :252-263) is absent.
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("MBTiles export with tile re-encoding is not ported"));
    }
    // unreachable in legacy (u_ExportProviderMBTiles.pas:215-218 sets the storage and the version together)
    if (!mapVersion_) {
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("MBTiles export without a map version"));
    }

    if (exportPath_.isEmpty()) { // ForceDirectories('') raises EInOutError (:195-199)
        return core::fail(core::ErrorCode::IoError, QStringLiteral("Unable to create directory"));
    }
    if (!QDir(exportPath_).exists() && !QDir().mkpath(exportPath_)) { // :195-199
        return core::fail(core::ErrorCode::IoError, exportPath_);
    }

    std::vector<geo::TileIteratorPtr> tileIterators; // :201
    qint64 tilesToProcess = 0;
    const geo::IProjectionSet& projectionSet = tileStorage_->projectionSet(); // :206, no projection check
    for (const quint8 zoom : zooms_) {                                        // :214-218
        tileIterators.push_back(makeTileIterator(projectionSet.zoom(zoom)));
        tilesToProcess += tileIterators.back()->tilesTotal();
    }

    {
        // GetLonLatRect :280-290: the tile-aligned rectangle of the lowest zoom
        const geo::TileRect& tilesRect = tileIterators[0]->tilesRect();
        const geo::LonLatRect lonLatRect = tilesRect.projection()->tileRect2LonLatRect(tilesRect.rect());
        // An error is returned at once, close is NOT called (:220-228: the try/finally starts after Open).
        if (auto r = sqliteStorage_->open(lonLatRect, zooms_); !r) {
            return r;
        }
    }

    // :230 from here close runs on every path
    core::Result<void> result;
    progressInfo().setCaption(QCoreApplication::translate("cg::region", "Export"));
    progressInfo().setFirstLine(QCoreApplication::translate("cg::region", "Total to save:") + u' ' +
                                QString::number(tilesToProcess) + u' ' +
                                QCoreApplication::translate("cg::region", "files")); // :231-234
    qint64 tilesProcessed = 0;
    progressFormUpdateOnProgress(tilesProcessed, tilesToProcess);

    for (std::size_t i = 0; i < zooms_.size() && result; ++i) {
        const quint8 zoom = zooms_[i];
        geo::ITileIterator& tileIterator = *tileIterators[i];
        geo::PointI tile;
        while (result && tileIterator.next(tile)) {
            if (stopToken().stop_requested()) { // :244-246
                result = core::fail(core::ErrorCode::Cancelled, QString());
                break;
            }
            // A Result error is the port's form of a legacy exception out of GetTileInfoEx (:249): it
            // ends the loop. Values that do not exist (Unknown, NotExists, TneExists) are written nowhere.
            auto info = tileStorage_->tileInfoEx(tile, zoom, *mapVersion_, storage::LoadMode::WithData);
            if (!info) {
                result = core::fail(info.error().code(), info.error().what());
                break;
            }
            if (info->isExists()) {
                if (auto r = sqliteStorage_->add(tile, zoom, info->data); !r) { // :250
                    result = r;
                    break;
                }
            }

            ++tilesProcessed; // :265
            if (tilesProcessed % 100 == 0) {
                progressFormUpdateOnProgress(tilesProcessed, tilesToProcess);
            }
            if (tilesProcessed % 10000 == 0) {
                result = sqliteStorage_->commitAndBeginTran();
            }
        }
    }

    if (auto closeResult = sqliteStorage_->close(); !closeResult) { // :275-277
        if (result) {
            result = closeResult;
        } else {
            qCWarning(logRegion) << "MBTiles close failed after an earlier error:"
                                 << closeResult.error().what();
        }
    }
    return result;
}

} // namespace cg::region
