// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of T-0161: Src/BitmapTileSaveLoad/u_BitmapTileFreeImage.pas (loader :195-268, constructors :272-346,
// saver base :350-371, Save :373-495 for the 24 and 32 bit branches, BMP :499-511, PNG :532-556);
// Includes/FreeImage/FreeBitmap.pas:954-975 (the loader sniffs the bytes), FreeImage.pas:593-599.
// FEAT-RND-041 (loader base, PNG saver), FEAT-RND-038 (BMP saver)
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"
#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/bitmap/bitmap_tile_free_image.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QString>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../corpus_skip.h"

namespace {

using namespace cg::bitmap;
using cg::core::Color32;

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
    QFile f(fixturesDir() + "/legacy-test-data/bitmaps/" + QString::fromUtf8(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << name;
    return f.readAll();
}

std::shared_ptr<const IBitmap32StaticFactory> staticFactory() {
    return std::make_shared<Bitmap32StaticFactory>(std::make_shared<Bitmap32BufferFactorySimple>());
}

// A buffer factory that refuses every size (legacy :230-234 `if VBuffer = nil then Exit`).
class RefusingBufferFactory final : public IBitmap32BufferFactory {
public:
    [[nodiscard]] Bitmap32BufferPtr build(QPoint, const Color32*) const override { return nullptr; }
    [[nodiscard]] Bitmap32BufferPtr buildEmpty(QPoint) const override { return nullptr; }
    [[nodiscard]] Bitmap32BufferPtr buildEmptyClear(QPoint, Color32) const override { return nullptr; }
};

// Pixels that differ from each other; alpha is 0xFF except where `alphaAt` says otherwise.
Bitmap32StaticPtr makeTile(int w, int h, bool varyAlpha, std::uint32_t oneAlpha = 0xFF) {
    std::vector<Color32> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color32 b = static_cast<Color32>((x * 7) & 0xFF);
            const Color32 g = static_cast<Color32>((y * 5 + 3) & 0xFF);
            const Color32 r = static_cast<Color32>(((x + y) * 3) & 0xFF);
            Color32 a = 0xFF;
            if (varyAlpha) {
                a = static_cast<Color32>((x * 13 + y * 29) & 0xFF); // includes 0x00 at (0, 0)
            } else if (x == w - 1 && y == h - 1) {
                a = oneAlpha;
            }
            px[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return staticFactory()->build(QPoint(w, h), px.data());
}

std::uint32_t be32(const QByteArray& d, qsizetype at) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at + 3]));
}

std::uint32_t le32(const QByteArray& d, qsizetype at) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at + 3])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at + 1])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[at]));
}

struct ParsedPng {
    bool signature = false;
    std::vector<std::string> chunks;
    QByteArray ihdr;
    QByteArray phys;
    QByteArray idat; // the concatenated IDAT payloads (the zlib stream)
};

