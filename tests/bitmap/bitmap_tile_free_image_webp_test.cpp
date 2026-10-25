// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of T-0162: Src/BitmapTileSaveLoad/u_BitmapTileFreeImage.pas WebP savers (:149-164, :560-594), GIF
// saver (:129-136, :515-528), TIFF saver (:166-174, :597-611), 32-bit branch of Save :456-467, :476-491,
// failure text :478-482. The decoder in the tests is libwebp's own (WebPDecodeBGRA), independent of the
// saver. FEAT-RND-044 (WebP save), FEAT-RND-039 (GIF save), FEAT-RND-043 (TIFF save)
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"
#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/bitmap/bitmap_tile_free_image.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <webp/decode.h>

namespace {

using namespace cg::bitmap;
using cg::core::Color32;

std::shared_ptr<const IBitmap32StaticFactory> staticFactory() {
    return std::make_shared<Bitmap32StaticFactory>(std::make_shared<Bitmap32BufferFactorySimple>());
}

enum class AlphaMode { Opaque, Varied };

// Smooth gradient. Varied: alpha FF except column 5 (0x80), a pixel (3, 3) with alpha 0 and non-zero RGB, and
// a pixel (10, 7) with alpha 1.
std::vector<Color32> gradientPixels(int w, int h, AlphaMode mode) {
    std::vector<Color32> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color32 b = static_cast<Color32>(x * 255 / (w - 1));
            const Color32 g = static_cast<Color32>(y * 255 / (h - 1));
            const Color32 r = static_cast<Color32>((x + y) * 255 / (w + h - 2));
            Color32 a = 0xFF;
            if (mode == AlphaMode::Varied) {
                if (x == 5) {
                    a = 0x80;
                }
                if (x == 3 && y == 3) {
                    a = 0x00;
                }
                if (x == 10 && y == 7) {
                    a = 0x01;
                }
            }
            px[static_cast<std::size_t>(y * w + x)] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return px;
}

// Deterministic noise (linear congruential), alpha FF.
std::vector<Color32> noisePixels(int w, int h) {
    std::vector<Color32> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::uint32_t state = 12345U;
    for (auto& p : px) {
        state = state * 1664525U + 1013904223U;
        p = 0xFF000000U | (state >> 8);
    }
    return px;
}

Bitmap32StaticPtr buildTile(int w, int h, const std::vector<Color32>& px) {
    return staticFactory()->build(QPoint(w, h), px.data());
}

struct Decoded {
    int width = 0;
    int height = 0;
    std::vector<Color32> pixels; // B, G, R, A in memory = Color32 of this platform
};

Decoded decodeWebp(const QByteArray& data) {
    Decoded d;
    uint8_t* out = WebPDecodeBGRA(reinterpret_cast<const uint8_t*>(data.constData()),
                                  static_cast<std::size_t>(data.size()), &d.width, &d.height);
    if (out == nullptr) {
        ADD_FAILURE() << "libwebp cannot decode the saver output";
        return d;
    }
    d.pixels.resize(static_cast<std::size_t>(d.width) * static_cast<std::size_t>(d.height));
    std::memcpy(d.pixels.data(), out, d.pixels.size() * 4);
    WebPFree(out);
    return d;
}

std::string fourcc(const QByteArray& d, qsizetype at) {
    return std::string(d.constData() + at, 4);
}

// RIFF header facts and the list of chunk ids of a WebP file.
struct ParsedWebp {
    bool riffWebp = false;
    bool sizeFieldMatches = false;
    std::vector<std::string> chunks;
};

ParsedWebp parseWebp(const QByteArray& d) {
    ParsedWebp p;
    p.riffWebp = d.size() >= 20 && fourcc(d, 0) == "RIFF" && fourcc(d, 8) == "WEBP";
    if (!p.riffWebp) {
        return p;
    }
    const auto riffSize = static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[4])) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[5])) << 8) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[6])) << 16) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[7])) << 24);
    p.sizeFieldMatches = qsizetype{riffSize} + 8 == d.size();
    qsizetype at = 12;
    while (at + 8 <= d.size()) {
        p.chunks.push_back(fourcc(d, at));
        const auto len = static_cast<qsizetype>(static_cast<std::uint8_t>(d[at + 4])) |
                         (static_cast<qsizetype>(static_cast<std::uint8_t>(d[at + 5])) << 8) |
                         (static_cast<qsizetype>(static_cast<std::uint8_t>(d[at + 6])) << 16) |
                         (static_cast<qsizetype>(static_cast<std::uint8_t>(d[at + 7])) << 24);
        at += 8 + len + (len & 1);
    }
    return p;
}

