// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-033. Ports Network/u_TilePostDownloadCropConfigStatic.pas;
// docs/specs/maptype.md §2.6.6, §7.3 (crop/cut validity), §7.2 (corpus keys).
#include "cg/core/config_provider_ini.h"
#include "cg/maptype/tile_post_download_crop_config.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>

#include <gtest/gtest.h>

#include <map>

#include "maptype_test_util.h"

using cg::geo::PointI;
using cg::geo::RectI;
using cg::maptype::TilePostDownloadCropConfigStatic;

namespace {

TEST(TilePostDownloadCropConfigStatic, CropValidityVectors) {
    struct Row {
        RectI in;
        bool on;
    };
    const Row rows[] = {
        {{0, 0, 256, 256}, true},    {{10, 20, 30, 40}, true},    {{0, 0, 9999, 9999}, true},
        {{0, 0, 0, 0}, false},       {{-1, 0, 256, 256}, false},  {{0, -1, 256, 256}, false},
        {{5, 0, 5, 256}, false},     {{0, 5, 256, 5}, false},     {{6, 0, 5, 256}, false},
        {{0, 0, 10000, 256}, false}, {{0, 0, 256, 10000}, false},
    };
    for (const Row& r : rows) {
        const TilePostDownloadCropConfigStatic c(r.in, {}, {}, {}, QString());
        SCOPED_TRACE(::testing::Message()
                     << r.in.left << ',' << r.in.top << ',' << r.in.right << ',' << r.in.bottom);
        EXPECT_EQ(c.isCropOnDownload(), r.on);
        const RectI want = r.on ? r.in : RectI{0, 0, 0, 0};
        EXPECT_EQ(c.cropRect(), want);
    }
}

TEST(TilePostDownloadCropConfigStatic, CutValidityVectors) {
    struct Row {
        PointI count, size, tile;
        const char* skip;
        bool on;
    };
    const Row rows[] = {
        {{0, 0}, {0, 0}, {0, 0}, "", false}, // all default -> no cut
        {{2, 2}, {0, 0}, {0, 0}, "", true},  // by count, tile inside
        {{2, 2}, {0, 0}, {1, 1}, "", true},
        {{2, 2}, {0, 0}, {-1, -1}, "", true}, // negative tile = whole image
        {{2, 0}, {0, 0}, {0, 0}, "", false},  // count needs both axes
        {{0, 2}, {0, 0}, {0, 0}, "", false},
        {{2, 2}, {0, 0}, {2, 0}, "", true}, // tile outside count: the count clause fails, tile!=0 rescues
        {{2, 2}, {0, 0}, {0, 2}, "", true},
        {{0, 0}, {256, 0}, {0, 0}, "", true}, // by size, either axis
        {{0, 0}, {0, 256}, {0, 0}, "", true},
        {{0, 0}, {-5, -5}, {0, 0}, "", false},   // negative size is not a trigger
        {{0, 0}, {0, 0}, {0, 0}, "(0,0)", true}, // skip list
        {{0, 0}, {0, 0}, {0, 1}, "", true},      // a tile offset alone
        {{0, 0}, {0, 0}, {-1, 0}, "", true},
    };
    for (const Row& r : rows) {
        const TilePostDownloadCropConfigStatic c({}, r.count, r.size, r.tile, QString::fromLatin1(r.skip));
        SCOPED_TRACE(::testing::Message()
                     << r.count.x() << ',' << r.count.y() << '/' << r.size.x() << ',' << r.size.y() << '/'
                     << r.tile.x() << ',' << r.tile.y() << '/' << r.skip);
        EXPECT_EQ(c.isCutOnDownload(), r.on);
        EXPECT_EQ(c.cutCount(), r.on ? r.count : PointI(0, 0));
        EXPECT_EQ(c.cutSize(), r.on ? r.size : PointI(0, 0));
        EXPECT_EQ(c.cutTile(), r.on ? r.tile : PointI(0, 0));
    }
}

TEST(TilePostDownloadCropConfigStatic, CutSkipItemMatchesTheItemAndTheItemMinusCountY) {
    const TilePostDownloadCropConfigStatic c({}, {2, 3}, {}, {}, QStringLiteral("(1,1);(4,-1)"));
    EXPECT_TRUE(c.cutSkipItem({1, 1}, {2, 3}));
    EXPECT_TRUE(c.cutSkipItem({4, 2}, {2, 3})); // (4, 2-3) = (4,-1)
    EXPECT_FALSE(c.cutSkipItem({4, 2}, {2, 1}));
    EXPECT_FALSE(c.cutSkipItem({0, 0}, {2, 3}));
    const TilePostDownloadCropConfigStatic none({}, {2, 3}, {}, {}, QString());
    EXPECT_FALSE(none.cutSkipItem({1, 1}, {2, 3}));
}

// Real corpus: the TileRLeft/Top/Right/Bottom and Cut* keys of every stock params.txt. The
// oracle is a plain `grep -iE '^tiler(left|top|right|bottom)='` over the fixture (2026-10-12):
// nine ZMPs carry a crop rectangle, all of them (0,0,w,h) with w,h in 180..512; no ZMP has a Cut*
// key. (Spec §2.6.6's remark "all nine crop rects are 0,0,0,0" is a misreading of the count 9.)
TEST(TilePostDownloadCropConfigStatic, StockCorpusHasNineCropsAndNoCuts) {
    CG_SKIP_IF_NO_CORPUS();
    const std::map<QString, RectI> expected = {
        {QStringLiteral("atlogis_Landsat5.zmp"), {0, 0, 512, 512}},
        {QStringLiteral("atlogis_LS7.zmp"), {0, 0, 512, 512}},
        {QStringLiteral("balticmaps.eu.map.zmp"), {0, 0, 300, 300}},
        {QStringLiteral("balticmaps.eu.topo.zmp"), {0, 0, 300, 300}},
        {QStringLiteral("Bing_Sat_BE_N.zmp"), {0, 0, 256, 180}},
        {QStringLiteral("in-sfera.ru.zmp"), {0, 0, 250, 250}},
        {QStringLiteral("MAmet.zmp"), {0, 0, 256, 256}},
        {QStringLiteral("gallica.bnf.fr.zmp"), {0, 0, 256, 256}},
        {QStringLiteral("zoomify_usage_example.zmp"), {0, 0, 256, 256}},
    };
    std::map<QString, RectI> found;
    int zmpCount = 0;
    int cutOn = 0;
    QDirIterator it(cg::maptype::test::fixtureDir() + QStringLiteral("/zmp/sas.maps"),
                    {QStringLiteral("params.txt")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const auto ini = cg::core::ConfigProviderIni::open(path);
        ASSERT_TRUE(ini.has_value()) << path.toStdString();
        const auto p = (*ini)->subItem(u"PARAMS");
        ASSERT_NE(p, nullptr) << path.toStdString();
        ++zmpCount;
        const RectI rect{p->readInt(u"TileRLeft", 0), p->readInt(u"TileRTop", 0),
                         p->readInt(u"TileRRight", 0), p->readInt(u"TileRBottom", 0)};
        const TilePostDownloadCropConfigStatic c(
            rect, {p->readInt(u"CutCountX", 0), p->readInt(u"CutCountY", 0)},
            {p->readInt(u"CutSizeX", 0), p->readInt(u"CutSizeY", 0)},
            {p->readInt(u"CutTileX", 0), p->readInt(u"CutTileY", 0)}, p->readString(u"CutToSkip", QString()));
        if (c.isCropOnDownload()) {
            found.emplace(QFileInfo(QFileInfo(path).absolutePath()).fileName(), c.cropRect());
        }
        cutOn += c.isCutOnDownload() ? 1 : 0;
    }
    EXPECT_EQ(zmpCount, 514);
    EXPECT_EQ(found, expected);
    EXPECT_EQ(cutOn, 0);
}

} // namespace
