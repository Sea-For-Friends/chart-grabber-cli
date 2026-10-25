// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: (content-type registry, not independently claimed -- see core.md 6); docs/specs/core.md
// 2.21, test plan 7.15
#include "cg/core/content_type.h"

#include <gtest/gtest.h>

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::core::ContentTypeRegistry;

TEST(ContentType, StandardRegistryHasNineBitmapEntries) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    // B1..B9, keyed by their most distinctive extension.
    const struct {
        const char* ext;
        const char* type;
    } bitmaps[] = {
        {".jpg", "image/jpg"},   {".jpeg", "image/jpg"}, {".png", "image/png"},
        {".gif", "image/gif"},   {".bmp", "image/bmp"},  {".ico", "image/x-icon"},
        {".webp", "image/webp"}, {".tif", "image/tiff"}, {".tiff", "image/tiff"},
    };
    for (const auto& b : bitmaps) {
        const ContentTypeInfo* info = r.infoByExt(b.ext);
        ASSERT_NE(info, nullptr) << b.ext;
        EXPECT_EQ(info->type, QByteArray(b.type)) << b.ext;
        EXPECT_EQ(info->kind, ContentKind::Bitmap) << b.ext;
    }
}

TEST(ContentType, StandardRegistryHasEightVectorEntries) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    const struct {
        const char* type;
        const char* ext;
    } vectors[] = {
        {"application/vnd.sas.wikimapia.kml+xml", ".kml"},
        {"application/vnd.sas.wikimapia.kmz", ".kmz"},
        {"application/vnd.google-earth.kml+xml", ".kml"},
        {"application/vnd.google-earth.kmz", ".kmz"},
        {"application/gpx+xml", ".gpx"},
        {"application/json", ".json"},
        {"application/geo+json", ".geojson"},
        {"application/vnd.sas.wikimapia.txt", ".wikitxt"},
    };
    for (const auto& v : vectors) {
        const ContentTypeInfo* info = r.infoByType(v.type);
        ASSERT_NE(info, nullptr) << v.type;
        EXPECT_EQ(info->defaultExt, QByteArray(v.ext)) << v.type;
        EXPECT_EQ(info->kind, ContentKind::Vector) << v.type;
    }
}

TEST(ContentType, JpegAliasesResolveToImageSlashJpg) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    for (const char* alias : {"image/jpg", "image/jpeg", "image/pjpeg"}) {
        const ContentTypeInfo* info = r.infoByType(alias);
        ASSERT_NE(info, nullptr) << alias;
        EXPECT_EQ(info->type, QByteArray("image/jpg"));
        EXPECT_EQ(info->defaultExt, QByteArray(".jpg"));
    }
}

TEST(ContentType, JpegExtKeepsItsOwnDefaultExt) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    const ContentTypeInfo* byExt = r.infoByExt(".jpeg");
    ASSERT_NE(byExt, nullptr);
    EXPECT_EQ(byExt->defaultExt, QByteArray(".jpeg"));
    const ContentTypeInfo* byType = r.infoByType("image/jpeg");
    ASSERT_NE(byType, nullptr);
    EXPECT_EQ(byType->defaultExt, QByteArray(".jpg"));
}

TEST(ContentType, TiffExtKeepsItsOwnDefaultExt) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    const ContentTypeInfo* byExt = r.infoByExt(".tiff");
    ASSERT_NE(byExt, nullptr);
    EXPECT_EQ(byExt->defaultExt, QByteArray(".tiff"));
    const ContentTypeInfo* byType = r.infoByType("image/tiff");
    ASSERT_NE(byType, nullptr);
    EXPECT_EQ(byType->defaultExt, QByteArray(".tif"));
}

TEST(ContentType, PngModeParameterAliasResolves) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    const ContentTypeInfo* info = r.infoByType("image/png; mode=24bit");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->type, QByteArray("image/png"));
}

TEST(ContentType, BmpAliasesResolve) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    for (const char* alias : {"image/bmp", "image/x-ms-bmp", "image/x-windows-bmp"}) {
        ASSERT_NE(r.infoByType(alias), nullptr) << alias;
    }
}

TEST(ContentType, IconAliasesResolve) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    for (const char* alias : {"image/x-icon", "image/vnd.microsoft.icon"}) {
        ASSERT_NE(r.infoByType(alias), nullptr) << alias;
    }
}

TEST(ContentType, LookupIsCaseInsensitive) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    EXPECT_NE(r.infoByType("IMAGE/JPG"), nullptr);
    EXPECT_NE(r.infoByExt(".JPG"), nullptr);
    EXPECT_EQ(r.infoByType("IMAGE/JPG"), r.infoByType("image/jpg"));
}

