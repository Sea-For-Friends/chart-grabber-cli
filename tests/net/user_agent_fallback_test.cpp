// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// No legacy counterpart: the host-scoped User-Agent fallback.
#include "cg/net/curl_global.h"
#include "cg/net/user_agent_fallback.h"

#include <QRegularExpression>

#include <gtest/gtest.h>

#include <atomic>
#include <curl/curl.h>
#include <latch>
#include <thread>
#include <vector>

using cg::net::UserAgentFallback;

TEST(UserAgentFallbackTest, SwitchIsPerHostAndSticky) {
    UserAgentFallback f("curl/9.9.9");
    EXPECT_EQ(f.agent(), QByteArrayLiteral("curl/9.9.9"));
    EXPECT_TRUE(f.noteRetry(QStringLiteral("a"), true));
    EXPECT_TRUE(f.isSwitched(QStringLiteral("a")));
    EXPECT_FALSE(f.isSwitched(QStringLiteral("b")));
    EXPECT_FALSE(f.shouldRetry(QStringLiteral("a")));
    EXPECT_TRUE(f.shouldRetry(QStringLiteral("b")));
    EXPECT_FALSE(f.noteRetry(QStringLiteral("a"), true));
}

TEST(UserAgentFallbackTest, ThreeFailedTriesDecideTheHost) {
    UserAgentFallback f("curl/9.9.9");
    const QString a = QStringLiteral("a");
    EXPECT_FALSE(f.noteRetry(a, false));
    EXPECT_FALSE(f.noteRetry(a, false));
    EXPECT_TRUE(f.shouldRetry(a));
    EXPECT_FALSE(f.noteRetry(a, false));
    EXPECT_FALSE(f.shouldRetry(a));
    EXPECT_FALSE(f.isSwitched(a));
    EXPECT_TRUE(f.shouldRetry(QStringLiteral("b")));
}

TEST(UserAgentFallbackTest, AcceptedAfterTwoFailuresStillSwitches) {
    UserAgentFallback f("curl/9.9.9");
    const QString a = QStringLiteral("a");
    EXPECT_FALSE(f.noteRetry(a, false));
    EXPECT_FALSE(f.noteRetry(a, false));
    EXPECT_TRUE(f.noteRetry(a, true));
    EXPECT_TRUE(f.isSwitched(a));
}

TEST(UserAgentFallbackTest, OnlyOneThreadAnnounces) {
    UserAgentFallback f("curl/9.9.9");
    constexpr int kThreads = 8;
    std::latch start(1);
    std::atomic<int> announced{0};
    {
        std::vector<std::jthread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&] {
                start.wait();
                if (f.noteRetry(QStringLiteral("h"), true)) {
                    ++announced;
                }
            });
        }
        start.count_down();
    }
    EXPECT_EQ(announced.load(), 1);
}

TEST(UserAgentFallbackTest, CurlUserAgentNamesTheLinkedLibrary) {
    const QByteArray agent = cg::net::curlUserAgent();
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^curl/\\d+\\.\\d+\\.\\d+"))
                    .match(QString::fromLatin1(agent))
                    .hasMatch())
        << agent.toStdString();
    EXPECT_EQ(agent, "curl/" + QByteArray(curl_version_info(CURLVERSION_NOW)->version));
}
