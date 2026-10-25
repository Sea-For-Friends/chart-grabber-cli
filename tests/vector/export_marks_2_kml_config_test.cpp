// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_ExportMarks2KMLConfig.pas (T-0151, V10). INI goes through core/config_provider_ini.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/vector/export_marks_2_kml_config.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace v = cg::vector;
namespace c = cg::core;

namespace {

std::shared_ptr<c::ConfigProviderIni> wrapText(const QByteArray& text) {
    auto parsed = c::IniFile::fromBytes(text);
    EXPECT_TRUE(parsed.has_value());
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*parsed)));
}

void readText(v::ExportMarks2KmlConfig& config, const QByteArray& body) {
    config.readConfig(wrapText("[S]\r\n" + body)->subItem(u"S"));
}

// Puts every field away from its default, with distinct values per field (brief 16).
void setNonDefault(v::ExportMarks2KmlConfig& config) {
    config.setUseCoordFormatting(true);
    config.setCoordPrecision(9);
    config.setSortingType(v::KmlSortingType::ByNameDesc);
    config.setIconScaleType(v::KmlIconScaleType::Medium);
    config.setUseAbsPathToIcon(true);
    config.setAbsPathToIcon(QStringLiteral(u"C:\\icons\\"));
}

} // namespace

// :108-117: defaults false, 6, None, Abs, false, "" and the snapshot of a fresh element equals them.
TEST(ExportMarks2KmlConfig, DefaultsAndFreshSnapshot) {
    v::ExportMarks2KmlConfig config;
    EXPECT_FALSE(config.useCoordFormatting());
    EXPECT_EQ(config.coordPrecision(), 6);
    EXPECT_EQ(config.sortingType(), v::KmlSortingType::None);
    EXPECT_EQ(config.iconScaleType(), v::KmlIconScaleType::Abs);
    EXPECT_FALSE(config.useAbsPathToIcon());
    EXPECT_TRUE(config.absPathToIcon().isEmpty());
    const auto snap = config.getStatic();
    ASSERT_NE(snap, nullptr);
    EXPECT_FALSE(snap->useCoordFormatting());
    EXPECT_EQ(snap->coordPrecision(), 6);
    EXPECT_EQ(snap->sortingType(), v::KmlSortingType::None);
    EXPECT_EQ(snap->iconScaleType(), v::KmlIconScaleType::Abs);
    EXPECT_FALSE(snap->useAbsPathToIcon());
    EXPECT_TRUE(snap->absPathToIcon().isEmpty());
}

// :142-157: each key alone changes exactly its own field (field and snapshot).
TEST(ExportMarks2KmlConfig, ReadEachKeyChangesOnlyItsField) {
    struct Row {
        const char* text;
        bool coordFmt;
        int precision;
        v::KmlSortingType sorting;
        v::KmlIconScaleType scale;
        bool absPath;
        const char16_t* path;
    };
    const Row rows[] = {
        {"UseCoordFormatting=1\r\n", true, 6, v::KmlSortingType::None, v::KmlIconScaleType::Abs, false, u""},
        {"CoordPrecision=9\r\n", false, 9, v::KmlSortingType::None, v::KmlIconScaleType::Abs, false, u""},
        {"SortingType=2\r\n", false, 6, v::KmlSortingType::ByNameDesc, v::KmlIconScaleType::Abs, false, u""},
        {"IconScaleType=3\r\n", false, 6, v::KmlSortingType::None, v::KmlIconScaleType::Large, false, u""},
        {"UseAbsPathToIcon=1\r\n", false, 6, v::KmlSortingType::None, v::KmlIconScaleType::Abs, true, u""},
        {"AbsPathToIcon=Город\r\n", false, 6, v::KmlSortingType::None, v::KmlIconScaleType::Abs, false,
         u"Город"},
    };
    for (const auto& row : rows) {
        SCOPED_TRACE(row.text);
        v::ExportMarks2KmlConfig config;
        readText(config, row.text);
        const QString path = QString::fromUtf16(row.path);
        EXPECT_EQ(config.useCoordFormatting(), row.coordFmt);
        EXPECT_EQ(config.coordPrecision(), row.precision);
        EXPECT_EQ(config.sortingType(), row.sorting);
        EXPECT_EQ(config.iconScaleType(), row.scale);
        EXPECT_EQ(config.useAbsPathToIcon(), row.absPath);
        EXPECT_EQ(config.absPathToIcon(), path);
        const auto snap = config.getStatic();
        EXPECT_EQ(snap->useCoordFormatting(), row.coordFmt);
        EXPECT_EQ(snap->coordPrecision(), row.precision);
        EXPECT_EQ(snap->sortingType(), row.sorting);
        EXPECT_EQ(snap->iconScaleType(), row.scale);
        EXPECT_EQ(snap->useAbsPathToIcon(), row.absPath);
        EXPECT_EQ(snap->absPathToIcon(), path);
    }
}

