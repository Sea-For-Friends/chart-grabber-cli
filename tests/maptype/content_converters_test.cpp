// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of ContentConverters (T-0120, FEAT-NET-013): the converter matrix of
// Src/ContentType/u_ContentTypeManagerSimple.pas:297-303 (Fixed, Synonyms, FixedWithSynonyms, Bitmaps) and
// u_ContentConverterBitmap.pas:75-78. Expectations are literals from the Pascal and from docs/specs/bitmap.md
// 2.6; pixels of a conversion are compared with an independent decode of the same bytes.
#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/maptype/content_converters.h"

#include <QByteArray>
#include <QList>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "maptype_test_util.h"

using namespace cg;
using cg::maptype::ContentConverterPtr;

namespace {

const maptype::ContentConverters& converters() {
    return *maptype::test::testContentConverters();
}

const QByteArray kPngSignature("\x89PNG\r\n\x1a\n", 8);

QByteArray jpegOf(const bitmap::Bitmap32StaticPtr& b) {
    return maptype::test::testBitmapFactory()->createJpegSaver()->save(b);
}
QByteArray pngOf(const bitmap::Bitmap32StaticPtr& b) {
    return maptype::test::testBitmapFactory()->createPngSaver()->save(b);
}

std::vector<core::Color32> pixelsOf(const bitmap::Bitmap32StaticPtr& b) {
    return {b->data(), b->data() + bitmap::pixelCount(b->size())};
}

QByteArray convertOrFail(const char* source, const char* target, const QByteArray& data) {
    const ContentConverterPtr c = converters().converter(source, target);
    EXPECT_NE(c, nullptr) << source << " -> " << target;
    if (!c) {
        return {};
    }
    const auto out = c->convert(data);
    EXPECT_TRUE(out.has_value());
    return out.value_or(QByteArray());
}

// The 14 keys of the bitmap type list and their canonical types (docs/specs/bitmap.md 2.6).
struct KeyRow {
    const char* key;
    const char* canonical;
};
constexpr std::array<KeyRow, 14> kBitmapKeys{{
    {"image/jpg", "image/jpg"},
    {"image/jpeg", "image/jpg"},
    {"image/pjpeg", "image/jpg"},
    {"image/png", "image/png"},
    {"image/x-png", "image/png"},
    {"image/png; mode=24bit", "image/png"},
    {"image/gif", "image/gif"},
    {"image/bmp", "image/bmp"},
    {"image/x-ms-bmp", "image/bmp"},
    {"image/x-windows-bmp", "image/bmp"},
    {"image/x-icon", "image/x-icon"},
    {"image/vnd.microsoft.icon", "image/x-icon"},
    {"image/webp", "image/webp"},
    {"image/tiff", "image/tiff"},
}};

} // namespace

// Proof 1: the pairs that re-encode (ConverterMatrixUpdateBitmaps :539-575; Convert = Save(Load) :75-78).
TEST(ContentConverters, BitmapPairsReEncode) {
    const auto factory = maptype::test::testBitmapFactory();
    const auto solid = maptype::test::testBitmap(16, 16, 0xFF3366CC);
    const QByteArray jpeg = jpegOf(solid);
    const QByteArray png = pngOf(solid);

    const QByteArray jpegToPng = convertOrFail("image/jpeg", "image/png", jpeg);
    EXPECT_TRUE(jpegToPng.startsWith(kPngSignature));
    const auto viaPng = factory->createPngLoader()->load(jpegToPng);
    const auto direct = factory->createJpegLoader()->load(jpeg);
    ASSERT_TRUE(viaPng && direct);
    EXPECT_EQ(viaPng->size(), QPoint(16, 16));
    EXPECT_EQ(pixelsOf(viaPng), pixelsOf(direct));

    const QByteArray pngToJpg = convertOrFail("image/png", "image/jpg", png);
    EXPECT_TRUE(pngToJpg.startsWith(QByteArray("\xFF\xD8", 2)));
    const auto jpgDecoded = factory->createJpegLoader()->load(pngToJpg);
    ASSERT_TRUE(jpgDecoded);
    EXPECT_EQ(jpgDecoded->size(), QPoint(16, 16));

    const QByteArray pngToWebp = convertOrFail("image/png", "image/webp", png);
    EXPECT_TRUE(pngToWebp.startsWith("RIFF"));
    EXPECT_EQ(pngToWebp.mid(8, 4), QByteArray("WEBP"));

    // lossless pair of an opaque picture: the pixels survive
    const auto picture = maptype::test::testBitmap(16, 12, 0, true);
    const QByteArray pngToBmp = convertOrFail("image/png", "image/bmp", pngOf(picture));
    const auto bmpDecoded = factory->createBmpLoader()->load(pngToBmp);
    ASSERT_TRUE(bmpDecoded);
    EXPECT_EQ(bmpDecoded->size(), QPoint(16, 12));
    EXPECT_EQ(pixelsOf(bmpDecoded), pixelsOf(picture));
}

