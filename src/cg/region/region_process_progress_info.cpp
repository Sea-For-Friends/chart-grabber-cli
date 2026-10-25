// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/region/region_process_progress_info.h"

#include <mutex>
#include <utility>

namespace cg::region {

RegionProcessProgressInfo::RegionProcessProgressInfo(std::stop_token stop)
    : stop_(std::move(stop)) {} // :70-80

double RegionProcessProgressInfo::processedRatio() const {
    return processedRatio_.load(); // :112-115
}

bool RegionProcessProgressInfo::finished() const {
    return finished_.load(); // :97-100
}

QString RegionProcessProgressInfo::caption() const { // :92-95
    const core::ReadLock guard(stringsMutex_);
    return caption_;
}

QString RegionProcessProgressInfo::firstLine() const { // :102-105
    const core::ReadLock guard(stringsMutex_);
    return firstLine_;
}

QString RegionProcessProgressInfo::secondLine() const { // :117-120
    const core::ReadLock guard(stringsMutex_);
    return secondLine_;
}

std::stop_token RegionProcessProgressInfo::stopToken() const {
    return stop_; // GetCancelNotifier :87-90 + GetOperationID :107-110
}

void RegionProcessProgressInfo::setProcessedRatio(double value) { // :132-141
    // Order of the tests is the legacy one: a NaN fails both and is stored as is.
    if (value < 0) {
        processedRatio_.store(0.0);
    } else if (value > 1) {
        processedRatio_.store(1.0);
    } else {
        processedRatio_.store(value);
    }
}

void RegionProcessProgressInfo::finish() {
    finished_.store(true);
} // :82-85

void RegionProcessProgressInfo::setCaption(const QString& value) { // :122-125
    const core::WriteLock guard(stringsMutex_);
    caption_ = value;
}

void RegionProcessProgressInfo::setFirstLine(const QString& value) { // :127-130
    const core::WriteLock guard(stringsMutex_);
    firstLine_ = value;
}

void RegionProcessProgressInfo::setSecondLine(const QString& value) { // :143-146
    const core::WriteLock guard(stringsMutex_);
    secondLine_ = value;
}

} // namespace cg::region