// :145-147: CoordPrecision outside 4..12 becomes 6, bounds are kept.
TEST(ExportMarks2KmlConfig, CoordPrecisionClampOnRead) {
    struct Row {
        const char* text;
        int expected;
    };
    const Row rows[] = {{"CoordPrecision=3\r\n", 6},
                        {"CoordPrecision=4\r\n", 4},
                        {"CoordPrecision=12\r\n", 12},
                        {"CoordPrecision=13\r\n", 6},
                        {"CoordPrecision=-1\r\n", 6}};
    for (const auto& row : rows) {
        SCOPED_TRACE(row.text);
        v::ExportMarks2KmlConfig config;
        setNonDefault(config); // precision 9 as the current value: the read result is not the old value
        readText(config, row.text);
        EXPECT_EQ(config.coordPrecision(), row.expected);
        EXPECT_EQ(config.getStatic()->coordPrecision(), row.expected);
    }
}

// :145: the test runs on the field after the read, so an absent key resets a setter-made 99 to 6 ...
TEST(ExportMarks2KmlConfig, AbsentCoordPrecisionKeyResetsOutOfRangeCurrentValue) {
    v::ExportMarks2KmlConfig config;
    config.setCoordPrecision(99);
    readText(config, "Other=1\r\n");
    EXPECT_EQ(config.coordPrecision(), 6);
    EXPECT_EQ(config.getStatic()->coordPrecision(), 6);
}

// ... and an absent key keeps an in-range current value (default 6 stays 6, 9 stays 9).
TEST(ExportMarks2KmlConfig, AbsentCoordPrecisionKeyKeepsInRangeValue) {
    v::ExportMarks2KmlConfig config;
    readText(config, "Other=1\r\n");
    EXPECT_EQ(config.coordPrecision(), 6);
    config.setCoordPrecision(9);
    readText(config, "Other=1\r\n");
    EXPECT_EQ(config.coordPrecision(), 9);
}

// :149-156: enum keys outside their range leave the (non-default) current value; inside they are taken.
TEST(ExportMarks2KmlConfig, EnumKeysAssignedOnlyInsideRange) {
    struct Row {
        const char* text;
        v::KmlSortingType sorting;
        v::KmlIconScaleType scale;
    };
    // Current values (set below): ByNameAsc and Small.
    const Row rows[] = {
        {"SortingType=-1\r\n", v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Small},
        {"SortingType=3\r\n", v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Small},
        {"SortingType=0\r\n", v::KmlSortingType::None, v::KmlIconScaleType::Small},
        {"SortingType=2\r\n", v::KmlSortingType::ByNameDesc, v::KmlIconScaleType::Small},
        {"IconScaleType=-1\r\n", v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Small},
        {"IconScaleType=4\r\n", v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Small},
        {"IconScaleType=0\r\n", v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Abs},
        {"IconScaleType=3\r\n", v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Large},
    };
    for (const auto& row : rows) {
        SCOPED_TRACE(row.text);
        v::ExportMarks2KmlConfig config;
        config.setSortingType(v::KmlSortingType::ByNameAsc);
        config.setIconScaleType(v::KmlIconScaleType::Small);
        readText(config, row.text);
        EXPECT_EQ(config.sortingType(), row.sorting);
        EXPECT_EQ(config.iconScaleType(), row.scale);
        EXPECT_EQ(config.getStatic()->sortingType(), row.sorting);
        EXPECT_EQ(config.getStatic()->iconScaleType(), row.scale);
    }
}

// :158 SetChanged is unconditional: a provider without the keys still notifies once; :135 a null provider
// notifies 0 times.
TEST(ExportMarks2KmlConfig, ReadNotifiesOncePerNonNullProvider) {
    v::ExportMarks2KmlConfig config;
    int notified = 0;
    const auto sub = config.changed().subscribe([&notified] { ++notified; });

    readText(config, "Other=1\r\n");
    EXPECT_EQ(notified, 1);

    config.readConfig(nullptr);
    EXPECT_EQ(notified, 1);
}