ParsedPng parsePng(const QByteArray& d) {
    ParsedPng p;
    const unsigned char sig[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    p.signature = d.size() >= 8 && std::memcmp(d.constData(), sig, 8) == 0;
    qsizetype at = 8;
    while (p.signature && at + 12 <= d.size()) {
        const qsizetype len = be32(d, at);
        const std::string type(d.constData() + at + 4, 4);
        const QByteArray body = d.mid(at + 8, len);
        p.chunks.push_back(type);
        if (type == "IHDR") {
            p.ihdr = body;
        } else if (type == "pHYs") {
            p.phys = body;
        } else if (type == "IDAT") {
            p.idat += body;
        }
        at += 12 + len;
    }
    return p;
}

// Inflates the zlib stream through qUncompress (4-byte big-endian length prefix + stream); the expected
// length is checked by Qt, so a stream of another size gives an empty array.
QByteArray inflateIdat(const QByteArray& zlibStream, std::uint32_t expectedSize) {
    QByteArray framed;
    for (int shift = 24; shift >= 0; shift -= 8) {
        framed.append(static_cast<char>((expectedSize >> shift) & 0xFF));
    }
    framed += zlibStream;
    return qUncompress(framed);
}

QImage decodeQt(const QByteArray& data, const char* format) {
    return QImage::fromData(data, format).convertToFormat(QImage::Format_ARGB32);
}

// The rows of an ARGB32 image equal the bitmap; `rgbOnly` compares the 24 colour bits.
void expectSamePixels(const QImage& img, const Bitmap32StaticPtr& bmp, bool rgbOnly) {
    ASSERT_EQ(img.width(), bmp->size().x());
    ASSERT_EQ(img.height(), bmp->size().y());
    const Color32 mask = rgbOnly ? 0x00FFFFFFU : 0xFFFFFFFFU;
    int bad = 0;
    for (int y = 0; y < img.height(); ++y) {
        const auto* row = reinterpret_cast<const Color32*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const Color32 want = bmp->data()[static_cast<std::size_t>(y) * img.width() + x];
            bad += (row[x] & mask) != (want & mask) ? 1 : 0;
        }
    }
    EXPECT_EQ(bad, 0);
}

std::string messageOfLoad(const QByteArray& data) {
    try {
        (void)BitmapTileFreeImageLoaderPng(staticFactory()).load(data);
    } catch (const BitmapCodecError& e) {
        return e.what();
    }
    return "no exception";
}

// ---- loader

// Proof 1: the sizes are read from the files with file(1); the loader is the same class whatever the name.
TEST(FreeImageLoader, FixturesDecodeToTheirSize) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    struct Case {
        const char* file;
        int w;
        int h;
    };
    const Case cases[] = {{"test.png", 256, 256}, {"test2.png", 91, 69},   {"test.gif", 256, 256},
                          {"test.bmp", 256, 256}, {"test.webp", 256, 256}, {"test.tif", 256, 256}};
    for (const Case& c : cases) {
        const auto bmp = BitmapTileFreeImageLoaderPng(staticFactory()).load(readFixture(c.file));
        ASSERT_TRUE(bmp) << c.file;
        EXPECT_EQ(bmp->size(), QPoint(c.w, c.h)) << c.file;
    }
}

// Proof 1: straight alpha (legacy ConvertTo32Bits keeps colour and alpha unpremultiplied). Values of
// test2.png (RGBA 91 x 69): pixel (81, 2) is r=24 g=16 b=8 a=57; (0, 0) has alpha 0; (45, 34) is opaque.
TEST(FreeImageLoader, Rgba8PngKeepsStraightAlpha) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const auto bmp = BitmapTileFreeImageLoaderPng(staticFactory()).load(readFixture("test2.png"));
    ASSERT_TRUE(bmp);
    const Color32* px = bmp->data();
    EXPECT_EQ(px[2 * 91 + 81], 0x39181008U);
    EXPECT_EQ(px[0] >> 24, 0U);
    EXPECT_EQ(px[34 * 91 + 45], 0xFFEFB58CU);
}

// Proof 1 and the L row of T-0161: the registered format is ignored, the bytes decide.
TEST(FreeImageLoader, PngLoaderDecodesJpeg) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const auto bmp = BitmapTileFreeImageLoaderPng(staticFactory()).load(readFixture("test.jpg"));
    ASSERT_TRUE(bmp);
    EXPECT_EQ(bmp->size(), QPoint(256, 256));
}

TEST(FreeImageLoader, GifLoaderDecodesPng) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const QByteArray png = readFixture("test.png");
    const auto viaGif = BitmapTileFreeImageLoaderGif(staticFactory()).load(png);
    const auto viaPng = BitmapTileFreeImageLoaderPng(staticFactory()).load(png);
    ASSERT_TRUE(viaGif);
    ASSERT_TRUE(viaPng);
    EXPECT_EQ(viaGif->size(), QPoint(256, 256));
    EXPECT_EQ(std::memcmp(viaGif->data(), viaPng->data(), 256U * 256U * 4U), 0);
}

