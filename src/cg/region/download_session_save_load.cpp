// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Download/u_DownloadSession.pas (Save :230-284, Load :286-478, inner
// _CheckZoom :293-304 and _ReadZoom :305-324). The key order and the text forms follow
// docs/specs/region-download.md section 2.3.1 (`:292-352`).

#include "cg/core/config_writer.h"
#include "cg/geo/config_provider_helpers.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/map_type_set.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_changeable.h"
#include "cg/net/global_download_config.h"
#include "cg/region/download_session.h"
#include "cg/region/zoom_array_func.h"

#include <QDate>
#include <QPoint>
#include <QTime>
#include <QUuid>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cg::region {

namespace {

// legacy GUIDToString: upper case, braced
QString guidToString(const QUuid& guid) {
    return guid.toString(QUuid::WithBraces).toUpper();
}

// legacy _CheckZoom (:293-304): zero is refused; the value is decremented and must pass the projection set
void checkZoom(const geo::IProjectionSet& projectionSet, quint8& zoom) {
    if (zoom == 0) {
        throw std::runtime_error("Unknown zoom: 0");
    }
    --zoom;
    if (!projectionSet.checkZoom(zoom)) {
        throw std::runtime_error("Unknown zoom: " + std::to_string(static_cast<int>(zoom)));
    }
}

// legacy _ReadZoom (:305-324): the 1-based "Zoom" key and "ZoomArr" list, both checked and made 0-based
void readZoom(const geo::IProjectionSet& projectionSet, const core::IConfigProvider& section, quint8& vZoom,
              std::vector<quint8>& vZoomArr) {
    vZoom = static_cast<quint8>(section.readInt(u"Zoom", 0));
    checkZoom(projectionSet, vZoom);
    if (!zoomArrayFromStr(section.readString(u"ZoomArr"), vZoomArr)) {
        vZoomArr = {static_cast<quint8>(vZoom + 1)};
    }
    for (quint8& element : vZoomArr) {
        checkZoom(projectionSet, element);
    }
}

// legacy TDateTime of a date-only key: that date at 00:00, a naive local wall clock (TileDate)
storage::TileDate dateAtMidnight(const QDate& date) {
    return storage::TileDate(date, QTime(0, 0));
}

// legacy Trunc(Double) into Int64. NaN and out-of-range values give Int64.MinValue (the x86
// indefinite integer). Hand-edited files only; the legacy result for them is not traced.
qint64 truncToInt64(double value) {
    constexpr double kTwoPow63 = 9223372036854775808.0;
    const double truncated = std::trunc(value);
    if (!(truncated >= -kTwoPow63 && truncated < kTwoPow63)) {
        return std::numeric_limits<qint64>::min();
    }
    return static_cast<qint64>(truncated);
}

} // namespace

