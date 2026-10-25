// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_AppearanceOfMarkFactory.pas:167-249 (hash recipes).
// Expected values are assembled from byte buffers and the raw CityHash64WithSeed primitive,
// independently of the factory (docs/specs/vector.md §2.3.4).
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_mark_line.h"
#include "cg/vector/appearance_of_mark_point.h"
#include "cg/vector/appearance_of_mark_polygon.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "fake_mark_picture.h"

using namespace cg::vector;
using cg::core::HashValue;

namespace {

// Little-endian byte buffers, fed to the raw primitive as legacy's UpdateHashBy* do.
std::string le(std::uint64_t v, int bytes) {
    std::string s;
    for (int i = 0; i < bytes; ++i)
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    return s;
}
HashValue seeded(const std::string& bytes, HashValue seed) {
    return cg::core::cityHash64WithSeed(bytes.data(), bytes.size(), seed);
}
HashValue step(HashValue seed, std::uint32_t v) {
    return seeded(le(v, 4), seed);
}
HashValue stepHash(HashValue seed, HashValue v) {
    return seeded(le(v, 8), seed);
}

// All bytes of every field of the recipe, hand-chained.
HashValue expectedLine(std::uint32_t color, std::uint32_t width) {
    return step(step(0x443e31d70873bb6bULL, color), width);
}
HashValue expectedPolygonH1(std::uint32_t line, std::uint32_t width) {
    return step(step(0x8184bab36bb79df0ULL, line), width);
}
HashValue expectedPolygonH2(std::uint32_t fill) {
    return step(0x11b87fb1b900cc39ULL, fill);
}
HashValue expectedPointH1(std::uint32_t text, std::uint32_t bg, std::uint32_t font) {
    return step(step(step(0xdf2118b946ed0b43ULL, text), bg), font);
}

TEST(AppearanceOfMarkFactory, LineHash) {
    const AppearanceOfMarkFactory f;
    const auto a = f.createLineAppearance(0xFF112233U, 3);
    EXPECT_EQ(a->hash(), expectedLine(0xFF112233U, 3));
    const auto* line = dynamic_cast<const IAppearanceLine*>(a.get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->lineColor(), 0xFF112233U);
    EXPECT_EQ(line->lineWidth(), 3);
    // negative width is stored and hashed as its two's-complement bytes
    EXPECT_EQ(f.createLineAppearance(1, -1)->hash(), expectedLine(1, 0xFFFFFFFFU));
}

TEST(AppearanceOfMarkFactory, PolygonHashes) {
    const AppearanceOfMarkFactory f;
    const auto a = f.createPolygonAppearance(0xFF010203U, 5, 0x80AABBCCU);
    const HashValue h1 = expectedPolygonH1(0xFF010203U, 5);
    const HashValue h2 = expectedPolygonH2(0x80AABBCCU);
    EXPECT_EQ(a->hash(), stepHash(stepHash(0x501f3e9b18861e44ULL, h1), h2));
    const auto* border = dynamic_cast<const IAppearancePolygonBorder*>(a.get());
    const auto* fill = dynamic_cast<const IAppearancePolygonFill*>(a.get());
    ASSERT_NE(border, nullptr);
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(border->hashBorder(), h1);
    EXPECT_EQ(fill->hashFill(), h2);
    EXPECT_EQ(border->lineWidth(), 5);
    EXPECT_EQ(fill->fillColor(), 0x80AABBCCU);
}

struct PointRow {
    const char* label;
    std::uint32_t text, bg;
    int font, marker;
    QString name;
    bool withPic;
};

TEST(AppearanceOfMarkFactory, PointHashesPictureVersusName) {
    const AppearanceOfMarkFactory f;
    const cg::core::HashValue picHash = 0x0123456789ABCDEFULL;
    const MarkPicturePtr pic =
        std::make_shared<cg::vector::test::FakeMarkPicture>(QStringLiteral("real"), picHash);
    const PointRow rows[] = {
        {"name", 0xFF000001U, 0x80000002U, 12, 32, QStringLiteral("icon.png"), false},
        {"name non-ASCII", 0xFF000001U, 0x80000002U, 12, 32, QStringLiteral("икон"), false},
        {"empty name (no-op)", 0xFF000001U, 0x80000002U, 12, 32, QString(), false},
        {"picture", 0xFF000001U, 0x80000002U, 12, 32, QStringLiteral("ignored"), true},
    };
    for (const auto& r : rows) {
        SCOPED_TRACE(r.label);
        const auto a =
            f.createPointAppearance(r.text, r.bg, r.font, r.name, r.withPic ? pic : nullptr, r.marker);
        const HashValue h1 = expectedPointH1(r.text, r.bg, r.font);
        HashValue h2;
        if (r.withPic) {
            h2 = stepHash(0x7498e432f9619b27ULL, picHash);
        } else {
            h2 = 0x31bfcd0d9f48d1d3ULL;
            if (!r.name.isEmpty()) { // UTF-16 code units, little-endian, no length prefix, no terminator
                std::string bytes;
                for (const QChar c : r.name)
                    bytes += le(c.unicode(), 2);
                h2 = seeded(bytes, h2);
            }
        }
        h2 = step(h2, static_cast<std::uint32_t>(r.marker));
        EXPECT_EQ(a->hash(), stepHash(stepHash(0x40fd28c43506c95dULL, h1), h2));
        const auto* cap = dynamic_cast<const IAppearancePointCaption*>(a.get());
        const auto* icon = dynamic_cast<const IAppearancePointIcon*>(a.get());
        ASSERT_NE(cap, nullptr);
        ASSERT_NE(icon, nullptr);
        EXPECT_EQ(cap->hashCaption(), h1);
        EXPECT_EQ(icon->hashIcon(), h2);
        EXPECT_EQ(icon->picName(), r.withPic ? QStringLiteral("real") : r.name);
    }
}

TEST(AppearanceOfMarkFactory, PictureNameDoesNotEnterTheHash) {
    const AppearanceOfMarkFactory f;
    const MarkPicturePtr pic = std::make_shared<cg::vector::test::FakeMarkPicture>(QStringLiteral("x"), 77);
    EXPECT_EQ(f.createPointAppearance(1, 2, 3, QStringLiteral("a"), pic, 4)->hash(),
              f.createPointAppearance(1, 2, 3, QStringLiteral("b"), pic, 4)->hash());
}

} // namespace