// :255-257
TEST(FreeImageLoader, GarbageAndEmptyThrowLoadFromMemoryFail) {
    EXPECT_EQ(messageOfLoad(QByteArray("these bytes are not an image of any kind")),
              "FreeBitmap.LoadFromMemory FAIL!");
    EXPECT_EQ(messageOfLoad(QByteArray()), "FreeBitmap.LoadFromMemory FAIL!");
}

// :230-234
TEST(FreeImageLoader, RefusedBufferGivesNullptr) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const auto factory = std::make_shared<Bitmap32StaticFactory>(std::make_shared<RefusingBufferFactory>());
    EXPECT_EQ(BitmapTileFreeImageLoaderPng(factory).load(readFixture("test.png")), nullptr);
}

// Proof 2: row 0 of the bitmap is the top row of the file (legacy flips FreeImage's bottom-up rows; QImage is
// top-down, so the port copies row i to row i).
TEST(FreeImageLoader, RowsKeepFileOrder) {
    const Color32 want[6] = {0xFF102030U, 0xFF405060U, 0xFF708090U, 0xFFA0B0C0U, 0xFFD0E0F0U, 0xFF010203U};
    QImage img(2, 3, QImage::Format_ARGB32);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            img.setPixel(x, y, want[y * 2 + x]);
        }
    }
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    ASSERT_TRUE(QImageWriter(&buf, "png").write(img));

    const auto bmp = BitmapTileFreeImageLoaderPng(staticFactory()).load(png);
    ASSERT_TRUE(bmp);
    ASSERT_EQ(bmp->size(), QPoint(2, 3));
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(bmp->data()[i], want[i]) << i;
    }
}

// ---- PNG saver

// Proof 3: structure of the output. Opaque 32-bit input -> colour type 2.
TEST(FreeImageSaverPng, OpaqueInputIsColourType2) {
    const auto tile = makeTile(40, 30, false);
    const QByteArray out = BitmapTileFreeImageSaverPng(6, 32).save(tile);
    const ParsedPng p = parsePng(out);
    ASSERT_TRUE(p.signature);
    ASSERT_EQ(p.ihdr.size(), 13);
    EXPECT_EQ(be32(p.ihdr, 0), 40U);
    EXPECT_EQ(be32(p.ihdr, 4), 30U);
    EXPECT_EQ(static_cast<int>(p.ihdr[8]), 8);  // bit depth
    EXPECT_EQ(static_cast<int>(p.ihdr[9]), 2);  // colour type RGB
    EXPECT_EQ(static_cast<int>(p.ihdr[12]), 0); // no interlace
    expectSamePixels(decodeQt(out, "PNG"), tile, false);
}

// One pixel with alpha FE switches to colour type 6 (:456-467, spec §2.5); the alpha survives exactly.
TEST(FreeImageSaverPng, OneNonOpaquePixelIsColourType6) {
    const auto tile = makeTile(40, 30, false, 0xFE);
    const QByteArray out = BitmapTileFreeImageSaverPng(6, 32).save(tile);
    const ParsedPng p = parsePng(out);
    ASSERT_EQ(p.ihdr.size(), 13);
    EXPECT_EQ(static_cast<int>(p.ihdr[9]), 6);
    EXPECT_EQ(tile->data()[40 * 30 - 1] >> 24, 0xFEU);
    expectSamePixels(decodeQt(out, "PNG"), tile, false);
}

// The decode of an image with alpha 0 .. FF everywhere (a transparent pixel keeps its colour: straight alpha,
// no premultiply) equals the input; this also pins png_set_bgr (a B / R swap would differ).
TEST(FreeImageSaverPng, VariedAlphaRoundTripsExactly) {
    const auto tile = makeTile(37, 23, true);
    ASSERT_EQ(tile->data()[0] >> 24, 0U);
    ASSERT_NE(tile->data()[0] & 0xFFFFFFU, 0U);
    const QByteArray out = BitmapTileFreeImageSaverPng(9, 32).save(tile);
    EXPECT_EQ(static_cast<int>(parsePng(out).ihdr[9]), 6);
    expectSamePixels(decodeQt(out, "PNG"), tile, false);
}

