// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of T-0163: Src/BitmapTileSaveLoad/u_BitmapTileSaveLoadFactory.pas (creators :158-335), the DUnit
// u_BitmapTileSaveLoadFactory_Test.pas (:89-155) and Src/ContentType/u_ContentTypeManagerSimple.pas
// TContentTypeManagerBitmap (:579-762). FEAT-STO-033 (bitmap half of the content-type registry).
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"
#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/bitmap/bitmap_tile_free_image.h"
#include "cg/bitmap/bitmap_tile_lib_jpeg.h"
#include "cg/bitmap/bitmap_tile_save_load_factory.h"
#include "cg/bitmap/content_type_manager_bitmap.h"
#include "cg/core/content_type.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <webp/decode.h>

#include "../corpus_skip.h"
#include "sanitizer_skip.h"

namespace {

using namespace cg::bitmap;
using cg::core::Color32;
using namespace Qt::StringLiterals;

std::shared_ptr<const IBitmap32StaticFactory> staticFactory() {
    return std::make_shared<Bitmap32StaticFactory>(std::make_shared<Bitmap32BufferFactorySimple>());
}

std::shared_ptr<const IBitmapTileSaveLoadFactory> makeFactory() {
    return std::make_shared<BitmapTileSaveLoadFactory>(staticFactory());
}

// CG_FIXTURES_DIR comes from CMake; the nocmake fallback finds tests/fixtures from the working directory.
QString fixturesDir() {
#ifdef CG_FIXTURES_DIR
    return QString::fromUtf8(CG_FIXTURES_DIR);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.filePath(QStringLiteral("tests/fixtures"));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("tests/fixtures");
#endif
}

QByteArray readFixture(const char* name) {
    QFile f(fixturesDir() + u"/legacy-test-data/bitmaps/"_s + QString::fromLatin1(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << name;
    return f.readAll();
}

// A tile with repetition (so that the zlib level matters) and some noise; alpha FF except one pixel.
Bitmap32StaticPtr makeTile(int w, int h, bool withAlpha) {
    std::vector<Color32> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::uint32_t state = 777U;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            state = state * 1664525U + 1013904223U;
            const Color32 noise = ((state >> 24) & 7U);
            const Color32 b = static_cast<Color32>(((x / 8) * 31) & 0xFF) + noise;
            const Color32 g = static_cast<Color32>(((y / 8) * 29) & 0xFF);
            const Color32 r = static_cast<Color32>((((x + y) / 16) * 17) & 0xFF);
            Color32 a = 0xFF;
            if (withAlpha && x == 5 && y == 7) {
                a = 0x80;
            }
            px[static_cast<std::size_t>(y * w + x)] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return staticFactory()->build(QPoint(w, h), px.data());
}

template <typename T, typename P> bool is(const P& p) {
    return dynamic_cast<const T*>(p.get()) != nullptr;
}

// ---- proof 1: the DUnit port (u_BitmapTileSaveLoadFactory_Test.pas :104-155)

QByteArray saveSameFormat(const IBitmapTileSaveLoadFactory& f, const char* file, const Bitmap32StaticPtr& b) {
    const std::string name(file);
    if (name == "test.jpg") {
        return f.createJpegSaver()->save(b);
    }
    if (name == "test.png") {
        return f.createPngSaver()->save(b);
    }
    if (name == "test.webp") {
        return f.createWebpSaver()->save(b);
    }
    if (name == "test.bmp") {
        return f.createBmpSaver()->save(b);
    }
    if (name == "test.gif") {
        return f.createGifSaver()->save(b);
    }
    return f.createTiffSaver()->save(b);
}

BitmapTileLoaderPtr loaderFor(const IBitmapTileSaveLoadFactory& f, const char* file) {
    const std::string name(file);
    if (name == "test.jpg") {
        return f.createJpegLoader();
    }
    if (name == "test.png") {
        return f.createPngLoader();
    }
    if (name == "test.webp") {
        return f.createWebpLoader();
    }
    if (name == "test.bmp") {
        return f.createBmpLoader();
    }
    if (name == "test.gif") {
        return f.createGifLoader();
    }
    return f.createTiffLoader();
}

TEST(BitmapTileSaveLoadFactory, DunitPortLoadFixtureThenSaveWithSameFormat) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    CG_SKIP_LONGJMP_UNDER_TSAN();
    struct Row {
        const char* file;
        const char* refusal; // null: the saver output is decoded back
    };
    const Row rows[] = {
        {"test.bmp", nullptr}, {"test.gif", "GIF saver is not ported"},  {"test.jpg", nullptr},
        {"test.png", nullptr}, {"test.tif", "TIFF saver is not ported"}, {"test.webp", nullptr},
    };
    const auto factory = makeFactory();
    for (const Row& row : rows) {
        SCOPED_TRACE(row.file);
        const auto loader = loaderFor(*factory, row.file);
        ASSERT_TRUE(loader);
        const auto bmp = loader->load(readFixture(row.file));
        ASSERT_TRUE(bmp);
        EXPECT_EQ(bmp->size(), QPoint(256, 256)); // `file` reports 256 x 256 for all six
        if (row.refusal != nullptr) {
            try {
                static_cast<void>(saveSameFormat(*factory, row.file, bmp));
                ADD_FAILURE() << "the saver must refuse";
            } catch (const BitmapCodecError& e) {
                EXPECT_STREQ(e.what(), row.refusal);
            }
            continue;
        }
        const QByteArray out = saveSameFormat(*factory, row.file, bmp);
        ASSERT_FALSE(out.isEmpty());
        const auto back = loader->load(out);
        ASSERT_TRUE(back);
        EXPECT_EQ(back->size(), QPoint(256, 256));
    }
}

// ---- proof 2: creators

TEST(BitmapTileSaveLoadFactory, LoadersAreTheMatchingClasses) {
    const auto f = makeFactory();
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderBmp>(f->createBmpLoader()));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderGif>(f->createGifLoader()));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderIco>(f->createIcoLoader()));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderPng>(f->createPngLoader()));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderTiff>(f->createTiffLoader()));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderWebp>(f->createWebpLoader()));
    EXPECT_TRUE(is<LibJpegTileLoader>(f->createJpegLoader()));
}

