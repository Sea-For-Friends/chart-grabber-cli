// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of T-0160: Src/BitmapTileSaveLoad/u_BitmapTileLibJpeg.pas (TLibJpegTileLoader :117-157,
// TLibJpegTileSaver :207-254), Includes/LibJpeg/LibJpegRead.pas (:146, :237-290), LibJpegWrite.pas
// (:194-204, :277), LibJpegErrorHandler.pas (:17-40: a warning is an error), LibJpegInOutDataManager.pas
// (:80-98: fake EOI without a warning).
// FEAT-RND-042
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"
#include "cg/bitmap/bitmap_codec_error.h"
#include "cg/bitmap/bitmap_tile_lib_jpeg.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../corpus_skip.h"
#include "sanitizer_skip.h"

extern "C" {
#include <jpeglib.h>
}

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

LibJpegTileLoader makeLoader() {
    return LibJpegTileLoader(staticFactory());
}

// A bitmap whose pixels differ from each other.
Bitmap32StaticPtr makeBitmap(int w, int h, bool smooth, Color32 alphaMask = 0xFF000000U) {
    std::vector<Color32> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color32 r;
            Color32 g;
            Color32 b;
            if (smooth) {
                b = static_cast<Color32>(x * 4);
                g = static_cast<Color32>(y * 4);
                r = static_cast<Color32>((x + y) * 2);
            } else {
                b = static_cast<Color32>((x * 37 + y * 11) & 0xFF);
                g = static_cast<Color32>((x * 5 + y * 91 + 7) & 0xFF);
                r = static_cast<Color32>((x * 53 ^ y * 29) & 0xFF);
            }
            px[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                alphaMask | (r << 16) | (g << 8) | b;
        }
    }
    return staticFactory()->build(QPoint(w, h), px.data());
}

// Reference decode with libjpeg directly: JCS_EXT_BGRA, every other setting the default.
std::vector<std::uint8_t> referenceDecode(const QByteArray& data, int& w, int& h) {
    jpeg_decompress_struct cinfo{};
    jpeg_error_mgr err{};
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(data.constData()),
                 static_cast<unsigned long>(data.size()));
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_EXT_BGRA;
    jpeg_start_decompress(&cinfo);
    w = static_cast<int>(cinfo.output_width);
    h = static_cast<int>(cinfo.output_height);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row =
            out.data() + static_cast<std::size_t>(cinfo.output_scanline) * static_cast<std::size_t>(w) * 4;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return out;
}

// Natural-order index of zigzag position i (ITU T.81 figure A.6).
constexpr std::array<int, 64> kZigzag = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                         12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                         35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                         58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// Quantisation tables (natural order) that libjpeg produces for jpeg_set_quality(q, TRUE).
std::array<std::array<unsigned, 64>, 2> referenceTables(int quality) {
    jpeg_compress_struct cinfo{};
    jpeg_error_mgr err{};
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_compress(&cinfo);
    cinfo.image_width = 8;
    cinfo.image_height = 8;
    cinfo.input_components = 4;
    cinfo.in_color_space = JCS_EXT_BGRA;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    std::array<std::array<unsigned, 64>, 2> t{};
    for (int id = 0; id < 2; ++id) {
        for (int i = 0; i < 64; ++i) {
            t[static_cast<std::size_t>(id)][static_cast<std::size_t>(i)] =
                cinfo.quant_tbl_ptrs[id]->quantval[i];
        }
    }
    jpeg_destroy_compress(&cinfo);
    return t;
}

struct Parsed {
    bool soi = false;
    std::vector<int> markers; // marker bytes up to and including SOS
    QByteArray app0;
    QByteArray sof;
    std::array<std::array<unsigned, 64>, 2> dqt{}; // natural order
    int dqtCount = 0;
};