bool hasChunk(const ParsedWebp& p, const char* id) {
    return std::find(p.chunks.begin(), p.chunks.end(), id) != p.chunks.end();
}

int channel(Color32 c, int shift) {
    return static_cast<int>((c >> shift) & 0xFF);
}

// Largest difference over B, G, R (libwebp 1.6.0 gives 6 on the quality 90 gradient; the literal leaves
// slack) B, G, R of the pixels whose input alpha is not 0.
int maxColourError(const std::vector<Color32>& in, const std::vector<Color32>& out) {
    int worst = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (channel(in[i], 24) == 0) {
            continue;
        }
        for (const int shift : {0, 8, 16}) {
            worst = std::max(worst, std::abs(channel(in[i], shift) - channel(out[i], shift)));
        }
    }
    return worst;
}

std::string saveFailure(const IBitmapTileSaver& saver, const Bitmap32StaticPtr& bitmap) {
    try {
        (void)saver.save(bitmap);
    } catch (const BitmapCodecError& e) {
        return e.what();
    }
    return "<no exception>";
}

// ---- Proof 1: lossless is exact (VP8L, use_argb)

TEST(WebpLossless, OpaqueTileDecodesToTheInputExactly) {
    constexpr int kW = 37; // odd size on purpose: detects a stride or a width / height swap
    constexpr int kH = 21;
    const auto px = noisePixels(kW, kH);
    const QByteArray out = BitmapTileFreeImageSaverWebpLossless().save(buildTile(kW, kH, px));
    const ParsedWebp p = parseWebp(out);
    ASSERT_TRUE(p.riffWebp);
    EXPECT_TRUE(p.sizeFieldMatches);
    ASSERT_FALSE(p.chunks.empty());
    EXPECT_EQ(p.chunks.front(), "VP8L"); // lossless, no alpha -> no VP8X
    const Decoded d = decodeWebp(out);
    EXPECT_EQ(d.width, kW);
    EXPECT_EQ(d.height, kH);
    EXPECT_EQ(d.pixels, px);
}

TEST(WebpLossless, VariedAlphaKeepsAlphaAndColourOfVisiblePixels) {
    constexpr int kW = 37;
    constexpr int kH = 21;
    const auto px = gradientPixels(kW, kH, AlphaMode::Varied);
    const QByteArray out = BitmapTileFreeImageSaverWebpLossless().save(buildTile(kW, kH, px));
    const Decoded d = decodeWebp(out);
    ASSERT_EQ(d.width, kW);
    ASSERT_EQ(d.height, kH);
    ASSERT_EQ(d.pixels.size(), px.size());
    for (std::size_t i = 0; i < px.size(); ++i) {
        EXPECT_EQ(channel(d.pixels[i], 24), channel(px[i], 24)) << "alpha of pixel " << i;
        if (channel(px[i], 24) != 0) {
            // the RGB of a pixel with alpha 0 is not stable (libwebp 1.6.0 does not set config.exact)
            EXPECT_EQ(d.pixels[i], px[i]) << "pixel " << i;
        }
    }
    // pixel (3, 3) has alpha 0: the alpha is kept (checked above); the alpha-1 pixel keeps its colour
    EXPECT_EQ(channel(d.pixels[7 * kW + 10], 24), 0x01);
}

// ---- Proof 2: lossy, container chunks and decode

