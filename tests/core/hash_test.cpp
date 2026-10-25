// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT directly (feeds FEAT-RND-063 tile hashing); docs/specs/core.md 2.9, plan 7.9
#include "cg/core/hash.h"

#include <QByteArray>
#include <QUuid>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using cg::core::cityHash64;
using cg::core::cityHash64WithSeed;
using cg::core::crc32;
using cg::core::crc64;
using cg::core::crc64WithSeed;
using cg::core::hashOf;
using cg::core::hashUpdate;
using cg::core::HashValue;
using cg::core::kNoHash;

namespace {

// Upstream city-test.cc `setup()`, truncated to the first 1 KiB. The golden values below were
// generated from the vendored third_party/cityhash build after it passed upstream's own
// 300-case self-test (third_party/cityhash/README.md). Regenerate, never invent.
std::array<char, 1024> upstreamData() {
    constexpr std::uint64_t k0 = 0xc3a5c85c97cb3127ULL;
    std::array<char, 1024> data{};
    std::uint64_t a = 9;
    std::uint64_t b = 777;
    for (std::uint64_t i = 0; i < data.size(); ++i) {
        a += b;
        b += a;
        a = (a ^ (a >> 41U)) * k0;
        b = (b ^ (b >> 41U)) * k0 + i;
        data[i] = static_cast<char>(static_cast<std::uint8_t>(b >> 37U));
    }
    return data;
}

struct LengthAndHash {
    std::size_t length;
    HashValue expected;
};

constexpr std::array<LengthAndHash, 20> cityHash64Vectors = {{
    {0, 0x9ae16a3b2f90404fULL},   {1, 0xf592025a96e41782ULL},    {2, 0x3dd1fc3f768cb689ULL},
    {3, 0x87cbba12f9cfba24ULL},   {7, 0x5d913e0b64412450ULL},    {8, 0xec8e16f499899345ULL},
    {9, 0xbdfb4a93277911f1ULL},   {15, 0x1069c03a33af7ab1ULL},   {16, 0xb16cbeb13d01a441ULL},
    {17, 0xc5aa7ea87bfee0c4ULL},  {31, 0x06e65024d181ada0ULL},   {32, 0xdfd34f271d38b837ULL},
    {33, 0x1b0c9524abb4f39fULL},  {63, 0xc3878e9ea97d4c23ULL},   {64, 0x2c4b8589f2e6f719ULL},
    {65, 0xb75b0f2c52c8896aULL},  {127, 0xf9e32c2ef13b2a80ULL},  {128, 0x280a58e5e0296ea3ULL},
    {129, 0x8c9c117bdcf04da6ULL}, {1024, 0x7287588e42c29655ULL},
}};

constexpr HashValue kUpstreamSeed = 1234567ULL;

constexpr std::array<LengthAndHash, 20> cityHash64SeedVectors = {{
    {0, 0x75106db890237a4aULL},   {1, 0x6454bf2bf3887eadULL},    {2, 0x410b559c4d39f2bfULL},
    {3, 0xfd0a3052b5e20d71ULL},   {7, 0x181dfc3a91dda47cULL},    {8, 0x564c07a451124037ULL},
    {9, 0x2fe1da4ac369a7e7ULL},   {15, 0xd78044b5182a1043ULL},   {16, 0x56993ffee70c0e73ULL},
    {17, 0x3ed47fd5149f765bULL},  {31, 0xcc1578f9dec0d5a4ULL},   {32, 0xd1c77c2c826baccdULL},
    {33, 0xffc889acad65e33dULL},  {63, 0xa91789c279fb3d31ULL},   {64, 0xf67dd1758c76366cULL},
    {65, 0x65fbfd7b7d4030b1ULL},  {127, 0xae31c03f27829724ULL},  {128, 0x6bfc0e770f3d7355ULL},
    {129, 0xa24a87291386431cULL}, {1024, 0xc0d8730a37cba850ULL},
}};

} // namespace

TEST(Hash, CityHash64MatchesUpstreamVectors) {
    const std::array<char, 1024> data = upstreamData();
    ASSERT_EQ(QByteArray(data.data(), 16).toHex(), QByteArray("6ae44a823be7aa6642aab0ce6a47ea25"));
    for (const LengthAndHash& vector : cityHash64Vectors) {
        EXPECT_EQ(cityHash64(data.data(), vector.length), vector.expected) << "length " << vector.length;
    }
}

TEST(Hash, CityHash64WithSeedMatchesUpstreamVectors) {
    const std::array<char, 1024> data = upstreamData();
    for (const LengthAndHash& vector : cityHash64SeedVectors) {
        EXPECT_EQ(cityHash64WithSeed(data.data(), vector.length, kUpstreamSeed), vector.expected)
            << "length " << vector.length;
    }
}

TEST(Hash, CityHash64EmptyBuffer) {
    EXPECT_EQ(cityHash64(nullptr, 0), 0x9ae16a3b2f90404fULL); // upstream k2
    EXPECT_EQ(cityHash64("", 0), 0x9ae16a3b2f90404fULL);
}

