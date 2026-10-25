// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-008; docs/specs/net.md §2.18.1 (a proxy given by environment bypasses the resolver),
// T-0201
#include "cg/net/proxy_resolver.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <array>

namespace {

// The eight names the resolver treats as "a proxy is given by environment" (proxy_resolver.cpp
// kProxyEnvNames).
constexpr std::array<const char*, 8> kEnvNames = {"http_proxy", "https_proxy", "all_proxy", "no_proxy",
                                                  "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY"};

// Clears all eight for the test, restores them after.
class EnvScope {
public:
    EnvScope() {
        for (std::size_t i = 0; i < kEnvNames.size(); ++i) {
            set_[i] = qEnvironmentVariableIsSet(kEnvNames[i]);
            saved_[i] = qgetenv(kEnvNames[i]);
            qunsetenv(kEnvNames[i]);
        }
    }
    ~EnvScope() {
        for (std::size_t i = 0; i < kEnvNames.size(); ++i) {
            if (set_[i]) {
                qputenv(kEnvNames[i], saved_[i]);
            } else {
                qunsetenv(kEnvNames[i]);
            }
        }
    }
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;

private:
    std::array<bool, 8> set_{};
    std::array<QByteArray, 8> saved_;
};

} // namespace

// With a proxy variable set, resolve() returns false and leaves `out` untouched (curl reads the variable
// itself). If the check were removed, libproxy (behind QNetworkProxyFactory::systemProxyForQuery) would
// answer with the variable's proxy and resolve() would return true with its address.
TEST(SystemProxyResolver, EnvironmentProxyBypassesTheResolver) {
    const EnvScope scope;
    ASSERT_TRUE(qputenv("http_proxy", "http://envproxy.invalid:3128"));
    const auto resolver = cg::net::makeSystemProxyResolver();
    cg::net::CurlProxy out;
    out.address = "untouched";
    EXPECT_FALSE(resolver->resolve("http://example.invalid/tile.png", out));
    EXPECT_EQ(out.address, QByteArray("untouched"));
}
