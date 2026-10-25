// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/zmp_config.h"

namespace cg::maptype {

ZmpConfig::ZmpConfig() = default;
ZmpConfig::~ZmpConfig() = default;

quint32 ZmpConfig::maxConnectToServerCount() const {
    const auto guard = readLock();
    return maxConnectToServerCount_;
}

void ZmpConfig::setMaxConnectToServerCount(quint32 v) {
    const auto guard = writeLock();
    if (maxConnectToServerCount_ != v) {
        maxConnectToServerCount_ = v;
        setChanged();
    }
}

bool ZmpConfig::useMemCache() const {
    const auto guard = readLock();
    return useMemCache_;
}

void ZmpConfig::setUseMemCache(bool v) {
    const auto guard = writeLock();
    if (useMemCache_ != v) {
        useMemCache_ = v;
        setChanged();
    }
}

qint32 ZmpConfig::memCacheCapacity() const {
    const auto guard = readLock();
    return memCacheCapacity_;
}

void ZmpConfig::setMemCacheCapacity(qint32 v) {
    const auto guard = writeLock();
    if (memCacheCapacity_ != v) {
        memCacheCapacity_ = v;
        setChanged();
    }
}

quint32 ZmpConfig::memCacheTtl() const {
    const auto guard = readLock();
    return memCacheTtl_;
}

void ZmpConfig::setMemCacheTtl(quint32 v) {
    const auto guard = writeLock();
    if (memCacheTtl_ != v) {
        memCacheTtl_ = v;
        setChanged();
    }
}

qint32 ZmpConfig::memCacheClearStrategy() const {
    const auto guard = readLock();
    return memCacheClearStrategy_;
}

void ZmpConfig::setMemCacheClearStrategy(qint32 v) {
    const auto guard = writeLock();
    if (memCacheClearStrategy_ != v) {
        memCacheClearStrategy_ = v;
        setChanged();
    }
}

// u_ZmpConfig.pas DoReadConfig: the fields are assigned directly (no compare) and a non-null
// provider always ends with SetChanged. The Cardinal fields travel through ReadInteger, so a
// negative INI value wraps to a large unsigned one, as in Delphi.
void ZmpConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    maxConnectToServerCount_ = static_cast<quint32>(
        src->readInt(u"MaxConnectToServerCount", static_cast<qint32>(maxConnectToServerCount_)));
    useMemCache_ = src->readBool(u"UseMemCache", useMemCache_);
    memCacheCapacity_ = src->readInt(u"MemCacheCapacity", memCacheCapacity_);
    memCacheTtl_ = static_cast<quint32>(src->readInt(u"MemCacheTTL", static_cast<qint32>(memCacheTtl_)));
    memCacheClearStrategy_ = src->readInt(u"MemCacheClearStrategy", memCacheClearStrategy_);
    setChanged();
}

// All five keys are written unconditionally.
void ZmpConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    if (!dst) { // unreachable in legacy (u_MapTypesMainList.pas:503-504 always passes a writer)
        return;
    }
    dst->writeInt(u"MaxConnectToServerCount", static_cast<qint32>(maxConnectToServerCount_));
    dst->writeBool(u"UseMemCache", useMemCache_);
    dst->writeInt(u"MemCacheCapacity", memCacheCapacity_);
    dst->writeInt(u"MemCacheTTL", static_cast<qint32>(memCacheTtl_));
    dst->writeInt(u"MemCacheClearStrategy", memCacheClearStrategy_);
}

} // namespace cg::maptype