// u_HashFunctionCityHash.pas:59-63: CalcHash(nil, n) = 0; an empty Delphi string is nil, so
// CalcHashByString('') = 0 (u_HashFunctionByImpl.pas:288-296). A non-nil empty buffer is hashed.
TEST(Hash, EmptyInputHashesToZeroWhereLegacyPassesNil) {
    EXPECT_EQ(hashOf(QStringView{}), 0U);
    EXPECT_EQ(hashOf(QStringView(u"")), 0U);
    EXPECT_EQ(hashOf(QByteArrayView{}), 0U);
    EXPECT_EQ(hashOf(QByteArrayView("", 0)), 0x9ae16a3b2f90404fULL);
}

TEST(Hash, Crc64JonesKnownVectors) {
    std::array<std::uint8_t, 256> all{};
    for (std::size_t i = 0; i < all.size(); ++i) {
        all[i] = static_cast<std::uint8_t>(i);
    }
    EXPECT_EQ(crc64("", 0), 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(crc64("a", 1), 0xccfd7b88d19ad4faULL);
    EXPECT_EQ(crc64("123456789", 9), 0x66a2364420e6c605ULL);
    EXPECT_EQ(crc64(all.data(), all.size()), 0x8dbeb4d09a24c54fULL);
}

TEST(Hash, Crc64NoFinalXorOut) {
    // u_HashFunctionCRC64.pas:82 seeds with NOT 0 and returns the register as-is.
    EXPECT_EQ(crc64("", 0), ~HashValue{0});
    EXPECT_EQ(crc64WithSeed("abc", 3, 7ULL), 0x7b12f0799ce9ac10ULL);
    EXPECT_EQ(crc64WithSeed("", 0, 12345ULL), 12345ULL);
}

TEST(Hash, Crc32MatchesZlib) {
    EXPECT_EQ(crc32(0, "123456789", 9), 0xCBF43926U);
    EXPECT_EQ(crc32(0, "", 0), 0U);
}

TEST(Hash, Crc32IsIncremental) {
    const QByteArray whole("the quick brown fox");
    const std::uint32_t oneShot = crc32(0, whole.constData(), static_cast<std::size_t>(whole.size()));
    std::uint32_t running = crc32(0, whole.constData(), 4);
    running = crc32(running, whole.constData() + 4, static_cast<std::size_t>(whole.size()) - 4);
    EXPECT_EQ(running, oneShot);
}

TEST(Hash, HashOfStringHashesUtf16CodeUnits) {
    const QByteArray bytes("abc");
    EXPECT_NE(hashOf(QStringView(u"abc")), hashOf(QByteArrayView(bytes)));
    const std::array<char16_t, 3> units = {u'a', u'b', u'c'};
    EXPECT_EQ(hashOf(QStringView(u"abc")), cityHash64(units.data(), units.size() * sizeof(char16_t)));
}

TEST(Hash, HashOfByteArrayEqualsCityHash64) {
    const QByteArray bytes("some tile body");
    EXPECT_EQ(hashOf(QByteArrayView(bytes)),
              cityHash64(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

TEST(Hash, HashOfUuidHashesSixteenRawBytes) {
    const QUuid id = QUuid::fromString(QStringLiteral("{01BDC3A9-D0C0-4B84-BFB5-13AAF9D100F7}"));
    const std::array<std::uint8_t, 16> expected = {0xA9, 0xC3, 0xBD, 0x01, 0xC0, 0xD0, 0x84, 0x4B,
                                                   0xBF, 0xB5, 0x13, 0xAA, 0xF9, 0xD1, 0x00, 0xF7};
    EXPECT_EQ(hashOf(id), cityHash64(expected.data(), expected.size()));
}

TEST(Hash, HashUpdateChainsWithSeed) {
    const QByteArray first("head");
    const QByteArray second("tail");
    HashValue running = hashOf(QByteArrayView(first));
    hashUpdate(running, QByteArrayView(second));
    EXPECT_EQ(running, cityHash64WithSeed(second.constData(), static_cast<std::size_t>(second.size()),
                                          hashOf(QByteArrayView(first))));
}

TEST(Hash, HashUpdateEmptyBufferLeavesRunningUnchanged) {
    HashValue running = 0x0123456789ABCDEFULL;
    hashUpdate(running, QByteArrayView(QByteArray()));
    EXPECT_EQ(running, 0x0123456789ABCDEFULL);
    hashUpdate(running, QStringView());
    EXPECT_EQ(running, 0x0123456789ABCDEFULL);
}

TEST(Hash, HashUpdateByHashUsesEightLittleEndianBytes) {
    HashValue running = 0x1111222233334444ULL;
    const HashValue value = 0x00FF00FF00FF00FFULL;
    hashUpdate(running, value);
    const std::array<std::uint8_t, 8> littleEndian = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    EXPECT_EQ(running, cityHash64WithSeed(littleEndian.data(), littleEndian.size(), 0x1111222233334444ULL));
}

TEST(Hash, KNoHashIsZero) {
    EXPECT_EQ(kNoHash, 0ULL);
    HashValue running = kNoHash;
    hashUpdate(running, 42);
    EXPECT_NE(running, kNoHash);
}
