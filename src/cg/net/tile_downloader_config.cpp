// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: i_TileDownloaderConfig.pas, u_TileDownloaderConfig.pas:102-179,331-410,
// u_TileDownloaderConfigStatic.pas. docs/specs/net.md §2.16.
#include "cg/net/tile_downloader_config.h"

#include <utility>

namespace cg::net {

TileDownloaderConfig::TileDownloaderConfig(std::shared_ptr<InetConfig> inetConfig,
                                           TileDownloaderSettings zmpDefaults)
    : inetConfig_(std::move(inetConfig)), zmpDefault_(std::move(zmpDefaults)), enabled_(zmpDefault_.enabled),
      allowUseCookie_(zmpDefault_.allowUseCookie), waitIntervalMs_(zmpDefault_.waitIntervalMs),
      maxConnectToServerCount_(zmpDefault_.maxConnectToServerCount),
      ignoreMimeType_(zmpDefault_.ignoreMimeType), detectMimeType_(zmpDefault_.detectMimeType),
      expectedMimeTypes_(zmpDefault_.expectedMimeTypes), defaultMimeType_(zmpDefault_.defaultMimeType),
      iteratorSubRectSize_(zmpDefault_.iteratorSubRectSize),
      restartDownloadOnMemCacheTTL_(zmpDefault_.restartDownloadOnMemCacheTTL) {
    // Ports Add(FIntetConfig, nil, False, False, False, True) (u_TileDownloaderConfig.pas:122):
    // strategy nil means the base class's DoReadConfig/DoWriteConfig loop skips this item entirely
    // (TConfigDataElementComplexBase.DoReadConfig/.DoWriteConfig both guard on
    // `VItem.SaveLoadStrategy <> nil`) -- InetConfig is persisted at the app level, not underneath
    // a per-map tile downloader section. Only the change-listen flag was set, which is exactly what
    // addChangeable() ports (the IChangeable Add() overload, always False,False,False,True).
    addChangeable(*inetConfig_);
    doBeforeChangeNotify(); // seed the snapshot; no setChanged() here (mirrors proxy_config.cpp)
}

TileDownloaderConfig::~TileDownloaderConfig() = default;

std::shared_ptr<const TileDownloaderSettings> TileDownloaderConfig::settings() const {
    return settings_.get();
}

bool TileDownloaderConfig::enabled() const {
    const auto guard = readLock();
    return enabled_;
}

void TileDownloaderConfig::setEnabled(bool v) {
    const auto guard = writeLock();
    // A map disabled in its ZMP can never be enabled locally (u_TileDownloaderConfig.pas:331-348).
    const bool coerced = zmpDefault_.enabled ? v : false;
    if (enabled_ == coerced) {
        return;
    }
    enabled_ = coerced;
    setChanged();
}

bool TileDownloaderConfig::allowUseCookie() const {
    const auto guard = readLock();
    return allowUseCookie_;
}

void TileDownloaderConfig::setAllowUseCookie(bool v) {
    const auto guard = writeLock();
    if (allowUseCookie_ == v) {
        return;
    }
    allowUseCookie_ = v;
    setChanged();
}

quint32 TileDownloaderConfig::waitIntervalMs() const {
    const auto guard = readLock();
    return waitIntervalMs_;
}

void TileDownloaderConfig::setWaitIntervalMs(quint32 v) {
    const auto guard = writeLock();
    if (waitIntervalMs_ == v) {
        return;
    }
    waitIntervalMs_ = v;
    setChanged();
}

quint32 TileDownloaderConfig::maxConnectToServerCount() const {
    const auto guard = readLock();
    return maxConnectToServerCount_;
}

void TileDownloaderConfig::setMaxConnectToServerCount(quint32 v) {
    const auto guard = writeLock();
    setMaxConnectToServerCountLocked(v);
}

void TileDownloaderConfig::setMaxConnectToServerCountLocked(quint32 v) {
    // The 1..64 clamp is not a constant to "improve" (ticket notes): >64 -> 64, ==0 -> 1.
    quint32 clamped = v;
    if (clamped > 64) {
        clamped = 64;
    }
    if (clamped == 0) {
        clamped = 1;
    }
    if (maxConnectToServerCount_ == clamped) {
        return;
    }
    maxConnectToServerCount_ = clamped;
    setChanged();
}

bool TileDownloaderConfig::ignoreMimeType() const {
    const auto guard = readLock();
    return ignoreMimeType_;
}

void TileDownloaderConfig::setIgnoreMimeType(bool v) {
    const auto guard = writeLock();
    if (ignoreMimeType_ == v) {
        return;
    }
    ignoreMimeType_ = v;
    setChanged();
}

bool TileDownloaderConfig::detectMimeType() const {
    const auto guard = readLock();
    return detectMimeType_;
}

void TileDownloaderConfig::setDetectMimeType(bool v) {
    const auto guard = writeLock();
    if (detectMimeType_ == v) {
        return;
    }
    detectMimeType_ = v;
    setChanged();
}

QByteArray TileDownloaderConfig::expectedMimeTypes() const {
    const auto guard = readLock();
    return expectedMimeTypes_;
}

void TileDownloaderConfig::setExpectedMimeTypes(QByteArray v) {
    const auto guard = writeLock();
    if (expectedMimeTypes_ == v) {
        return;
    }
    expectedMimeTypes_ = std::move(v);
    setChanged();
}

QByteArray TileDownloaderConfig::defaultMimeType() const {
    const auto guard = readLock();
    return defaultMimeType_;
}

void TileDownloaderConfig::setDefaultMimeType(QByteArray v) {
    const auto guard = writeLock();
    if (defaultMimeType_ == v) {
        return;
    }
    defaultMimeType_ = std::move(v);
    setChanged();
}

QPoint TileDownloaderConfig::iteratorSubRectSize() const {
    const auto guard = readLock();
    return iteratorSubRectSize_;
}

void TileDownloaderConfig::setIteratorSubRectSize(QPoint v) {
    const auto guard = writeLock();
    if (iteratorSubRectSize_ == v) {
        return;
    }
    iteratorSubRectSize_ = v;
    setChanged();
}

bool TileDownloaderConfig::restartDownloadOnMemCacheTTL() const {
    const auto guard = readLock();
    return restartDownloadOnMemCacheTTL_;
}

void TileDownloaderConfig::setRestartDownloadOnMemCacheTTL(bool v) {
    const auto guard = writeLock();
    if (restartDownloadOnMemCacheTTL_ == v) {
        return;
    }
    restartDownloadOnMemCacheTTL_ = v;
    setChanged();
}

void TileDownloaderConfig::doReadConfig(const cg::core::IConfigProviderPtr& src) {
    cg::core::ConfigElementComplex::doReadConfig(src); // legacy `inherited`; no persisted children
    if (!src) {
        return;
    }
    // enabled and iteratorSubRectSize are NEVER read from the local INI -- ZMP-only (spec table).
    allowUseCookie_ = src->readBool(u"AllowUseCookie", allowUseCookie_);
    ignoreMimeType_ = src->readBool(u"IgnoreContentType", ignoreMimeType_);
    detectMimeType_ = src->readBool(u"DetectContentType", detectMimeType_);
    defaultMimeType_ = src->readString(u"DefaultContentType", QString::fromUtf8(defaultMimeType_)).toUtf8();
    expectedMimeTypes_ = src->readString(u"ContentType", QString::fromUtf8(expectedMimeTypes_)).toUtf8();
    waitIntervalMs_ = static_cast<quint32>(src->readInt(u"Sleep", static_cast<qint32>(waitIntervalMs_)));
    setMaxConnectToServerCountLocked(static_cast<quint32>(
        src->readInt(u"MaxConnectToServerCount", static_cast<qint32>(maxConnectToServerCount_))));
    restartDownloadOnMemCacheTTL_ =
        src->readBool(u"RestartDownloadOnMemCacheTTL", restartDownloadOnMemCacheTTL_);
    setChanged(); // unconditional, matches u_TileDownloaderConfig.pas:160 (SetChanged after every read)
}

void TileDownloaderConfig::doWriteConfig(const cg::core::IConfigWriterPtr& dst) const {
    cg::core::ConfigElementComplex::doWriteConfig(dst); // legacy `inherited`; no persisted children
    if (!dst) {
        return;
    }
    // Differential: only these two keys are ever written, and only when they differ from the ZMP
    // baseline -- every other key is read-only-from-INI (u_TileDownloaderConfig.pas:163-179). This
    // asymmetry matters: persisting the other six keys would make a later ZMP update stop applying.
    if (waitIntervalMs_ != zmpDefault_.waitIntervalMs) {
        dst->writeInt(u"Sleep", static_cast<qint32>(waitIntervalMs_));
    } else {
        dst->deleteValue(u"Sleep");
    }
    if (maxConnectToServerCount_ != zmpDefault_.maxConnectToServerCount) {
        dst->writeInt(u"MaxConnectToServerCount", static_cast<qint32>(maxConnectToServerCount_));
    } else {
        dst->deleteValue(u"MaxConnectToServerCount");
    }
}

void TileDownloaderConfig::doBeforeChangeNotify() {
    settings_.set(std::make_shared<const TileDownloaderSettings>(
        TileDownloaderSettings{inetConfig_->settings(), enabled_, allowUseCookie_, waitIntervalMs_,
                               maxConnectToServerCount_, ignoreMimeType_, detectMimeType_, expectedMimeTypes_,
                               defaultMimeType_, iteratorSubRectSize_, restartDownloadOnMemCacheTTL_}));
}

} // namespace cg::net
