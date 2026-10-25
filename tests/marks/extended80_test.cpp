// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests of the Delphi Extended (x87 80-bit) codec used by t_GeometryPointSML.pas. All byte vectors are
// literals produced by an independent script (struct, not this code); see docs/handoffs/T-0109.md.
#include "cg/marks/sml/extended80.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace m = cg::marks;

namespace {

QByteArray hex(const char* text) {
    QByteArray b = QByteArray::fromHex(text);
    EXPECT_EQ(b.size(), m::kExtended80Size) << text;
    return b;
}

const std::uint8_t* u8(const QByteArray& b) {
    return reinterpret_cast<const std::uint8_t*>(b.constData());
}

double toDouble(const char* text) {
    const QByteArray b = hex(text);
    return m::extended80ToDouble(u8(b));
}

QByteArray widen(double v) {
    QByteArray b(m::kExtended80Size, '\0');
    m::doubleToExtended80(v, reinterpret_cast<std::uint8_t*>(b.data()));
    return b;
}

std::uint64_t bitsOf(double d) {
    return std::bit_cast<std::uint64_t>(d);
}

} // namespace

// Exact values, both directions: double -> 10 bytes -> the same double bits.
TEST(Extended80, ExactVectorsBothDirections) {
    struct Row {
        const char* name;
        double value;
        const char* bytes;
    };
    const std::vector<Row> rows = {
        {"+0", 0.0, "00000000000000000000"},
        {"-0", -0.0, "00000000000000000080"},
        {"1", 1.0, "0000000000000080ff3f"},
        {"-1", -1.0, "0000000000000080ffbf"},
        {"37.6173", 37.6173, "008048bf7d1d78960440"},
        {"-122.4194", -122.4194, "0080e2c798bbd6f405c0"},
        {"359.99999999", 359.99999999, "007086eaffffffb30740"},
        {"5e-324", 5e-324, "0000000000000080cd3b"},
        {"DBL_MAX", std::numeric_limits<double>::max(), "00f8fffffffffffffe43"},
        {"0.1", 0.1,
         "00d0cccccccccccc"
         "fb3f"},
        {"180", 180.0, "00000000000000b40640"},
        {"+inf", std::numeric_limits<double>::infinity(), "0000000000000080ff7f"},
        {"-inf", -std::numeric_limits<double>::infinity(), "0000000000000080ffff"},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        EXPECT_EQ(widen(r.value), hex(r.bytes));
        const QByteArray b = hex(r.bytes);
        EXPECT_EQ(bitsOf(m::extended80ToDouble(u8(b))), bitsOf(r.value));
    }
}

// The three fixed patterns of the legacy writer (u_GeometryToStreamSML.pas:88-92, :254-255) and what the
// widening of a NaN keeps (sign and payload).
TEST(Extended80, NaNPatternsAndWidening) {
    EXPECT_EQ(QByteArray(reinterpret_cast<const char*>(m::kExtended80NaN), 10), hex("00000000000000c0ffff"));
    EXPECT_EQ(QByteArray(reinterpret_cast<const char*>(m::kExtended80MinusOne), 10),
              hex("0000000000000080ffbf"));
    // Delphi's NaN widened from a double is the negative quiet NaN 0xFFF8...: FF FF C0 00...
    EXPECT_EQ(widen(std::bit_cast<double>(0xFFF8000000000000ULL)), hex("00000000000000c0ffff"));
    // a positive quiet NaN stays positive; a signalling NaN is made quiet
    EXPECT_EQ(widen(std::bit_cast<double>(0x7FF8000000000000ULL)), hex("00000000000000c0ff7f"));
    EXPECT_EQ(widen(std::bit_cast<double>(0x7FF4000000000000ULL)), hex("00000000000000e0ff7f"));
}

// Reading: NaN and unsupported encodings are NaN, infinity is not; a pseudo-denormal is a finite number.
TEST(Extended80, IsNaNClassification) {
    struct Row {
        const char* name;
        const char* bytes;
        bool nan;
    };
    const std::vector<Row> rows = {
        {"default NaN", "00000000000000c0ffff", true},
        {"positive qNaN", "00000000000000c0ff7f", true},
        {"sNaN", "00000000000000a0ff7f", true},
        {"NaN with payload", "0100000000000080ff7f", true},
        {"+inf", "0000000000000080ff7f", false},
        {"-inf", "0000000000000080ffff", false},
        {"pseudo-infinity (integer bit clear)", "0000000000000000ff7f", true},
        {"pseudo-NaN", "0000000000000040ff7f", true},
        {"unnormal", "0000000000000040ff3f", true},
        {"zero with exponent", "0000000000000000ff3f", true},
        {"pseudo-denormal", "00000000000000800000", false},
        {"denormal", "01000000000000000000", false},
        {"+0", "00000000000000000000", false},
        {"1", "0000000000000080ff3f", false},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        const QByteArray b = hex(r.bytes);
        EXPECT_EQ(m::extended80IsNaN(u8(b)), r.nan);
    }
}

// Narrowing: round to nearest even, carry, overflow, subnormals, underflow.
TEST(Extended80, NarrowingRounding) {
    struct Row {
        const char* name;
        const char* bytes;
        std::uint64_t expectBits;
    };
    const std::vector<Row> rows = {
        {"1 + 2^-63 rounds down", "0100000000000080ff3f", 0x3FF0000000000000ULL},
        {"tie, even neighbour stays", "0004000000000080ff3f", 0x3FF0000000000000ULL},
        {"tie, odd neighbour goes up", "000c000000000080ff3f", 0x3FF0000000000002ULL},
        {"above a tie goes up", "0104000000000080ff3f", 0x3FF0000000000001ULL},
        {"carry into the exponent", "ffffffffffffffffff3f", 0x4000000000000000ULL},
        {"2^1024 overflows to +inf", "0000000000000080ff43", 0x7FF0000000000000ULL},
        {"negative overflow", "0000000000000080ffc3", 0xFFF0000000000000ULL},
        {"exact half of the least subnormal ties to 0", "0000000000000080cc3b", 0x0000000000000000ULL},
        {"just above half rounds to the least subnormal", "0100000000000080cc3b", 0x0000000000000001ULL},
        {"one and a half units ties to 2", "00000000000000c0cd3b", 0x0000000000000002ULL},
        {"2^-1074 exactly", "0000000000000080cd3b", 0x0000000000000001ULL},
        {"2^-1022 is the least normal", "0000000000000080013c", 0x0010000000000000ULL},
        {"far below the subnormals", "00000000000000800100", 0x0000000000000000ULL},
        {"negative underflow keeps the sign", "00000000000000800180", 0x8000000000000000ULL},
        {"pseudo-denormal reads as exponent 1 (2^-16382)", "00000000000000800000", 0x0000000000000000ULL},
    };
    for (const Row& r : rows) {
        SCOPED_TRACE(r.name);
        const QByteArray b = hex(r.bytes);
        EXPECT_EQ(bitsOf(m::extended80ToDouble(u8(b))), r.expectBits);
    }
}

// NaN keeps sign and the top payload bits and comes back as a quiet NaN.
TEST(Extended80, NaNNarrowing) {
    EXPECT_EQ(bitsOf(toDouble("00000000000000c0ffff")), 0xFFF8000000000000ULL);
    EXPECT_EQ(bitsOf(toDouble("00000000000000c0ff7f")), 0x7FF8000000000000ULL);
    EXPECT_EQ(bitsOf(toDouble("00000000000000a0ff7f")), 0x7FFC000000000000ULL);
    EXPECT_TRUE(std::isnan(toDouble("0000000000000040ff3f"))); // unnormal -> NaN
}
