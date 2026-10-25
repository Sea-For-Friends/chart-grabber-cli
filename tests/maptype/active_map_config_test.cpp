// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0145: ActiveMapConfig (u_ActiveMapConfig.pas). Expectations are literals taken from the Pascal.
#include "cg/maptype/active_map_config.h"

#include <QByteArray>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>

#include "maptype_test_util.h"

using namespace cg::maptype;
using cg::maptype::test::iniFrom;

namespace {

const QUuid kMain(u"{AB12CD34-5678-4ABC-9DEF-0123456789AB}");  // initial value
const QUuid kOther(u"{0F0E0D0C-0B0A-4908-8706-050403020100}"); // a different value
const QByteArray kZeroText = "[S]\r\nMap={00000000-0000-0000-0000-000000000000}\r\n";

struct Counted {
    std::shared_ptr<ActiveMapConfig> config;
    int notifications = 0;
    cg::core::Subscription sub;
    Counted(bool allowZero, const QUuid& initial)
        : config(std::make_shared<ActiveMapConfig>(allowZero, initial)),
          sub(config->changed().subscribe([this] { ++notifications; })) {}
};

} // namespace

// The write path is u_ActiveMapConfig.pas:88-95 (WriteString of GUIDToString).
TEST(ActiveMapConfig, WritesBracedUpperCaseTextAlsoForTheZeroGuid) {
    ActiveMapConfig config(true, QUuid(u"{ab12cd34-5678-4abc-9def-0123456789ab}"));
    auto ini = iniFrom({});
    config.writeConfig(ini->orCreateSubItem(u"S"));
    EXPECT_EQ(ini->file().toBytes().toStdString(), "[S]\r\nMap={AB12CD34-5678-4ABC-9DEF-0123456789AB}\r\n");

    ActiveMapConfig zero(true, QUuid());
    auto ini2 = iniFrom({});
    zero.writeConfig(ini2->orCreateSubItem(u"S"));
    EXPECT_EQ(ini2->file().toBytes().toStdString(), kZeroText.toStdString());
}

// D row of T-0145: a null writer returns (legacy: access violation).
TEST(ActiveMapConfig, NullWriterIsIgnored) {
    ActiveMapConfig config(true, kMain);
    config.writeConfig(nullptr);
    EXPECT_EQ(config.mainMapGuid(), kMain);
}

TEST(ActiveMapConfig, ReadsANonZeroGuidWhateverTheZeroPolicy) {
    for (const bool allow : {true, false}) {
        Counted c(allow, kMain);
        c.config->readConfig(iniFrom("[S]\r\nMap={0F0E0D0C-0B0A-4908-8706-050403020100}\r\n")->subItem(u"S"));
        EXPECT_EQ(c.config->mainMapGuid(), kOther) << allow;
        EXPECT_EQ(c.notifications, 1) << allow;
    }
}

// u_ActiveMapConfig.pas:78-85 and :107-134: {zero allowed, not allowed} x {read zero, key missing,
// null provider, setter with zero}. Allowed: the zero is applied (1 notification); else the value stays.
TEST(ActiveMapConfig, ZeroTable) {
    struct Row {
        bool allow;
        int how; // 0 read zero, 1 key missing, 2 null provider, 3 setter with zero
        bool expectZero;
        int notifications;
    };
    const Row rows[] = {
        {true, 0, true, 1},   {true, 1, true, 1},   {true, 2, true, 1},   {true, 3, true, 1},
        {false, 0, false, 0}, {false, 1, false, 0}, {false, 2, false, 0}, {false, 3, false, 0},
    };
    for (const Row& r : rows) {
        Counted c(r.allow, kMain);
        switch (r.how) {
        case 0:
            c.config->readConfig(iniFrom(kZeroText)->subItem(u"S"));
            break;
        case 1:
            c.config->readConfig(iniFrom("[S]\r\nOther=1\r\n")->subItem(u"S"));
            break;
        case 2:
            c.config->readConfig(nullptr);
            break;
        default:
            c.config->setMainMapGuid(QUuid());
            break;
        }
        EXPECT_EQ(c.config->mainMapGuid(), r.expectZero ? QUuid() : kMain) << r.allow << r.how;
        EXPECT_EQ(c.notifications, r.notifications) << r.allow << r.how;
    }
}

TEST(ActiveMapConfig, SetterNotifiesOnlyOnAChange) {
    Counted c(false, kMain);
    c.config->setMainMapGuid(kMain);
    EXPECT_EQ(c.notifications, 0);
    c.config->setMainMapGuid(kOther);
    EXPECT_EQ(c.notifications, 1);
    EXPECT_EQ(c.config->mainMapGuid(), kOther);
}