TEST(FreeImageSaverPng, ChunkListAndPhys) {
    for (const int bpp : {24, 32}) {
        const QByteArray out = BitmapTileFreeImageSaverPng(6, bpp).save(makeTile(64, 64, false));
        const ParsedPng p = parsePng(out);
        ASSERT_GE(p.chunks.size(), 4U) << bpp;
        EXPECT_EQ(p.chunks.front(), "IHDR");
        EXPECT_EQ(p.chunks[1], "pHYs");
        for (std::size_t i = 2; i + 1 < p.chunks.size(); ++i) {
            EXPECT_EQ(p.chunks[i], "IDAT") << bpp << " chunk " << i;
        }
        EXPECT_EQ(p.chunks.back(), "IEND");
        ASSERT_EQ(p.phys.size(), 9);
        EXPECT_EQ(be32(p.phys, 0), 2835U);
        EXPECT_EQ(be32(p.phys, 4), 2835U);
        EXPECT_EQ(static_cast<int>(p.phys[8]), 1); // unit: metre
    }
}

// Every scanline starts with a filter byte of NONE (0), SUB (1) or PAETH (4) (png_set_filter, spec §2.5).
// The second tile repeats one row, where libpng would pick UP (2) if it were allowed.
TEST(FreeImageSaverPng, FilterBytesAreNoneSubPaeth) {
    const int w = 50;
    const int h = 60;
    std::vector<Color32> columns(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const auto x = static_cast<Color32>(i % w);
        columns[i] = 0xFF000000U | ((x * 5) << 16) | ((x * x) << 8) | (x * 3);
    }
    const Bitmap32StaticPtr tiles[] = {makeTile(w, h, false), makeTile(w, h, true),
                                       staticFactory()->build(QPoint(w, h), columns.data())};
    for (const auto& tile : tiles) {
        for (const int bpp : {24, 32}) {
            const ParsedPng p = parsePng(BitmapTileFreeImageSaverPng(6, bpp).save(tile));
            const int bytesPerPixel = p.ihdr[9] == 6 ? 4 : 3;
            const int stride = 1 + w * bytesPerPixel;
            const QByteArray raw = inflateIdat(p.idat, static_cast<std::uint32_t>(h * stride));
            ASSERT_EQ(raw.size(), h * stride) << bpp;
            for (int y = 0; y < h; ++y) {
                const int f = static_cast<int>(raw[y * stride]);
                EXPECT_TRUE(f == 0 || f == 1 || f == 4) << "row " << y << " filter " << f;
            }
        }
    }
}

// 24-bit mode on input with varied alpha: colour type 2, RGB equals the input, no blending.
TEST(FreeImageSaverPng, Bpp24DropsAlphaWithoutBlending) {
    const auto tile = makeTile(37, 23, true);
    const QByteArray out = BitmapTileFreeImageSaverPng(6, 24).save(tile);
    EXPECT_EQ(static_cast<int>(parsePng(out).ihdr[9]), 2);
    const QImage img = decodeQt(out, "PNG");
    expectSamePixels(img, tile, true);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            ASSERT_EQ(reinterpret_cast<const Color32*>(img.constScanLine(y))[x] >> 24, 0xFFU);
        }
    }
    // one pixel with alpha 0x40 and colour 336699 decodes to the colour, opaque
    std::vector<Color32> one = {0x40336699U};
    const QImage px = decodeQt(
        BitmapTileFreeImageSaverPng(6, 24).save(staticFactory()->build(QPoint(1, 1), one.data())), "PNG");
    EXPECT_EQ(*reinterpret_cast<const Color32*>(px.constScanLine(0)), 0xFF336699U);
}

