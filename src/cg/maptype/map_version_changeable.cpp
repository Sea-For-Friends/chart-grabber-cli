// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_version_changeable.h"

#include <utility>

namespace cg::maptype {

MapVersionChangeable::MapVersionChangeable(std::shared_ptr<MapVersionConfig> config,
                                           std::shared_ptr<MapVersionFactoryChangeable> versionFactory)
    : config_(std::move(config)), versionFactory_(std::move(versionFactory)) {
    factorySubscription_ = versionFactory_->changed().subscribe([this] { onFactoryChange(); });
    configSubscription_ = config_->changed().subscribe([this] { onConfigChange(); });
    onFactoryChange();
}

MapVersionChangeable::~MapVersionChangeable() {
    factorySubscription_.reset();
    configSubscription_.reset();
}

storage::MapVersionInfoPtr MapVersionChangeable::getStatic() const {
    const core::ReadLock lock(mutex_);
    return static_;
}

void MapVersionChangeable::onFactoryChange() {
    update();
}

void MapVersionChangeable::onConfigChange() {
    update();
}

// L-152: the freshly built version is compared with static_ but never stored into it, so
// static_ stays null, getStatic() yields nullptr and every call notifies.
void MapVersionChangeable::update() {
    bool needNotify = false;
    {
        const core::WriteLock lock(mutex_);
        const storage::MapVersionInfoPtr version =
            versionFactory_->getStatic()->byStoreString(config_->version());
        if (static_ == nullptr || !version->isSame(*static_)) { // IsSame(nil) is False
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

} // namespace cg::maptype
