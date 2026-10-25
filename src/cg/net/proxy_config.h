// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ProxyConfig.pas, u_ProxyConfig.pas:114-202,279-365. docs/specs/net.md §2.1.
// FEAT-NET-008.

#include "cg/core/config_element.h"
#include "cg/core/snapshot.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>

namespace cg::net {

// Legacy ordinals are persisted in the INI; do not renumber. i_ProxyConfig.pas:30-37
// Underlying type is std::uint8_t (not the spec's literal "int") only to satisfy clang-tidy's
// performance-enum-size; every read/write still goes through an explicit qint32 cast, so the INI
// ordinal (a Delphi Integer) is unaffected -- see docs/handoffs/T-0011A.md.
enum class ProxyType : std::uint8_t {
    Http = 0,
    Https = 1,
    Socks4 = 2,
    Socks4a = 3,
    Socks5 = 4,
    Socks5h = 5,
};

// The curl address prefix for each type. u_DownloaderHttpByCurl.pas:354-366
[[nodiscard]] QByteArray proxySchemePrefix(ProxyType t);

// Immutable snapshot (legacy IProxyConfigStatic, u_ProxyConfig.pas:34-63).
struct ProxySettings {
    bool useSystemSettings = true; // legacy UseIESettings
    bool useProxy = false;
    ProxyType type = ProxyType::Http;
    QByteArray host = "127.0.0.1:8080"; // host:port, no scheme
    bool useLogin = false;
    QString login;
    QString password;

    [[nodiscard]] bool operator==(const ProxySettings&) const = default; // ports IsEqual
};

class ProxyConfig final : public cg::core::ConfigElement {
public:
    ProxyConfig();
    ~ProxyConfig() override;

    [[nodiscard]] std::shared_ptr<const ProxySettings> settings() const; // ports GetStatic

    // Getters take readLock(); setters take writeLock(). See the notification rule below.
    [[nodiscard]] bool useSystemSettings() const;
    void setUseSystemSettings(bool v);
    [[nodiscard]] bool useProxy() const;
    void setUseProxy(bool v);
    [[nodiscard]] ProxyType type() const;
    void setType(ProxyType v);
    [[nodiscard]] QByteArray host() const;
    void setHost(QByteArray v);
    [[nodiscard]] bool useLogin() const;
    void setUseLogin(bool v);
    [[nodiscard]] QString login() const;
    void setLogin(QString v);
    [[nodiscard]] QString password() const;
    void setPassword(QString v);

    // Normalises a user-typed address to host:port. u_ProxyConfig.pas:158-176. Takes `raw` by
    // const-ref, not by value as the spec literally shows: the body never mutates or moves it
    // (clang-tidy performance-unnecessary-value-param); purely a parameter-passing convention, no
    // behavior change.
    [[nodiscard]] static QByteArray normalizeAddress(const QByteArray& raw);

protected:
    void doReadConfig(const cg::core::IConfigProviderPtr& src) override;
    void doWriteConfig(const cg::core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the ProxySettings snapshot

private:
    bool useSystemSettings_ = true;
    bool useProxy_ = false;
    ProxyType type_ = ProxyType::Http;
    QByteArray host_ = "127.0.0.1:8080";
    bool useLogin_ = false;
    QString login_;
    QString password_;

    cg::core::Snapshot<ProxySettings> settings_;
};

} // namespace cg::net