TEST(ContentType, ExtensionRequiresLeadingDot) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    EXPECT_EQ(r.infoByExt("jpg"), nullptr);
    EXPECT_NE(r.infoByExt(".jpg"), nullptr);
}

TEST(ContentType, UnknownTypeReturnsNull) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    EXPECT_EQ(r.infoByType("application/octet-stream"), nullptr);
    EXPECT_EQ(r.infoByExt(".unknown"), nullptr);
    EXPECT_EQ(r.infoByFileName(u"file.unknown"), nullptr);
}

TEST(ContentType, ParameterisedMimeDoesNotMatch) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    EXPECT_EQ(r.infoByType("image/jpeg; charset=x"), nullptr);
}

TEST(ContentType, WikimapiaTypesAreNotRegisteredByExtension) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    const ContentTypeInfo* kml = r.infoByExt(".kml");
    ASSERT_NE(kml, nullptr);
    EXPECT_EQ(kml->type, QByteArray("application/vnd.google-earth.kml+xml"));
    const ContentTypeInfo* kmz = r.infoByExt(".kmz");
    ASSERT_NE(kmz, nullptr);
    EXPECT_EQ(kmz->type, QByteArray("application/vnd.google-earth.kmz"));
}

TEST(ContentType, KnownExtensionsAreTheExpectedFifteen) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    // Byte-wise sorted (std::sort over QByteArray, spec core.md 2.21.1's own example line lists
    // ".gif" before ".geojson", which is not actually lexicographic order -- see the handoff).
    const QList<QByteArray> expected = {".bmp",  ".geojson", ".gif",  ".gpx",  ".ico",
                                        ".jpeg", ".jpg",     ".json", ".kml",  ".kmz",
                                        ".png",  ".tif",     ".tiff", ".webp", ".wikitxt"};
    EXPECT_EQ(r.knownExtensions(), expected);
}

TEST(ContentType, DuplicateRegistrationIsAnError) {
    ContentTypeRegistry r;
    ASSERT_TRUE(
        r.addByType("image/jpg", ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}).has_value());
    const auto second = r.addByType(
        "IMAGE/JPG", ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}); // case-insensitive
    EXPECT_FALSE(second.has_value());
    ASSERT_TRUE(r.addByExt(".jpg", ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}).has_value());
    EXPECT_FALSE(r.addByExt(".jpg", ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}).has_value());
}

// T-0115 (OQ-14): u_ContentTypeManagerSimple.pas ConverterMatrixUpdateSynonyms (:390-424) and the three
// TContentConverterSimpleCopy entries of ConverterMatrixUpdateFixed (:305-388); everything else in the matrix
// is a codec or an archive converter and is false here.
TEST(ContentType, SimpleCopyConverterTable) {
    const ContentTypeRegistry& r = ContentTypeRegistry::standard();
    const struct {
        const char* source;
        const char* target;
        bool simple;
    } rows[] = {
        {"image/jpeg", "image/jpg", true},              // synonyms of one canonical type
        {"image/pjpeg", "image/jpeg", true},            // two aliases of image/jpg
        {"image/png; mode=24bit", "image/x-png", true}, // two aliases of image/png
        {"image/gif", "image/gif", true},               // a key with itself
        {"IMAGE/PNG", "image/x-png", true},             // keys are case-insensitive
        {"application/vnd.sas.wikimapia.kmz", "application/vnd.sas.wikimapia.kmz", true},
        {"image/jpg", "image/png", false}, // bitmap codec converter, not simple
        {"image/png", "image/jpeg", false},
        {"application/vnd.google-earth.kml+xml", "application/vnd.sas.wikimapia.kml+xml", true},  // :316
        {"application/vnd.sas.wikimapia.kml+xml", "application/vnd.google-earth.kml+xml", false}, // one way
        {"application/json", "application/geo+json", true},                                       // :365
        {"application/geo+json", "application/json", true},                                       // :377
        {"application/vnd.google-earth.kml+xml", "application/vnd.google-earth.kmz", false},      // Kml2Kmz
        {"application/vnd.google-earth.kmz", "application/vnd.google-earth.kml+xml", false},      // Kmz2Kml
        {"application/gpx+xml", "application/json", false},
        {"image/jpg", "application/json", false},
        {"image/unknown", "image/unknown", false}, // not registered: legacy Get answers nil
        {"image/jpg", "image/unknown", false},
        {"", "", false},
        {"image/\xd0\x93", "image/\xd0\x93", false}, // non-ASCII key: not in the matrix
    };
    for (const auto& row : rows) {
        EXPECT_EQ(r.hasSimpleCopyConverter(row.source, row.target), row.simple)
            << row.source << " -> " << row.target;
    }
}
