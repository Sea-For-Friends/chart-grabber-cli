// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_set_changeable_simple.h"

#include <utility>

namespace cg::maptype {

MapTypeSetChangeableSimple::MapTypeSetChangeableSimple(
    std::shared_ptr<const IMapTypeSetBuilderFactory> factory, const IMapTypeSetPtr& current)
    : factory_(std::move(factory)) {
    setStatic(current);
}

MapTypeSetChangeableSimple::~MapTypeSetChangeableSimple() = default;

IMapTypeSetPtr MapTypeSetChangeableSimple::getStatic() const {
    return static_.get();
}

void MapTypeSetChangeableSimple::setStatic(const IMapTypeSetPtr& value) {
    if (value == nullptr) {
        static_.set(factory_->build(false)->makeAndClear()); // legacy :84-85
    } else {
        static_.set(value);
    }
}

} // namespace cg::maptype