// FEAT-REG-017
void DownloadSession::save(core::IConfigWriter& sessionSection) {
    // unreachable in legacy (u_ProviderTilesDownload.pas:333, :354-355); no register row
    if (!mapType_ || !versionForDownload_) {
        throw std::logic_error("DownloadSession::save: no map type or download version");
    }

    QString vVersionForCheck;
    bool vVersionForCheckUseOther = false;
    if (versionForCheck_) {
        if (versionForCheck_->baseVersion) {
            vVersionForCheck = versionForCheck_->baseVersion->storeString();
        }
        vVersionForCheckUseOther = versionForCheck_->showOtherVersions;
    }

    if (lastSuccessfulPoint_ == lastProcessedPoint_) {
        processedFromLastSuccessfulPoint_ = 0;
    }

    sessionSection.writeString(u"MapGUID", guidToString(mapType_->guid()));
    sessionSection.writeString(u"VersionDownload", versionForDownload_->storeString());
    sessionSection.writeString(u"VersionCheck", vVersionForCheck);
    sessionSection.writeBool(u"VersionCheckOther", vVersionForCheckUseOther);
    sessionSection.writeInt(u"Zoom", zoom_ + 1);
    sessionSection.writeString(u"ZoomArr", zoomArrayToStr(zoomArr_));
    sessionSection.writeBool(u"ReplaceExistTiles", replaceExistTiles_);
    sessionSection.writeBool(u"CheckExistTileSize", checkExistTileSize_);
    sessionSection.writeBool(u"CheckExistTileDate", checkExistTileDate_);
    sessionSection.writeDate(u"CheckTileDate", checkTileDate_.date()); // date only, DateToStr
    sessionSection.writeBool(u"SecondLoadTNE", secondLoadTne_);

    if (replaceTneOlderDate_.isValid()) { // legacy `if not IsNan(...)`
        sessionSection.writeDate(u"LoadTneOlderDate", replaceTneOlderDate_.date());
    }

    // legacy WriteInteger(Integer) with an Int64 argument, range checks off: the 32-bit narrowing
    sessionSection.writeInt(u"ProcessedTileCount", static_cast<qint32>(downloadedCount_));
    sessionSection.writeInt(u"Processed", static_cast<qint32>(processed_));
    sessionSection.writeInt(u"ProcessedFromLastSuccessfulPoint",
                            static_cast<qint32>(processedFromLastSuccessfulPoint_));
    sessionSection.writeInt(u"LastProcessedCount", static_cast<qint32>(lastProcessedCount_));
    sessionSection.writeFloat(u"ProcessedSize", static_cast<double>(downloadedSize_) / 1024.0);
    sessionSection.writeInt(u"StartX", lastProcessedPoint_.x());
    sessionSection.writeInt(u"StartY", lastProcessedPoint_.y());
    sessionSection.writeInt(u"LastSuccessfulStartX", lastSuccessfulPoint_.x());
    sessionSection.writeInt(u"LastSuccessfulStartY", lastSuccessfulPoint_.y());
    sessionSection.writeFloat(u"ElapsedTime", elapsedTime_);
    sessionSection.writeBool(u"AutoCloseAtFinish", autoCloseAtFinish_);
    sessionSection.writeInt(u"AutoSaveInterval", autoSaveInterval_);
    sessionSection.writeString(u"AutoSavePrefix", autosavePrefix_);
    sessionSection.writeInt(u"WorkersCount", workersCount_);
    sessionSection.writeInt(u"WorkerIndex", workerIndex_);

    geo::writePolygon(sessionSection, polygon_);
}

