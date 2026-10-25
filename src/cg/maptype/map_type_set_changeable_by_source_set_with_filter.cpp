// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_set_changeable_by_source_set_with_filter.h"

#include "cg/maptype/zmp_info.h"

#include <QString>

#include <stdexcept>
#include <utility>

namespace cg::maptype {

MapTypeSetChangeableBySourceSetWithFilter::MapTypeSetChangeableBySourceSetWithFilter(
    std::shared_ptr<const IMapTypeSetBuilderFactory> factory,
    std::shared_ptr<IMapTypeSetChangeable> sourceSet)
    : factory_(std::move(factory)), sourceSet_(std::move(sourceSet)) {
    if (!factory_ || !sourceSet_) {
        throw std::invalid_argument("MapTypeSetChangeableBySourceSetWithFilter: a collaborator is null");
    }
}

// :98-106 -- the member `subscription_` is destroyed first and removes the listener.
MapTypeSetChangeableBySourceSetWithFilter::~MapTypeSetChangeableBySourceSetWithFilter() = default;

void MapTypeSetChangeableBySourceSetWithFilter::subscribe() {
    // :91-92 add the listener first, so that no change is lost.
    subscription_ = sourceSet_->changed().subscribe([weak = weak_from_this()] {
        if (const auto self = weak.lock()) {
            self->onActiveMapsSetChange();
        }
    });
    // :94-95, under the write lock (legacy does this without CS: legacy-defects row of T-0146).
    const core::WriteLock lock(mutex_);
    prevSourceSetStatic_ = sourceSet_->getStatic();
    static_ = createStatic();
}

IMapTypeSetPtr MapTypeSetChangeableBySourceSetWithFilter::createStatic() const {
    const std::unique_ptr<IMapTypeSetBuilder> result = factory_->build(false); // :114
    if (prevSourceSetStatic_ != nullptr) {
        for (int i = 0; i < prevSourceSetStatic_->count(); ++i) {
            const IMapTypePtr mapType = prevSourceSetStatic_->item(i);
            if (isValidMapType(mapType)) {
                result->add(mapType);
            }
        }
    }
    return result->makeAndClear();
}

IMapTypeSetPtr MapTypeSetChangeableBySourceSetWithFilter::getStatic() const {
    const core::ReadLock lock(mutex_);
    return static_;
}

bool MapTypeSetChangeableBySourceSetWithFilter::isValidMapType(const IMapTypePtr&) const {
    return true;
}

void MapTypeSetChangeableBySourceSetWithFilter::onActiveMapsSetChange() {
    bool changed = false;
    {
        const core::WriteLock lock(mutex_);
        const IMapTypeSetPtr newSet = sourceSet_->getStatic(); // :152
        if (prevSourceSetStatic_ != nullptr && prevSourceSetStatic_->isEqual(newSet)) {
            return; // :153-155
        }
        // Null items cannot occur: unreachable, the source sets are built with Build(False),
        // u_MapTypeSetChangeable.pas:144,234. The scans skip them anyway.
        if (prevSourceSetStatic_ != nullptr) {
            for (int i = 0; i < prevSourceSetStatic_->count(); ++i) {
                const IMapTypePtr mapType = prevSourceSetStatic_->item(i);
                if (mapType != nullptr &&
                    (newSet == nullptr || newSet->mapTypeByGuid(mapType->guid()) == nullptr)) {
                    if (isValidMapType(mapType)) {
                        changed = true; // :156-165
                    }
                }
            }
        }
        if (newSet != nullptr) {
            for (int i = 0; i < newSet->count(); ++i) {
                const IMapTypePtr mapType = newSet->item(i);
                if (mapType != nullptr && (prevSourceSetStatic_ == nullptr ||
                                           prevSourceSetStatic_->mapTypeByGuid(mapType->guid()) == nullptr)) {
                    if (isValidMapType(mapType)) {
                        changed = true; // :166-175
                    }
                }
            }
        }
        prevSourceSetStatic_ = newSet; // :176, always
        if (changed) {
            static_ = createStatic(); // :177-179
        }
    }
    if (changed) {
        fireChanged(); // :183-185, after the lock is released
    }
}

bool MapTypeSetChangeableBySourceSetWithFilterBitmap::isValidMapType(const IMapTypePtr& mapType) const {
    return mapType != nullptr && mapType->isBitmapTiles();
}

bool MapTypeSetChangeableBySourceSetWithFilterVector::isValidMapType(const IMapTypePtr& mapType) const {
    return mapType != nullptr && mapType->isKmlTiles();
}

bool MapTypeSetChangeableBySourceSetWithFilterLicenseNotEmpty::isValidMapType(
    const IMapTypePtr& mapType) const {
    if (mapType == nullptr) {
        return false;
    }
    const auto zmp = mapType->zmp();
    if (zmp == nullptr || zmp->license() == nullptr) { // unreachable: the source sets are built with
                                                       // Build(False), u_MapTypeSetChangeable.pas:144,234
        return false;
    }
    return !zmp->license()->getDefault().isEmpty(); // :213
}

} // namespace cg::maptype
