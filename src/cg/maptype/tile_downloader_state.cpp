// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_downloader_state.h"

namespace cg::maptype {

TileDownloaderStateInternal::TileDownloaderStateInternal() : static_(createStatic()) {}

TileDownloaderStateStaticPtr TileDownloaderStateInternal::createStatic() const {
    return std::make_shared<const TileDownloaderStateStatic>(TileDownloaderStateStatic{enabled_, reason_});
}

void TileDownloaderStateInternal::disable(const QString& reason) {
    bool needNotify = false;
    {
        const core::WriteLock lock(mutex_);
        if (enabled_) { // u_TileDownloaderStateInternal.pas:97
            enabled_ = false;
            reason_ = reason;
            static_ = createStatic();
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

void TileDownloaderStateInternal::enable() {
    bool needNotify = false;
    {
        const core::WriteLock lock(mutex_);
        if (!enabled_) { // :118
            enabled_ = true;
            reason_.clear();
            static_ = createStatic();
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

QString TileDownloaderStateInternal::disableReason() const {
    const core::ReadLock lock(mutex_);
    return enabled_ ? QString() : reason_; // :136-140
}

bool TileDownloaderStateInternal::enabled() const {
    const core::ReadLock lock(mutex_);
    return enabled_;
}

TileDownloaderStateStaticPtr TileDownloaderStateInternal::getStatic() const {
    const core::ReadLock lock(mutex_);
    return static_;
}

} // namespace cg::maptype