TEST(WebpLossy, OpaqueTileIsPlainVp8WithTheInputSize) {
    constexpr int kW = 64;
    constexpr int kH = 48;
    const auto px = gradientPixels(kW, kH, AlphaMode::Opaque);
    const QByteArray out = BitmapTileFreeImageSaverWebp(90).save(buildTile(kW, kH, px));
    const ParsedWebp p = parseWebp(out);
    ASSERT_TRUE(p.riffWebp);
    EXPECT_TRUE(p.sizeFieldMatches);
    ASSERT_FALSE(p.chunks.empty());
    EXPECT_EQ(p.chunks.front(), "VP8 "); // lossy bitstream, no VP8X for an opaque image
    EXPECT_FALSE(hasChunk(p, "ALPH"));
    const Decoded d = decodeWebp(out);
    EXPECT_EQ(d.width, kW);
    EXPECT_EQ(d.height, kH);
    for (const Color32 c : d.pixels) {
        EXPECT_EQ(channel(c, 24), 0xFF);
    }
    EXPECT_LE(maxColourError(px, d.pixels), 8);
}

TEST(WebpLossy, NonOpaqueTileHasVp8xAlphChunkAndExactAlpha) {
    constexpr int kW = 64;
    constexpr int kH = 48;
    const auto px = gradientPixels(kW, kH, AlphaMode::Varied);
    const QByteArray out = BitmapTileFreeImageSaverWebp(90).save(buildTile(kW, kH, px));
    const ParsedWebp p = parseWebp(out);
    ASSERT_TRUE(p.riffWebp);
    EXPECT_TRUE(p.sizeFieldMatches);
    ASSERT_FALSE(p.chunks.empty());
    EXPECT_EQ(p.chunks.front(), "VP8X");
    EXPECT_TRUE(hasChunk(p, "ALPH"));
    EXPECT_TRUE(hasChunk(p, "VP8 "));
    const Decoded d = decodeWebp(out);
    ASSERT_EQ(d.width, kW);
    ASSERT_EQ(d.height, kH);
    ASSERT_EQ(d.pixels.size(), px.size());
    for (std::size_t i = 0; i < px.size(); ++i) {
        EXPECT_EQ(channel(d.pixels[i], 24), channel(px[i], 24)) << "alpha of pixel " << i;
    }
}

TEST(WebpLossy, OnePixelWithAlphaFeMakesTheTileNonOpaque) {
    constexpr int kW = 16;
    constexpr int kH = 16;
    auto px = gradientPixels(kW, kH, AlphaMode::Opaque);
    px[static_cast<std::size_t>(kW * kH - 1)] &= 0x00FFFFFFU;
    px[static_cast<std::size_t>(kW * kH - 1)] |= 0xFE000000U;
    const QByteArray out = BitmapTileFreeImageSaverWebp(90).save(buildTile(kW, kH, px));
    const ParsedWebp p = parseWebp(out);
    ASSERT_FALSE(p.chunks.empty());
    EXPECT_EQ(p.chunks.front(), "VP8X");
    EXPECT_TRUE(hasChunk(p, "ALPH"));
    EXPECT_EQ(channel(decodeWebp(out).pixels.back(), 24), 0xFE);
}

TEST(WebpLossy, ByteOrderBgraIsNotSwapped) {
    // One flat blue tile: Color32 0xFF0000FF is B = FF, G = 00, R = 00. A swapped R / B import would decode
    // red.
    const std::vector<Color32> px(16U * 16U, 0xFF0000FFU);
    const Decoded d = decodeWebp(BitmapTileFreeImageSaverWebp(100).save(buildTile(16, 16, px)));
    ASSERT_EQ(d.pixels.size(), px.size());
    EXPECT_GE(channel(d.pixels[0], 0), 0xF0);  // B
    EXPECT_LE(channel(d.pixels[0], 16), 0x10); // R
}

// ---- Proof 3: quality

TEST(WebpQuality, OutOfRangeThrowsWithTheLegacyText) {
    struct Case {
        std::uint8_t quality;
        const char* text;
    };
    const Case cases[] = {{0, "Invalid WebP quality value: 0"},
                          {101, "Invalid WebP quality value: 101"},
                          {255, "Invalid WebP quality value: 255"}};
    for (const Case& c : cases) {
        try {
            BitmapTileFreeImageSaverWebp saver(c.quality);
            ADD_FAILURE() << "quality " << int{c.quality} << " accepted";
        } catch (const BitmapCodecError& e) {
            EXPECT_STREQ(e.what(), c.text);
        }
    }
}