// FEAT-REG-019
void DownloadSession::load(const core::IConfigProviderPtr& sessionSection,
                           const maptype::IMapTypeSet& fullMapsSet,
                           const net::IGlobalDownloadConfig& downloadConfig,
                           const geo::GeometryLonLatFactory& lonLatFactory) {
    if (!sessionSection) {
        throw std::runtime_error("No SLS data");
    }
    const core::IConfigProvider& section = *sessionSection;

    const QUuid vGuid = section.readUuid(u"MapGUID", QUuid());
    if (vGuid.isNull()) {
        throw std::runtime_error("Map GUID is empty");
    }

    const maptype::IMapTypePtr vMapType = fullMapsSet.mapTypeByGuid(vGuid);
    if (!vMapType) {
        throw std::runtime_error("Map with GUID = " + guidToString(vGuid).toStdString() + " not found");
    }

    // legacy VMapType.VersionFactory.GetStatic (:376, :389); a null static factory is not traced
    const auto createVersion = [&vMapType](const QString& text) {
        const storage::MapVersionFactoryPtr factory = vMapType->versionFactory()->getStatic();
        if (!factory) {
            throw std::logic_error("DownloadSession::load: map has no version factory");
        }
        return factory->byStoreString(text);
    };

    const QString vVersionDownloadText = section.readString(u"VersionDownload");
    const storage::MapVersionInfoPtr vVersionForDownload =
        vVersionDownloadText.isEmpty() ? vMapType->versionRequest()->getStatic()->baseVersion
                                       : createVersion(vVersionDownloadText);

    const bool vVersionCheckShowPrev = section.readBool(u"VersionCheckOther", false);
    const QString vVersionCheckText = section.readString(u"VersionCheck");
    const storage::MapVersionInfoPtr vVersionCheckBase =
        vVersionCheckText.isEmpty() ? vVersionForDownload : createVersion(vVersionCheckText);
    const maptype::MapVersionRequestPtr vVersionForCheck = std::make_shared<const storage::MapVersionRequest>(
        storage::MapVersionRequest{vVersionCheckBase, vVersionCheckShowPrev});

    quint8 vZoom = 0;
    std::vector<quint8> vZoomArr;
    readZoom(*vMapType->projectionSet(), section, vZoom, vZoomArr);

    const bool vReplaceExistTiles = section.readBool(u"ReplaceExistTiles", false);
    const bool vCheckExistTileSize = section.readBool(u"CheckExistTileSize", false);
    const bool vCheckExistTileDate = section.readBool(u"CheckExistTileDate", false);
    // legacy ReadDate('CheckTileDate', Now) (:405): an absent or unparsable key keeps Now WITH its time of
    // day
    const QDate vCheckTileDateKey = section.readDate(u"CheckTileDate", QDate());
    const storage::TileDate vCheckTileDate =
        vCheckTileDateKey.isValid() ? dateAtMidnight(vCheckTileDateKey) : storage::tileDateNow();
    qint64 vProcessed = section.readInt(u"Processed", 0);
    qint64 vProcessedFromLastSuccessfulPoint = section.readInt(u"ProcessedFromLastSuccessfulPoint", 0);
    qint64 vLastProcessedCount = section.readInt(u"LastProcessedCount", 0);
    const qint64 vProcessedTileCount = section.readInt(u"ProcessedTileCount", 0);
    const qint64 vProcessedSize = truncToInt64(section.readFloat(u"ProcessedSize", 0.0) * 1024.0);
    const bool vSecondLoadTne = section.readBool(u"SecondLoadTNE", false);
    const QDate vLoadTneOlderDateKey = section.readDate(u"LoadTneOlderDate", QDate());
    const storage::TileDate vLoadTneOlderDate =
        vLoadTneOlderDateKey.isValid() ? dateAtMidnight(vLoadTneOlderDateKey) : storage::TileDate();
    const double vElapsedTime = section.readFloat(u"ElapsedTime", 0.0);
    const bool vAutoCloseAtFinish = section.readBool(u"AutoCloseAtFinish", false);
    const int vAutoSaveInterval = section.readInt(u"AutoSaveInterval", 0);
    const QString vAutoSavePrefix = section.readString(u"AutoSavePrefix");
    const int vWorkersCount = section.readInt(u"WorkersCount", 1);
    const int vWorkerIndex = section.readInt(u"WorkerIndex", 0);

    geo::PointI vLastSuccessfulPoint{section.readInt(u"LastSuccessfulStartX", -1),
                                     section.readInt(u"LastSuccessfulStartY", -1)};
    geo::PointI vLastProcessedPoint{section.readInt(u"StartX", -1), section.readInt(u"StartY", -1)};

    if (vLastSuccessfulPoint == vLastProcessedPoint) {
        vProcessedFromLastSuccessfulPoint = 0;
    }

    if (downloadConfig.isUseSessionLastSuccess()) {
        // rollback processed point to last successful point (:430-440)
        vLastProcessedPoint = vLastSuccessfulPoint;
        if (vProcessed > 0 && vProcessedFromLastSuccessfulPoint > 0) {
            vProcessed -= vProcessedFromLastSuccessfulPoint;
            vLastProcessedCount -= vProcessedFromLastSuccessfulPoint;
            vProcessedFromLastSuccessfulPoint = 0;
        }
    }

    const PolygonPtr vPolygon = geo::readPolygon(section, lonLatFactory);
    if (!vPolygon) {
        throw std::runtime_error("Empty polygon");
    }

    // assigned only now: a throw above leaves the session untouched (:447-477)
    mapType_ = vMapType;
    versionForCheck_ = vVersionForCheck;
    versionForDownload_ = vVersionForDownload;
    zoom_ = vZoom;
    zoomArr_ = std::move(vZoomArr);
    replaceExistTiles_ = vReplaceExistTiles;
    checkExistTileSize_ = vCheckExistTileSize;
    checkExistTileDate_ = vCheckExistTileDate;
    checkTileDate_ = vCheckTileDate;
    processed_ = vProcessed;
    processedFromLastSuccessfulPoint_ = vProcessedFromLastSuccessfulPoint;
    lastProcessedCount_ = vLastProcessedCount;
    downloadedCount_ = vProcessedTileCount;
    downloadedSize_ = static_cast<quint64>(vProcessedSize);
    secondLoadTne_ = vSecondLoadTne;
    replaceTneOlderDate_ = vLoadTneOlderDate;
    elapsedTime_ = vElapsedTime;
    autoCloseAtFinish_ = vAutoCloseAtFinish;
    autoSaveInterval_ = vAutoSaveInterval;
    autosavePrefix_ = vAutoSavePrefix;
    workersCount_ = vWorkersCount;
    workerIndex_ = vWorkerIndex;
    lastProcessedPoint_ = vLastProcessedPoint;
    lastSuccessfulPoint_ = vLastSuccessfulPoint;
    polygon_ = vPolygon;
}

} // namespace cg::region
