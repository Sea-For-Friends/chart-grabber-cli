// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: docs/specs/net.md §2.16, docs/specs/maptype.md §11.2 P-8 (T-0118).
// Ports u_GlobalDownloadConfig.pas (defaults :61-63, read :66-75, write :77-85, setters :117-154).
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/net/global_download_config.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <gtest/gtest.h>

#include <array>
#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IConfigProviderPtr;
using cg::core::IniFile;
using cg::core::Subscription;
using cg::net::GlobalDownloadConfig;
using cg::net::IGlobalDownloadConfig;

namespace {

using Getter = bool (IGlobalDownloadConfig::*)() const;
using Setter = void (IGlobalDownloadConfig::*)(bool);

struct Flag {
    QStringView key;
    bool def; // the Pascal default (u_GlobalDownloadConfig.pas:61-63)
    Getter get;
    Setter set;
};

const std::array<Flag, 3> kFlags{{
    {u"GoNextTile", true, &IGlobalDownloadConfig::isGoNextTileIfDownloadError,
     &IGlobalDownloadConfig::setGoNextTileIfDownloadError},
    {u"SessionLastSuccess", true, &IGlobalDownloadConfig::isUseSessionLastSuccess,
     &IGlobalDownloadConfig::setUseSessionLastSuccess},
    {u"SaveTNE", false, &IGlobalDownloadConfig::isSaveTileNotExists,
     &IGlobalDownloadConfig::setSaveTileNotExists},
}};

std::shared_ptr<ConfigProviderIni> iniOf(const QByteArray& text) {
    auto ini = IniFile::fromBytes(text);
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));
}

IConfigProviderPtr section(const QByteArray& body) {
    return iniOf("[G]\n" + body)->subItem(u"G");
}

QByteArray line(QStringView key, bool value) {
    return key.toUtf8() + (value ? "=1\n" : "=0\n");
}

} // namespace

TEST(GlobalDownloadConfig, DefaultsMatchLegacy) {
    const GlobalDownloadConfig config;
    EXPECT_TRUE(config.isGoNextTileIfDownloadError());
    EXPECT_TRUE(config.isUseSessionLastSuccess());
    EXPECT_FALSE(config.isSaveTileNotExists());
}

TEST(GlobalDownloadConfig, ReadsEachKeyOnlyIntoItsOwnFlag) {
    // One key flipped at a time in the INI: exactly that flag changes (swapped keys are caught).
    for (std::size_t i = 0; i < kFlags.size(); ++i) {
        GlobalDownloadConfig config;
        config.readConfig(section(line(kFlags[i].key, !kFlags[i].def)));
        for (std::size_t j = 0; j < kFlags.size(); ++j) {
            const bool expected = (i == j) ? !kFlags[j].def : kFlags[j].def;
            EXPECT_EQ((config.*kFlags[j].get)(), expected) << "key " << i << ", flag " << j;
        }
    }
}

TEST(GlobalDownloadConfig, MissingKeyKeepsCurrentValue) {
    GlobalDownloadConfig config;
    for (const Flag& f : kFlags) {
        (config.*f.set)(!f.def);
    }
    config.readConfig(section(""));
    for (const Flag& f : kFlags) {
        EXPECT_EQ((config.*f.get)(), !f.def) << "key " << f.key.toUtf8().constData();
    }
}

TEST(GlobalDownloadConfig, NullProviderChangesNothingAndDoesNotNotify) {
    GlobalDownloadConfig config;
    for (const Flag& f : kFlags) {
        (config.*f.set)(!f.def);
    }
    int fired = 0;
    const Subscription sub = config.changed().subscribe([&fired] { ++fired; });

    config.readConfig(nullptr);

    EXPECT_EQ(fired, 0);
    for (const Flag& f : kFlags) {
        EXPECT_EQ((config.*f.get)(), !f.def) << "key " << f.key.toUtf8().constData();
    }
}

TEST(GlobalDownloadConfig, ProviderReadNotifiesEvenWhenNothingChanged) {
    // u_GlobalDownloadConfig.pas:69-73: SetChanged runs whenever the provider is non-nil.
    GlobalDownloadConfig config;
    int fired = 0;
    const Subscription sub = config.changed().subscribe([&fired] { ++fired; });

    config.readConfig(section(""));
    EXPECT_EQ(fired, 1);
    config.readConfig(section("GoNextTile=1\nSessionLastSuccess=1\nSaveTNE=0\n"));
    EXPECT_EQ(fired, 2);
}

TEST(GlobalDownloadConfig, WriteThenReadRoundTrips) {
    // Three booleans cannot take three distinct non-default values; (false, false, true) is the
    // only non-default combination, and the per-key tables above and below catch swapped keys.
    GlobalDownloadConfig out;
    out.setGoNextTileIfDownloadError(false);
    out.setUseSessionLastSuccess(false);
    out.setSaveTileNotExists(true);

    const auto root = iniOf("");
    out.writeConfig(root->orCreateSubItem(u"G"));

    GlobalDownloadConfig in;
    in.readConfig(root->subItem(u"G"));
    EXPECT_FALSE(in.isGoNextTileIfDownloadError());
    EXPECT_FALSE(in.isUseSessionLastSuccess());
    EXPECT_TRUE(in.isSaveTileNotExists());
}

TEST(GlobalDownloadConfig, WritesEachFlagUnderItsOwnKey) {
    // Flip one flag at a time, write, and check the INI value of every key (1 = true, 0 = false).
    for (std::size_t i = 0; i < kFlags.size(); ++i) {
        GlobalDownloadConfig config;
        (config.*kFlags[i].set)(!kFlags[i].def);
        const auto root = iniOf("");
        config.writeConfig(root->orCreateSubItem(u"G"));
        for (std::size_t j = 0; j < kFlags.size(); ++j) {
            const bool value = (i == j) ? !kFlags[j].def : kFlags[j].def;
            EXPECT_EQ(root->subItem(u"G")->readInt(kFlags[j].key, -1), value ? 1 : 0)
                << "flipped " << i << ", key " << j;
        }
    }
}

TEST(GlobalDownloadConfig, SettersNotifyOnlyOnRealChange) {
    for (std::size_t i = 0; i < kFlags.size(); ++i) {
        const Flag& f = kFlags[i];
        GlobalDownloadConfig config;
        int fired = 0;
        const Subscription sub = config.changed().subscribe([&fired] { ++fired; });

        (config.*f.set)(f.def); // same as the current value: no notification
        EXPECT_EQ(fired, 0) << "flag " << i;
        (config.*f.set)(!f.def); // real change
        EXPECT_EQ(fired, 1) << "flag " << i;
        EXPECT_EQ((config.*f.get)(), !f.def) << "flag " << i;
        (config.*f.set)(!f.def); // same value again
        EXPECT_EQ(fired, 1) << "flag " << i;
        (config.*f.set)(f.def); // back to the default
        EXPECT_EQ(fired, 2) << "flag " << i;
        EXPECT_EQ((config.*f.get)(), f.def) << "flag " << i;
    }
}
