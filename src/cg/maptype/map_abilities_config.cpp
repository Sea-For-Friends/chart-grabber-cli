// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_abilities_config.h"

#include <utility>

namespace cg::maptype {

MapAbilitiesConfig::MapAbilitiesConfig(MapAbilitiesConfigStaticPtr defaults)
    : defaults_(std::move(defaults)), isShowOnSmMap_(defaults_->isShowOnSmMap),
      useDownload_(defaults_->useDownload) {
    doBeforeChangeNotify(); // seed the snapshot (legacy: AfterConstruction); no setChanged()
}

MapAbilitiesConfig::~MapAbilitiesConfig() = default;

bool MapAbilitiesConfig::isShowOnSmMap() const {
    const auto guard = readLock();
    return isShowOnSmMap_;
}

void MapAbilitiesConfig::setIsShowOnSmMap(bool v) {
    const auto guard = writeLock();
    setIsShowOnSmMapLocked(v);
}

bool MapAbilitiesConfig::useDownload() const {
    const auto guard = readLock();
    return useDownload_;
}

void MapAbilitiesConfig::setUseDownload(bool v) {
    const auto guard = writeLock();
    setUseDownloadLocked(v);
}

MapAbilitiesConfigStaticPtr MapAbilitiesConfig::getStatic() const {
    return snapshot_.get();
}

void MapAbilitiesConfig::setIsShowOnSmMapLocked(bool v) {
    if (isShowOnSmMap_ != v) {
        isShowOnSmMap_ = v;
        setChanged();
    }
}

void MapAbilitiesConfig::setUseDownloadLocked(bool v) {
    const bool value = defaults_->useDownload && v;
    if (useDownload_ != value) {
        useDownload_ = value;
        setChanged();
    }
}

// A non-null provider always ends with setChanged(), even when nothing changed (spec §2.7.1).
void MapAbilitiesConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    setIsShowOnSmMapLocked(src->readBool(u"CanShowOnSmMap", isShowOnSmMap_));
    setUseDownloadLocked(src->readBool(u"UseDwn", useDownload_));
    setChanged();
}

// Differs-rule: a value equal to the ZMP's is deleted from the section, not written.
void MapAbilitiesConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // unreachable in legacy (u_MapTypesMainList.pas:503-504 always passes a writer)
        return;
    }
    if (isShowOnSmMap_ != defaults_->isShowOnSmMap) {
        dst->writeBool(u"CanShowOnSmMap", isShowOnSmMap_);
    } else {
        dst->deleteValue(u"CanShowOnSmMap");
    }
    if (useDownload_ != defaults_->useDownload) {
        dst->writeBool(u"UseDwn", useDownload_);
    } else {
        dst->deleteValue(u"UseDwn");
    }
}

void MapAbilitiesConfig::doBeforeChangeNotify() {
    snapshot_.set(std::make_shared<const MapAbilitiesConfigStatic>(
        MapAbilitiesConfigStatic{isShowOnSmMap_, useDownload_}));
}

} // namespace cg::maptype
