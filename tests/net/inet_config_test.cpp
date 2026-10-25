// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-007; docs/specs/net.md §2.2, test plan §7.8
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/net/inet_config.h"

#include <QString>

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <memory>
#include <thread>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::net::InetConfig;
using cg::net::kDefaultUserAgent;

TEST(InetConfig, DefaultsMatchLegacy) {
    const InetConfig config;
    const auto s = config.settings();
    EXPECT_EQ(s->userAgent, QByteArray(kDefaultUserAgent));
    EXPECT_EQ(s->timeoutMs, 40000u);
    EXPECT_EQ(s->sleepOnResetConnectionMs, 30000u);
    EXPECT_EQ(s->downloadTryCount, 2);
}

TEST(InetConfig, UserAgentIsTheFirefoxString) {
    // c_InetConfig.pas:26-28, verbatim.
    EXPECT_STREQ(kDefaultUserAgent,
                 "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:146.0) Gecko/20100101 Firefox/146.0");
}

TEST(InetConfig, ReadWriteRoundTrip) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Internet");
    section->writeString(u"UserAgentString", QStringLiteral("CustomAgent/1.0"));
    section->writeInt(u"TimeOut", 12345);
    section->writeInt(u"DownloadTryCount", 7);
    section->writeInt(u"SleepOnResetConnection", 9000);
    section->writeInt(u"NetworkEngine", 1);
    section->writeBool(u"IgnoreTlsErrors", true);
    const auto proxySection = root->orCreateSubItem(u"Internet_Proxy");
    proxySection->writeBool(u"UseIEProxySettings", false);
    proxySection->writeBool(u"UseProxy", true);
    proxySection->writeString(u"Host", QStringLiteral("10.0.0.5:3128"));
    proxySection->writeBool(u"UseAuth", false);
    proxySection->writeString(u"Login", QString());
    proxySection->writeString(u"Password", QString());
    proxySection->writeInt(u"ProxyType", 1); // Https

    InetConfig config;
    config.readConfig(root->subItem(u"Internet"));

    EXPECT_EQ(config.userAgent(), QByteArray("CustomAgent/1.0"));
    EXPECT_EQ(config.timeoutMs(), 12345u);
    EXPECT_EQ(config.downloadTryCount(), 7);
    EXPECT_EQ(config.sleepOnResetConnectionMs(), 9000u);
    EXPECT_TRUE(config.ignoreTlsErrors());
    EXPECT_FALSE(config.proxy().useSystemSettings());
    EXPECT_EQ(config.proxy().host(), QByteArray("10.0.0.5:3128"));

    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"Internet");
    config.writeConfig(outSection);

    EXPECT_EQ(outSection->readString(u"UserAgentString", QString()), QStringLiteral("CustomAgent/1.0"));
    EXPECT_EQ(outSection->readInt(u"TimeOut", -1), 12345);
    EXPECT_EQ(outSection->readInt(u"DownloadTryCount", -1), 7);
    EXPECT_EQ(outSection->readInt(u"SleepOnResetConnection", -1), 9000);
    EXPECT_TRUE(outSection->readBool(u"IgnoreTlsErrors", false));
    EXPECT_EQ(outFile->keyNames(u"Internet").size(), 5); // UserAgentString/TimeOut/DownloadTryCount/
                                                         // SleepOnResetConnection/IgnoreTlsErrors --
                                                         // NetworkEngine never written ([D-1])

    const auto outProxySection = outSection->subItem(u"Proxy");
    ASSERT_NE(outProxySection, nullptr);
    EXPECT_EQ(outFile->keyNames(u"Internet_Proxy").size(), 7); // exactly the seven §2.1 keys
}

TEST(InetConfig, TimeOutHasNoClamp) {
    InetConfig config;
    config.setTimeoutMs(1); // no bounds check at all (u_InetConfig.pas:137)
    EXPECT_EQ(config.timeoutMs(), 1u);
    config.setTimeoutMs(999999999u);
    EXPECT_EQ(config.timeoutMs(), 999999999u);
}

