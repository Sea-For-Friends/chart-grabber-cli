// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034. Ports u_ThreadConfig.pas (no DUnit suite exists).
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/thread_config.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::core::Subscription;
using cg::core::ThreadConfig;
using cg::core::ThreadPriority;

namespace {

std::shared_ptr<ConfigProviderIni> iniOf(const QByteArray& text) {
    auto ini = IniFile::fromBytes(text);
    return ConfigProviderIni::wrap(std::make_shared<IniFile>(std::move(ini.value())));
}

cg::core::IConfigProviderPtr section(const QByteArray& body) {
    return iniOf("[S]\n" + body)->subItem(u"S");
}

} // namespace

TEST(ThreadConfig, DefaultOutsideTheValidRangeBecomesLower) {
    EXPECT_EQ(ThreadConfig(ThreadPriority::Normal).priority(), ThreadPriority::Normal);
    EXPECT_EQ(ThreadConfig(ThreadPriority::Higher).priority(), ThreadPriority::Higher);
    EXPECT_EQ(ThreadConfig(ThreadPriority::Highest).priority(), ThreadPriority::Lower);
    EXPECT_EQ(ThreadConfig(ThreadPriority::TimeCritical).priority(), ThreadPriority::Lower);
}

TEST(ThreadConfig, SetPriorityRejectsOutOfRangeAndNotifiesOnlyOnChange) {
    ThreadConfig config(ThreadPriority::Normal);
    int fired = 0;
    const Subscription sub = config.changed().subscribe([&fired] { ++fired; });

    config.setPriority(ThreadPriority::Idle);
    EXPECT_EQ(config.priority(), ThreadPriority::Idle);
    EXPECT_EQ(fired, 1);
    config.setPriority(ThreadPriority::Idle);
    EXPECT_EQ(fired, 1);
    config.setPriority(ThreadPriority::TimeCritical); // invalid -> the default, not the old value
    EXPECT_EQ(config.priority(), ThreadPriority::Normal);
    EXPECT_EQ(fired, 2);
}

TEST(ThreadConfig, ReadsAndWritesThreadPriorityAsInteger) {
    ThreadConfig config(ThreadPriority::Normal);
    config.readConfig(section("ThreadPriority=1\n"));
    EXPECT_EQ(config.priority(), ThreadPriority::Lowest);

    config.readConfig(section("ThreadPriority=5\n")); // out of range -> default
    EXPECT_EQ(config.priority(), ThreadPriority::Normal);

    config.readConfig(section("")); // missing -> keeps the current value
    EXPECT_EQ(config.priority(), ThreadPriority::Normal);

    config.setPriority(ThreadPriority::Higher);
    const auto out = iniOf("");
    config.writeConfig(out->orCreateSubItem(u"S"));
    EXPECT_EQ(out->subItem(u"S")->readInt(u"ThreadPriority", -1), 4);
}