// :162-173: the six keys, in order, for (true, 9, ByNameDesc, Medium, true, "C:\icons\").
TEST(ExportMarks2KmlConfig, WriteEmitsSixKeysInOrder) {
    v::ExportMarks2KmlConfig config;
    setNonDefault(config);
    const auto root = wrapText("");
    config.writeConfig(root->orCreateSubItem(u"S"));
    EXPECT_EQ(root->file().toBytes(), QByteArray("[S]\r\nUseCoordFormatting=1\r\nCoordPrecision=9\r\n"
                                                 "SortingType=2\r\nIconScaleType=2\r\nUseAbsPathToIcon=1\r\n"
                                                 "AbsPathToIcon=C:\\icons\\\r\n"));
}

// :240-316: a setter notifies only when the value differs, for each of the six.
TEST(ExportMarks2KmlConfig, SettersNotifyOnlyOnChange) {
    v::ExportMarks2KmlConfig config;
    int notified = 0;
    const auto sub = config.changed().subscribe([&notified] { ++notified; });

    config.setUseCoordFormatting(false);
    config.setCoordPrecision(6);
    config.setSortingType(v::KmlSortingType::None);
    config.setIconScaleType(v::KmlIconScaleType::Abs);
    config.setUseAbsPathToIcon(false);
    config.setAbsPathToIcon(QString());
    EXPECT_EQ(notified, 0);

    config.setUseCoordFormatting(true);
    EXPECT_EQ(notified, 1);
    config.setCoordPrecision(7);
    EXPECT_EQ(notified, 2);
    config.setSortingType(v::KmlSortingType::ByNameAsc);
    EXPECT_EQ(notified, 3);
    config.setIconScaleType(v::KmlIconScaleType::Large);
    EXPECT_EQ(notified, 4);
    config.setUseAbsPathToIcon(true);
    EXPECT_EQ(notified, 5);
    config.setAbsPathToIcon(QStringLiteral(u"Город"));
    EXPECT_EQ(notified, 6);
    config.setAbsPathToIcon(QStringLiteral(u"город")); // differs by case only
    EXPECT_EQ(notified, 7);
}

// :253-264: no setter clamps.
TEST(ExportMarks2KmlConfig, SetCoordPrecisionStoresOutOfRangeValue) {
    v::ExportMarks2KmlConfig config;
    config.setCoordPrecision(99);
    EXPECT_EQ(config.coordPrecision(), 99);
    EXPECT_EQ(config.getStatic()->coordPrecision(), 99);
}

// Values set through the setters read back per field from getStatic(); an older snapshot is immutable.
TEST(ExportMarks2KmlConfig, SnapshotFollowsSettersAndOldOneIsImmutable) {
    v::ExportMarks2KmlConfig config;
    const auto before = config.getStatic();
    setNonDefault(config);
    const auto after = config.getStatic();
    EXPECT_TRUE(after->useCoordFormatting());
    EXPECT_EQ(after->coordPrecision(), 9);
    EXPECT_EQ(after->sortingType(), v::KmlSortingType::ByNameDesc);
    EXPECT_EQ(after->iconScaleType(), v::KmlIconScaleType::Medium);
    EXPECT_TRUE(after->useAbsPathToIcon());
    EXPECT_EQ(after->absPathToIcon(), QStringLiteral(u"C:\\icons\\"));

    EXPECT_FALSE(before->useCoordFormatting());
    EXPECT_EQ(before->coordPrecision(), 6);
    EXPECT_EQ(before->sortingType(), v::KmlSortingType::None);
    EXPECT_EQ(before->iconScaleType(), v::KmlIconScaleType::Abs);
    EXPECT_FALSE(before->useAbsPathToIcon());
    EXPECT_TRUE(before->absPathToIcon().isEmpty());
}

// :318-336: the static constructed directly answers every getter with its own argument.
TEST(ExportMarks2KmlConfig, StaticConstructorKeepsEachArgument) {
    const v::ExportMarks2KmlConfigStatic s(true, 7, v::KmlSortingType::ByNameAsc, v::KmlIconScaleType::Large,
                                           false, QStringLiteral(u"p"));
    EXPECT_TRUE(s.useCoordFormatting());
    EXPECT_EQ(s.coordPrecision(), 7);
    EXPECT_EQ(s.sortingType(), v::KmlSortingType::ByNameAsc);
    EXPECT_EQ(s.iconScaleType(), v::KmlIconScaleType::Large);
    EXPECT_FALSE(s.useAbsPathToIcon());
    EXPECT_EQ(s.absPathToIcon(), QStringLiteral(u"p"));
}
