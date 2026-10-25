// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-201, FEAT-NET-001 (ZMP folder members, maptype spec 2.2; L-117)
#include "cg/core/config_provider_folder.h"
#include "cg/core/platform/test_support.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "fixture_path.h"

using cg::core::ConfigProviderFolder;
using cg::core::IConfigProviderPtr;

namespace {

QString zmp(const QString& relative) {
    return cgFixture(QStringLiteral("zmp/sas.maps/") + relative);
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << path.toStdString();
    file.write(bytes);
}

} // namespace

TEST(FolderValues, StockZmpTextMembers) {
    CG_SKIP_IF_NO_CORPUS();
    const IConfigProviderPtr garmin = ConfigProviderFolder::create(zmp(QStringLiteral("Garmin.zmp")));
    EXPECT_EQ(garmin->value(u"::FileName").toString(), zmp(QStringLiteral("Garmin.zmp")));
    EXPECT_FALSE(garmin->value(u"GetUrlScript.txt").toString().isEmpty());
    EXPECT_FALSE(garmin->value(u"24.bmp").isValid());     // not .ini/.html/.txt
    EXPECT_FALSE(garmin->value(u"index.html").isValid()); // missing file
    EXPECT_EQ(garmin->readString(u"index.html", QStringLiteral("dflt")), QStringLiteral("dflt"));

    // Decoding: a UTF-8 file with a BOM, and a CP1251 file without one (non-ASCII text in both).
    struct Row {
        QString zmpPath;
        QString expected;
    };
    const Row rows[] = {
        {QStringLiteral("__anygis_filegenerator/Belarus_LatLon.zmp"),
         QString::fromUtf8(
             "\xD0\xAD\xD1\x82\xD0\xBE\xD1\x82 \xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB \xD0\xB1\xD1\x8B\xD0\xBB "
             "\xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xBD "
             "\xD0\xB0\xD0\xB2\xD1\x82\xD0\xBE\xD0\xBC\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0"
             "\xBA\xD0\xB8")},
        {QStringLiteral("GGC/karta_ggc_1km.zmp"),
         QString::fromUtf8(
             "\xD0\x92\xD1\x81\xD0\xB5 \xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xB0 \xD0\xBD\xD0\xB0 "
             "\xD0\xBA\xD0\xB0\xD1\x80\xD1\x82\xD1\x8B")},
    };
    for (const Row& row : rows) {
        const QString text = ConfigProviderFolder::create(zmp(row.zmpPath))->value(u"info.txt").toString();
        EXPECT_TRUE(text.contains(row.expected)) << row.zmpPath.toStdString();
        EXPECT_FALSE(text.startsWith(QChar(0xFEFF))) << row.zmpPath.toStdString(); // BOM consumed
    }
}

TEST(FolderValues, ValueNamesListEveryFileButIni) {
    CG_SKIP_IF_NO_CORPUS();
    const IConfigProviderPtr root = ConfigProviderFolder::create(zmp(QStringLiteral("Garmin.zmp")));
    EXPECT_EQ(root->valueNames(), (QStringList{QStringLiteral("24.bmp"), QStringLiteral("GetUrlScript.txt"),
                                               QStringLiteral("info.txt"), QStringLiteral("params.txt")}));
    // SubItemNames: ini/txt files only, no directories (legacy ReadSubItemsList).
    EXPECT_EQ(root->subItemNames(), (QStringList{QStringLiteral("GetUrlScript.txt"),
                                                 QStringLiteral("info.txt"), QStringLiteral("params.txt")}));
}

TEST(FolderValues, EmptyTilesOfStockZmp) {
    CG_SKIP_IF_NO_CORPUS();
    const IConfigProviderPtr root = ConfigProviderFolder::create(zmp(QStringLiteral("Bing/Bing_Sat.zmp")));
    const IConfigProviderPtr tiles = root->subItem(u"EmptyTiles");
    ASSERT_NE(tiles, nullptr);
    const QStringList names = tiles->valueNames();
    ASSERT_FALSE(names.isEmpty());
    EXPECT_TRUE(names.contains(QStringLiteral("NA.png")));
    EXPECT_EQ(tiles->binary(u"NA.png"), readFile(zmp(QStringLiteral("Bing/Bing_Sat.zmp/EmptyTiles/NA.png"))));
    EXPECT_FALSE(root->subItemNames().isEmpty());
    EXPECT_FALSE(root->subItemNames().contains(QStringLiteral("EmptyTiles")));
}

TEST(FolderValues, StrayFileInSampleFolderIsAValue) { // L-117
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("Thumbs.db")), "x");
    writeFile(dir.filePath(QStringLiteral("a.INI")), "[S]\n");
    writeFile(dir.filePath(QStringLiteral("b.txt")), "t");
    const IConfigProviderPtr root = ConfigProviderFolder::create(dir.path());
    EXPECT_EQ(root->valueNames(), (QStringList{QStringLiteral("Thumbs.db"), QStringLiteral("b.txt")}));
}

TEST(FolderCaseRule, ExactThenCaseInsensitive) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("Params.txt")), "[PARAMS]\nName=x\n");
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("emptytiles")));
    writeFile(dir.filePath(QStringLiteral("emptytiles/A.png")), "png");
    const IConfigProviderPtr root = ConfigProviderFolder::create(dir.path());
    EXPECT_EQ(root->value(u"params.txt").toString(), QStringLiteral("[PARAMS]\nName=x\n"));
    ASSERT_NE(root->subItem(u"PARAMS.TXT"), nullptr);
    EXPECT_EQ(root->binary(u"PARAMS.txt"), QByteArrayLiteral("[PARAMS]\nName=x\n"));
    const IConfigProviderPtr tiles = root->subItem(u"EmptyTiles");
    ASSERT_NE(tiles, nullptr);
    EXPECT_EQ(tiles->binary(u"a.PNG"), QByteArrayLiteral("png"));
    EXPECT_EQ(root->subItem(u"other.txt"), nullptr);
}

TEST(FolderCaseRule, ExactWinsAndTiesTakeFirstInNameOrder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    if (!cg::core::platform::directoryIsCaseSensitive(dir.path())) {
        GTEST_SKIP()
            << "the folder is case-insensitive (NTFS): three names that differ only in case cannot exist";
    }
    writeFile(dir.filePath(QStringLiteral("info.txt")), "lower");
    writeFile(dir.filePath(QStringLiteral("INFO.txt")), "upper");
    writeFile(dir.filePath(QStringLiteral("Info.TXT")), "mixed");
    const IConfigProviderPtr root = ConfigProviderFolder::create(dir.path());
    EXPECT_EQ(root->value(u"info.txt").toString(), QStringLiteral("lower"));
    EXPECT_EQ(root->value(u"INFO.txt").toString(), QStringLiteral("upper"));
    // No exact match: the first of "INFO.txt", "Info.TXT", "info.txt" in QDir::Name order.
    EXPECT_EQ(root->value(u"iNfO.txt").toString(), QStringLiteral("upper"));
}
