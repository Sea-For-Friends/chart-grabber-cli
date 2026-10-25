// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_version_config.h"

#include <utility>

namespace cg::maptype {

MapVersionConfig::MapVersionConfig(QString version) : version_(version), versionDef_(std::move(version)) {}

MapVersionConfig::~MapVersionConfig() = default;

QString MapVersionConfig::version() const {
    const auto guard = readLock();
    return version_;
}

void MapVersionConfig::setVersion(const QString& v) {
    const auto guard = writeLock();
    setVersionLocked(v);
}

void MapVersionConfig::setVersionLocked(const QString& v) {
    if (version_ != v) {
        version_ = v;
        setChanged();
    }
}

void MapVersionConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (src) {
        setVersionLocked(src->readString(u"Version", version_));
    }
}

void MapVersionConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // defensive; unreachable in legacy (u_MapTypesMainList.pas:503-504 always passes a writer)
        return;
    }
    if (version_ != versionDef_) {
        dst->writeString(u"Version", version_);
    } else {
        dst->deleteValue(u"Version");
    }
}

} // namespace cg::maptype