// compress 0 = no compression (flag $0100 -> zlib level 0, stored blocks) and 1..9 = that zlib level.
TEST(FreeImageSaverPng, CompressionLevels) {
    const auto tile = makeTile(128, 128, false);
    const std::size_t rawSize = 128U * (1U + 128U * 3U);
    const ParsedPng p0 = parsePng(BitmapTileFreeImageSaverPng(0, 32).save(tile));
    const ParsedPng p1 = parsePng(BitmapTileFreeImageSaverPng(1, 32).save(tile));
    const ParsedPng p9 = parsePng(BitmapTileFreeImageSaverPng(9, 32).save(tile));
    EXPECT_GT(static_cast<std::size_t>(p0.idat.size()), rawSize); // stored blocks add overhead
    EXPECT_LT(static_cast<std::size_t>(p9.idat.size()), rawSize / 2);
    EXPECT_GT(p0.idat.size(), p9.idat.size());
    ASSERT_GE(p1.idat.size(), 2);
    ASSERT_GE(p9.idat.size(), 2);
    // zlib header byte 2, bits 6-7 (FLEVEL): 0 = fastest (level 1), 3 = maximum (level 7..9)
    EXPECT_EQ((static_cast<std::uint8_t>(p1.idat[1]) >> 6) & 3, 0);
    EXPECT_EQ((static_cast<std::uint8_t>(p9.idat[1]) >> 6) & 3, 3);
}

TEST(FreeImageSaverPng, InvalidArgumentsThrow) {
    try {
        BitmapTileFreeImageSaverPng(10, 32);
        FAIL() << "no exception";
    } catch (const BitmapCodecError& e) {
        EXPECT_STREQ(e.what(), "Invalid PNG compression level: 10");
    }
    EXPECT_THROW(BitmapTileFreeImageSaverPng(6, 8), BitmapCodecError);
}

// D row of T-0161: a null bitmap is a BitmapCodecError (legacy: access violation at :391).
TEST(FreeImageSaverPng, NullBitmapThrows) {
    EXPECT_THROW((void)BitmapTileFreeImageSaverPng(6, 32).save(nullptr), BitmapCodecError);
}

// ---- BMP saver

// T-0177: the file legacy writes, FreeImage PluginBMP.cpp Save for a 32 bpp dib (unverified against the DLL).
TEST(FreeImageSaverBmp, HeaderFieldsAtTheirOffsets) {
    const QByteArray out = BitmapTileFreeImageSaverBmp().save(makeTile(40, 30, false));
    ASSERT_EQ(out.size(), 54 + 40 * 30 * 4);
    EXPECT_EQ(out.left(2), QByteArray("BM"));
    EXPECT_EQ(le32(out, 2), 54U + 40U * 30U * 4U); // bfSize
    EXPECT_EQ(le32(out, 6), 0U);                   // reserved
    EXPECT_EQ(le32(out, 10), 54U);                 // bfOffBits
    EXPECT_EQ(le32(out, 14), 40U);                 // biSize
    EXPECT_EQ(le32(out, 18), 40U);                 // biWidth
    EXPECT_EQ(le32(out, 22), 30U);                 // biHeight, positive: bottom-up
    EXPECT_EQ(le32(out, 26), 1U | (32U << 16));    // biPlanes 1, biBitCount 32
    EXPECT_EQ(le32(out, 30), 0U);                  // biCompression BI_RGB
    EXPECT_EQ(le32(out, 34), 0U);                  // biSizeImage (not set by FreeImage_AllocateT)
    EXPECT_EQ(le32(out, 38), 2835U);               // biXPelsPerMeter
    EXPECT_EQ(le32(out, 42), 2835U);               // biYPelsPerMeter
    EXPECT_EQ(le32(out, 46), 0U);                  // biClrUsed
    EXPECT_EQ(le32(out, 50), 0U);                  // biClrImportant
}