TEST(BitmapTileSaveLoadFactory, SaversAreTheMatchingClasses) {
    const auto f = makeFactory();
    EXPECT_TRUE(is<BitmapTileFreeImageSaverBmp>(f->createBmpSaver()));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverGif>(f->createGifSaver()));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverPng>(f->createPngSaver()));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverPng>(f->createPngSaver(ImageColorBitPerPix::I24bpp, 6)));
    EXPECT_TRUE(is<LibJpegTileSaver>(f->createJpegSaver()));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverTiff>(f->createTiffSaver()));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverTiff>(
        f->createTiffSaver(ImageColorBitPerPix::I8bpp, ImageTiffCompression::Lzw)));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverTiff>(
        f->createTiffSaver(ImageColorBitPerPix::I24bpp, ImageTiffCompression::Jpeg)));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverTiff>(
        f->createTiffSaver(ImageColorBitPerPix::I32bpp, ImageTiffCompression::None)));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverWebp>(f->createWebpSaver()));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverWebpLossless>(f->createWebpLosslessSaver()));
}

TEST(BitmapTileSaveLoadFactory, JpegSaverDefaultIsQuality75) {
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const auto f = makeFactory();
    const auto tile = makeTile(64, 64, false);
    const QByteArray def = f->createJpegSaver()->save(tile);
    EXPECT_EQ(def, LibJpegTileSaver(75).save(tile));
    EXPECT_NE(def, LibJpegTileSaver(85).save(tile));
    EXPECT_EQ(f->createJpegSaver(85)->save(tile), LibJpegTileSaver(85).save(tile));
}

TEST(BitmapTileSaveLoadFactory, PngSaverDefaultIs32BitLevel6) {
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const auto f = makeFactory();
    const auto tile = makeTile(128, 128, false);
    const QByteArray def = f->createPngSaver()->save(tile);
    EXPECT_EQ(def, BitmapTileFreeImageSaverPng(6, 32).save(tile));
    EXPECT_NE(def, BitmapTileFreeImageSaverPng(2, 32).save(tile));
    EXPECT_EQ(f->createPngSaver(ImageColorBitPerPix::I32bpp, 2)->save(tile),
              BitmapTileFreeImageSaverPng(2, 32).save(tile));
}

