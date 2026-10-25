// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/region/export_mbtiles (Src/RegionProcess/Export/MBTiles/fr_ExportMBTiles.pas, the
// headless half: GetMBTilesContentType :208-228, GetAllowExport :321-324, GetIsLayer :346-352,
// GetMapType :359-368, GetDirectTilesCopy :375-418, GetBitmapTileSaver :478-515 format half). T-0183.
// FEAT-REG-069 (Export to MBTiles) is contributed to, not closed: the recompose path is absent.
//
// Rig: FakeMapType (tests/region) with a chosen bitmap flag, content type and projection set.
// The content types come from the standard registry (image/png, image/jpg, image/webp, image/bmp,
// image/gif, image/tiff, and a real vector type).
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/region/export_mbtiles.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "region_fake_map_type.h"

using namespace cg;
using region::ExportMbtilesParams;
using region::MbtilesContentType;
using region::test::FakeMapType;

namespace {

geo::ProjectionSetPtr setOf(int epsg) {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(epsg, geo::kTileSplitQuadrate256x256);
}

const core::ContentTypeInfo* type(const char* mime) {
    const core::ContentTypeInfo* info = core::ContentTypeRegistry::standard().infoByType(mime);
    EXPECT_NE(info, nullptr) << mime;
    return info;
}

std::shared_ptr<FakeMapType> makeMap(const char* mime = "image/png", bool bitmap = true,
                                     int epsg = geo::kProjEpsgGoogle) {
    auto m = std::make_shared<FakeMapType>();
    m->contentTypeValue = type(mime);
    m->isBitmapTilesValue = bitmap;
    m->projectionSetValue = setOf(epsg);
    return m;
}

constexpr const char* kVectorMime = "application/vnd.sas.wikimapia.kml+xml";

ExportMbtilesParams onlyMap(std::shared_ptr<FakeMapType> m) {
    ExportMbtilesParams p;
    p.map = std::move(m);
    return p;
}

ExportMbtilesParams onlyOverlay(std::shared_ptr<FakeMapType> m) {
    ExportMbtilesParams p;
    p.overlay = std::move(m);
    return p;
}

} // namespace

// ---- GetMBTilesContentType :208-228 ----------------------------------------------------------------

TEST(ExportMbtiles, ContentTypeNamesMapToTheThreeMbtilesTypes) {
    struct Case {
        const char* mime;
        MbtilesContentType expected;
    };
    const std::vector<Case> cases = {
        {"image/jpg", MbtilesContentType::Jpg},      {"image/jpeg", MbtilesContentType::Jpg},
        {"IMAGE/PJPEG", MbtilesContentType::Jpg},    {"image/png", MbtilesContentType::Png},
        {"Image/PNG", MbtilesContentType::Png},      {"image/webp", MbtilesContentType::Webp},
        {"IMAGE/WEBP", MbtilesContentType::Webp},    {"image/bmp", MbtilesContentType::Unknown},
        {"image/gif", MbtilesContentType::Unknown},  {"image/tiff", MbtilesContentType::Unknown},
        {"image/png ", MbtilesContentType::Unknown}, {"", MbtilesContentType::Unknown},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(region::mbtilesContentType(QByteArray(c.mime)), c.expected) << c.mime;
    }
}

// ---- GetAllowExport :321-324 -----------------------------------------------------------------------

TEST(ExportMbtiles, AllowExportFollowsTheBitmapFlagOfTheMap) {
    EXPECT_TRUE(region::allowExport(*makeMap("image/png", true)));
    EXPECT_FALSE(region::allowExport(*makeMap(kVectorMime, false)));
}

// ---- GetIsLayer :346-352, GetMapType :359-368 ------------------------------------------------------

TEST(ExportMbtiles, IsLayerAndMapTypeForTheFourSelections) {
    const auto map = makeMap("image/png");
    const auto overlay = makeMap("image/jpg");

    ExportMbtilesParams mapOnly = onlyMap(map);
    EXPECT_FALSE(region::isLayer(mapOnly));
    EXPECT_EQ(region::mapType(mapOnly), map);

    ExportMbtilesParams overlayOnly = onlyOverlay(overlay);
    EXPECT_TRUE(region::isLayer(overlayOnly));
    EXPECT_EQ(region::mapType(overlayOnly), overlay);

    ExportMbtilesParams both = onlyMap(map);
    both.overlay = overlay;
    EXPECT_FALSE(region::isLayer(both));   // a map is set: not a layer
    EXPECT_EQ(region::mapType(both), map); // the map wins over the overlay

    ExportMbtilesParams neither;
    EXPECT_TRUE(region::isLayer(neither));
    EXPECT_EQ(region::mapType(neither), nullptr);
}

// ---- GetDirectTilesCopy :375-418 -------------------------------------------------------------------

