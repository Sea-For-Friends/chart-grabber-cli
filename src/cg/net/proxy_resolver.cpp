// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: the portable half of Src/Network/Curl/u_CurlProxyResolver.pas:479-551
// (GetProxyAuthenticateInfo); replaces u_InternetExplorerProxy.pas and u_WindowsCredentials.pas.
// docs/specs/net.md §2.18. FEAT-NET-008.
#include "cg/net/proxy_resolver.h"

#include "cg/net/http_headers.h"

#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QUrl>

#include <cstdint>

namespace cg::net {

namespace {

// http_proxy/https_proxy/all_proxy/no_proxy, upper and lower case -- libcurl already reads exactly
// these, with the same precedence, so the simplest correct move is to defer to it (net.md §2.18.1).
constexpr const char* kProxyEnvNames[] = {"http_proxy", "https_proxy", "all_proxy", "no_proxy",
                                          "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY"};

[[nodiscard]] bool anyProxyEnvVarSet() {
    for (const char* name : kProxyEnvNames) {
        if (qEnvironmentVariableIsSet(name)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] long authMaskForScheme(QByteArrayView schemeLower) {
    // Ports GetAuthTypeByName (u_CurlProxyResolver.pas:485-500) *exactly*: legacy recognizes only
    // basic/digest/bearer/negotiate and falls through to atAny (-> CURLAUTH_ANY) for everything
    // else, "ntlm" included, even though TCurlAuthType has an atNtlm member elsewhere
    // (t_CurlHttpClient.pas:53). docs/specs/net.md §2.18 claims an "ntlm -> CURLAUTH_NTLM" mapping
    // that legacy does not implement; per protocol rule 5 (legacy wins on disagreement), that branch
    // is NOT ported -- see docs/handoffs/T-0011A.md open questions.
    if (schemeLower == "basic") {
        return CURLAUTH_BASIC;
    }
    if (schemeLower == "digest") {
        return CURLAUTH_DIGEST;
    }
    if (schemeLower == "bearer") {
        return CURLAUTH_BEARER;
    }
    if (schemeLower == "negotiate") {
        return CURLAUTH_NEGOTIATE;
    }
    return CURLAUTH_ANY;
}

enum class CacheState : std::uint8_t { Ok, Fail };

struct CachedCredential {
    CacheState state;
    QByteArray user;
    QByteArray pass;
};

// The one call into Qt/libproxy/GIO. Not inlined on purpose: TSan unwinds through the uninstrumented glib
// with frame pointers and drops the frame of the function that owns the live frame pointer; with this call in
// its own frame, `SystemProxyResolver::resolve` shows in the stacks of the GDBus reports and
// tools/tsan_suppressions.txt (T-0201) can be anchored on it instead of on a whole library.
[[gnu::noinline]] QList<QNetworkProxy> systemProxies(const QNetworkProxyQuery& query) {
    return QNetworkProxyFactory::systemProxyForQuery(query);
}

class SystemProxyResolver final : public IProxyResolver {
public:
    bool resolve(QByteArrayView url, CurlProxy& out) override {
        if (anyProxyEnvVarSet()) {
            return false; // let curl read the environment itself
        }
        const QUrl target = QUrl::fromEncoded(url.toByteArray());
        const QNetworkProxyQuery query(target);
        const QList<QNetworkProxy> proxies = systemProxies(query);
        for (const QNetworkProxy& proxy : proxies) {
            ProxyType type{};
            switch (proxy.type()) {
            case QNetworkProxy::HttpProxy:
                type = ProxyType::Http;
                break;
            case QNetworkProxy::Socks5Proxy:
                type = ProxyType::Socks5;
                break;
            default:
                continue; // anything else -> no proxy (net.md §2.18.2)
            }
            const QByteArray hostPort = proxy.hostName().toUtf8() + ':' + QByteArray::number(proxy.port());
            out.address = proxySchemePrefix(type) + hostPort;
            out.noProxy.clear();
            return true;
        }
        return false;
    }

    void setCredentialPrompt(CredentialFn fn) override {
        const QMutexLocker locker(&mutex_);
        prompt_ = std::move(fn);
    }

    bool credentialsFor(QByteArrayView proxyAddress, const QString& description, QByteArray& user,
                        QByteArray& pass) override {
        const QByteArray key = proxyAddress.toByteArray();
        CredentialFn prompt;
        {
            const QMutexLocker locker(&mutex_);
            const auto it = cache_.constFind(key);
            if (it != cache_.constEnd()) {
                if (it->state == CacheState::Ok) {
                    user = it->user;
                    pass = it->pass;
                    return true;
                }
                return false; // prompted once already, and it failed/was refused
            }
            prompt = prompt_;
        }

        QString qUser;
        QString qPass;
        const bool ok = prompt ? prompt(description, qUser, qPass) : false;

        CachedCredential entry;
        entry.state = ok ? CacheState::Ok : CacheState::Fail;
        entry.user = qUser.toUtf8();
        entry.pass = qPass.toUtf8();
        {
            const QMutexLocker locker(&mutex_);
            cache_.insert(key, entry);
        }
        if (ok) {
            user = entry.user;
            pass = entry.pass;
        }
        return ok;
    }

private:
    QMutex mutex_;
    CredentialFn prompt_;
    QMap<QByteArray, CachedCredential> cache_;
};

} // namespace

std::optional<ProxyAuthChallenge> IProxyResolver::parseChallenge(QByteArrayView rawHeader) {
    const QByteArray header = getHeaderValueUp(rawHeader, "PROXY-AUTHENTICATE");
    if (header.isEmpty()) {
        return std::nullopt;
    }

    // Ports GetProxyAuthenticateInfo's character walk (u_CurlProxyResolver.pas:503-538) as an
    // equivalent, clearer scan: find the first space; everything before it is the scheme, and (if
    // present) a case-insensitive `realm="..."` follows after any run of spaces. When there is no
    // space at all -- or the header starts with one, legacy's own `I > 1` edge case -- the whole
    // header becomes the scheme name and no realm is parsed, exactly as legacy does.
    const qsizetype spacePos = header.indexOf(' ');
    QByteArray scheme;
    QString realm;
    if (spacePos <= 0) {
        scheme = header;
    } else {
        scheme = header.left(spacePos);
        qsizetype i = spacePos;
        while (i < header.size() && header[i] == ' ') {
            ++i;
        }
        static constexpr QByteArrayView kRealmMarker = "realm=\"";
        if (header.size() - i >= kRealmMarker.size() &&
            header.sliced(i, kRealmMarker.size()).compare(kRealmMarker, Qt::CaseInsensitive) == 0) {
            i += kRealmMarker.size();
            const qsizetype start = i;
            while (i < header.size() && header[i] != '"') {
                ++i;
            }
            if (i < header.size() && header[i] == '"') {
                realm = QString::fromUtf8(header.sliced(start, i - start));
            }
        }
    }

    ProxyAuthChallenge challenge;
    challenge.scheme = scheme;
    challenge.realm = realm;
    challenge.curlAuthMask = authMaskForScheme(scheme.toLower());
    return challenge;
}

std::shared_ptr<IProxyResolver> makeSystemProxyResolver() {
    return std::make_shared<SystemProxyResolver>();
}

} // namespace cg::net