TEST(BitmapTileSaveLoadFactory, Png24BitOnNonOpaqueTileIsColourType2) {
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const auto tile = makeTile(32, 32, true);
    const QByteArray out = makeFactory()->createPngSaver(ImageColorBitPerPix::I24bpp, 6)->save(tile);
    ASSERT_GT(out.size(), 26);
    EXPECT_EQ(std::memcmp(out.constData() + 12, "IHDR", 4), 0);
    EXPECT_EQ(static_cast<int>(out[25]), 2); // IHDR colour type RGB, no alpha channel
    // the 32-bit creator keeps the alpha: colour type 6
    const QByteArray out32 = makeFactory()->createPngSaver(ImageColorBitPerPix::I32bpp, 6)->save(tile);
    EXPECT_EQ(static_cast<int>(out32[25]), 6);
}

TEST(BitmapTileSaveLoadFactory, WebpSaverDefaultIsQuality75) {
    const auto f = makeFactory();
    const auto tile = makeTile(64, 64, false);
    const QByteArray def = f->createWebpSaver()->save(tile);
    EXPECT_EQ(def, BitmapTileFreeImageSaverWebp(75).save(tile));
    EXPECT_NE(def, BitmapTileFreeImageSaverWebp(20).save(tile));
    EXPECT_EQ(f->createWebpSaver(20)->save(tile), BitmapTileFreeImageSaverWebp(20).save(tile));
}

TEST(BitmapTileSaveLoadFactory, WebpLosslessDecodesToTheInput) {
    const auto tile = makeTile(48, 40, true);
    const QByteArray out = makeFactory()->createWebpLosslessSaver()->save(tile);
    int w = 0;
    int h = 0;
    uint8_t* raw = WebPDecodeBGRA(reinterpret_cast<const uint8_t*>(out.constData()),
                                  static_cast<std::size_t>(out.size()), &w, &h);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(w, 48);
    EXPECT_EQ(h, 40);
    EXPECT_EQ(std::memcmp(raw, tile->data(), 48U * 40U * 4U), 0);
    WebPFree(raw);
}

TEST(BitmapTileSaveLoadFactory, RefusalsAndParameterErrors) {
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const auto f = makeFactory();
    const auto expectThrows = [](const auto& create, const char* text) {
        try {
            static_cast<void>(create());
            ADD_FAILURE() << "must throw: " << text;
        } catch (const BitmapCodecError& e) {
            EXPECT_STREQ(e.what(), text);
        }
    };
    expectThrows([&] { return f->createPngSaver(ImageColorBitPerPix::I8bpp, 6); },
                 "PNG 8 bpp saver is not ported");
    expectThrows([&] { return f->createPngSaver(ImageColorBitPerPix::I32bpp, 10); },
                 "Invalid PNG compression level: 10");
    expectThrows([&] { return f->createWebpSaver(0); }, "Invalid WebP quality value: 0");
}

// ---- proof 3: the table

const char* const kTypeKeys[] = {
    "image/jpg",           "image/jpeg",   "image/pjpeg",
    "image/png",           "image/x-png",  "image/png; mode=24bit",
    "image/gif",           "image/bmp",    "image/x-ms-bmp",
    "image/x-windows-bmp", "image/x-icon", "image/vnd.microsoft.icon",
    "image/webp",          "image/tiff",
};
const char* const kExtKeys[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".ico", ".webp", ".tif", ".tiff"};

struct KeyRow {
    const char* key;
    const char* type;
    const char* ext;
};