Parsed parse(const QByteArray& d) {
    Parsed p;
    const auto* b = reinterpret_cast<const std::uint8_t*>(d.constData());
    const auto n = static_cast<std::size_t>(d.size());
    p.soi = n > 2 && b[0] == 0xFF && b[1] == 0xD8;
    std::size_t pos = 2;
    while (pos + 4 <= n && b[pos] == 0xFF) {
        const int m = b[pos + 1];
        p.markers.push_back(m);
        const std::size_t len = (static_cast<std::size_t>(b[pos + 2]) << 8) | b[pos + 3];
        const QByteArray body = d.mid(static_cast<qsizetype>(pos + 4), static_cast<qsizetype>(len - 2));
        if (m == 0xE0) {
            p.app0 = body;
        } else if (m == 0xC0) {
            p.sof = body;
        } else if (m == 0xDB) {
            std::size_t o = 0;
            while (o < static_cast<std::size_t>(body.size())) {
                const int pq = static_cast<std::uint8_t>(body[static_cast<qsizetype>(o)]) >> 4;
                const int id = static_cast<std::uint8_t>(body[static_cast<qsizetype>(o)]) & 15;
                ++o;
                for (int i = 0; i < 64; ++i) {
                    unsigned v = static_cast<std::uint8_t>(body[static_cast<qsizetype>(o++)]);
                    if (pq == 1) {
                        v = (v << 8) | static_cast<std::uint8_t>(body[static_cast<qsizetype>(o++)]);
                    }
                    p.dqt[static_cast<std::size_t>(id)]
                         [static_cast<std::size_t>(kZigzag[static_cast<std::size_t>(i)])] = v;
                }
                ++p.dqtCount;
            }
        }
        pos += 2 + len;
        if (m == 0xDA) {
            break;
        }
    }
    return p;
}

std::string messageOfLoad(const QByteArray& data) {
    try {
        (void)makeLoader().load(data);
    } catch (const BitmapCodecError& e) {
        return e.what();
    }
    return "<no throw>";
}

// SOI, SOF0 (65500 x 65500, one component), SOS; no scan data.
QByteArray hugeHeader() {
    const unsigned char bytes[] = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0xFF, 0xDC,
                                   0xFF, 0xDC, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xDA, 0x00,
                                   0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00};
    return QByteArray(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

} // namespace

// Proof 1: the loader equals libjpeg with JCS_EXT_BGRA and defaults; alpha FF (LibJpegRead.pas:252).
TEST(LibJpegLoader, FixtureEqualsDirectLibjpegDecode) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const QByteArray data = readFixture("test.jpg");
    ASSERT_GT(data.size(), 1000);
    const auto bmp = makeLoader().load(data);
    ASSERT_TRUE(bmp);
    EXPECT_EQ(bmp->size(), QPoint(256, 256));
    int w = 0;
    int h = 0;
    const auto ref = referenceDecode(data, w, h);
    ASSERT_EQ(w, 256);
    ASSERT_EQ(h, 256);
    ASSERT_EQ(ref.size(), 256U * 256U * 4U);
    EXPECT_EQ(std::memcmp(bmp->data(), ref.data(), ref.size()), 0);
    bool varied = false;
    for (std::size_t i = 0; i < 256U * 256U; ++i) {
        EXPECT_EQ(bmp->data()[i] >> 24, 0xFFU) << i;
        varied = varied || bmp->data()[i] != bmp->data()[0];
    }
    EXPECT_TRUE(varied);
}

// Proof 2: cut inside the entropy-coded data; libjpeg-turbo warns (OUTPUT [120]) and the warning fails
// the load (LibJpegErrorHandler.pas:30-40).
TEST(LibJpegLoader, TruncatedFileThrowsLibjpegText) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const QByteArray data = readFixture("test.jpg");
    const std::string msg = messageOfLoad(data.left(data.size() / 2));
    EXPECT_TRUE(msg.rfind("OUTPUT [", 0) == 0 || msg.rfind("ERROR [", 0) == 0) << msg;
}

// Proof 2: the final FF D9 is missing; the own source manager hands out the EOI without a warning
// (LibJpegInOutDataManager.pas:80-98), so the load succeeds and equals the intact file.
TEST(LibJpegLoader, MissingFinalEoiLoads) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const QByteArray data = readFixture("test.jpg");
    ASSERT_GT(data.size(), 4);
    ASSERT_EQ(static_cast<std::uint8_t>(data[data.size() - 2]), 0xFF);
    ASSERT_EQ(static_cast<std::uint8_t>(data[data.size() - 1]), 0xD9);
    const auto whole = makeLoader().load(data);
    const auto cut = makeLoader().load(data.left(data.size() - 2));
    ASSERT_TRUE(whole);
    ASSERT_TRUE(cut);
    EXPECT_EQ(std::memcmp(whole->data(), cut->data(), 256U * 256U * 4U), 0);
}

// Proof 2: 16 extraneous bytes before the final EOI make libjpeg-turbo warn ("Corrupt JPEG data: 16
// extraneous bytes before marker 0xd9"; up to 4 bytes are absorbed by the Huffman bit buffer and give no
// warning); the warning fails the load.
TEST(LibJpegLoader, GarbageBeforeEoiThrows) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    CG_SKIP_LONGJMP_UNDER_TSAN();
    QByteArray data = readFixture("test.jpg");
    data.insert(data.size() - 2, QByteArray(16, '\x12'));
    const std::string msg = messageOfLoad(data);
    EXPECT_EQ(msg.rfind("OUTPUT [", 0), 0U) << msg;
    EXPECT_NE(msg.find("extraneous"), std::string::npos) << msg;
}

