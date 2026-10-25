// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (held open). Ports u_MarkSystemConfig.pas,
// u_MarkSystemImplConfigBase.pas, SML/u_MarkSystemImplConfigSML.pas, mORMot/u_MarkSystemImplConfigORM.pas;
// docs/specs/marks.md section 2.6.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_system_config.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/sml/mark_system_impl_config_sml.h"

#include <gtest/gtest.h>

#include <memory>

#include "../corpus_skip.h"
#include "fixture_path.h"

namespace m = cg::marks;
namespace c = cg::core;

namespace {

std::shared_ptr<c::ConfigProviderIni> loadFixture(QByteArray* raw = nullptr) {
    auto file = c::IniFile::fromFile(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    EXPECT_TRUE(file.has_value());
    if (raw != nullptr) {
        *raw = file->toBytes();
    }
    return c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*file)));
}

} // namespace

// u_MarkSystemConfig.pas:186-324: the real [MarkSystemConfig] section reads and is written back
// byte-identically (the item is the legacy default: ORM, Marks.db3, 100 MB cache).
TEST(MarkSystemConfig, FixtureRoundTripIsByteIdentical) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/SASPlanet.marks-sections.ini")));
    QByteArray original;
    const auto root = loadFixture(&original);

    m::MarkSystemConfig config;
    config.readConfig(root);
    EXPECT_TRUE(config.readErrors().isEmpty());
    ASSERT_EQ(config.count(), 1);
    EXPECT_EQ(config.activeConfigId(), 1);
    const auto item = config.byId(1);
    ASSERT_TRUE(item);
    EXPECT_EQ(item->databaseGUID(), m::cORMSQLiteMarksDbGUID);
    EXPECT_EQ(item->displayName(), QStringLiteral("My Marks"));
    EXPECT_EQ(item->implConfig()->fileName(), QStringLiteral("Marks.db3"));
    const auto* orm = dynamic_cast<const m::IMarkSystemImplConfigORM*>(item->implConfig().get());
    ASSERT_NE(orm, nullptr);
    EXPECT_EQ(orm->cacheSizeMb(), 100U);

    config.writeConfig(root);
    EXPECT_EQ(root->file().toBytes(), original);
}

// LC-6 (spec section 8, reproduced): ActiveIndex stores an id, ids are re-issued densely on load.
TEST(MarkSystemConfig, ActiveIndexIsAnIdReissuedOnLoad) {
    m::MarkSystemConfig config;
    const auto sml = std::make_shared<m::MarkSystemImplConfigSML>(QStringLiteral("a.sml"), false);
    const auto orm1 = std::make_shared<m::MarkSystemImplConfigORM>(QStringLiteral("b.db3"), false, QString(),
                                                                   QString(), QString());
    const auto orm2 = std::make_shared<m::MarkSystemImplConfigORM>(
        QStringLiteral("c.db3"), true, QStringLiteral("u"), QString(), QStringLiteral("pw"), 7);
    EXPECT_EQ(config.add(m::cSMLMarksDbGUID, QStringLiteral("A"), sml, false), 1);
    EXPECT_EQ(config.add(m::cORMSQLiteMarksDbGUID, QStringLiteral("B"), orm1, true), 2);
    EXPECT_EQ(config.add(m::cORMSQLiteMarksDbGUID, QStringLiteral("C"), orm2, false), 3);
    config.deleteById(1); // the active item (id 2, "B") keeps its id here

    const auto root = c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>());
    config.writeConfig(root);
    EXPECT_EQ(root->subItem(u"MarkSystemConfig")->readInt(u"ActiveIndex", -1), 2);
    EXPECT_EQ(root->subItem(u"MarkSystemConfig")->readString(u"Item2_Password"), QStringLiteral("pw"));

    m::MarkSystemConfig reloaded;
    reloaded.readConfig(root);
    ASSERT_EQ(reloaded.count(), 2);
    EXPECT_EQ(reloaded.activeConfigId(), 2); // stored id, not "B"'s new id 1
    EXPECT_EQ(reloaded.activeConfig()->displayName(),
              QStringLiteral("C")); // the active item moved from B to C
    const auto* orm = dynamic_cast<const m::IMarkSystemImplConfigORM*>(reloaded.byId(2)->implConfig().get());
    ASSERT_NE(orm, nullptr);
    EXPECT_EQ(orm->cacheSizeMb(), 7U);
    EXPECT_TRUE(reloaded.byId(2)->implConfig()->isReadOnly());
}