TEST(ContentTypeManagerBitmap, TypeKeysResolveToCanonicalTypeAndDefaultExt) {
    const ContentTypeManagerBitmap m(makeFactory());
    const KeyRow rows[] = {
        {"image/jpg", "image/jpg", ".jpg"},       {"image/jpeg", "image/jpg", ".jpg"},
        {"image/pjpeg", "image/jpg", ".jpg"},     {"image/png", "image/png", ".png"},
        {"image/x-png", "image/png", ".png"},     {"image/png; mode=24bit", "image/png", ".png"},
        {"image/gif", "image/gif", ".gif"},       {"image/bmp", "image/bmp", ".bmp"},
        {"image/x-ms-bmp", "image/bmp", ".bmp"},  {"image/x-windows-bmp", "image/bmp", ".bmp"},
        {"image/x-icon", "image/x-icon", ".ico"}, {"image/vnd.microsoft.icon", "image/x-icon", ".ico"},
        {"image/webp", "image/webp", ".webp"},    {"image/tiff", "image/tiff", ".tif"},
    };
    for (const KeyRow& r : rows) {
        SCOPED_TRACE(r.key);
        const auto* info = m.infoByType(r.key);
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(info->type, QByteArray(r.type));
        EXPECT_EQ(info->defaultExt, QByteArray(r.ext));
        EXPECT_TRUE(info->loader);
        EXPECT_TRUE(m.isBitmapType(r.key));
    }
}

TEST(ContentTypeManagerBitmap, ExtensionKeysResolveToTheirOwnDefaultExt) {
    const ContentTypeManagerBitmap m(makeFactory());
    const KeyRow rows[] = {
        {".jpg", "image/jpg", ".jpg"},    {".jpeg", "image/jpg", ".jpeg"}, {".png", "image/png", ".png"},
        {".gif", "image/gif", ".gif"},    {".bmp", "image/bmp", ".bmp"},   {".ico", "image/x-icon", ".ico"},
        {".webp", "image/webp", ".webp"}, {".tif", "image/tiff", ".tif"},  {".tiff", "image/tiff", ".tiff"},
    };
    for (const KeyRow& r : rows) {
        SCOPED_TRACE(r.key);
        const auto* info = m.infoByExt(r.key);
        ASSERT_NE(info, nullptr);
        EXPECT_EQ(info->type, QByteArray(r.type));
        EXPECT_EQ(info->defaultExt, QByteArray(r.ext));
        EXPECT_TRUE(info->loader);
        EXPECT_TRUE(m.isBitmapExt(r.key));
    }
}

TEST(ContentTypeManagerBitmap, GroupsShareLoaderAndSaverObjects) {
    const ContentTypeManagerBitmap m(makeFactory());
    const auto* jpg = m.infoByExt(".jpg");
    const auto* jpeg = m.infoByExt(".jpeg");
    const auto* jpegType = m.infoByType("image/pjpeg");
    ASSERT_TRUE(jpg && jpeg && jpegType);
    EXPECT_EQ(jpg->loader, jpeg->loader);
    EXPECT_EQ(jpg->saver, jpeg->saver);
    EXPECT_EQ(jpg->loader, jpegType->loader);
    EXPECT_EQ(jpg->saver, jpegType->saver);
    EXPECT_NE(jpg->defaultExt, jpeg->defaultExt);

    const auto* tif = m.infoByExt(".tif");
    const auto* tiff = m.infoByExt(".tiff");
    ASSERT_TRUE(tif && tiff);
    EXPECT_EQ(tif->loader, tiff->loader);
    EXPECT_EQ(tif->saver, tiff->saver);
    EXPECT_EQ(tif->loader, m.infoByType("image/tiff")->loader);
    EXPECT_NE(tif->defaultExt, tiff->defaultExt);

    const auto* png = m.infoByType("image/png");
    const auto* png24 = m.infoByType("image/png; mode=24bit");
    EXPECT_EQ(png->saver, png24->saver);
    EXPECT_EQ(png->loader, m.infoByExt(".png")->loader);

    // groups are distinct objects
    EXPECT_NE(jpg->loader, png->loader);
    EXPECT_NE(jpg->saver, png->saver);
}

