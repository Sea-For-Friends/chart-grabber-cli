// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: i_InetConfig.pas, u_InetConfig.pas:94-176,263-343, u_InetConfigStatic.pas,
// c_InetConfig.pas:26-28. docs/specs/net.md §2.2. FEAT-NET-007.
#include "cg/net/inet_config.h"

#include "cg/core/config_element_complex.h"

#include <utility>

namespace cg::net {

InetConfig::InetConfig() : proxy_(std::make_shared<ProxyConfig>()) {
    // Ports Add(FProxyConfig, TConfigSaveLoadStrategyBasicProviderSubItem.Create('Proxy'))
    // (u_InetConfig.pas:107-108). Legacy's 2-arg Add() overload defaults every ChildFlags bool to
    // True (propagate read/write lock + batch + listen), but nothing in this port ever calls a
    // child accessor from inside InetConfig's own writeLock() scope, and core's own test suite
    // (config_element_complex_test.cpp, "finding 3") demonstrates that all-true propagation
    // self-deadlocks the instant that happens on our non-recursive mutex. listenToChanges is the
    // one flag with an observable effect here (the composite InetSettings snapshot must be rebuilt
    // when Proxy changes, matching the legacy comment "the downloader list is rebuilt on change"),
    // so it stays true; the lock/batch propagation flags stay at their safe, core.md-documented
    // default of false. See docs/handoffs/T-0011A.md.
    cg::core::ConfigElementChildFlags flags;
    flags.listenToChanges = true;
    addChild(proxy_, cg::core::SaveLoadSlot{QStringLiteral("Proxy")}, flags);

    doBeforeChangeNotify(); // seed the snapshot; no setChanged() here (mirrors proxy_config.cpp)
}

InetConfig::~InetConfig() = default;

std::shared_ptr<const InetSettings> InetConfig::settings() const {
    return settings_.get();
}

ProxyConfig& InetConfig::proxy() noexcept {
    return *proxy_;
}

QByteArray InetConfig::userAgent() const {
    const auto guard = readLock();
    return userAgent_;
}

void InetConfig::setUserAgent(QByteArray v) {
    const auto guard = writeLock();
    if (userAgent_ == v) {
        return;
    }
    userAgent_ = std::move(v);
    setChanged();
}

quint32 InetConfig::timeoutMs() const {
    const auto guard = readLock();
    return timeoutMs_;
}

void InetConfig::setTimeoutMs(quint32 v) {
    const auto guard = writeLock();
    if (timeoutMs_ == v) {
        return;
    }
    timeoutMs_ = v;
    setChanged();
}

quint32 InetConfig::sleepOnResetConnectionMs() const {
    const auto guard = readLock();
    return sleepOnResetConnectionMs_;
}

void InetConfig::setSleepOnResetConnectionMs(quint32 v) {
    const auto guard = writeLock();
    if (sleepOnResetConnectionMs_ == v) {
        return;
    }
    sleepOnResetConnectionMs_ = v;
    setChanged();
}

qint32 InetConfig::downloadTryCount() const {
    const auto guard = readLock();
    return downloadTryCount_;
}

void InetConfig::setDownloadTryCount(qint32 v) {
    const auto guard = writeLock();
    // 1..99 only (u_InetConfig.pas:263-264: "if (AValue > 0) and (AValue < 100)"); out-of-range
    // values are silently ignored, not clamped.
    if (v < 1 || v > 99) {
        return;
    }
    if (downloadTryCount_ == v) {
        return;
    }
    downloadTryCount_ = v;
    setChanged();
}

bool InetConfig::ignoreTlsErrors() const {
    const auto guard = readLock();
    return ignoreTlsErrors_;
}

void InetConfig::setIgnoreTlsErrors(bool v) {
    const auto guard = writeLock();
    if (ignoreTlsErrors_ == v) {
        return;
    }
    ignoreTlsErrors_ = v;
    setChanged();
}

void InetConfig::doReadConfig(const cg::core::IConfigProviderPtr& src) {
    cg::core::ConfigElementComplex::doReadConfig(src); // forwards to Proxy first (legacy `inherited`)
    if (!src) {
        return;
    }
    userAgent_ = src->readString(u"UserAgentString", QString::fromUtf8(userAgent_)).toUtf8();
    timeoutMs_ = static_cast<quint32>(src->readInt(u"TimeOut", static_cast<qint32>(timeoutMs_)));
    // Applied through the same 1..99 bounds check as the public setter (u_InetConfig.pas:154).
    const qint32 tryCount = src->readInt(u"DownloadTryCount", downloadTryCount_);
    if (tryCount >= 1 && tryCount <= 99) {
        downloadTryCount_ = tryCount;
    }
    sleepOnResetConnectionMs_ = static_cast<quint32>(
        src->readInt(u"SleepOnResetConnection", static_cast<qint32>(sleepOnResetConnectionMs_)));
    // [D-1]: read and discarded, never written -- belongs to the retired WinInet engine split.
    [[maybe_unused]] const qint32 networkEngine = src->readInt(u"NetworkEngine", 1);
    ignoreTlsErrors_ = src->readBool(u"IgnoreTlsErrors", ignoreTlsErrors_); // [D-2] new key
    setChanged(); // unconditional, matches u_InetConfig.pas:161 (SetChanged after every read)
}

void InetConfig::doWriteConfig(const cg::core::IConfigWriterPtr& dst) const {
    cg::core::ConfigElementComplex::doWriteConfig(dst); // forwards to Proxy first (legacy `inherited`)
    if (!dst) {
        return;
    }
    dst->writeString(u"UserAgentString", QString::fromUtf8(userAgent_));
    dst->writeInt(u"TimeOut", static_cast<qint32>(timeoutMs_));
    dst->writeInt(u"DownloadTryCount", downloadTryCount_);
    dst->writeInt(u"SleepOnResetConnection", static_cast<qint32>(sleepOnResetConnectionMs_));
    dst->writeBool(u"IgnoreTlsErrors", ignoreTlsErrors_);
    // NetworkEngine, BrowserEngine, PreInitBrowserEngine: never written here ([D-1]; the latter two
    // belong to ui/ADR-0008 and are not read here either).
}

void InetConfig::doBeforeChangeNotify() {
    settings_.set(std::make_shared<const InetSettings>(InetSettings{proxy_->settings(), userAgent_,
                                                                    timeoutMs_, sleepOnResetConnectionMs_,
                                                                    downloadTryCount_, ignoreTlsErrors_}));
}

} // namespace cg::net
