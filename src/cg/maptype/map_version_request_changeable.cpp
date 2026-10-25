// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_version_request_changeable.h"

#include <utility>

namespace cg::maptype {

MapVersionRequestChangeable::MapVersionRequestChangeable(
    std::shared_ptr<MapVersionRequestConfig> config,
    std::shared_ptr<MapVersionFactoryChangeable> versionFactory)
    : config_(std::move(config)), versionFactory_(std::move(versionFactory)) {
    factorySubscription_ = versionFactory_->changed().subscribe([this] { onFactoryChange(); });
    configSubscription_ = config_->changed().subscribe([this] { onConfigChange(); });
    onFactoryChange();
}

MapVersionRequestChangeable::~MapVersionRequestChangeable() {
    factorySubscription_.reset();
    configSubscription_.reset();
}

std::shared_ptr<const storage::MapVersionRequest> MapVersionRequestChangeable::getStatic() const {
    const core::ReadLock lock(mutex_);
    return static_;
}

void MapVersionRequestChangeable::onFactoryChange() {
    update();
}

void MapVersionRequestChangeable::onConfigChange() {
    update();
}

void MapVersionRequestChangeable::update() {
    {
        const core::WriteLock lock(mutex_);
        const MapVersionRequestConfigStaticPtr cfg = config_->getStatic();
        static_ = std::make_shared<const storage::MapVersionRequest>(storage::MapVersionRequest{
            versionFactory_->getStatic()->byStoreString(cfg->version), cfg->showOtherVersions});
    }
    fireChanged();
}

} // namespace cg::maptype
