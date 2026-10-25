// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: i_ProxyConfig.pas, u_ProxyConfig.pas:114-202,279-365. docs/specs/net.md §2.1.
// FEAT-NET-008.
#include "cg/net/proxy_config.h"

#include "cg/core/log.h"

namespace cg::net {

QByteArray proxySchemePrefix(ProxyType t) {
    switch (t) {
    case ProxyType::Http:
        return "";
    case ProxyType::Https:
        return "https://";
    case ProxyType::Socks4:
        return "socks4://";
    case ProxyType::Socks4a:
        return "socks4a://";
    case ProxyType::Socks5:
        return "socks5://";
    case ProxyType::Socks5h:
        return "socks5h://";
    }
    return ""; // unreachable for a valid ProxyType, but keeps -Wreturn-type happy
}

ProxyConfig::ProxyConfig() {
    doBeforeChangeNotify(); // seed the snapshot; no setChanged() here, mirrors path_config.cpp
}

ProxyConfig::~ProxyConfig() = default;

std::shared_ptr<const ProxySettings> ProxyConfig::settings() const {
    return settings_.get();
}

bool ProxyConfig::useSystemSettings() const {
    const ReadGuard guard = readLock();
    return useSystemSettings_;
}

void ProxyConfig::setUseSystemSettings(bool v) {
    const WriteGuard guard = writeLock();
    if (useSystemSettings_ == v) {
        return;
    }
    useSystemSettings_ = v;
    setChanged(); // always, on an actual change (spec table)
}

bool ProxyConfig::useProxy() const {
    const ReadGuard guard = readLock();
    return useProxy_;
}

void ProxyConfig::setUseProxy(bool v) {
    const WriteGuard guard = writeLock();
    if (useProxy_ == v) {
        return;
    }
    useProxy_ = v;
    if (!useSystemSettings_) {
        setChanged();
    }
}

ProxyType ProxyConfig::type() const {
    const ReadGuard guard = readLock();
    return type_;
}

void ProxyConfig::setType(ProxyType v) {
    const WriteGuard guard = writeLock();
    if (type_ == v) {
        return;
    }
    type_ = v;
    setChanged(); // always, on an actual change (spec table)
}

QByteArray ProxyConfig::host() const {
    const ReadGuard guard = readLock();
    return host_;
}

void ProxyConfig::setHost(QByteArray v) {
    const WriteGuard guard = writeLock();
    if (host_ == v) {
        return;
    }
    host_ = std::move(v);
    if (!useSystemSettings_ && useProxy_) {
        setChanged();
    }
}

bool ProxyConfig::useLogin() const {
    const ReadGuard guard = readLock();
    return useLogin_;
}

void ProxyConfig::setUseLogin(bool v) {
    const WriteGuard guard = writeLock();
    if (useLogin_ == v) {
        return;
    }
    useLogin_ = v;
    if (!useSystemSettings_ && useProxy_) {
        setChanged();
    }
}

QString ProxyConfig::login() const {
    const ReadGuard guard = readLock();
    return login_;
}

void ProxyConfig::setLogin(QString v) {
    const WriteGuard guard = writeLock();
    if (login_ == v) {
        return;
    }
    login_ = std::move(v);
    if (!useSystemSettings_ && useProxy_ && useLogin_) {
        setChanged();
    }
}

QString ProxyConfig::password() const {
    const ReadGuard guard = readLock();
    return password_;
}

void ProxyConfig::setPassword(QString v) {
    const WriteGuard guard = writeLock();
    if (password_ == v) {
        return;
    }
    password_ = std::move(v);
    if (!useSystemSettings_ && useProxy_ && useLogin_) {
        setChanged();
    }
}

QByteArray ProxyConfig::normalizeAddress(const QByteArray& raw) {
    QByteArray v = raw.trimmed();
    const qsizetype eq = v.indexOf('=');
    if (eq >= 0) {
        v = v.mid(eq + 1);
    }
    const qsizetype scheme = v.indexOf("://");
    if (scheme >= 0) {
        v = v.mid(scheme + 3);
    }
    return v;
}

void ProxyConfig::doReadConfig(const cg::core::IConfigProviderPtr& src) {
    if (!src) {
        return;
    }
    // AnsiString INI fields (Host, and the QByteArray members of the other config classes) are
    // read/written through IConfigProvider's QString-based readString/writeString and converted
    // with toUtf8()/fromUtf8() -- there is no separate AnsiString accessor in IConfigProvider, and
    // every such field here is ASCII (host:port, UA string, MIME token), so UTF-8 round-trips it
    // byte-for-byte.
    useSystemSettings_ = src->readBool(u"UseIEProxySettings", useSystemSettings_);
    useProxy_ = src->readBool(u"UseProxy", useProxy_);
    host_ = normalizeAddress(src->readString(u"Host", QString::fromUtf8(host_)).toUtf8());
    useLogin_ = src->readBool(u"UseAuth", useLogin_);
    login_ = src->readString(u"Login", login_);
    password_ = src->readString(u"Password", password_);

    const qint32 rawType = src->readInt(u"ProxyType", static_cast<qint32>(type_));
    if (rawType >= static_cast<qint32>(ProxyType::Http) &&
        rawType <= static_cast<qint32>(ProxyType::Socks5h)) {
        type_ = static_cast<ProxyType>(rawType);
    } else {
        qCWarning(cg::core::logNet) << "ProxyConfig: unexpected ProxyType value:" << rawType;
    }
    setChanged(); // unconditional, matches u_ProxyConfig.pas:186 (SetChanged after every read)
}

void ProxyConfig::doWriteConfig(const cg::core::IConfigWriterPtr& dst) const {
    if (!dst) {
        return;
    }
    dst->writeBool(u"UseIEProxySettings", useSystemSettings_);
    dst->writeBool(u"UseProxy", useProxy_);
    dst->writeString(u"Host", QString::fromUtf8(host_)); // written raw: no normalizeAddress (asymmetry, kept)
    dst->writeBool(u"UseAuth", useLogin_);
    dst->writeString(u"Login", login_);
    dst->writeString(u"Password", password_); // [D-8] plaintext, as in legacy
    dst->writeInt(u"ProxyType", static_cast<qint32>(type_));
}

void ProxyConfig::doBeforeChangeNotify() {
    settings_.set(std::make_shared<const ProxySettings>(
        ProxySettings{useSystemSettings_, useProxy_, type_, host_, useLogin_, login_, password_}));
}

} // namespace cg::net
