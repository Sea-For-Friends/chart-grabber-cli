// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_InetConfig.pas, u_InetConfig.pas:94-176,263-343, u_InetConfigStatic.pas,
// c_InetConfig.pas:26-28. docs/specs/net.md §2.2. FEAT-NET-007.

#include "cg/core/config_element_complex.h"
#include "cg/core/snapshot.h"
#include "cg/net/proxy_config.h"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

namespace cg::net {

// The legacy default UA, verbatim from c_InetConfig.pas:26-28. Map servers key off it
// (ADR-0004 open question 3, answered: ship the same string).
inline constexpr char kDefaultUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:146.0) Gecko/20100101 Firefox/146.0";

struct InetSettings { // ports IInetConfigStatic
    std::shared_ptr<const ProxySettings> proxy;
    QByteArray userAgent = kDefaultUserAgent;
    quint32 timeoutMs = 40000; // used for BOTH total and connect timeout
    quint32 sleepOnResetConnectionMs = 30000;
    qint32 downloadTryCount = 2;
    bool ignoreTlsErrors = false; // NEW, ADR-0004 ruling Open-1 [D-2]
};

class InetConfig final : public cg::core::ConfigElementComplex {
public:
    InetConfig();
    ~InetConfig() override;

    [[nodiscard]] std::shared_ptr<const InetSettings> settings() const;
    [[nodiscard]] ProxyConfig& proxy() noexcept; // owned child, sub-item "Proxy"

    [[nodiscard]] QByteArray userAgent() const;
    void setUserAgent(QByteArray v);
    [[nodiscard]] quint32 timeoutMs() const;
    void setTimeoutMs(quint32 v);
    [[nodiscard]] quint32 sleepOnResetConnectionMs() const;
    void setSleepOnResetConnectionMs(quint32 v);
    [[nodiscard]] qint32 downloadTryCount() const;
    void setDownloadTryCount(qint32 v); // 1..99 only; other values IGNORED, not clamped
    [[nodiscard]] bool ignoreTlsErrors() const;
    void setIgnoreTlsErrors(bool v);

protected:
    void doReadConfig(const cg::core::IConfigProviderPtr& src) override;
    void doWriteConfig(const cg::core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override;

private:
    std::shared_ptr<ProxyConfig> proxy_;

    QByteArray userAgent_ = kDefaultUserAgent;
    quint32 timeoutMs_ = 40000;
    quint32 sleepOnResetConnectionMs_ = 30000;
    qint32 downloadTryCount_ = 2;
    bool ignoreTlsErrors_ = false;

    cg::core::Snapshot<InetSettings> settings_;
};

} // namespace cg::net
