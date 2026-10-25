// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/active_map_config.h"

#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"

namespace cg::maptype {

ActiveMapConfig::ActiveMapConfig(bool isAllowZeroGuid, const QUuid& initialMainMapGuid)
    : isAllowZeroGuid_(isAllowZeroGuid), mainMapGuid_(initialMainMapGuid) {}

QUuid ActiveMapConfig::mainMapGuid() const {
    const ReadGuard guard = readLock();
    return mainMapGuid_;
}

void ActiveMapConfig::setMainMapGuid(const QUuid& mapGuid) {
    const WriteGuard guard = writeLock();
    setMainMapGuidLocked(mapGuid);
}

// Ports SetMainMapGUID (u_ActiveMapConfig.pas:107-134). A zero GUID that is not allowed is ignored
// (legacy Assert at :121 is off).
void ActiveMapConfig::setMainMapGuidLocked(const QUuid& mapGuid) {
    if (mapGuid.isNull() && !isAllowZeroGuid_) {
        return;
    }
    if (mainMapGuid_ != mapGuid) {
        mainMapGuid_ = mapGuid;
        setChanged();
    }
}

// Ports DoReadConfig (:78-85). A null provider reads as an absent key: legacy ReadGUID checks for
// nil and returns the default (u_ConfigProviderHelpers.pas:438-439), so the zero branch still runs.
void ActiveMapConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    const QUuid guid = src ? src->readUuid(u"Map", QUuid()) : QUuid();
    if (guid.isNull()) {
        if (isAllowZeroGuid_) {
            setMainMapGuidLocked(guid);
        }
    } else {
        setMainMapGuidLocked(guid);
    }
}

// Ports DoWriteConfig (:93-94). writeUuid writes the same braced upper-case text as GUIDToString
// (config_writer.cpp:35-38). Always written, also the zero GUID.
void ActiveMapConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // defensive; a null writer is a legacy access violation (see deviations.md)
        return;
    }
    dst->writeUuid(u"Map", mainMapGuid_);
}

} // namespace cg::maptype
