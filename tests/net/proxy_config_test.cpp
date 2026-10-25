// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-008; docs/specs/net.md §2.1, test plan §7.7
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/net/proxy_config.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::net::ProxyConfig;
using cg::net::proxySchemePrefix;
using cg::net::ProxySettings;
using cg::net::ProxyType;

namespace {

std::shared_ptr<cg::core::IConfigWriter> makeSection(std::shared_ptr<IniFile>& file) {
    file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    return root->orCreateSubItem(u"Proxy");
}

} // namespace

TEST(ProxyConfig, DefaultsMatchLegacy) {
    const ProxyConfig config;
    const auto s = config.settings();
    EXPECT_TRUE(s->useSystemSettings);
    EXPECT_FALSE(s->useProxy);
    EXPECT_EQ(s->type, ProxyType::Http);
    EXPECT_EQ(s->host, QByteArray("127.0.0.1:8080"));
    EXPECT_FALSE(s->useLogin);
    EXPECT_TRUE(s->login.isEmpty());
    EXPECT_TRUE(s->password.isEmpty());
}

TEST(ProxyConfig, ReadWriteRoundTripAllKeys) {
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    section->writeBool(u"UseIEProxySettings", false);
    section->writeBool(u"UseProxy", true);
    section->writeString(u"Host", QStringLiteral("10.0.0.5:3128"));
    section->writeBool(u"UseAuth", true);
    section->writeString(u"Login", QStringLiteral("bob"));
    section->writeString(u"Password", QStringLiteral("secret"));
    section->writeInt(u"ProxyType", 4); // Socks5

    ProxyConfig config;
    const auto root = ConfigProviderIni::wrap(file);
    config.readConfig(root->subItem(u"Proxy"));

    EXPECT_FALSE(config.useSystemSettings());
    EXPECT_TRUE(config.useProxy());
    EXPECT_EQ(config.host(), QByteArray("10.0.0.5:3128"));
    EXPECT_TRUE(config.useLogin());
    EXPECT_EQ(config.login(), QStringLiteral("bob"));
    EXPECT_EQ(config.password(), QStringLiteral("secret"));
    EXPECT_EQ(config.type(), ProxyType::Socks5);

    auto outFile = std::make_shared<IniFile>();
    const auto outRoot = ConfigProviderIni::wrap(outFile);
    const auto outSection = outRoot->orCreateSubItem(u"Proxy");
    config.writeConfig(outSection);

    EXPECT_FALSE(outSection->readBool(u"UseIEProxySettings", true));
    EXPECT_TRUE(outSection->readBool(u"UseProxy", false));
    EXPECT_EQ(outSection->readString(u"Host", QString()), QStringLiteral("10.0.0.5:3128"));
    EXPECT_TRUE(outSection->readBool(u"UseAuth", false));
    EXPECT_EQ(outSection->readString(u"Login", QString()), QStringLiteral("bob"));
    EXPECT_EQ(outSection->readString(u"Password", QString()), QStringLiteral("secret"));
    EXPECT_EQ(outSection->readInt(u"ProxyType", -1), 4);
    EXPECT_EQ(outFile->keyNames(u"Proxy").size(), 7); // exactly the seven §2.1 keys
}

TEST(ProxyConfig, NormalizeStripsSchemePrefix) {
    EXPECT_EQ(ProxyConfig::normalizeAddress("https://10.0.0.1:3128"), QByteArray("10.0.0.1:3128"));
}

TEST(ProxyConfig, NormalizeStripsEqualsPrefix) {
    EXPECT_EQ(ProxyConfig::normalizeAddress("proxy=10.0.0.1:3128"), QByteArray("10.0.0.1:3128"));
}

TEST(ProxyConfig, NormalizeTrims) {
    EXPECT_EQ(ProxyConfig::normalizeAddress("  10.0.0.1:3128  "), QByteArray("10.0.0.1:3128"));
}

