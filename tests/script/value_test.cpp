// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/value.h"

#include <gtest/gtest.h>

using namespace cg::script;

TEST(Value, DefaultIsEmpty) {
    Value v;
    EXPECT_EQ(v.kind(), ValueKind::Empty);
    EXPECT_FALSE(v.isTruthy());
    EXPECT_EQ(v.toStr(), QByteArray());
}

TEST(Value, IntRoundTrips) {
    Value v = Value::fromInt(42);
    ASSERT_EQ(v.kind(), ValueKind::Int);
    EXPECT_EQ(*v.toInt(), 42);
    EXPECT_TRUE(v.isTruthy());
    EXPECT_EQ(v.toStr(), QByteArrayLiteral("42"));
}

TEST(Value, IntWidensToReal) {
    Value v = Value::fromInt(7);
    ASSERT_TRUE(v.toReal().has_value());
    EXPECT_DOUBLE_EQ(static_cast<double>(*v.toReal()), 7.0);
}

TEST(Value, RealDoesNotImplicitlyConvertToInt) {
    Value v = Value::fromReal(1.5L);
    EXPECT_FALSE(v.toInt().has_value());
}

TEST(Value, BoolConvertsToOrdinalInt) {
    EXPECT_EQ(*Value::fromBool(true).toInt(), 1);
    EXPECT_EQ(*Value::fromBool(false).toInt(), 0);
}

TEST(Value, EmptyActsAsZeroForConversions) {
    Value v;
    EXPECT_EQ(*v.toInt(), 0);
    EXPECT_DOUBLE_EQ(static_cast<double>(*v.toReal()), 0.0);
    EXPECT_EQ(*v.toBool(), false);
    EXPECT_EQ(v.toStr(), QByteArray());
}

TEST(Value, StrDoesNotImplicitlyConvertToInt) {
    Value v = Value::fromStr(QByteArrayLiteral("5"));
    EXPECT_FALSE(v.toInt().has_value()); // StrToInt must be explicit
}

TEST(Value, StrRoundTrips) {
    Value v = Value::fromStr(QByteArrayLiteral("hello"));
    ASSERT_EQ(v.kind(), ValueKind::Str);
    EXPECT_EQ(v.toStr(), QByteArrayLiteral("hello"));
}

TEST(Value, BoolToStrIsUppercase) {
    EXPECT_EQ(Value::fromBool(true).toStr(), QByteArrayLiteral("TRUE"));
    EXPECT_EQ(Value::fromBool(false).toStr(), QByteArrayLiteral("FALSE"));
}

TEST(Value, IsTruthyOnlyForBoolAndNonzeroInt) {
    EXPECT_TRUE(Value::fromBool(true).isTruthy());
    EXPECT_FALSE(Value::fromBool(false).isTruthy());
    EXPECT_TRUE(Value::fromInt(3).isTruthy());
    EXPECT_FALSE(Value::fromInt(0).isTruthy());
    EXPECT_FALSE(Value::fromStr(QByteArrayLiteral("x")).isTruthy());
}

TEST(Value, RecordFieldsAreValueCopiedOnWrite) {
    // `TL := BR` must not alias.
    Value a = Value::fromRecord({Value::fromReal(1.0L), Value::fromReal(2.0L)});
    Value b = a; // shares the COW buffer until a write happens
    b.fields()[0] = Value::fromReal(99.0L);
    EXPECT_DOUBLE_EQ(static_cast<double>(*a.fields()[0].toReal()), 1.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(*b.fields()[0].toReal()), 99.0);
}

TEST(Value, ArrayCarriesLowBound) {
    Value v = Value::fromArray({Value::fromInt(1), Value::fromInt(2)}, 5);
    EXPECT_EQ(v.lowBound(), 5);
    EXPECT_EQ(v.fields().size(), 2u);
}

TEST(Value, InterfaceAssignedTracksNullness) {
    Value nullRef = Value::fromInterface(nullptr, InterfaceId::Converter);
    EXPECT_FALSE(nullRef.isAssigned());
    Value liveRef = Value::fromInterface(std::make_shared<int>(1), InterfaceId::Converter);
    EXPECT_TRUE(liveRef.isAssigned());
}

TEST(Value, EmptyIsNeverAssigned) {
    EXPECT_FALSE(Value().isAssigned());
}

// --- Declared-type range-wrap/rounding -------------------------------

TEST(Value, WrappedToByteWrapsMod256) {
    EXPECT_EQ(*Value::fromInt(265).wrappedTo(DeclaredType::Byte).toInt(), 9);   // overflow
    EXPECT_EQ(*Value::fromInt(-1).wrappedTo(DeclaredType::Byte).toInt(), 255);  // underflow
    EXPECT_EQ(*Value::fromInt(255).wrappedTo(DeclaredType::Byte).toInt(), 255); // in range
}

TEST(Value, WrappedToWordWrapsMod65536) {
    EXPECT_EQ(*Value::fromInt(65537).wrappedTo(DeclaredType::Word).toInt(), 1);
    EXPECT_EQ(*Value::fromInt(-1).wrappedTo(DeclaredType::Word).toInt(), 65535);
}

TEST(Value, WrappedToCardinalWrapsMod2To32) {
    EXPECT_EQ(*Value::fromInt(-1).wrappedTo(DeclaredType::Cardinal).toInt(), 4294967295LL);
    EXPECT_EQ(*Value::fromInt(qint64{1} << 32).wrappedTo(DeclaredType::Cardinal).toInt(), 0);
}

