// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_UsedMarksConfig.pas / u_UsedMarksConfigStatic.pas (T-0147). INI goes through
// core/config_provider_ini.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/marks/used_marks_config.h"
#include "cg/marks/used_marks_config_static.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace m = cg::marks;
namespace c = cg::core;

namespace {

std::shared_ptr<c::ConfigProviderIni> wrapText(const QByteArray& text) {
    auto parsed = c::IniFile::fromBytes(text);
    EXPECT_TRUE(parsed.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*parsed)));
}

} // namespace

// u_UsedMarksConfig.pas:65-71: defaults true / false / false, and the snapshot of a fresh element equals
// them.
TEST(UsedMarksConfig, DefaultsAndFreshSnapshot) {
    m::UsedMarksConfig config;
    EXPECT_TRUE(config.isUseMarks());
    EXPECT_FALSE(config.ignoreCategoriesVisible());
    EXPECT_FALSE(config.ignoreMarksVisible());
    const auto snap = config.getStatic();
    ASSERT_NE(snap, nullptr);
    EXPECT_TRUE(snap->isUseMarks());
    EXPECT_FALSE(snap->ignoreCategoriesVisible());
    EXPECT_FALSE(snap->ignoreMarksVisible());
}

// u_UsedMarksConfig.pas:89-94: each key alone, set against its default, flips exactly its own field.
TEST(UsedMarksConfig, ReadEachKeyFlipsOnlyItsField) {
    struct Row {
        const char* text;
        bool use;
        bool cat;
        bool marks;
    };
    const Row rows[] = {
        {"[S]\r\nIsUseMarks=0\r\n", false, false, false},
        {"[S]\r\nIgnoreCategoriesVisible=1\r\n", true, true, false},
        {"[S]\r\nIgnoreMarksVisible=1\r\n", true, false, true},
    };
    for (const auto& row : rows) {
        SCOPED_TRACE(row.text);
        m::UsedMarksConfig config;
        config.readConfig(wrapText(row.text)->subItem(u"S"));
        EXPECT_EQ(config.isUseMarks(), row.use);
        EXPECT_EQ(config.ignoreCategoriesVisible(), row.cat);
        EXPECT_EQ(config.ignoreMarksVisible(), row.marks);
        const auto snap = config.getStatic();
        EXPECT_EQ(snap->isUseMarks(), row.use);
        EXPECT_EQ(snap->ignoreCategoriesVisible(), row.cat);
        EXPECT_EQ(snap->ignoreMarksVisible(), row.marks);
    }
}

// :93 SetChanged is unconditional: a provider without the keys keeps the values and still notifies once;
// :89 a null provider notifies 0 times.
TEST(UsedMarksConfig, ReadNotifiesOncePerNonNullProvider) {
    m::UsedMarksConfig config;
    int notified = 0;
    const auto sub = config.changed().subscribe([&notified] { ++notified; });

    config.readConfig(wrapText("[S]\r\nOther=1\r\n")->subItem(u"S"));
    EXPECT_EQ(notified, 1);
    EXPECT_TRUE(config.isUseMarks());
    EXPECT_FALSE(config.ignoreCategoriesVisible());
    EXPECT_FALSE(config.ignoreMarksVisible());

    config.readConfig(nullptr);
    EXPECT_EQ(notified, 1);
}

// :97-103: the three keys, in the order IsUseMarks, IgnoreCategoriesVisible, IgnoreMarksVisible.
TEST(UsedMarksConfig, WriteEmitsThreeKeysInOrder) {
    m::UsedMarksConfig config;
    config.setIsUseMarks(false);
    config.setIgnoreCategoriesVisible(true);
    config.setIgnoreMarksVisible(false);
    const auto root = wrapText("");
    config.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->file().toBytes(), QByteArray("[S]\r\nIsUseMarks=0\r\nIgnoreCategoriesVisible=1\r\n"
                                                 "IgnoreMarksVisible=0\r\n"));
}

// :140-177: a setter notifies only when the value differs.
TEST(UsedMarksConfig, SettersNotifyOnlyOnChange) {
    m::UsedMarksConfig config;
    int notified = 0;
    const auto sub = config.changed().subscribe([&notified] { ++notified; });

    config.setIsUseMarks(true);
    config.setIgnoreCategoriesVisible(false);
    config.setIgnoreMarksVisible(false);
    EXPECT_EQ(notified, 0);

    config.setIsUseMarks(false);
    EXPECT_EQ(notified, 1);
    config.setIgnoreCategoriesVisible(true);
    EXPECT_EQ(notified, 2);
    config.setIgnoreMarksVisible(true);
    EXPECT_EQ(notified, 3);
}

// :73-84 CreateStatic passes (use, marks, categories); values (false, true, false) set through the setters
// read back per field, and an older snapshot keeps its values.
TEST(UsedMarksConfig, SnapshotFollowsSettersAndOldOneIsImmutable) {
    m::UsedMarksConfig config;
    const auto before = config.getStatic();

    config.setIsUseMarks(false);
    config.setIgnoreMarksVisible(true);
    const auto snap = config.getStatic();
    EXPECT_FALSE(snap->isUseMarks());
    EXPECT_TRUE(snap->ignoreMarksVisible());
    EXPECT_FALSE(snap->ignoreCategoriesVisible());

    EXPECT_TRUE(before->isUseMarks());
    EXPECT_FALSE(before->ignoreMarksVisible());
}

// u_UsedMarksConfigStatic.pas:41-45: constructor order (isUseMarks, ignoreMarksVisible,
// ignoreCategoriesVisible).
TEST(UsedMarksConfigStatic, ConstructorOrder) {
    const m::UsedMarksConfigStatic s(true, true, false);
    EXPECT_TRUE(s.isUseMarks());
    EXPECT_TRUE(s.ignoreMarksVisible());
    EXPECT_FALSE(s.ignoreCategoriesVisible());
    const m::UsedMarksConfigStatic t(false, true, false);
    EXPECT_FALSE(t.isUseMarks());
}
