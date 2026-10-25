// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-030, FEAT-NET-002. Ports u_MapTypeGUIConfig.pas, u_MapTypeGUIConfigStatic.pas;
// docs/specs/maptype.md §2.7, §2.10, §7.2 item 6; legacy-defects L-151 (pnum), L-153 (HotKey).
// Oracle: tests/fixtures/maptype/Maps_stock_extract.ini, written by legacy SAS.Planet.
#include "cg/core/language_manager.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/map_version_request_config.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <climits>
#include <memory>

#include "maptype_test_util.h"
#include "zmp_info_gui_stub.h"

using namespace cg::maptype;
using cg::maptype::test::iniFrom;
using cg::maptype::test::ZmpGuiStub;
using namespace Qt::StringLiterals;

namespace {

constexpr auto kGuid = u"{D0439E79-1D2D-4364-A61E-41757BD819AF}"; // sky_chandracomp.zmp

// en, then es fa fr ru tr uk (stock lang folder; the files' content does not matter).
struct Fixture {
    QTemporaryDir dir;
    std::shared_ptr<cg::core::LanguageManager> languages;
    std::shared_ptr<ZmpGuiStub> zmp = std::make_shared<ZmpGuiStub>();
    std::shared_ptr<MapTypeGuiConfig> config;
    Fixture() {
        for (const char* code : {"es", "fa", "fr", "ru", "tr", "uk"}) {
            QFile f(QDir(dir.path()).filePath(QString::fromLatin1(code) + u".mo"_s));
            EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        }
        languages = std::make_shared<cg::core::LanguageManager>(dir.path());
        config = std::make_shared<MapTypeGuiConfig>(languages, zmp);
    }
};

// First section of the stock extract (BOM included), without the blank separator line.
// The BOM belongs to the file writer (GlobalState), not to the elements: an INI made from nothing
// has none, so the proof compares the stock bytes after the 3 BOM bytes (header and keys are the port's).
QByteArray stockFirstSection(bool withBom = true) {
    QFile f(cg::maptype::test::fixtureDir() + u"/maptype/Maps_stock_extract.ini"_s);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    const QByteArray section = data.left(data.indexOf("\r\n\r\n") + 2);
    return withBom ? section : section.mid(3);
}

// What MapType.SaveConfig writes for the map: the GUI config, then the version request config.
QByteArray save(const MapTypeGuiConfig& gui, const MapVersionRequestConfig& version,
                const QByteArray& before = {}) {
    auto ini =
        iniFrom(before.isEmpty() ? QByteArray()
                                 : "\xEF\xBB\xBF[" + QString::fromUtf16(kGuid).toUtf8() + "]\r\n" + before);
    const auto section = ini->orCreateSubItem(kGuid);
    gui.writeConfig(section);
    version.writeConfig(section);
    return ini->file().toBytes();
}

TEST(MapTypeGuiConfig, UntouchedMapWithSortIndexSetWritesTheStockShape) {
    CG_SKIP_IF_NO_FIXTURE(cg::maptype::test::fixtureDir() + u"/maptype/Maps_stock_extract.ini"_s);
    Fixture f;
    MapVersionRequestConfig version(u""_s);
    // Without the main list's renumbering pnum would be INT_MAX against the raw ZMP -1 (L-151).
    EXPECT_EQ(save(*f.config, version), "[{D0439E79-1D2D-4364-A61E-41757BD819AF}]\r\n"
                                        "MapName_=chandracomp (Google)\r\npnum=2147483647\r\n"
                                        "ShowOtherVersions=1\r\n");
    f.config->setSortIndex(1);
    const QByteArray written = save(*f.config, version);
    EXPECT_EQ(written, stockFirstSection(false));
}

// Read of the legacy-written section, then write: the §7.2 item 6 round trip.
TEST(MapTypeGuiConfig, StockSectionReadsAndWritesBackUnchanged) {
    CG_SKIP_IF_NO_FIXTURE(cg::maptype::test::fixtureDir() + u"/maptype/Maps_stock_extract.ini"_s);
    Fixture f;
    MapVersionRequestConfig version(u""_s);
    const auto stock = iniFrom(stockFirstSection());
    f.config->readConfig(stock->subItem(kGuid));
    version.readConfig(stock->subItem(kGuid));
    EXPECT_EQ(f.config->sortIndex(), 1);
    EXPECT_EQ(save(*f.config, version), stockFirstSection(false));
}

// Each own key is written when it differs from the raw ZMP value and deleted when it equals it;
// the name is MapName (and no MapName_) once it differs from the ZMP name.
TEST(MapTypeGuiConfig, OwnKeysFollowTheDiffersRuleInTheLegacyOrder) {
    Fixture f;
    f.config->setSeparator(true);
    f.config->setEnabled(false);
    f.config->setSortIndex(7);
    f.config->setHotKey(0x8041);
    f.config->name()->setValue(u"Mine"_s);
    f.config->parentSubMenu()->setValue(u"A\\B"_s);
    MapVersionRequestConfig version(u""_s);
    EXPECT_EQ(
        save(*f.config, version, "MapName_=x\r\n").toStdString(),
        std::string(
            "\xEF\xBB\xBF[{D0439E79-1D2D-4364-A61E-41757BD819AF}]\r\nMapName=Mine\r\nParentSubMenu=A\\B\r\n"
            "Separator=1\r\nEnabled=0\r\npnum=7\r\nHotKey=32833\r\nShowOtherVersions=1\r\n"));
    f.config->setSeparator(false);
    f.config->setEnabled(true);
    f.config->setSortIndex(-3); // stored as INT_MAX
    EXPECT_EQ(f.config->sortIndex(), INT_MAX);
    f.config->setHotKey(0);
    const QByteArray again = save(*f.config, version, "Separator=1\r\nEnabled=0\r\nHotKey=5\r\n");
    EXPECT_FALSE(again.contains("Separator"));
    EXPECT_FALSE(again.contains("Enabled"));
    EXPECT_FALSE(again.contains("HotKey"));
    EXPECT_TRUE(again.contains("pnum=2147483647"));
}

TEST(MapTypeGuiConfig, ReadNormalisesSortIndexTruncatesHotKeyAndAlwaysNotifies) {
    Fixture f;
    int n = 0;
    const auto sub = f.config->changed().subscribe([&] { ++n; });
    f.config->readConfig(
        iniFrom("[S]\r\npnum=-5\r\nHotKey=70000\r\nseparator=1\r\nEnabled=0\r\n")->subItem(u"S"));
    EXPECT_EQ(n, 1);
    const auto st = f.config->getStatic();
    EXPECT_EQ(st->sortIndex, INT_MAX);
    EXPECT_EQ(st->hotKey, 4464); // L-153: 70000 & 0xFFFF
    EXPECT_TRUE(st->separator);
    EXPECT_FALSE(st->enabled);
    f.config->readConfig(iniFrom("[S]\r\n")->subItem(u"S")); // nothing differs, still notifies
    EXPECT_EQ(n, 2);
    f.config->readConfig(nullptr);
    EXPECT_EQ(n, 2);
}

TEST(MapTypeGuiConfig, LanguageChangeRebuildsTheStaticBeforeSubscribersRun) {
    Fixture f;
    EXPECT_EQ(f.config->getStatic()->name, u"chandracomp (Google)"_s);
    EXPECT_EQ(f.config->getStatic()->infoUrl, u"http://example/info"_s);
    QString seen;
    const auto sub = f.config->changed().subscribe([&] { seen = f.config->getStatic()->name; });
    f.languages->setCurrentLanguageCode(u"ru"_s);
    EXPECT_EQ(seen, u"Звёзды Chandracomp (Google)"_s);
    EXPECT_EQ(f.config->getStatic()->parentSubMenu, u"Для просмотра кэша\\Космос"_s);
}

} // namespace
