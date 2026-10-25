// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the portable half of Src/Network/Curl/u_CurlProxyResolver.pas:479-551 (GetProxyAuthenticateInfo);
// replaces u_InternetExplorerProxy.pas and u_WindowsCredentials.pas. docs/specs/net.md §2.18.
// FEAT-NET-008.
//
// CurlProxy is spec'd (net.md §2.8, :603-609) to live in curl_session.h, owned by T-0010B, which
// has not landed in this worktree yet (T-0011A's own header, proxy_resolver.h, is the only other
// place this ticket's public API needs it). It is defined here, field-for-field identical to the
// spec, as the canonical definition until T-0010B lands; that ticket's curl_session.h must then
// #include this header for CurlProxy rather than redefine it (or the two must be reconciled some
// other way) -- see docs/handoffs/T-0011A.md open questions.

#include "cg/net/proxy_config.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <curl/curl.h>
#include <functional>
#include <memory>
#include <optional>

namespace cg::net {

struct CurlProxy {       // ports TCurlProxy, t_CurlHttpClient.pas:59-68 -- see the header comment above
    QByteArray address;  // scheme prefix + host:port, or empty for "no proxy"
    QByteArray noProxy;  // CURLOPT_NOPROXY bypass list
    QByteArray userName; // URL-encoded by the session, not by the caller
    QByteArray userPass;
    long authMask = CURLAUTH_ANY;

    [[nodiscard]] bool operator==(const CurlProxy&) const = default;
};

struct ProxyAuthChallenge {
    QByteArray scheme;
    QString realm;
    long curlAuthMask = CURLAUTH_ANY;
};

class IProxyResolver {
public:
    virtual ~IProxyResolver() = default;

    // Fills address/noProxy for the "use system settings" case. Returning false means "no proxy"
    // (curl then honours the environment itself).
    virtual bool resolve(QByteArrayView url, CurlProxy& out) = 0;

    // Ports GetProxyAuthenticateInfo (u_CurlProxyResolver.pas:479-551): parses the
    // Proxy-Authenticate header(s) of a 407 response.
    [[nodiscard]] static std::optional<ProxyAuthChallenge> parseChallenge(QByteArrayView rawHeader);

    // Ports DoResolveProxyAuth: prompt once, cache process-wide keyed by proxy address. `ui`
    // installs the real prompt; headless builds and tests install one that returns false (or,
    // equivalently, install none at all -- see credentialsFor()).
    using CredentialFn = std::function<bool(const QString& description, QString& user, QString& pass)>;
    virtual void setCredentialPrompt(CredentialFn fn) = 0;
    virtual bool credentialsFor(QByteArrayView proxyAddress, const QString& description, QByteArray& user,
                                QByteArray& pass) = 0;
};

// The Linux implementation: environment variables first, then QNetworkProxyFactory. The one place
// in cg::net's transport that touches Qt6::Network (tech-lead-rulings.md 2026-09-17 net.md §10,
// point 4); kept in its own translation unit so the QNAM-IN-DOWNLOADER validate.sh rule stays simple.
[[nodiscard]] std::shared_ptr<IProxyResolver> makeSystemProxyResolver();

} // namespace cg::net