// Proof 2: not a JPEG at all (an ERROR of the library, LibJpegErrorHandler.pas:17-28).
TEST(LibJpegLoader, PngThrowsErrorText) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    CG_SKIP_LONGJMP_UNDER_TSAN();
    const std::string msg = messageOfLoad(readFixture("test.png"));
    EXPECT_EQ(msg.rfind("ERROR [", 0), 0U) << msg;
    EXPECT_NE(msg.find("Not a JPEG file"), std::string::npos) << msg;
}

// T-0176 (the D row of T-0176): a JPEG with restart intervals whose first RST marker is wrong. Legacy calls a
// nil resync_to_restart (LibJpegRead.pas:224-229); the port resyncs inside libjpeg, which warns, and the
// warning fails the load with a BitmapCodecError.
TEST(LibJpegLoader, WrongRestartMarkerThrows) {
    CG_SKIP_LONGJMP_UNDER_TSAN();
    std::vector<std::uint8_t> raw(64U * 64U * 3U);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>((i * 7 + i / 192) & 0xFF);
    }
    jpeg_compress_struct c{};
    jpeg_error_mgr err{};
    c.err = jpeg_std_error(&err);
    jpeg_create_compress(&c);
    unsigned char* buf = nullptr;
    unsigned long len = 0;
    jpeg_mem_dest(&c, &buf, &len);
    c.image_width = 64;
    c.image_height = 64;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    c.restart_interval = 4; // MCUs
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < c.image_height) {
        JSAMPROW row = raw.data() + static_cast<std::size_t>(c.next_scanline) * 192U;
        jpeg_write_scanlines(&c, &row, 1);
    }
    jpeg_finish_compress(&c);
    QByteArray data(reinterpret_cast<const char*>(buf), static_cast<int>(len));
    jpeg_destroy_compress(&c);
    std::free(buf);

    ASSERT_NE(makeLoader().load(data), nullptr); // intact: loads
    const int rst = data.indexOf(QByteArray("\xFF\xD0", 2));
    ASSERT_GT(rst, 0);
    data[rst + 1] = static_cast<char>(0xD4); // RST0 -> RST4
    const std::string msg = messageOfLoad(data);
    EXPECT_NE(msg, "<no throw>");
    EXPECT_FALSE(msg.empty());
}

// Proof 5: 65500 x 65500 is refused by the buffer factory (x * y >= 2^28): nullptr, no allocation.
TEST(LibJpegLoader, OversizeFrameGivesNullptr) {
    EXPECT_EQ(makeLoader().load(hugeHeader()), nullptr);
}

