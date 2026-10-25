// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapVersionChangeable.pas, u_MapVersionChangeable.pas
//
// Yields the storage::MapVersionInfo of a MapVersionConfig built by the current factory
// (spec maptype.md section 2.9). Legacy never instantiates it; see L-152 for the unassigned
// static, reproduced here.

#include "cg/core/changeable.h"
#include "cg/core/notifier.h"
#include "cg/core/sync.h"
#include "cg/maptype/map_version_config.h"
#include "cg/maptype/map_version_factory_changeable.h"

#include <memory>

namespace cg::maptype {

// FEAT-NET-002
class MapVersionChangeable final : public core::Changeable {
public:
    MapVersionChangeable(std::shared_ptr<MapVersionConfig> config,
                         std::shared_ptr<MapVersionFactoryChangeable> versionFactory);
    ~MapVersionChangeable() override;

    [[nodiscard]] storage::MapVersionInfoPtr getStatic() const;

private:
    void onFactoryChange();
    void onConfigChange();
    void update(); // OnFactoryChange and OnConfigChange have the same body in legacy

    std::shared_ptr<MapVersionConfig> config_;
    std::shared_ptr<MapVersionFactoryChangeable> versionFactory_;
    mutable core::Mutex mutex_;
    storage::MapVersionInfoPtr static_; // guarded by mutex_
    core::Subscription factorySubscription_;
    core::Subscription configSubscription_;
};

} // namespace cg::maptype