TEST(Value, WrappedToIntegerWrapsSigned32Bit) {
    EXPECT_EQ(*Value::fromInt(qint64{1} << 31).wrappedTo(DeclaredType::Integer).toInt(),
              -(qint64{1} << 31)); // 2^31 wraps to INT32_MIN
    EXPECT_EQ(*Value::fromInt(2147483647).wrappedTo(DeclaredType::Integer).toInt(), 2147483647);
    EXPECT_EQ(*Value::fromInt(2147483648LL).wrappedTo(DeclaredType::Integer).toInt(), -2147483648LL);
}

TEST(Value, WrappedToShortIntAndSmallIntSignedWrap) {
    EXPECT_EQ(*Value::fromInt(200).wrappedTo(DeclaredType::ShortInt).toInt(), -56); // 200 - 256
    EXPECT_EQ(*Value::fromInt(-129).wrappedTo(DeclaredType::ShortInt).toInt(), 127);
    EXPECT_EQ(*Value::fromInt(40000).wrappedTo(DeclaredType::SmallInt).toInt(), -25536); // 40000 - 65536
}

TEST(Value, WrappedToInt64AndUnknownNeverWrap) {
    EXPECT_EQ(*Value::fromInt(-1).wrappedTo(DeclaredType::Int64).toInt(), -1);
    EXPECT_EQ(*Value::fromInt(-1).wrappedTo(DeclaredType::Unknown).toInt(), -1); // Today's behaviour
}

TEST(Value, WrappedToDoubleRoundsFromExtendedPrecision) {
    if (sizeof(long double) == sizeof(double)) {
        GTEST_SKIP() << "long double is a plain double here (arm64): no extended precision to test";
    }
    // A long double value whose extra (beyond `double`) mantissa bits are non-zero: narrowing to
    // `double` and back must change the represented value.
    const long double extended = 1.0L / 3.0L;
    const Value wrapped = Value::fromReal(extended).wrappedTo(DeclaredType::Double);
    ASSERT_EQ(wrapped.kind(), ValueKind::Real);
    EXPECT_EQ(*wrapped.toReal(), static_cast<long double>(static_cast<double>(extended)));
    EXPECT_NE(*wrapped.toReal(), extended); // precision was actually dropped
}

TEST(Value, WrappedToSingleRoundsToFloatPrecision) {
    const long double extended = 1.0L / 3.0L;
    const Value wrapped = Value::fromReal(extended).wrappedTo(DeclaredType::Single);
    ASSERT_EQ(wrapped.kind(), ValueKind::Real);
    EXPECT_EQ(*wrapped.toReal(), static_cast<long double>(static_cast<float>(extended)));
    EXPECT_NE(*wrapped.toReal(), extended);
}

TEST(Value, WrappedToExtendedAndUnknownKeepFullPrecision) {
    const long double extended = 1.0L / 3.0L;
    EXPECT_EQ(*Value::fromReal(extended).wrappedTo(DeclaredType::Extended).toReal(), extended);
    EXPECT_EQ(*Value::fromReal(extended).wrappedTo(DeclaredType::Unknown).toReal(), extended);
}

TEST(Value, WrappedToIsANoOpForNonNumericKinds) {
    Value b = Value::fromBool(true);
    EXPECT_EQ(b.wrappedTo(DeclaredType::Byte).kind(), ValueKind::Bool);
    Value s = Value::fromStr(QByteArrayLiteral("x"));
    EXPECT_EQ(s.wrappedTo(DeclaredType::Byte).toStr(), QByteArrayLiteral("x"));
}

// `FloatToStr` is Delphi's `ffGeneral` format, not QByteArray::number(double,'g',15) - 15
// significant digits, fixed notation switching to scientific outside exponent range [-3, 15]
// (Borland convention), upper-case `E`, no `+`, no zero-padded exponent, locale-independent.
//
// Delphi's `ffGeneral` goes scientific when the Borland exponent `e` (exp10+1) is below -3: 0.0001
// stays fixed, 1e-5 gives "1E-5". An earlier threshold of `e < -1` was wrong. No row below has `e`
// equal to -2 or -3, the only two values whose classification differs between the two thresholds,
// so no existing expectation changes.
struct FloatToStrCase {
    long double input;
    const char* expected;
};

class Value_FloatToStr : public ::testing::TestWithParam<FloatToStrCase> {};

TEST_P(Value_FloatToStr, MatchesDelphiGeneralFormat) {
    EXPECT_EQ(Value::fromReal(GetParam().input).toStr(), QByteArray(GetParam().expected));
}

INSTANTIATE_TEST_SUITE_P(Goldens, Value_FloatToStr,
                         ::testing::Values(FloatToStrCase{0.0L, "0"}, FloatToStrCase{-0.5L, "-0.5"},
                                           FloatToStrCase{1e15L, "1E15"}, FloatToStrCase{1e16L, "1E16"},
                                           FloatToStrCase{1e-5L, "1E-5"}, FloatToStrCase{1e25L, "1E25"},
                                           FloatToStrCase{0.1L + 0.2L, "0.3"},
                                           FloatToStrCase{123456789.125L, "123456789.125"},
                                           FloatToStrCase{100.0L, "100"}, FloatToStrCase{0.01L, "0.01"},
                                           FloatToStrCase{-123.0L, "-123"},
                                           // e=exp10+1=-2, not < -3 -> stays fixed
                                           // (`FloatToStr(0.001)` gave `1E-3` under the earlier
                                           // threshold).
                                           FloatToStrCase{0.001L, "0.001"}));
// 1e-5 is not a new row: it is the existing `1e-5L, "1E-5"` golden above (e=exp10+1=-4, < -3, so
// it goes scientific).