// Proof 2: a synonym pair is a copy (UpdateSynonyms :390-424), bytes pass through untouched.
TEST(ContentConverters, SynonymPairsAreSimpleCopies) {
    struct Pair {
        const char* source;
        const char* target;
    };
    const Pair pairs[] = {
        {"image/jpeg", "image/jpg"},
        {"image/x-png", "image/png"},
        {"image/png; mode=24bit", "image/png"},
        {"image/gif", "image/gif"},
        {"application/json", "application/json"},
    };
    const QByteArray notAnImage("\x01\x02\x03", 3);
    for (const Pair& p : pairs) {
        SCOPED_TRACE(std::string(p.source) + " -> " + p.target);
        const ContentConverterPtr c = converters().converter(p.source, p.target);
        ASSERT_NE(c, nullptr);
        EXPECT_TRUE(c->isSimpleCopy());
        EXPECT_EQ(c->convert(notAnImage), notAnImage);
    }
    // not synonyms: a re-encoding converter, not a copy
    const ContentConverterPtr reEncode = converters().converter("image/png", "image/jpg");
    ASSERT_NE(reEncode, nullptr);
    EXPECT_FALSE(reEncode->isSimpleCopy());
}

// Proof 3: ConverterMatrixUpdateFixed :313-323, :365-387 (entries 2-4 are T-0178).
TEST(ContentConverters, FixedCopiesAreDirectional) {
    const QByteArray body("\x01\x02\x03", 3);
    const std::pair<const char*, const char*> copies[] = {
        {"application/json", "application/geo+json"},
        {"application/geo+json", "application/json"},
        {"application/vnd.google-earth.kml+xml", "application/vnd.sas.wikimapia.kml+xml"},
    };
    for (const auto& [source, target] : copies) {
        SCOPED_TRACE(std::string(source) + " -> " + target);
        const ContentConverterPtr c = converters().converter(source, target);
        ASSERT_NE(c, nullptr);
        EXPECT_TRUE(c->isSimpleCopy());
        EXPECT_EQ(c->convert(body), body);
    }
    EXPECT_EQ(converters().converter("application/vnd.sas.wikimapia.kml+xml",
                                     "application/vnd.google-earth.kml+xml"),
              nullptr);
}

// Proof 4: existence over the 14 x 14 bitmap keys (:539-575): every ordered pair except those INTO an icon
// from a non-icon (no saver); a copy exactly when the canonical types are equal (:390-424).
TEST(ContentConverters, BitmapKeyMatrixExistenceAndCopies) {
    int checked = 0;
    for (const KeyRow& s : kBitmapKeys) {
        for (const KeyRow& t : kBitmapKeys) {
            SCOPED_TRACE(std::string(s.key) + " -> " + t.key);
            const bool sameType = std::string(s.canonical) == t.canonical;
            const bool intoIcon = std::string(t.canonical) == "image/x-icon";
            const ContentConverterPtr c = converters().converter(s.key, t.key);
            if (intoIcon && !sameType) {
                EXPECT_EQ(c, nullptr);
            } else {
                ASSERT_NE(c, nullptr);
                EXPECT_EQ(c->isSimpleCopy(), sameType);
            }
            ++checked;
        }
    }
    EXPECT_EQ(checked, 196);
}

