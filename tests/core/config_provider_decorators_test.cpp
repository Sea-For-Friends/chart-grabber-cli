// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-201, FEAT-UI-203, FEAT-UI-204; docs/specs/core.md 2.15, test plan 7.8
#include "cg/core/config_provider_folder.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/config_provider_resource.h"
#include "cg/core/config_provider_virtual.h"
#include "cg/core/config_provider_with_global.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

#include "fixture_path.h"

using cg::core::ConfigProviderFolder;
using cg::core::ConfigProviderIni;
using cg::core::ConfigProviderResource;
using cg::core::ConfigProviderVirtualWithSubItem;
using cg::core::ConfigProviderWithGlobal;
using cg::core::IConfigProvider;
using cg::core::IConfigProviderPtr;
using cg::core::IConfigWriterPtr;
using cg::core::Result;

TEST(Folder, SubItemForDirectoryIsFolderProvider) {
    const IConfigProviderPtr root = ConfigProviderFolder::create(cgFixture(QStringLiteral("ini")));
    const IConfigProviderPtr terrain = root->subItem(u"TerrainData");
    ASSERT_NE(terrain, nullptr);
    EXPECT_TRUE(terrain->subItemNames().contains(QStringLiteral("SASTerrain.Default.ini")));
}

TEST(Folder, SubItemForIniAndTxtIsIniProvider) {
    const IConfigProviderPtr root = ConfigProviderFolder::create(cgFixture(QStringLiteral("ini")));
    const IConfigProviderPtr ini = root->subItem(u"TileStorage_DBMS.Default.ini");
    ASSERT_NE(ini, nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<const ConfigProviderIni>(ini), nullptr);
    EXPECT_TRUE(ini->subItemNames().contains(QStringLiteral("SASGIS_DBMS\\$")));
}

TEST(Folder, SubItemForOtherExtensionIsNull) {
    const IConfigProviderPtr root = ConfigProviderFolder::create(cgFixture(QStringLiteral("ini")));
    EXPECT_EQ(root->subItem(u"README.md"), nullptr);
}

TEST(Folder, BinaryReturnsFileBytes) {
    const IConfigProviderPtr root = ConfigProviderFolder::create(cgFixture(QStringLiteral("ini")));
    QFile direct(cgFixture(QStringLiteral("ini/README.md")));
    ASSERT_TRUE(direct.open(QIODevice::ReadOnly));
    EXPECT_EQ(root->binary(u"README.md"), direct.readAll());
    EXPECT_TRUE(root->binary(u"no-such-file").isEmpty());
}

TEST(Resource, BinaryReadsQtResource) {
    const IConfigProviderPtr resource = ConfigProviderResource::create();
    EXPECT_EQ(resource->binary(u"dummy_a.txt"), QByteArrayLiteral("sas dummy resource A\n"));
    EXPECT_EQ(resource->binary(u"dummy_b.txt"), QByteArrayLiteral("sas dummy resource B\n"));
    EXPECT_TRUE(resource->binary(u"no-such-resource").isEmpty());
}

TEST(Resource, SubItemIsAlwaysNull) {
    const IConfigProviderPtr resource = ConfigProviderResource::create();
    EXPECT_EQ(resource->subItem(u"dummy_a.txt"), nullptr);
    EXPECT_TRUE(resource->subItemNames().isEmpty());
    EXPECT_TRUE(resource->valueNames().isEmpty());
    EXPECT_FALSE(resource->value(u"dummy_a.txt").isValid());
}

TEST(WithGlobal, PrefixedIdentRoutesToGlobal) {
    Result<std::shared_ptr<ConfigProviderIni>> main =
        ConfigProviderIni::open(cgFixture(QStringLiteral("ini/quirks.ini")));
    ASSERT_TRUE(main.has_value());
    const IConfigProviderPtr global = ConfigProviderVirtualWithSubItem::create(
        QStringLiteral("Resource"), ConfigProviderResource::create());
    const IConfigWriterPtr withGlobal =
        ConfigProviderWithGlobal::create(main.value(), QStringLiteral("sas:\\"), global);

    const IConfigProviderPtr resourceNode = withGlobal->subItem(u"sas:\\Resource");
    ASSERT_NE(resourceNode, nullptr);
    EXPECT_EQ(resourceNode->binary(u"dummy_a.txt"), QByteArrayLiteral("sas dummy resource A\n"));
    EXPECT_EQ(withGlobal->subItem(u"sas:\\NotResource"), nullptr);
}

