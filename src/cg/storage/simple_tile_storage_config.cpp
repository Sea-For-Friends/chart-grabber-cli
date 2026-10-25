// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/simple_tile_storage_config.h"

#include <utility>

namespace cg::storage {

SimpleTileStorageConfig::SimpleTileStorageConfig(SimpleTileStorageConfigStaticPtr defaults)
    : defaults_(std::move(defaults)), cacheType_(defaults_->cacheType), nameInCache_(defaults_->nameInCache),
      isReadOnly_(defaults_->abilities.isReadOnly()), allowRead_(defaults_->abilities.allowRead),
      allowScan_(defaults_->abilities.allowScan), allowDelete_(defaults_->abilities.allowDelete),
      allowAdd_(defaults_->abilities.allowAdd), allowReplace_(defaults_->abilities.allowReplace),
      useMemCache_(defaults_->useMemCache), memCacheCapacity_(defaults_->memCacheCapacity),
      memCacheTtl_(defaults_->memCacheTtl), memCacheClearStrategy_(defaults_->memCacheClearStrategy) {
    doBeforeChangeNotify(); // seed the snapshot; mirrors proxy_config.cpp, no setChanged() here
}

SimpleTileStorageConfig::~SimpleTileStorageConfig() = default;

SimpleTileStorageConfigStaticPtr SimpleTileStorageConfig::snapshot() const {
    return snapshot_.get();
}

CacheTypeId SimpleTileStorageConfig::cacheType() const {
    const auto guard = readLock();
    return cacheType_;
}

void SimpleTileStorageConfig::setCacheType(CacheTypeId v) {
    const auto guard = writeLock();
    setCacheTypeLocked(v);
}

void SimpleTileStorageConfig::setCacheTypeLocked(CacheTypeId v) {
    // u_SimpleTileStorageConfig.pas:440-455: a GoogleEarth map (ZMP default is Ge) can never be
    // switched to another cache type, and no map can be switched TO Ge at runtime.
    if (defaults_->cacheType == CacheTypeId::Ge || v == CacheTypeId::Ge) {
        return;
    }
    if (cacheType_ == v) {
        return;
    }
    cacheType_ = v;
    setChanged();
}

QString SimpleTileStorageConfig::nameInCache() const {
    const auto guard = readLock();
    return nameInCache_;
}

void SimpleTileStorageConfig::setNameInCache(QString v) {
    const auto guard = writeLock();
    setNameInCacheLocked(std::move(v));
}

void SimpleTileStorageConfig::setNameInCacheLocked(QString v) {
    if (nameInCache_ == v) {
        return;
    }
    nameInCache_ = std::move(v);
    setReadOnlyLocked(isReadOnly_); // re-run the rule now that nameInCache_ (isEmpty) changed
    setChanged();
}

QByteArray SimpleTileStorageConfig::mainContentType() const noexcept {
    return defaults_->mainContentType;
}

QByteArray SimpleTileStorageConfig::tileFileExt() const noexcept {
    return defaults_->tileFileExt;
}

bool SimpleTileStorageConfig::readOnly() const {
    const auto guard = readLock();
    return isReadOnly_;
}

void SimpleTileStorageConfig::setReadOnly(bool v) {
    const auto guard = writeLock();
    setReadOnlyLocked(v);
}

void SimpleTileStorageConfig::setReadOnlyLocked(bool v) {
    const bool value = defaults_->abilities.isReadOnly() || nameInCache_.isEmpty() || v;
    if (isReadOnly_ == value) {
        return;
    }
    isReadOnly_ = value;
    // Re-apply the narrowing allow*-setters with their OWN current values now that isReadOnly_
    // changed (u_SimpleTileStorageConfig.pas:466-468: SetAllowDelete(FAllowDelete) etc.).
    setAllowDeleteLocked(allowDelete_);
    setAllowAddLocked(allowAdd_);
    setAllowReplaceLocked(allowReplace_);
    setChanged();
}

bool SimpleTileStorageConfig::allowRead() const {
    const auto guard = readLock();
    return allowRead_;
}

void SimpleTileStorageConfig::setAllowRead(bool v) {
    const auto guard = writeLock();
    setAllowReadLocked(v);
}

void SimpleTileStorageConfig::setAllowReadLocked(bool v) {
    const bool value = defaults_->abilities.allowRead && v;
    if (allowRead_ == value) {
        return;
    }
    allowRead_ = value;
    setChanged();
}

bool SimpleTileStorageConfig::allowScan() const {
    const auto guard = readLock();
    return allowScan_;
}

void SimpleTileStorageConfig::setAllowScan(bool v) {
    const auto guard = writeLock();
    setAllowScanLocked(v);
}

void SimpleTileStorageConfig::setAllowScanLocked(bool v) {
    const bool value = defaults_->abilities.allowScan && v;
    if (allowScan_ == value) {
        return;
    }
    allowScan_ = value;
    setChanged();
}

bool SimpleTileStorageConfig::allowDelete() const {
    const auto guard = readLock();
    return allowDelete_;
}

void SimpleTileStorageConfig::setAllowDelete(bool v) {
    const auto guard = writeLock();
    setAllowDeleteLocked(v);
}

void SimpleTileStorageConfig::setAllowDeleteLocked(bool v) {
    const bool value = defaults_->abilities.allowDelete && !isReadOnly_ && v;
    if (allowDelete_ == value) {
        return;
    }
    allowDelete_ = value;
    setChanged();
}

bool SimpleTileStorageConfig::allowAdd() const {
    const auto guard = readLock();
    return allowAdd_;
}

void SimpleTileStorageConfig::setAllowAdd(bool v) {
    const auto guard = writeLock();
    setAllowAddLocked(v);
}

void SimpleTileStorageConfig::setAllowAddLocked(bool v) {
    const bool value = defaults_->abilities.allowAdd && !isReadOnly_ && v;
    if (allowAdd_ == value) {
        return;
    }
    allowAdd_ = value;
    setChanged();
}

bool SimpleTileStorageConfig::allowReplace() const {
    const auto guard = readLock();
    return allowReplace_;
}

void SimpleTileStorageConfig::setAllowReplace(bool v) {
    const auto guard = writeLock();
    setAllowReplaceLocked(v);
}

void SimpleTileStorageConfig::setAllowReplaceLocked(bool v) {
    const bool value = defaults_->abilities.allowReplace && !isReadOnly_ && v;
    if (allowReplace_ == value) {
        return;
    }
    allowReplace_ = value;
    setChanged();
}

bool SimpleTileStorageConfig::useMemCache() const {
    const auto guard = readLock();
    return useMemCache_;
}

void SimpleTileStorageConfig::setUseMemCache(bool v) {
    const auto guard = writeLock();
    if (useMemCache_ == v) {
        return;
    }
    useMemCache_ = v;
    setChanged();
}

int SimpleTileStorageConfig::memCacheCapacity() const {
    const auto guard = readLock();
    return memCacheCapacity_;
}

void SimpleTileStorageConfig::setMemCacheCapacity(int v) {
    const auto guard = writeLock();
    if (memCacheCapacity_ == v) {
        return;
    }
    memCacheCapacity_ = v;
    setChanged();
}

std::chrono::milliseconds SimpleTileStorageConfig::memCacheTtl() const {
    const auto guard = readLock();
    return memCacheTtl_;
}

void SimpleTileStorageConfig::setMemCacheTtl(std::chrono::milliseconds v) {
    const auto guard = writeLock();
    if (memCacheTtl_ == v) {
        return;
    }
    memCacheTtl_ = v;
    setChanged();
}

ClearStrategy SimpleTileStorageConfig::memCacheClearStrategy() const {
    const auto guard = readLock();
    return memCacheClearStrategy_;
}

void SimpleTileStorageConfig::setMemCacheClearStrategy(ClearStrategy v) {
    const auto guard = writeLock();
    if (memCacheClearStrategy_ == v) {
        return;
    }
    memCacheClearStrategy_ = v;
    setChanged();
}

void SimpleTileStorageConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    // Ports u_SimpleTileStorageConfig.pas:160-176. Already write-locked by
    // ConfigElement::readConfig(), so every rule below goes through the "...Locked()" helpers,
    // never the public setters.
    if (!src) {
        return;
    }
    setCacheTypeLocked(static_cast<CacheTypeId>(src->readInt(u"CacheType", static_cast<qint32>(cacheType_))));
    setNameInCacheLocked(src->readString(u"NameInCache", nameInCache_));
    setReadOnlyLocked(src->readBool(u"IsReadOnly", isReadOnly_));
    setAllowReadLocked(src->readBool(u"AllowRead", allowRead_));
    setAllowScanLocked(src->readBool(u"AllowScan", allowScan_));
    setAllowAddLocked(src->readBool(u"AllowAdd", allowAdd_));
    setAllowDeleteLocked(src->readBool(u"AllowDelete", allowDelete_));
    setAllowReplaceLocked(src->readBool(u"AllowReplace", allowReplace_));
    setChanged(); // unconditional, matches u_SimpleTileStorageConfig.pas:174
}

void SimpleTileStorageConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    // Ports u_SimpleTileStorageConfig.pas:178-223: write only when it differs from the ZMP
    // default, otherwise delete the key -- so an unmodified map gains no redundant ini lines.
    if (!dst) {
        return;
    }
    if (cacheType_ != defaults_->cacheType) {
        dst->writeInt(u"CacheType", static_cast<qint32>(cacheType_));
    } else {
        dst->deleteValue(u"CacheType");
    }
    if (nameInCache_ != defaults_->nameInCache) {
        dst->writeString(u"NameInCache", nameInCache_);
    } else {
        dst->deleteValue(u"NameInCache");
    }
    if (isReadOnly_ != defaults_->abilities.isReadOnly()) {
        dst->writeBool(u"IsReadOnly", isReadOnly_);
    } else {
        dst->deleteValue(u"IsReadOnly");
    }
    if (allowRead_ != defaults_->abilities.allowRead) {
        dst->writeBool(u"AllowRead", allowRead_);
    } else {
        dst->deleteValue(u"AllowRead");
    }
    if (allowScan_ != defaults_->abilities.allowScan) {
        dst->writeBool(u"AllowScan", allowScan_);
    } else {
        dst->deleteValue(u"AllowScan");
    }
    if (allowAdd_ != defaults_->abilities.allowAdd) {
        dst->writeBool(u"AllowAdd", allowAdd_);
    } else {
        dst->deleteValue(u"AllowAdd");
    }
    if (allowDelete_ != defaults_->abilities.allowDelete) {
        dst->writeBool(u"AllowDelete", allowDelete_);
    } else {
        dst->deleteValue(u"AllowDelete");
    }
    if (allowReplace_ != defaults_->abilities.allowReplace) {
        dst->writeBool(u"AllowReplace", allowReplace_);
    } else {
        dst->deleteValue(u"AllowReplace");
    }
}

void SimpleTileStorageConfig::doBeforeChangeNotify() {
    snapshot_.set(std::make_shared<const SimpleTileStorageConfigStatic>(SimpleTileStorageConfigStatic{
        cacheType_, nameInCache_, defaults_->mainContentType, defaults_->tileFileExt,
        TileStorageAbilities{allowRead_, allowScan_, allowAdd_, allowDelete_, allowReplace_}, useMemCache_,
        memCacheCapacity_, memCacheTtl_, memCacheClearStrategy_}));
}

} // namespace cg::storage