// Proof 5: GIF and TIFF savers exist and refuse (bitmap.md 8.2; handoff T-0162): the converter exists,
// convert throws the codec text.
TEST(ContentConverters, GifAndTiffTargetsExistAndThrow) {
    const QByteArray png = pngOf(maptype::test::testBitmap(16, 16, 0xFF3366CC));
    const std::pair<const char*, const char*> targets[] = {
        {"image/gif", "GIF saver is not ported"},
        {"image/tiff", "TIFF saver is not ported"},
    };
    for (const auto& [target, text] : targets) {
        SCOPED_TRACE(target);
        const ContentConverterPtr c = converters().converter("image/png", target);
        ASSERT_NE(c, nullptr);
        try {
            (void)c->convert(png);
            FAIL() << "no exception";
        } catch (const bitmap::BitmapCodecError& e) {
            EXPECT_STREQ(e.what(), text);
        }
    }
}

// Proof 6: GetConverter / matrix Get (:93-106): nil for an absent key, keys matched case-insensitively.
TEST(ContentConverters, LookupRules) {
    EXPECT_EQ(converters().converter("image/unknown", "image/png"), nullptr);
    EXPECT_EQ(converters().converter("image/png", "image/unknown"), nullptr);
    EXPECT_EQ(converters().converter("", "image/png"), nullptr);
    EXPECT_EQ(converters().converter("image/png", ""), nullptr);
    EXPECT_EQ(converters().converter("", ""), nullptr);
    EXPECT_EQ(converters().converter("\xD0\x98\xD0\xBC\xD0\xB0\xD0\xB3", "image/png"), nullptr);
    EXPECT_EQ(converters().converter("image/png", "image/jp\xC3\xA9g"), nullptr);

    const ContentConverterPtr lower = converters().converter("image/jpeg", "image/png");
    ASSERT_NE(lower, nullptr);
    EXPECT_EQ(converters().converter("Image/Jpeg", "IMAGE/PNG"), lower);
    EXPECT_EQ(converters().converter("IMAGE/JPEG", "image/png"), lower);

    EXPECT_EQ(converters().converter("image/png", "application/json"), nullptr);
    EXPECT_EQ(converters().converter("application/json", "image/png"), nullptr);
}

// Proof 11: one immutable set shared by four workers. The barrier makes all four start every round together;
// the test pins equal output, TSan (20 repeats) checks the sharing.
TEST(ContentConverters, SharedBetweenThreads) {
    const QByteArray jpeg = jpegOf(maptype::test::testBitmap(16, 16, 0xFF3366CC));
    const QByteArray first = convertOrFail("image/jpeg", "image/png", jpeg);
    ASSERT_TRUE(first.startsWith(kPngSignature));

    constexpr int kThreads = 4;
    constexpr int kRounds = 50;
    std::barrier round(kThreads);
    std::latch start(kThreads);
    std::atomic<int> mismatches{0};
    std::atomic<int> done{0};
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([&] {
                start.arrive_and_wait();
                for (int i = 0; i < kRounds; ++i) {
                    round.arrive_and_wait();
                    const ContentConverterPtr c = converters().converter("image/jpeg", "image/png");
                    if (!c || c->convert(jpeg) != first) {
                        ++mismatches;
                    }
                    ++done;
                }
            });
        }
    }
    EXPECT_EQ(done.load(), kThreads * kRounds);
    EXPECT_EQ(mismatches.load(), 0);
}
