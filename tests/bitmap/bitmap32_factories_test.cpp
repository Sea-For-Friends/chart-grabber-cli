// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of T-0155: Src/Bitmap32/u_Bitmap32BufferFactorySimple.pas (size rule :141-147, Build :111-128,
// BuildEmptyClear :150-158), u_Bitmap32BufferFactory.pas (:82-124), u_Bitmap32StaticFactory.pas (:61-106),
// u_Bitmap32Static.pas (:55-81).
#include "cg/bitmap/bitmap32_buffer_factory.h"
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace cg::bitmap;
using cg::core::Color32;

// Both buffer factories carry the same rules (Build is written twice in legacy).
std::vector<Bitmap32BufferFactoryPtr> bufferFactories() {
    return {std::make_shared<Bitmap32BufferFactorySimple>(), std::make_shared<Bitmap32BufferFactory>()};
}

// Buffer that reports its lifetime; hands out a fixed pixel array.
class TrackedBuffer final : public IBitmap32Buffer {
public:
    TrackedBuffer(QPoint size, std::vector<Color32> pixels) : size_(size), pixels_(std::move(pixels)) {}
    QPoint size() const override { return size_; }
    Color32* data() override { return pixels_.data(); }
    const Color32* data() const override { return pixels_.data(); }

private:
    QPoint size_;
    std::vector<Color32> pixels_;
};

// Buffer factory that never produces a buffer (the nullptr branch of TBitmap32StaticFactory.Build, :86-91).
class NullBufferFactory final : public IBitmap32BufferFactory {
public:
    Bitmap32BufferPtr build(QPoint, const Color32*) const override { return nullptr; }
    Bitmap32BufferPtr buildEmpty(QPoint) const override { return nullptr; }
    Bitmap32BufferPtr buildEmptyClear(QPoint, Color32) const override { return nullptr; }
};

} // namespace

// Proof 1: u_Bitmap32BufferFactorySimple.pas:141-147.
TEST(Bitmap32BufferFactory, SizeRuleRejectsOutOfRange) {
    struct Case {
        int x;
        int y;
    };
    const Case rejected[] = {
        {0, 5},         {5, 0},     {0, 0},     {-1, 5},
        {5, -1},        {65536, 1}, {1, 65536}, {16384, 16384}, // x * y = 2^28
        {32768, 8192},                                          // x * y = 2^28
        {46342, 46342}, // x * y > 2^31: wraps in the 32-bit legacy product
        {65535, 65535},
    };
    for (const auto& f : bufferFactories()) {
        for (const auto& c : rejected) {
            EXPECT_EQ(f->buildEmpty(QPoint(c.x, c.y)), nullptr) << c.x << " x " << c.y;
            EXPECT_EQ(f->buildEmptyClear(QPoint(c.x, c.y), 0xFF010203U), nullptr) << c.x << " x " << c.y;
        }
    }
}

TEST(Bitmap32BufferFactory, SizeRuleAcceptsLargeThinShapes) {
    struct Case {
        int x;
        int y;
    };
    const Case accepted[] = {{65535, 1}, {1, 65535}, {1, 1}, {3, 2}};
    for (const auto& f : bufferFactories()) {
        for (const auto& c : accepted) {
            const auto buffer = f->buildEmpty(QPoint(c.x, c.y));
            ASSERT_NE(buffer, nullptr) << c.x << " x " << c.y;
            EXPECT_EQ(buffer->size(), QPoint(c.x, c.y));
            ASSERT_NE(buffer->data(), nullptr);
            // The last pixel must be writable (the array has x * y values).
            buffer->data()[static_cast<std::size_t>(c.x) * static_cast<std::size_t>(c.y) - 1] = 0xFFFFFFFFU;
        }
    }
}

TEST(Bitmap32BufferFactory, BuildOfZeroWidthGivesNullWithoutCrash) {
    const Color32 pixels[4] = {1, 2, 3, 4};
    for (const auto& f : bufferFactories()) {
        EXPECT_EQ(f->build(QPoint(0, 4), pixels), nullptr);
        EXPECT_EQ(f->build(QPoint(4, 0), pixels), nullptr);
    }
}

// Legacy copies only `if AData <> nil` (:124-125, :95-96): a buffer of the asked size comes back.
TEST(Bitmap32BufferFactory, BuildWithoutDataGivesBufferOfAskedSize) {
    for (const auto& f : bufferFactories()) {
        const auto buffer = f->build(QPoint(5, 3), nullptr);
        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(buffer->size(), QPoint(5, 3));
    }
}

// Proof 2.
TEST(Bitmap32BufferFactory, BuildCopiesEveryValueWithoutAliasing) {
    for (const auto& f : bufferFactories()) {
        std::vector<Color32> source = {0x00000001U, 0x11000002U, 0x22000003U,
                                       0x33000004U, 0x44000005U, 0x55000006U};
        const auto buffer = f->build(QPoint(3, 2), source.data());
        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(buffer->size(), QPoint(3, 2));
        EXPECT_NE(buffer->data(), source.data());
        const Color32 expected[6] = {0x00000001U, 0x11000002U, 0x22000003U,
                                     0x33000004U, 0x44000005U, 0x55000006U};
        for (int i = 0; i < 6; ++i) {
            EXPECT_EQ(buffer->data()[i], expected[i]) << "index " << i;
        }
        source[0] = 0xDEADBEEFU; // the copy does not follow the source
        EXPECT_EQ(buffer->data()[0], 0x00000001U);
    }
}