// The alpha byte of a non-opaque pixel is in the file, bytes B, G, R, A; rows are bottom-up.
TEST(FreeImageSaverBmp, PixelBytesAreBgraBottomUp) {
    // 2 x 2: top row (0x11 22 33 44 as A R G B) and bottom row distinct
    const Color32 px[4] = {0x80112233U, 0x01445566U, 0xFE778899U, 0x00AABBCCU};
    const auto tile = staticFactory()->build(QPoint(2, 2), px);
    const QByteArray out = BitmapTileFreeImageSaverBmp().save(tile);
    ASSERT_EQ(out.size(), 54 + 16);
    const unsigned char want[16] = {0x99, 0x88, 0x77, 0xFE, 0xCC, 0xBB, 0xAA, 0x00,  // bottom row first
                                    0x33, 0x22, 0x11, 0x80, 0x66, 0x55, 0x44, 0x01}; // then the top row
    EXPECT_EQ(std::memcmp(out.constData() + 54, want, 16), 0);
}

// Round trip through the port's own BMP loader (opaque: the alpha of a 32 bpp BI_RGB file on load is the
// first D row of T-0161, out of scope here).
TEST(FreeImageSaverBmp, RoundTripsThroughTheBmpLoader) {
    const auto tile = makeTile(40, 30, false);
    const QByteArray out = BitmapTileFreeImageSaverBmp().save(tile);
    const auto back = BitmapTileFreeImageLoaderBmp(staticFactory()).load(out);
    ASSERT_TRUE(back);
    ASSERT_EQ(back->size(), QPoint(40, 30));
    for (int i = 0; i < 40 * 30; ++i) {
        EXPECT_EQ(back->data()[i] & 0x00FFFFFFU, tile->data()[i] & 0x00FFFFFFU) << i;
    }
}

TEST(FreeImageSaverBmp, NullBitmapThrows) {
    EXPECT_THROW((void)BitmapTileFreeImageSaverBmp().save(nullptr), BitmapCodecError);
}

// ---- Proof 5: loaders and savers are stateless. The latch starts all threads together; with --tsan this
// shows that the objects hold no state, and every result must equal the single-thread one.
TEST(FreeImageThreads, SharedObjectsGiveEqualResults) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const QByteArray data = readFixture("test2.png");
    const auto loader = std::make_shared<BitmapTileFreeImageLoaderPng>(staticFactory());
    const auto png = std::make_shared<BitmapTileFreeImageSaverPng>(6, 32);
    const auto bmp = std::make_shared<BitmapTileFreeImageSaverBmp>();
    const auto tile = makeTile(48, 32, true);
    const auto expectLoaded = loader->load(data);
    ASSERT_TRUE(expectLoaded);
    const QByteArray expectPng = png->save(tile);
    const QByteArray expectBmp = bmp->save(tile);

    constexpr int kN = 4;
    std::latch go(3 * kN);
    std::vector<Bitmap32StaticPtr> loaded(kN);
    std::vector<QByteArray> pngs(kN);
    std::vector<QByteArray> bmps(kN);
    std::vector<std::thread> threads;
    for (int i = 0; i < kN; ++i) {
        const auto at = static_cast<std::size_t>(i);
        threads.emplace_back([&, at] {
            go.arrive_and_wait();
            loaded[at] = loader->load(data);
        });
        threads.emplace_back([&, at] {
            go.arrive_and_wait();
            pngs[at] = png->save(tile);
        });
        threads.emplace_back([&, at] {
            go.arrive_and_wait();
            bmps[at] = bmp->save(tile);
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    for (std::size_t i = 0; i < kN; ++i) {
        ASSERT_TRUE(loaded[i]);
        EXPECT_EQ(std::memcmp(loaded[i]->data(), expectLoaded->data(), 91U * 69U * 4U), 0);
        EXPECT_EQ(pngs[i], expectPng);
        EXPECT_EQ(bmps[i], expectBmp);
    }
}

} // namespace
