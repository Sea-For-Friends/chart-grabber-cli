// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/region/region_process_progress_info_download.h"

#include "cg/storage/tile_date.h"

#include <utility>

namespace cg::region {

RegionProcessProgressInfoDownload::RegionProcessProgressInfoDownload(
    std::shared_ptr<core::LogRingBuffer> log, std::shared_ptr<IDownloadSession> session, bool paused,
    std::function<double()> now)
    : now_(std::move(now)), log_(std::move(log)), session_(std::move(session)), needPause_(paused),
      paused_(paused), startTime_(this->now()) {
} // :128-145, FEAT-REG-013: needPause_ / paused_ start as `paused`

double RegionProcessProgressInfoDownload::now() const {
    // Legacy Now: the wall clock, so a clock change shifts the elapsed time (reproduced).
    return now_ ? now_() : storage::toDelphiDateTime(storage::tileDateNow());
}

void RegionProcessProgressInfoDownload::addDownloadedTile(geo::PointI tile, quint32 size) { // :147-161
    const core::WriteLock guard(mutex_);
    lastSuccessfulSize_ = static_cast<int>(size);
    session_->setLastSuccessfulPoint(tile);
    session_->setDownloadedSize(session_->downloadedSize() + size);
    session_->setDownloadedCount(session_->downloadedCount() + 1);
}

void RegionProcessProgressInfoDownload::addManyProcessedTile(geo::PointI lastTile,
                                                             quint32 count) { // :163-175
    const core::WriteLock guard(mutex_);
    session_->setLastProcessedPoint(lastTile);
    session_->setProcessed(session_->processed() + count);
}

void RegionProcessProgressInfoDownload::addNotNecessaryTile(geo::PointI tile) { // :177-188
    const core::WriteLock guard(mutex_);
    session_->setLastSuccessfulPoint(tile);
    lastSuccessfulSize_ = -1;
}

void RegionProcessProgressInfoDownload::addProcessedTile(geo::PointI tile) { // :190-201
    const core::WriteLock guard(mutex_);
    session_->setLastProcessedPoint(tile);
    session_->setProcessed(session_->processed() + 1);
}

void RegionProcessProgressInfoDownload::finish() { // :203-211
    // No finish reason: docs/specs/region-download.md OQ-5, not ruled.
    const core::WriteLock guard(mutex_);
    finished_ = true;
}

qint64 RegionProcessProgressInfoDownload::downloaded() const { // :213-221
    const core::ReadLock guard(mutex_);
    return session_->downloadedCount();
}

quint64 RegionProcessProgressInfoDownload::downloadSize() const { // :223-231
    const core::ReadLock guard(mutex_);
    return session_->downloadedSize();
}

double RegionProcessProgressInfoDownload::elapsedTime() const { // :233-245
    const core::ReadLock guard(mutex_);
    if (finished_ || paused_) {
        return session_->elapsedTime();
    }
    return session_->elapsedTime() + (now() - startTime_);
}

bool RegionProcessProgressInfoDownload::finished() const { // :247-255
    const core::ReadLock guard(mutex_);
    return finished_;
}

bool RegionProcessProgressInfoDownload::isPaused() const { // :257-265
    const core::ReadLock guard(mutex_);
    return needPause_; // sic: needPause, not paused
}

std::shared_ptr<core::LogRingBuffer> RegionProcessProgressInfoDownload::log() const { // :267-270, no lock
    return log_;
}

std::shared_ptr<core::LogRingBuffer>
RegionProcessProgressInfoDownload::logProvider() const { // :272-275, no lock
    return log_;
}

bool RegionProcessProgressInfoDownload::needPause() const { // :277-285
    const core::ReadLock guard(mutex_);
    return needPause_;
}

qint64 RegionProcessProgressInfoDownload::processed() const { // :287-295
    const core::ReadLock guard(mutex_);
    return session_->processed();
}

double RegionProcessProgressInfoDownload::processedRatio() const { // :297-313
    const core::ReadLock guard(mutex_);
    if (finished_) {
        return 1;
    }
    if (totalInRegion_ == 0) {
        return 0;
    }
    if (session_->processed() > totalInRegion_) {
        return 1;
    }
    return static_cast<double>(session_->processed()) / static_cast<double>(totalInRegion_);
}

int RegionProcessProgressInfoDownload::sessionAutosaveInterval() const { // :315-323
    const core::ReadLock guard(mutex_);
    return session_->autosaveInterval();
}

QString RegionProcessProgressInfoDownload::sessionAutosavePrefix() const { // :325-333
    const core::ReadLock guard(mutex_);
    return session_->autosavePrefix();
}

qint64 RegionProcessProgressInfoDownload::totalToProcess() const { // :335-343
    const core::ReadLock guard(mutex_);
    return totalInRegion_;
}

void RegionProcessProgressInfoDownload::getWorkerInfo(int& workerIndex, int& workersCount) const { // :345-357
    const core::ReadLock guard(mutex_);
    workerIndex = session_->workerIndex();
    workersCount = session_->workersCount();
}

void RegionProcessProgressInfoDownload::setTotalToProcess(qint64 value) { // :359-367
    const core::WriteLock guard(mutex_);
    totalInRegion_ = value;
}

quint8 RegionProcessProgressInfoDownload::zoom() const { // :369-377
    const core::ReadLock guard(mutex_);
    return session_->zoom();
}

void RegionProcessProgressInfoDownload::setZoom(quint8 value) { // :379-387
    const core::WriteLock guard(mutex_);
    session_->setZoom(value);
}

qint64 RegionProcessProgressInfoDownload::connectionErrorStreak() const { // T-0271, no legacy counterpart
    return connectionErrorStreak_.load(std::memory_order_relaxed);
}

void RegionProcessProgressInfoDownload::noteDownloadAnswer(bool connectionError) { // T-0271
    if (connectionError) {
        connectionErrorStreak_.fetch_add(1, std::memory_order_relaxed);
    } else {
        connectionErrorStreak_.store(0, std::memory_order_relaxed);
    }
}

FrameAnswers RegionProcessProgressInfoDownload::frameAnswers() const { // T-0356, no legacy counterpart
    return {unavailableRun_.load(std::memory_order_relaxed), inClass_.load(std::memory_order_relaxed),
            other_.load(std::memory_order_relaxed)};
}

void RegionProcessProgressInfoDownload::noteFrameAnswer(FrameAnswer answer) { // T-0356
    if (answer == FrameAnswer::Unavailable) {
        unavailableRun_.fetch_add(1, std::memory_order_relaxed);
    } else {
        unavailableRun_.store(0, std::memory_order_relaxed);
    }
    if (answer == FrameAnswer::Other) {
        other_.fetch_add(1, std::memory_order_relaxed);
    } else {
        inClass_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::vector<quint8> RegionProcessProgressInfoDownload::zoomArray() const { // :389-397
    const core::ReadLock guard(mutex_);
    return session_->zoomArr();
}

void RegionProcessProgressInfoDownload::getLastTileInfo(quint8& zoom, geo::PointI& point,
                                                        int& size) const { // :399-417
    const core::ReadLock guard(mutex_);
    zoom = session_->zoom();
    point = session_->lastProcessedPoint();
    size = (session_->lastSuccessfulPoint() == session_->lastProcessedPoint()) ? lastSuccessfulSize_ : 0;
}

void RegionProcessProgressInfoDownload::saveState(core::IConfigWriter& slsSection) { // :419-432
    const core::WriteLock guard(mutex_);
    if (!needPause_) {
        // DEVIATION (T-0274, L-266, deviations.md): the stretch is folded in once and startTime_ restarts;
        // legacy keeps FStartTime (u_RegionProcessProgressInfoDownload.pas:419-432) and counts it again in
        // elapsedTime(). The needPause test and the finish drop stay legacy (spec region-download.md LR-1).
        const double current = now(); // one read: the stretch ends where the next one starts
        session_->setElapsedTime(session_->elapsedTime() + (current - startTime_));
        startTime_ = current;
    }
    session_->save(slsSection); // may throw: the lock is released by RAII, the added time stays (as legacy)
}

void RegionProcessProgressInfoDownload::pause() { // :434-442
    const core::WriteLock guard(mutex_);
    needPause_ = true;
}

void RegionProcessProgressInfoDownload::resume() { // :444-452
    const core::WriteLock guard(mutex_);
    needPause_ = false;
}

void RegionProcessProgressInfoDownload::setNeedPause(bool value) { // :454-462
    const core::WriteLock guard(mutex_);
    needPause_ = value;
}

void RegionProcessProgressInfoDownload::setPaused() { // :464-473, FEAT-REG-013
    const core::WriteLock guard(mutex_);
    paused_ = true;
    session_->setElapsedTime(session_->elapsedTime() + (now() - startTime_));
}

void RegionProcessProgressInfoDownload::setStarted() { // :475-484, FEAT-REG-013
    const core::WriteLock guard(mutex_);
    paused_ = false;
    startTime_ = now();
}

void RegionProcessProgressInfoDownload::setAutoCloseAtFinish(bool value) { // :486-496
    const core::WriteLock guard(mutex_);
    session_->setAutoCloseAtFinish(value);
}

bool RegionProcessProgressInfoDownload::autoCloseAtFinish() const { // :498-506
    const core::ReadLock guard(mutex_);
    return session_->autoCloseAtFinish();
}

} // namespace cg::region
