// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Download/u_DownloadSession.pas (constructors :155-199, _InitSession :201-228,
// getters and setters :480-663). Save and load are in download_session_save_load.cpp.

#include "cg/region/download_session.h"

#include <utility>

namespace cg::region {

namespace {

// legacy Point(-1, -1): the "no point yet" value of both points
const geo::PointI kNoPoint{-1, -1};

} // namespace

DownloadSession::DownloadSession() {
    initSession();
}

DownloadSession::DownloadSession(maptype::IMapTypePtr mapType, maptype::MapVersionRequestPtr versionForCheck,
                                 storage::MapVersionInfoPtr versionForDownload, quint8 zoom,
                                 std::vector<quint8> zoomArr, PolygonPtr polygon, bool secondLoadTne,
                                 storage::TileDate replaceTneOlderDate, bool replaceExistTiles,
                                 bool checkExistTileSize, bool checkExistTileDate,
                                 storage::TileDate checkTileDate, bool autoCloseAtFinish,
                                 int autoSaveInterval, QString autosavePrefix, int workersCount,
                                 int workerIndex) {
    initSession();

    mapType_ = std::move(mapType);
    versionForCheck_ = std::move(versionForCheck);
    versionForDownload_ = std::move(versionForDownload);
    zoom_ = zoom;
    zoomArr_ = std::move(zoomArr); // legacy Copy(AZoomArr)
    replaceExistTiles_ = replaceExistTiles;
    checkExistTileSize_ = checkExistTileSize;
    checkExistTileDate_ = checkExistTileDate;
    checkTileDate_ = std::move(checkTileDate);
    secondLoadTne_ = secondLoadTne;
    replaceTneOlderDate_ = std::move(replaceTneOlderDate);
    polygon_ = std::move(polygon);
    workersCount_ = workersCount;
    workerIndex_ = workerIndex;
    autoCloseAtFinish_ = autoCloseAtFinish;
    autoSaveInterval_ = autoSaveInterval;
    autosavePrefix_ = std::move(autosavePrefix);
}

void DownloadSession::initSession() {
    mapType_.reset();
    versionForCheck_.reset();
    versionForDownload_.reset();
    zoom_ = 0;
    zoomArr_.clear();
    polygon_.reset();
    secondLoadTne_ = false;
    // legacy NaN: an invalid TileDate is the "off" value of the replace-older date
    replaceTneOlderDate_ = storage::TileDate();
    replaceExistTiles_ = false;
    checkExistTileSize_ = false;
    checkExistTileDate_ = false;
    checkTileDate_ = storage::tileDateNow(); // legacy Now
    elapsedTime_ = 0;
    processed_ = 0;
    processedFromLastSuccessfulPoint_ = 0;
    downloadedSize_ = 0;
    downloadedCount_ = 0;
    lastProcessedPoint_ = kNoPoint;
    lastProcessedCount_ = 0;
    lastSuccessfulPoint_ = kNoPoint;
    autoCloseAtFinish_ = false;
    autoSaveInterval_ = 0;
    autosavePrefix_.clear();
    workerIndex_ = 0;
    workersCount_ = 1;
}

maptype::IMapTypePtr DownloadSession::mapType() const {
    return mapType_;
}

// FEAT-REG-020
maptype::MapVersionRequestPtr DownloadSession::versionForCheck() const {
    return versionForCheck_;
}

// FEAT-REG-020
storage::MapVersionInfoPtr DownloadSession::versionForDownload() const {
    return versionForDownload_;
}

quint8 DownloadSession::zoom() const {
    return zoom_;
}

void DownloadSession::setZoom(quint8 value) {
    if (zoom_ != value) {
        zoom_ = value;
        lastSuccessfulPoint_ = kNoPoint;
        lastProcessedPoint_ = kNoPoint;
        processedFromLastSuccessfulPoint_ = 0;
        lastProcessedCount_ = 0;
    }
}

std::vector<quint8> DownloadSession::zoomArr() const {
    return zoomArr_;
}

PolygonPtr DownloadSession::polygon() const {
    return polygon_;
}

bool DownloadSession::secondLoadTne() const {
    return secondLoadTne_;
}

storage::TileDate DownloadSession::replaceTneOlderDate() const {
    return replaceTneOlderDate_;
}

bool DownloadSession::replaceExistTiles() const {
    return replaceExistTiles_;
}

bool DownloadSession::checkExistTileSize() const {
    return checkExistTileSize_;
}

bool DownloadSession::checkExistTileDate() const {
    return checkExistTileDate_;
}

storage::TileDate DownloadSession::checkTileDate() const {
    return checkTileDate_;
}

qint64 DownloadSession::processed() const {
    return processed_;
}

void DownloadSession::setProcessed(qint64 value) {
    processed_ = value;
}

quint64 DownloadSession::downloadedSize() const {
    return downloadedSize_;
}

void DownloadSession::setDownloadedSize(quint64 value) {
    downloadedSize_ = value;
}

qint64 DownloadSession::downloadedCount() const {
    return downloadedCount_;
}

void DownloadSession::setDownloadedCount(qint64 value) {
    downloadedCount_ = value;
}

geo::PointI DownloadSession::lastProcessedPoint() const {
    return lastProcessedPoint_;
}

void DownloadSession::setLastProcessedPoint(geo::PointI value) {
    lastProcessedPoint_ = value;
    if (lastSuccessfulPoint_ == lastProcessedPoint_) {
        processedFromLastSuccessfulPoint_ = 0;
    } else {
        ++processedFromLastSuccessfulPoint_;
    }
    ++lastProcessedCount_;
}

qint64 DownloadSession::lastProcessedCount() const {
    return lastProcessedCount_;
}

geo::PointI DownloadSession::lastSuccessfulPoint() const {
    return lastSuccessfulPoint_;
}

void DownloadSession::setLastSuccessfulPoint(geo::PointI value) {
    lastSuccessfulPoint_ = value;
    if (lastSuccessfulPoint_ == lastProcessedPoint_) {
        processedFromLastSuccessfulPoint_ = 0;
    }
}

double DownloadSession::elapsedTime() const {
    return elapsedTime_;
}

void DownloadSession::setElapsedTime(double value) {
    elapsedTime_ = value;
}

// FEAT-REG-015
bool DownloadSession::autoCloseAtFinish() const {
    return autoCloseAtFinish_;
}

// FEAT-REG-015
void DownloadSession::setAutoCloseAtFinish(bool value) {
    autoCloseAtFinish_ = value;
}

int DownloadSession::workersCount() const {
    return workersCount_;
}

int DownloadSession::workerIndex() const {
    return workerIndex_;
}

QString DownloadSession::autosavePrefix() const {
    return autosavePrefix_;
}

void DownloadSession::setAutosavePrefix(const QString& value) {
    autosavePrefix_ = value;
}

int DownloadSession::autosaveInterval() const {
    return autoSaveInterval_;
}

void DownloadSession::setAutosaveInterval(int value) {
    autoSaveInterval_ = value;
}

} // namespace cg::region
