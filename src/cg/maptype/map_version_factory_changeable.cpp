// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_version_factory_changeable.h"

#include <utility>

namespace cg::maptype {

MapVersionFactoryChangeable::MapVersionFactoryChangeable(storage::MapVersionFactoryPtr defFactory)
    : factory_(std::move(defFactory)) {}

MapVersionFactoryChangeable::~MapVersionFactoryChangeable() = default;

storage::MapVersionFactoryPtr MapVersionFactoryChangeable::getStatic() const {
    const core::ReadLock lock(mutex_);
    return factory_;
}

void MapVersionFactoryChangeable::setFactory(const storage::MapVersionFactoryPtr& value) {
    bool needNotify = false;
    {
        const core::WriteLock lock(mutex_);
        if (value == nullptr) { // IsSameFactoryClass(nil) is False; any other factory is the same class
            factory_ = value;
            needNotify = true;
        }
    }
    if (needNotify) {
        fireChanged();
    }
}

} // namespace cg::maptype
