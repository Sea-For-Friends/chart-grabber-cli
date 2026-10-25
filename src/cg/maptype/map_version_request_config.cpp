// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_version_request_config.h"

#include <utility>

namespace cg::maptype {

MapVersionRequestConfig::MapVersionRequestConfig(QString version)
    : versionDef_(version), version_(std::move(version)) {
    doBeforeChangeNotify(); // seed the snapshot (legacy: AfterConstruction); no setChanged()
}

MapVersionRequestConfig::~MapVersionRequestConfig() = default;

QString MapVersionRequestConfig::version() const {
    const auto guard = readLock();
    return version_;
}

void MapVersionRequestConfig::setVersion(const QString& v) {
    const auto guard = writeLock();
    if (version_ != v) {
        version_ = v;
        setChanged();
    }
}

bool MapVersionRequestConfig::showOtherVersions() const {
    const auto guard = readLock();
    return showOtherVersions_;
}

void MapVersionRequestConfig::setShowOtherVersions(bool v) {
    const auto guard = writeLock();
    if (showOtherVersions_ != v) {
        showOtherVersions_ = v;
        setChanged();
    }
}

MapVersionRequestConfigStaticPtr MapVersionRequestConfig::getStatic() const {
    return snapshot_.get();
}

// A non-null provider always ends with setChanged(), even when nothing changed.
void MapVersionRequestConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    version_ = src->readString(u"Version", version_);
    showOtherVersions_ = src->readBool(u"ShowOtherVersions", showOtherVersions_);
    setChanged();
}

void MapVersionRequestConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // defensive; unreachable in legacy (u_MapTypesMainList.pas:503-504 always passes a writer)
        return;
    }
    if (version_ != versionDef_) {
        dst->writeString(u"Version", version_);
    } else {
        dst->deleteValue(u"Version");
    }
    dst->writeBool(u"ShowOtherVersions", showOtherVersions_); // always written
}

void MapVersionRequestConfig::doBeforeChangeNotify() {
    snapshot_.set(std::make_shared<const MapVersionRequestConfigStatic>(
        MapVersionRequestConfigStatic{version_, showOtherVersions_}));
}

} // namespace cg::maptype