TEST(WithGlobal, UnprefixedIdentRoutesToMain) {
    Result<std::shared_ptr<ConfigProviderIni>> main =
        ConfigProviderIni::open(cgFixture(QStringLiteral("ini/quirks.ini")));
    ASSERT_TRUE(main.has_value());
    const IConfigWriterPtr withGlobal =
        ConfigProviderWithGlobal::create(main.value(), QStringLiteral("sas:\\"), nullptr);
    const IConfigProviderPtr plain = withGlobal->subItem(u"Plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->readString(u"Simple", QString()), QStringLiteral("value"));
    // The child must be re-wrapped (not the raw ConfigProviderIniSection) so that sas:\ routing
    // and the missing-branch sentinel both hold below the root too.
    EXPECT_NE(std::dynamic_pointer_cast<const ConfigProviderWithGlobal>(plain), nullptr);
}

TEST(WithGlobal, SasPrefixRoutesToGlobalTwoLevelsDeep) {
    Result<std::shared_ptr<ConfigProviderIni>> main =
        ConfigProviderIni::open(cgFixture(QStringLiteral("ini/quirks.ini")));
    ASSERT_TRUE(main.has_value());
    const IConfigProviderPtr global = ConfigProviderVirtualWithSubItem::create(
        QStringLiteral("Resource"), ConfigProviderResource::create());
    const IConfigWriterPtr withGlobal =
        ConfigProviderWithGlobal::create(main.value(), QStringLiteral("sas:\\"), global);

    const IConfigProviderPtr plain = withGlobal->subItem(u"Plain");
    ASSERT_NE(plain, nullptr);
    const IConfigProviderPtr resourceNode = plain->subItem(u"sas:\\Resource");
    ASSERT_NE(resourceNode, nullptr);
    EXPECT_EQ(resourceNode->binary(u"dummy_a.txt"), QByteArrayLiteral("sas dummy resource A\n"));
}

TEST(WithGlobal, MissingSectionSubItemReturnsDefaultsNotNull) {
    Result<std::shared_ptr<ConfigProviderIni>> main =
        ConfigProviderIni::open(cgFixture(QStringLiteral("ini/quirks.ini")));
    ASSERT_TRUE(main.has_value());
    const IConfigWriterPtr withGlobal =
        ConfigProviderWithGlobal::create(main.value(), QStringLiteral("sas:\\"), nullptr);

    const IConfigProviderPtr missing = withGlobal->subItem(u"NoSuchSection");
    ASSERT_NE(missing, nullptr);
    EXPECT_EQ(missing->readString(u"Whatever", QStringLiteral("def")), QStringLiteral("def"));
}

TEST(WithGlobal, ChainedDescentThroughMissingSectionKeepsAnsweringDefaults) {
    Result<std::shared_ptr<ConfigProviderIni>> main =
        ConfigProviderIni::open(cgFixture(QStringLiteral("ini/quirks.ini")));
    ASSERT_TRUE(main.has_value());
    const IConfigWriterPtr withGlobal =
        ConfigProviderWithGlobal::create(main.value(), QStringLiteral("sas:\\"), nullptr);

    const IConfigProviderPtr missing = withGlobal->subItem(u"NoSuchSection");
    ASSERT_NE(missing, nullptr);
    const IConfigProviderPtr grandchild = missing->subItem(u"AnythingElse");
    ASSERT_NE(grandchild, nullptr);
    EXPECT_EQ(grandchild->readString(u"Whatever", QStringLiteral("def")), QStringLiteral("def"));
}

TEST(WithGlobal, NullMainSubItemReturnsSelf) {
    const IConfigWriterPtr withGlobal =
        ConfigProviderWithGlobal::create(nullptr, QStringLiteral("sas:\\"), nullptr);
    const IConfigProviderPtr self = withGlobal->subItem(u"AnythingAtAll");
    EXPECT_EQ(self.get(), static_cast<const IConfigProvider*>(withGlobal.get()));
    // Reads through the self-referencing sentinel answer with defaults, never crash.
    EXPECT_EQ(self->readString(u"Whatever", QStringLiteral("def")), QStringLiteral("def"));
}

TEST(Virtual, OnlyTheNamedChildResolves) {
    const IConfigProviderPtr resource = ConfigProviderResource::create();
    const IConfigProviderPtr v =
        ConfigProviderVirtualWithSubItem::create(QStringLiteral("Resource"), resource);
    EXPECT_EQ(v->subItem(u"Resource"), resource);
    EXPECT_EQ(v->subItem(u"Other"), nullptr);
    EXPECT_EQ(v->subItemNames(), (QStringList{QStringLiteral("Resource")}));
    EXPECT_FALSE(v->value(u"Resource").isValid());
}