TEST(ContentTypeManagerBitmap, IcoHasLoaderAndNoSaver) {
    const ContentTypeManagerBitmap m(makeFactory());
    for (const auto* info :
         {m.infoByType("image/x-icon"), m.infoByType("image/vnd.microsoft.icon"), m.infoByExt(".ico")}) {
        ASSERT_NE(info, nullptr);
        EXPECT_TRUE(info->loader);
        EXPECT_FALSE(info->saver);
    }
    for (const char* ext : {".jpg", ".png", ".gif", ".bmp", ".webp", ".tif"}) {
        EXPECT_TRUE(m.infoByExt(ext)->saver) << ext;
    }
}

TEST(ContentTypeManagerBitmap, ClassesPerGroup) {
    const ContentTypeManagerBitmap m(makeFactory());
    EXPECT_TRUE(is<LibJpegTileLoader>(m.infoByExt(".jpg")->loader));
    EXPECT_TRUE(is<LibJpegTileSaver>(m.infoByExt(".jpg")->saver));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderPng>(m.infoByExt(".png")->loader));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverPng>(m.infoByExt(".png")->saver));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderGif>(m.infoByExt(".gif")->loader));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverGif>(m.infoByExt(".gif")->saver));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderBmp>(m.infoByExt(".bmp")->loader));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverBmp>(m.infoByExt(".bmp")->saver));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderIco>(m.infoByExt(".ico")->loader));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderWebp>(m.infoByExt(".webp")->loader));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverWebp>(m.infoByExt(".webp")->saver));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderTiff>(m.infoByExt(".tif")->loader));
    EXPECT_TRUE(is<BitmapTileFreeImageSaverTiff>(m.infoByExt(".tif")->saver));
}

// Saver parameters of the table (:686, :701, :742) observed through output.
TEST(ContentTypeManagerBitmap, SaverParametersOfTheTable) {
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const ContentTypeManagerBitmap m(makeFactory());
    const auto tile = makeTile(128, 128, false);
    EXPECT_EQ(m.infoByExt(".jpg")->saver->save(tile), LibJpegTileSaver(85).save(tile));
    EXPECT_EQ(m.infoByExt(".png")->saver->save(tile), BitmapTileFreeImageSaverPng(2, 32).save(tile));
    EXPECT_EQ(m.infoByExt(".webp")->saver->save(tile), BitmapTileFreeImageSaverWebp(75).save(tile));
}

TEST(ContentTypeManagerBitmap, TypeKeysAreTheLiteralSet) {
    const ContentTypeManagerBitmap m(makeFactory());
    QList<QByteArray> got = m.typeKeys();
    std::sort(got.begin(), got.end());
    QList<QByteArray> want;
    for (const char* k : kTypeKeys) {
        want.append(k);
    }
    std::sort(want.begin(), want.end());
    EXPECT_EQ(got, want);
}

TEST(ContentTypeManagerBitmap, LookupIsAsciiCaseInsensitive) {
    const ContentTypeManagerBitmap m(makeFactory());
    ASSERT_NE(m.infoByType("IMAGE/JPG"), nullptr);
    EXPECT_EQ(m.infoByType("IMAGE/JPG"), m.infoByType("image/jpg"));
    ASSERT_NE(m.infoByType("Image/PNG; Mode=24Bit"), nullptr);
    EXPECT_EQ(m.infoByType("Image/PNG; Mode=24Bit")->type, QByteArray("image/png"));
    ASSERT_NE(m.infoByExt(".JPG"), nullptr);
    EXPECT_EQ(m.infoByExt(".JPG")->defaultExt, QByteArray(".jpg"));
    EXPECT_EQ(m.infoByExt(".TiFf")->defaultExt, QByteArray(".tiff"));
    EXPECT_TRUE(m.isBitmapExt(".WEBP"));
    EXPECT_TRUE(m.isBitmapType("IMAGE/WEBP"));
}