TEST(ProxyConfig, HostWrittenRaw) {
    // setHost() never normalises, and neither does doWriteConfig() -- the read/write asymmetry
    // (spec §2.1: "read through normalizeAddress, written raw").
    ProxyConfig config;
    config.setHost("http=http://10.0.0.1:3128");

    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Proxy");
    config.writeConfig(section);

    EXPECT_EQ(section->readString(u"Host", QString()), QStringLiteral("http=http://10.0.0.1:3128"));
}

TEST(ProxyConfig, ProxyTypeOrdinalsArePersisted) {
    const ProxyType types[] = {ProxyType::Http,    ProxyType::Https,  ProxyType::Socks4,
                               ProxyType::Socks4a, ProxyType::Socks5, ProxyType::Socks5h};
    for (int ordinal = 0; ordinal < 6; ++ordinal) {
        std::shared_ptr<IniFile> file;
        const auto section = makeSection(file);
        section->writeInt(u"ProxyType", ordinal);

        ProxyConfig config;
        const auto root = ConfigProviderIni::wrap(file);
        config.readConfig(root->subItem(u"Proxy"));
        EXPECT_EQ(config.type(), types[ordinal]) << "ordinal " << ordinal;

        auto outFile = std::make_shared<IniFile>();
        const auto outRoot = ConfigProviderIni::wrap(outFile);
        const auto outSection = outRoot->orCreateSubItem(u"Proxy");
        config.writeConfig(outSection);
        EXPECT_EQ(outSection->readInt(u"ProxyType", -1), ordinal);
    }
}

TEST(ProxyConfig, ProxyTypeOutOfRangeKeepsOldValue) {
    std::shared_ptr<IniFile> file;
    const auto section = makeSection(file);
    section->writeInt(u"ProxyType", 99); // out of range: 0..5 only

    ProxyConfig config;
    ASSERT_EQ(config.type(), ProxyType::Http); // pre-existing default value
    const auto root = ConfigProviderIni::wrap(file);
    config.readConfig(root->subItem(u"Proxy"));
    EXPECT_EQ(config.type(), ProxyType::Http); // unchanged, not clamped
}

TEST(ProxyConfig, SchemePrefixTable) {
    EXPECT_EQ(proxySchemePrefix(ProxyType::Http), QByteArray(""));
    EXPECT_EQ(proxySchemePrefix(ProxyType::Https), QByteArray("https://"));
    EXPECT_EQ(proxySchemePrefix(ProxyType::Socks4), QByteArray("socks4://"));
    EXPECT_EQ(proxySchemePrefix(ProxyType::Socks4a), QByteArray("socks4a://"));
    EXPECT_EQ(proxySchemePrefix(ProxyType::Socks5), QByteArray("socks5://"));
    EXPECT_EQ(proxySchemePrefix(ProxyType::Socks5h), QByteArray("socks5h://"));
}

TEST(ProxyConfig, SetHostDoesNotNotifyWhenSystemSettings) {
    ProxyConfig config; // useSystemSettings defaults true
    int notifications = 0;
    const auto sub = config.changed().subscribe([&] { ++notifications; });
    config.setHost("10.0.0.9:8080");
    EXPECT_EQ(notifications, 0);
}

TEST(ProxyConfig, SetLoginNotifiesOnlyWhenUseLogin) {
    ProxyConfig config;
    config.setUseSystemSettings(false);
    config.setUseProxy(true);

    int notifications = 0;
    const auto sub = config.changed().subscribe([&] { ++notifications; });

    config.setLogin(QStringLiteral("alice")); // useLogin_ is still false: no notification
    EXPECT_EQ(notifications, 0);

    config.setUseLogin(true);
    EXPECT_EQ(notifications, 1);

    config.setLogin(QStringLiteral("bob")); // now useLogin_ is true: notifies
    EXPECT_EQ(notifications, 2);
}

TEST(ProxyConfig, SetProxyTypeAlwaysNotifies) {
    ProxyConfig config; // useSystemSettings=true, useProxy=false: every other setter is silent here
    int notifications = 0;
    const auto sub = config.changed().subscribe([&] { ++notifications; });
    config.setType(ProxyType::Socks5);
    EXPECT_EQ(notifications, 1);
}