TEST(Bitmap32BufferFactory, BuildEmptyClearFillsEveryValue) {
    // A different colour per factory: a freed block of the previous factory must not hide an unfilled value.
    Color32 color = 0xA1B2C3D4U;
    for (const auto& f : bufferFactories()) {
        const auto buffer = f->buildEmptyClear(QPoint(7, 3), color);
        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(buffer->size(), QPoint(7, 3));
        for (int i = 0; i < 21; ++i) {
            EXPECT_EQ(buffer->data()[i], color) << "index " << i;
        }
        color = 0x1F2E3D4CU;
    }
}

// Proof 3: u_Bitmap32StaticFactory.pas:94-106, hash over x * y * SizeOf(TColor32) bytes.
TEST(Bitmap32StaticFactory, HashIsCityHash64OfPixelBytes) {
    const Bitmap32StaticFactory factory(std::make_shared<Bitmap32BufferFactory>());
    const Color32 pixels[6] = {0x11223344U, 0xAABBCCDDU, 0x01020304U, 0xF0E0D0C0U, 0x05060708U, 0x99887766U};
    // Memory image of the pixels: B, G, R, A of each value.
    const std::uint8_t bytes[24] = {0x44, 0x33, 0x22, 0x11, 0xDD, 0xCC, 0xBB, 0xAA, 0x04, 0x03, 0x02, 0x01,
                                    0xC0, 0xD0, 0xE0, 0xF0, 0x08, 0x07, 0x06, 0x05, 0x66, 0x77, 0x88, 0x99};
    const auto bitmap = factory.build(QPoint(3, 2), pixels);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_EQ(bitmap->hash(), cg::core::cityHash64(bytes, 24));
    EXPECT_NE(bitmap->hash(), cg::core::kNoHash);
}

TEST(Bitmap32StaticFactory, EqualPixelsEqualHashChangedPixelChangesIt) {
    const Bitmap32StaticFactory factory(std::make_shared<Bitmap32BufferFactory>());
    Color32 a[4] = {0x10000001U, 0x20000002U, 0x30000003U, 0x40000004U};
    Color32 b[4] = {0x10000001U, 0x20000002U, 0x30000003U, 0x40000004U};
    Color32 c[4] = {0x10000001U, 0x20000002U, 0x30000003U, 0x40000005U};
    const auto sa = factory.build(QPoint(2, 2), a);
    const auto sb = factory.build(QPoint(2, 2), b);
    const auto sc = factory.build(QPoint(2, 2), c);
    ASSERT_TRUE(sa && sb && sc);
    EXPECT_EQ(sa->hash(), sb->hash());
    EXPECT_NE(sa->hash(), sc->hash());
}

TEST(Bitmap32StaticFactory, StaticReportsSizeAndDataOfItsBuffer) {
    const Bitmap32StaticFactory factory(std::make_shared<Bitmap32BufferFactory>());
    auto buffer =
        std::make_shared<TrackedBuffer>(QPoint(2, 1), std::vector<Color32>{0x01020304U, 0x05060708U});
    const Color32* raw = buffer->data();
    const auto bitmap = factory.buildWithOwnBuffer(buffer);
    ASSERT_NE(bitmap, nullptr);
    EXPECT_EQ(bitmap->size(), QPoint(2, 1));
    EXPECT_EQ(bitmap->data(), raw);
    EXPECT_EQ(bitmap->data()[1], 0x05060708U);
}

TEST(Bitmap32StaticFactory, NullBufferGivesNullStatic) {
    // buildWithOwnBuffer(nil): :101-106 leaves Result unset.
    const Bitmap32StaticFactory real(std::make_shared<Bitmap32BufferFactory>());
    EXPECT_EQ(real.buildWithOwnBuffer(nullptr), nullptr);
    // build: the buffer factory refuses (:86-91).
    const Color32 pixels[1] = {1};
    EXPECT_EQ(real.build(QPoint(0, 1), pixels), nullptr);
    const Bitmap32StaticFactory refusing(std::make_shared<NullBufferFactory>());
    EXPECT_EQ(refusing.build(QPoint(1, 1), pixels), nullptr);
}

// Proof 4.
TEST(Bitmap32StaticFactory, BufferFactoryIsTheOneGiven) {
    const auto given = std::make_shared<Bitmap32BufferFactory>();
    const Bitmap32StaticFactory factory(given);
    EXPECT_EQ(factory.bufferFactory(), given);
}

// Proof 5: the static owns the buffer (TBitmap32Static keeps FBuffer, u_Bitmap32Static.pas:55-66).
TEST(Bitmap32Static, KeepsBufferAliveAfterOtherReferencesAreDropped) {
    Bitmap32StaticPtr bitmap;
    std::weak_ptr<TrackedBuffer> watch;
    {
        const Bitmap32StaticFactory factory(std::make_shared<Bitmap32BufferFactory>());
        auto buffer = std::make_shared<TrackedBuffer>(QPoint(2, 2),
                                                      std::vector<Color32>{0x0A0B0C0DU, 2U, 3U, 0x7A7B7C7DU});
        watch = buffer;
        bitmap = factory.buildWithOwnBuffer(std::move(buffer));
    }
    ASSERT_NE(bitmap, nullptr);
    EXPECT_FALSE(watch.expired());
    EXPECT_EQ(bitmap->data()[0], 0x0A0B0C0DU);
    EXPECT_EQ(bitmap->data()[3], 0x7A7B7C7DU);
    bitmap.reset();
    EXPECT_TRUE(watch.expired());
}
