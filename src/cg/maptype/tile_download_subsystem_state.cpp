// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/tile_download_subsystem_state.h"

#include <QString>

#include <stdexcept>
#include <utility>

namespace cg::maptype {

namespace {

TileDownloaderStateStaticPtr makeStatic(bool enabled, const QString& reason) {
    return std::make_shared<const TileDownloaderStateStatic>(TileDownloaderStateStatic{enabled, reason});
}

} // namespace

TileDownloadSubsystemState::TileDownloadSubsystemState(
    bool zmpDownloadEnabled, std::shared_ptr<ITileDownloaderStateChangeable> requestBuilderState,
    std::shared_ptr<ITileDownloaderStateChangeable> saverState,
    std::shared_ptr<MapAbilitiesConfig> mapAbilitiesConfig)
    : zmpDownloadEnabled_(zmpDownloadEnabled) {
    if (zmpDownloadEnabled_) { // :67-77: the sources are kept only when the ZMP allows downloading
        if (!requestBuilderState || !saverState || !mapAbilitiesConfig) {
            throw std::invalid_argument("TileDownloadSubsystemState: a source of an enabled state is null");
        }
        requestBuilderState_ = std::move(requestBuilderState);
        saverState_ = std::move(saverState);
        mapAbilitiesConfig_ = std::move(mapAbilitiesConfig);
    }
    static_ = createStatic(); // AfterConstruction of the WithStatic base
}

void TileDownloadSubsystemState::subscribe() {
    if (!zmpDownloadEnabled_) {
        return;
    }
    auto slot = [weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->onSourceChange();
        }
    };
    subscriptions_.push_back(saverState_->changed().subscribe(slot));
    subscriptions_.push_back(requestBuilderState_->changed().subscribe(slot));
    subscriptions_.push_back(mapAbilitiesConfig_->changed().subscribe(slot));
}

TileDownloaderStateStaticPtr TileDownloadSubsystemState::getStatic() const {
    const core::ReadLock lock(mutex_);
    return static_;
}

void TileDownloadSubsystemState::onSourceChange() {
    fireChanged();
}

void TileDownloadSubsystemState::doBeforeChangeNotify() {
    const core::WriteLock lock(mutex_);
    static_ = createStatic();
}

TileDownloaderStateStaticPtr TileDownloadSubsystemState::createStatic() const {
    if (!zmpDownloadEnabled_) {
        return makeStatic(false, QStringLiteral("Disabled by Zmp")); // :88
    }
    if (!mapAbilitiesConfig_->useDownload()) {
        return makeStatic(false, QStringLiteral("Disabled by map params")); // :92
    }
    const TileDownloaderStateStaticPtr builder = requestBuilderState_->getStatic();
    if (!builder->enabled) {
        return makeStatic(false, builder->disableReason); // :94-96
    }
    const TileDownloaderStateStaticPtr saver = saverState_->getStatic();
    if (!saver->enabled) {
        return makeStatic(false, saver->disableReason); // :98-100
    }
    return makeStatic(true, QString()); // :102
}

} // namespace cg::maptype