// Writer: SML item stops after Impl, ORM item adds four keys (u_MarkSystemConfig.pas:291-318).
TEST(MarkSystemConfig, WritesSmlAndOrmItemsAndReadsThemBack) {
    m::MarkSystemConfig config;
    config.add(m::cSMLMarksDbGUID, QStringLiteral("S"),
               std::make_shared<m::MarkSystemImplConfigSML>(QStringLiteral("a.sml"), true), true);
    config.add(m::cORMSQLiteMarksDbGUID, QStringLiteral("O"),
               std::make_shared<m::MarkSystemImplConfigORM>(
                   QStringLiteral("b.db3"), false, QStringLiteral("usr"), QString(), QStringLiteral("pw"), 5,
                   QStringLiteral("  sch  ")),
               false);
    auto file = std::make_shared<c::IniFile>();
    const auto root = c::ConfigProviderIni::wrap(file);
    config.writeConfig(root);
    const QByteArray expected = "\xEF\xBB\xBF" // a fresh file starts with the UTF-8 BOM
                                "[MarkSystemConfig]\r\n"
                                "Count=2\r\n"
                                "ActiveIndex=1\r\n"
                                "Item1_Database={F5A9FB07-FF9B-4B4A-98D3-AB8C5F24F36B}\r\n"
                                "Item1_DisplayName=S\r\n"
                                "Item1_FileName=a.sml\r\n"
                                "Item1_IsReadOnly=1\r\n"
                                "Item1_Impl={E687ABD9-5F0C-4AF2-8E0C-EF09A4506A36}\r\n"
                                "Item2_Database={776BC34B-7A4B-4237-BE9A-D905228FDA1A}\r\n"
                                "Item2_DisplayName=O\r\n"
                                "Item2_FileName=b.db3\r\n"
                                "Item2_IsReadOnly=0\r\n"
                                "Item2_Impl={4922B216-1197-4BB0-93E1-CC2CC1B68787}\r\n"
                                "Item2_UserName=usr\r\n"
                                "Item2_Password=pw\r\n"
                                "Item2_CacheSizeMb=5\r\n"
                                "Item2_ForcedSchemaName=sch\r\n";
    EXPECT_EQ(file->toBytes(), expected);

    m::MarkSystemConfig reloaded;
    reloaded.readConfig(root);
    ASSERT_EQ(reloaded.count(), 2);
    EXPECT_NE(dynamic_cast<const m::IMarkSystemImplConfigSML*>(reloaded.byId(1)->implConfig().get()),
              nullptr);
    EXPECT_TRUE(reloaded.byId(1)->implConfig()->isReadOnly());
    EXPECT_EQ(reloaded.byId(1)->databaseGUID(), m::cSMLMarksDbGUID);
    const auto* orm = dynamic_cast<const m::IMarkSystemImplConfigORM*>(reloaded.byId(2)->implConfig().get());
    ASSERT_NE(orm, nullptr);
    EXPECT_EQ(orm->userName(), QStringLiteral("usr"));
    EXPECT_EQ(orm->forcedSchemaName(), QStringLiteral("sch"));
}

// Read errors (:214-256), missing ActiveIndex (-1, :210), a second read accumulates (L row).
TEST(MarkSystemConfig, ReadSkipsBadItemsAndAccumulatesOnSecondRead) {
    const QByteArray text = "[MarkSystemConfig]\r\n"
                            "Count=3\r\n"
                            "Item1_DisplayName=NoDb\r\n"
                            "Item1_Impl={E687ABD9-5F0C-4AF2-8E0C-EF09A4506A36}\r\n"
                            "Item2_Database={776BC34B-7A4B-4237-BE9A-D905228FDA1A}\r\n"
                            "Item2_Impl={11111111-2222-3333-4444-555555555555}\r\n"
                            "Item3_Database={F5A9FB07-FF9B-4B4A-98D3-AB8C5F24F36B}\r\n"
                            "Item3_DisplayName=Ok\r\n"
                            "Item3_FileName=x.sml\r\n"
                            "Item3_Impl={E687ABD9-5F0C-4AF2-8E0C-EF09A4506A36}\r\n";
    auto parsed = c::IniFile::fromBytes(text);
    ASSERT_TRUE(parsed.has_value());
    const auto root = c::ConfigProviderIni::wrap(std::make_shared<c::IniFile>(std::move(*parsed)));

    m::MarkSystemConfig config;
    config.readConfig(root);
    EXPECT_EQ(
        config.readErrors(),
        (QStringList{
            QStringLiteral("MarkSystemConfig: Item #1 Database GUID read error!"),
            QStringLiteral(
                "MarkSystemConfig: Item #2 has unknown Impl GUID: {11111111-2222-3333-4444-555555555555}")}));
    ASSERT_EQ(config.count(), 1);
    EXPECT_EQ(config.byId(1)->displayName(), QStringLiteral("Ok"));
    EXPECT_EQ(config.activeConfigId(), -1);
    EXPECT_EQ(config.activeConfig(), nullptr);

    config.readConfig(root); // list and id counter are not reset
    EXPECT_EQ(config.count(), 2);
    EXPECT_NE(config.byId(2), nullptr);
}

TEST(MarkSystemConfig, DeleteActiveMovesToSmallestIdAndNotifiesOnlyOnChange) {
    m::MarkSystemConfig config;
    const auto impl = std::make_shared<m::MarkSystemImplConfigSML>(QStringLiteral("a.sml"), false);
    for (int i = 0; i < 3; ++i) {
        config.add(m::cSMLMarksDbGUID, QStringLiteral("x"), impl, i == 2);
    }
    EXPECT_EQ(config.activeConfigId(), 3);
    int notified = 0;
    const auto sub = config.changed().subscribe([&notified] { ++notified; });

    config.deleteById(3);
    EXPECT_EQ(config.activeConfigId(), 1);
    EXPECT_EQ(notified, 1);
    config.deleteById(1);
    EXPECT_EQ(config.activeConfigId(), 2);
    config.deleteById(2);
    EXPECT_EQ(config.activeConfigId(), 0);
    EXPECT_EQ(notified, 3);

    config.deleteById(99);
    config.setActiveConfigId(0);
    EXPECT_EQ(notified, 3);

    EXPECT_EQ(config.add(m::cSMLMarksDbGUID, QStringLiteral("y"), impl, false), 4); // ids are not reused
    EXPECT_EQ(config.add(m::cSMLMarksDbGUID, QStringLiteral("z"), nullptr, false),
              0); // D-77: nil impl ignored
    config.update(4, m::cSMLMarksDbGUID, QStringLiteral("w"), nullptr);
    EXPECT_EQ(config.byId(4)->displayName(), QStringLiteral("y"));
    EXPECT_EQ(config.count(), 1);
}
