// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapVersionRequestChangeable.pas, u_MapVersionRequestChangeable.pas
//
// Yields the storage::MapVersionRequest (base version built by the current factory plus the
// show-other-versions flag); rebuilt and notified on every config or factory change, also when
// nothing differs (spec maptype.md section 2.9).

#include "cg/core/changeable.h"
#include "cg/core/notifier.h"
#include "cg/core/sync.h"
#include "cg/maptype/map_version_factory_changeable.h"
#include "cg/maptype/map_version_request_config.h"

#include <memory>

namespace cg::maptype {

// FEAT-NET-002
class MapVersionRequestChangeable final : public core::Changeable {
public:
    MapVersionRequestChangeable(std::shared_ptr<MapVersionRequestConfig> config,
                                std::shared_ptr<MapVersionFactoryChangeable> versionFactory);
    ~MapVersionRequestChangeable() override;

    [[nodiscard]] std::shared_ptr<const storage::MapVersionRequest> getStatic() const;

private:
    void onFactoryChange();
    void onConfigChange();
    void update(); // OnFactoryChange and OnConfigChange have the same body in legacy

    std::shared_ptr<MapVersionRequestConfig> config_;
    std::shared_ptr<MapVersionFactoryChangeable> versionFactory_;
    mutable core::Mutex mutex_;
    std::shared_ptr<const storage::MapVersionRequest> static_; // guarded by mutex_
    core::Subscription factorySubscription_;
    core::Subscription configSubscription_;
};

} // namespace cg::maptype