TEST(ContentTypeManagerBitmap, UnknownEmptyAndNonAsciiKeysGiveNull) {
    const ContentTypeManagerBitmap m(makeFactory());
    EXPECT_EQ(m.infoByType("image/unknown"), nullptr);
    EXPECT_EQ(m.infoByExt(".txt"), nullptr);
    EXPECT_EQ(m.infoByType(""), nullptr);
    EXPECT_EQ(m.infoByExt(""), nullptr);
    EXPECT_EQ(m.infoByExt("jpg"), nullptr); // no dot: not a key
    EXPECT_EQ(m.infoByExt(".jp\xC3\xA4"), nullptr);
    EXPECT_EQ(m.infoByType("image/jp\xC3\xA4g"), nullptr);
    EXPECT_EQ(m.infoByExt(".\xC3\x84.jpg"), nullptr);
    EXPECT_FALSE(m.isBitmapType("image/svg+xml"));
    EXPECT_FALSE(m.isBitmapType(""));
    EXPECT_FALSE(m.isBitmapExt(".txt"));
    EXPECT_FALSE(m.isBitmapExt(""));
}

// ---- proof 4: bitmapLoaderByFileName

TEST(ContentTypeManagerBitmap, LoaderByFileName) {
    const ContentTypeManagerBitmap m(makeFactory());
    const auto jpegLoader = m.infoByExt(".jpg")->loader;
    const auto tiffLoader = m.infoByExt(".tif")->loader;

    EXPECT_EQ(m.bitmapLoaderByFileName(u"C:/x/Фото.JPG"_s), jpegLoader);
    EXPECT_EQ(m.bitmapLoaderByFileName(u"a.tiff"_s), tiffLoader);
    EXPECT_EQ(m.bitmapLoaderByFileName(u"a.TIF"_s), tiffLoader);
    EXPECT_EQ(m.bitmapLoaderByFileName(u"a.b.jpeg"_s), jpegLoader); // the last dot
    EXPECT_EQ(m.bitmapLoaderByFileName(u"x/readme"_s), nullptr);
    EXPECT_EQ(m.bitmapLoaderByFileName(u"a.txt"_s), nullptr);
    EXPECT_EQ(m.bitmapLoaderByFileName(u"a.жпг"_s), nullptr);
    EXPECT_EQ(m.bitmapLoaderByFileName(u"dir.d/file"_s), nullptr);
    EXPECT_EQ(m.bitmapLoaderByFileName(u""_s), nullptr);
    EXPECT_TRUE(is<LibJpegTileLoader>(m.bitmapLoaderByFileName(u"q.jpg"_s)));
    EXPECT_TRUE(is<BitmapTileFreeImageLoaderTiff>(m.bitmapLoaderByFileName(u"q.tif"_s)));
}

// ---- proof 5: core and bitmap tables agree

TEST(ContentTypeManagerBitmap, CoreRegistryAgreesOnKeys) {
    const ContentTypeManagerBitmap m(makeFactory());
    const auto& core = cg::core::ContentTypeRegistry::standard();
    for (const char* key : kTypeKeys) {
        SCOPED_TRACE(key);
        const auto* c = core.infoByType(key);
        const auto* b = m.infoByType(key);
        ASSERT_NE(c, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(c->kind, cg::core::ContentKind::Bitmap);
        EXPECT_EQ(c->type, b->type);
        EXPECT_EQ(c->defaultExt, b->defaultExt);
    }
    for (const char* key : kExtKeys) {
        SCOPED_TRACE(key);
        const auto* c = core.infoByExt(key);
        const auto* b = m.infoByExt(key);
        ASSERT_NE(c, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(c->kind, cg::core::ContentKind::Bitmap);
        EXPECT_EQ(c->type, b->type);
        EXPECT_EQ(c->defaultExt, b->defaultExt);
    }
    // reverse: every bitmap extension of core is in the bitmap table
    for (const QByteArray& ext : core.knownExtensions()) {
        const auto* c = core.infoByExt(ext);
        ASSERT_NE(c, nullptr);
        if (c->kind != cg::core::ContentKind::Bitmap) {
            EXPECT_FALSE(m.isBitmapExt(ext)) << ext.toStdString();
            continue;
        }
        const auto* b = m.infoByExt(ext);
        ASSERT_NE(b, nullptr) << ext.toStdString();
        EXPECT_EQ(c->type, b->type);
        EXPECT_EQ(c->defaultExt, b->defaultExt);
    }
}

} // namespace