TEST(WebpQuality, BoundsOneAndHundredAreAccepted) {
    const auto px = gradientPixels(16, 16, AlphaMode::Opaque);
    const auto tile = buildTile(16, 16, px);
    for (const std::uint8_t quality : {std::uint8_t{1}, std::uint8_t{100}}) {
        BitmapTileFreeImageSaverWebp saver(quality);
        const Decoded d = decodeWebp(saver.save(tile));
        EXPECT_EQ(d.width, 16) << int{quality};
        EXPECT_EQ(d.height, 16) << int{quality};
    }
}

TEST(WebpQuality, QualityReachesTheEncoder) {
    const auto px = noisePixels(64, 64);
    const auto tile = buildTile(64, 64, px);
    const qsizetype low = BitmapTileFreeImageSaverWebp(30).save(tile).size();
    const qsizetype high = BitmapTileFreeImageSaverWebp(90).save(tile).size();
    EXPECT_LT(low, high);
}

TEST(WebpQuality, LossyIsLossyAndLosslessIsNot) {
    // A noisy tile cannot survive quality 30 unchanged; the lossless saver returns it unchanged.
    const auto px = noisePixels(32, 32);
    const auto tile = buildTile(32, 32, px);
    EXPECT_NE(decodeWebp(BitmapTileFreeImageSaverWebp(30).save(tile)).pixels, px);
    EXPECT_EQ(decodeWebp(BitmapTileFreeImageSaverWebpLossless().save(tile)).pixels, px);
}

// ---- Null bitmap (the D row of this ticket: legacy dereferences nil at :391)

TEST(WebpSavers, NullBitmapThrowsTheSaveFailureText) {
    EXPECT_EQ(saveFailure(BitmapTileFreeImageSaverWebp(50), nullptr), "FreeBitmap.SaveToStream FAIL!");
    EXPECT_EQ(saveFailure(BitmapTileFreeImageSaverWebpLossless(), nullptr), "FreeBitmap.SaveToStream FAIL!");
}

// ---- Proof 4: GIF and TIFF refuse

TEST(GifTiffSavers, SaveRefusesWithTheExactText) {
    const auto tile = buildTile(4, 4, gradientPixels(4, 4, AlphaMode::Opaque));
    EXPECT_EQ(saveFailure(BitmapTileFreeImageSaverGif(), tile), "GIF saver is not ported");
    EXPECT_EQ(saveFailure(BitmapTileFreeImageSaverTiff(7, 24), tile), "TIFF saver is not ported");
}

// ---- Proof 5: stateless savers. The latch starts the threads together; under --tsan this shows that the
// shared objects hold no state. The outputs of the threads must equal the single-thread output.

TEST(WebpThreads, SharedSaversGiveEqualResults) {
    const auto px = gradientPixels(48, 32, AlphaMode::Varied);
    const auto tile = buildTile(48, 32, px);
    const auto lossy = std::make_shared<BitmapTileFreeImageSaverWebp>(75);
    const auto lossless = std::make_shared<BitmapTileFreeImageSaverWebpLossless>();
    const QByteArray expectLossy = lossy->save(tile);
    const QByteArray expectLossless = lossless->save(tile);

    constexpr int kN = 4;
    std::latch go(2 * kN);
    std::vector<QByteArray> lossyOut(kN);
    std::vector<QByteArray> losslessOut(kN);
    std::vector<std::thread> threads;
    for (int i = 0; i < kN; ++i) {
        const auto at = static_cast<std::size_t>(i);
        threads.emplace_back([&, at] {
            go.arrive_and_wait();
            lossyOut[at] = lossy->save(tile);
        });
        threads.emplace_back([&, at] {
            go.arrive_and_wait();
            losslessOut[at] = lossless->save(tile);
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    for (std::size_t i = 0; i < kN; ++i) {
        EXPECT_EQ(lossyOut[i], expectLossy);
        EXPECT_EQ(losslessOut[i], expectLossless);
    }
}

} // namespace