TEST(ExportMbtiles, DirectCopyNeedsExactlyOneValidSelection) {
    const auto valid = makeMap("image/png");
    EXPECT_TRUE(region::directTilesCopy(onlyMap(valid)));
    EXPECT_TRUE(region::directTilesCopy(onlyOverlay(valid)));

    ExportMbtilesParams both = onlyMap(valid);
    both.overlay = makeMap("image/jpg");
    EXPECT_FALSE(region::directTilesCopy(both)); // both valid, still false (:410-415)

    ExportMbtilesParams bothInvalidOverlay = onlyMap(valid);
    bothInvalidOverlay.overlay = makeMap("image/bmp");
    EXPECT_FALSE(region::directTilesCopy(bothInvalidOverlay));

    EXPECT_FALSE(region::directTilesCopy(ExportMbtilesParams{}));
}

TEST(ExportMbtiles, DirectCopyOfASingleSelectionFailsOnEachConditionInTurn) {
    struct Case {
        const char* label;
        std::shared_ptr<FakeMapType> map;
        bool expected;
    };
    const std::vector<Case> cases = {
        {"png", makeMap("image/png"), true},
        {"jpg", makeMap("image/jpg"), true},
        {"webp", makeMap("image/webp"), true},
        {"bmp", makeMap("image/bmp"), false},
        {"gif", makeMap("image/gif"), false},
        {"tiff", makeMap("image/tiff"), false},
        {"vector", makeMap(kVectorMime, true), false},
        {"not bitmap flag", makeMap("image/png", false), false},
        {"epsg 4326", makeMap("image/png", true, geo::kProjEpsgGeographic), false},
        {"epsg 3395", makeMap("image/png", true, geo::kProjEpsgYandex), false},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(region::directTilesCopy(onlyMap(c.map)), c.expected) << "map " << c.label;
        EXPECT_EQ(region::directTilesCopy(onlyOverlay(c.map)), c.expected) << "overlay " << c.label;
    }
}

TEST(ExportMbtiles, EachReencodingSwitchAloneMakesTheDirectCopyImpossible) {
    const auto valid = makeMap("image/png");
    for (int which = 0; which < 4; ++which) {
        ExportMbtilesParams p = onlyMap(valid);
        ExportMbtilesParams q = onlyOverlay(valid);
        for (ExportMbtilesParams* x : {&p, &q}) {
            switch (which) {
            case 0:
                x->addVisibleLayers = true;
                break;
            case 1:
                x->addVisibleOverlays = true;
                break;
            case 2:
                x->useRecolor = true;
                break;
            default:
                x->usePrevZoom = true;
                break;
            }
        }
        EXPECT_FALSE(region::directTilesCopy(p)) << which;
        EXPECT_FALSE(region::directTilesCopy(q)) << which;
    }
}

// ---- GetBitmapTileSaver :478-515, the format half --------------------------------------------------

TEST(ExportMbtiles, ImgFormatFollowsTheContentTypeOfTheMapAndOfTheOverlay) {
    struct Case {
        const char* mime;
        const char* expected;
    };
    const std::vector<Case> cases = {{"image/png", "png"}, {"image/jpg", "jpg"}, {"image/webp", "webp"}};
    for (const Case& c : cases) {
        EXPECT_EQ(region::imgFormat(onlyMap(makeMap(c.mime))), QString::fromLatin1(c.expected)) << c.mime;
        EXPECT_EQ(region::imgFormat(onlyOverlay(makeMap(c.mime))), QString::fromLatin1(c.expected)) << c.mime;
    }
}

TEST(ExportMbtiles, ImgFormatWithoutASaverIsJpgForAMapAndPngForAnOverlay) {
    // :505-513. BMP, GIF and TIFF are bitmap types the frame's format list does not offer: no saver.
    for (const char* mime : {"image/bmp", "image/gif", "image/tiff"}) {
        EXPECT_EQ(region::imgFormat(onlyMap(makeMap(mime))), QStringLiteral("jpg")) << mime;
        EXPECT_EQ(region::imgFormat(onlyOverlay(makeMap(mime))), QStringLiteral("png")) << mime;
    }
    // a vector-kind map takes the same branch (the kind is not Bitmap)
    EXPECT_EQ(region::imgFormat(onlyMap(makeMap(kVectorMime, false))), QStringLiteral("jpg"));
    EXPECT_EQ(region::imgFormat(onlyOverlay(makeMap(kVectorMime, false))), QStringLiteral("png"));
}

TEST(ExportMbtiles, ImgFormatOfBothSelectionsReadsTheMap) {
    ExportMbtilesParams p = onlyMap(makeMap("image/webp"));
    p.overlay = makeMap("image/png");
    EXPECT_EQ(region::imgFormat(p), QStringLiteral("webp"));
}