TEST(InetConfig, DownloadTryCountOutOfRangeIsIgnoredNotClamped) {
    InetConfig config;
    ASSERT_EQ(config.downloadTryCount(), 2);
    config.setDownloadTryCount(0);
    EXPECT_EQ(config.downloadTryCount(), 2);
    config.setDownloadTryCount(100);
    EXPECT_EQ(config.downloadTryCount(), 2);
    config.setDownloadTryCount(-1);
    EXPECT_EQ(config.downloadTryCount(), 2);
}

TEST(InetConfig, DownloadTryCountAcceptsOneAndNinetyNine) {
    InetConfig config;
    config.setDownloadTryCount(1);
    EXPECT_EQ(config.downloadTryCount(), 1);
    config.setDownloadTryCount(99);
    EXPECT_EQ(config.downloadTryCount(), 99);
}

TEST(InetConfig, NetworkEngineKeyIsReadAndNotWritten) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Internet");
    section->writeInt(u"NetworkEngine", 1);

    InetConfig config;
    config.readConfig(root->subItem(u"Internet")); // must not throw/assert on the stray key

    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"Internet");
    config.writeConfig(outSection);
    EXPECT_FALSE(outFile->hasKey(u"Internet", u"NetworkEngine"));
}

TEST(InetConfig, ProxySubItemIsNamedProxy) {
    // ConfigElementComplex's SaveLoadSlot{"Proxy"} composes, via the shared config-tree machinery,
    // to the on-disk section "<Parent>_Proxy" (u_ConfigDataProviderByIniFileSection.pas:128:
    // `FSection + '_' + AIdent`) -- not "<Parent>.Proxy" as net.md §2.1's prose literally spells it;
    // the dot there is descriptive, not a literal section header (see docs/handoffs/T-0011A.md).
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto proxySection = root->orCreateSubItem(u"Internet_Proxy");
    proxySection->writeString(u"Host", QStringLiteral("proxy.example:8080"));

    InetConfig config;
    // orCreateSubItem(), not subItem(): the plain "Internet" section itself has no keys of its own
    // in this test, so it does not exist on disk yet and subItem() would return nullptr.
    config.readConfig(root->orCreateSubItem(u"Internet"));
    EXPECT_EQ(config.proxy().host(), QByteArray("proxy.example:8080"));
}

TEST(InetConfig, IgnoreTlsErrorsDefaultsFalse) {
    const InetConfig config;
    EXPECT_FALSE(config.ignoreTlsErrors());
    EXPECT_FALSE(config.settings()->ignoreTlsErrors);
}

// T-0094 part 2: the before-hook run from finishWrite takes the element lock (legacy
// u_ConfigDataElementBase.pas:221-238), so two writers neither race on the members the hook reads nor
// leave a snapshot older than the last write.
TEST(InetConfig, ConcurrentWritersLeavePublishedSnapshotEqualToAFreshRebuild) {
    InetConfig config;
    constexpr int kIterations = 2000;
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::latch go(1);
    const auto bracket = [&](auto&& call) {
        const int now = active.fetch_add(1) + 1;
        int prev = maxActive.load();
        while (now > prev && !maxActive.compare_exchange_weak(prev, now)) {
        }
        call();
        active.fetch_sub(1);
    };
    // Each writer runs at least kIterations times and keeps going until the two have overlapped.
    int lastA = 0;
    int lastB = 0;
    const auto keepGoing = [&](int i) { return i < kIterations || (maxActive.load() < 2 && i < 5000000); };
    std::thread a([&] {
        go.wait();
        for (int i = 0; keepGoing(i); ++i) {
            bracket([&] { config.setUserAgent(QByteArray("agent/") + QByteArray::number(i)); });
            lastA = i;
        }
    });
    std::thread b([&] {
        go.wait();
        for (int i = 0; keepGoing(i); ++i) {
            bracket([&] { config.setTimeoutMs(static_cast<quint32>(1000 + i)); });
            lastB = i;
        }
    });
    go.count_down();
    a.join();
    b.join();
    EXPECT_EQ(maxActive.load(), 2); // the two writers did overlap
    const auto s = config.settings();
    EXPECT_EQ(s->userAgent, QByteArray("agent/") + QByteArray::number(lastA));
    EXPECT_EQ(s->timeoutMs, static_cast<quint32>(1000 + lastB));
}