// Proof 3: structure of the saver output (LibJpegWrite.pas:194-204, :277; library defaults).
TEST(LibJpegSaver, OutputStructure) {
    const QByteArray out = LibJpegTileSaver(85).save(makeBitmap(64, 48, false));
    const Parsed p = parse(out);
    EXPECT_TRUE(p.soi);
    ASSERT_GE(out.size(), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(out[out.size() - 2]), 0xFF);
    EXPECT_EQ(static_cast<std::uint8_t>(out[out.size() - 1]), 0xD9);
    // JFIF 1.01, density unit 0, 1 x 1, no thumbnail
    const unsigned char jfif[] = {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    EXPECT_EQ(p.app0, QByteArray(reinterpret_cast<const char*>(jfif), sizeof(jfif)));
    ASSERT_FALSE(p.markers.empty());
    EXPECT_EQ(p.markers.front(), 0xE0);
    for (const int m : p.markers) {
        EXPECT_NE(m, 0xE1);
        EXPECT_NE(m, 0xE2);
        EXPECT_NE(m, 0xFE);
        EXPECT_NE(m, 0xC2); // not progressive
    }
    // SOF0 body: P=8, H=48, W=64, Nf=3; Y 0x22 tq0, Cb 0x11 tq1, Cr 0x11 tq1
    const unsigned char sof[] = {8, 0, 48, 0, 64, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1};
    EXPECT_EQ(p.sof, QByteArray(reinterpret_cast<const char*>(sof), sizeof(sof)));
}

// Proof 3: quality 85 and 75 give the tables of jpeg_set_quality(q, TRUE) and differ from each other.
TEST(LibJpegSaver, QuantTablesFollowQuality) {
    const auto bmp = makeBitmap(64, 48, false);
    const Parsed p85 = parse(LibJpegTileSaver(85).save(bmp));
    const Parsed p75 = parse(LibJpegTileSaver(75).save(bmp));
    EXPECT_EQ(p85.dqtCount, 2);
    EXPECT_EQ(p75.dqtCount, 2);
    EXPECT_EQ(p85.dqt, referenceTables(85));
    EXPECT_EQ(p75.dqt, referenceTables(75));
    EXPECT_NE(p85.dqt, p75.dqt);
}

// Proof 3: quality 100 and 1 are accepted; 0 is clamped to 1 by the library (passed as is).
TEST(LibJpegSaver, ExtremeQualities) {
    const auto bmp = makeBitmap(64, 48, false);
    const QByteArray q100 = LibJpegTileSaver(100).save(bmp);
    const QByteArray q1 = LibJpegTileSaver(1).save(bmp);
    const QByteArray q0 = LibJpegTileSaver(0).save(bmp);
    EXPECT_EQ(parse(q100).dqt, referenceTables(100));
    EXPECT_EQ(parse(q1).dqt, referenceTables(1));
    EXPECT_EQ(q0, q1);
    EXPECT_NE(q100, q1);
    EXPECT_TRUE(makeLoader().load(q100));
    EXPECT_TRUE(makeLoader().load(q1));
}

// Proof 4: round trip of a smooth gradient at quality 100 (BGRA order and alpha FF).
TEST(LibJpegSaver, RoundTripWithinThree) {
    const auto src = makeBitmap(64, 64, true);
    const auto back = makeLoader().load(LibJpegTileSaver(100).save(src));
    ASSERT_TRUE(back);
    ASSERT_EQ(back->size(), QPoint(64, 64));
    for (std::size_t i = 0; i < 64U * 64U; ++i) {
        const Color32 a = src->data()[i];
        const Color32 b = back->data()[i];
        EXPECT_EQ(b >> 24, 0xFFU);
        for (const int shift : {0, 8, 16}) {
            const int da = static_cast<int>((a >> shift) & 0xFFU);
            const int db = static_cast<int>((b >> shift) & 0xFFU);
            EXPECT_LE(std::abs(da - db), 3) << "pixel " << i << " shift " << shift;
        }
    }
}

// Proof 4: the alpha of the source is ignored.
TEST(LibJpegSaver, SourceAlphaIgnored) {
    const QByteArray opaque = LibJpegTileSaver(85).save(makeBitmap(32, 32, false, 0xFF000000U));
    const QByteArray half = LibJpegTileSaver(85).save(makeBitmap(32, 32, false, 0x80000000U));
    EXPECT_EQ(opaque, half);
}

// D row of T-0160: a null bitmap is a BitmapCodecError (legacy: access violation).
TEST(LibJpegSaver, NullBitmapThrows) {
    EXPECT_THROW((void)LibJpegTileSaver(85).save(nullptr), BitmapCodecError);
}

// Proof 6: one loader and one saver used from four threads each; results are equal. The latch makes
// all eight start together; with --tsan this also shows that the objects hold no state.
TEST(LibJpegThreads, SharedObjectsGiveEqualResults) {
    CG_SKIP_IF_NO_FIXTURE(fixturesDir() + QStringLiteral("/legacy-test-data/bitmaps/test.png"));
    const QByteArray data = readFixture("test.jpg");
    const auto loader = std::make_shared<LibJpegTileLoader>(staticFactory());
    const auto saver = std::make_shared<LibJpegTileSaver>(85);
    const auto tile = makeBitmap(64, 48, false);
    const QByteArray expectSaved = saver->save(tile);
    const auto expectLoaded = loader->load(data);
    ASSERT_TRUE(expectLoaded);

    constexpr int kN = 4;
    std::latch go(2 * kN);
    std::vector<Bitmap32StaticPtr> loaded(kN);
    std::vector<QByteArray> saved(kN);
    std::vector<std::thread> threads;
    for (int i = 0; i < kN; ++i) {
        threads.emplace_back([&, i] {
            go.arrive_and_wait();
            loaded[static_cast<std::size_t>(i)] = loader->load(data);
        });
        threads.emplace_back([&, i] {
            go.arrive_and_wait();
            saved[static_cast<std::size_t>(i)] = saver->save(tile);
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    for (int i = 0; i < kN; ++i) {
        ASSERT_TRUE(loaded[static_cast<std::size_t>(i)]);
        EXPECT_EQ(
            std::memcmp(loaded[static_cast<std::size_t>(i)]->data(), expectLoaded->data(), 256U * 256U * 4U),
            0);
        EXPECT_EQ(saved[static_cast<std::size_t>(i)], expectSaved);
    }
}
