// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT yet (T-0014 slice B). docs/specs/script.md 2.6 (the Value model).
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
    EXPECT_FALSE(v.toInt().has_value()); // spec 4.3 A1
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
    EXPECT_FALSE(v.toInt().has_value()); // spec 4.3 A3 - StrToInt must be explicit
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
    // ADR-0003 semantic table row 8: `TL := BR` must not alias.
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

// --- T-0014B: declared-type range-wrap/rounding (spec 4.1 N1/N2) -------------------------------

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
    EXPECT_EQ(*Value::fromInt(-1).wrappedTo(DeclaredType::Unknown).toInt(), -1); // N1: today's behaviour
}

TEST(Value, WrappedToDoubleRoundsFromExtendedPrecision) {
    // A long double value whose extra (beyond `double`) mantissa bits are non-zero: narrowing to
    // `double` and back must change the represented value (spec 4.1 N2).
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

// T-0014 rework Major 3: `FloatToStr` (spec 5.4/4.1 N2) is Delphi's `ffGeneral` format, not
// QByteArray::number(double,'g',15) - 15 significant digits, fixed notation switching to
// scientific outside exponent range [-3, 15] (Borland convention), upper-case `E`, no `+`, no
// zero-padded exponent, locale-independent.
//
// T-0014C1 rework (finding 8): the fixed/scientific threshold moved from `e < -1` to `e < -3`
// (Borland convention: `e` is exp10+1). The review measured Delphi's actual `ffGeneral` switch
// point as "Exponent < -3" (0.0001 stays fixed, values below 1e-5 go scientific); the ticket's own
// restatement ("fixed when value >= 0.00001") disagrees with the review for exactly one value,
// 0.00001 itself: e=exp10+1=-4 for 1e-5, and -4 < -3, so the review's rule sends it scientific
// ("1E-5") while the ticket's sentence would keep it fixed ("0.00001"). Per the Tech Lead ruling,
// the review's threshold wins; see docs/handoffs/T-0014C1.md "Rework" for this note restated.
// Checked against every existing row below: none has `e` (Borland exp10+1) equal to -2 or -3 -
// the only two values whose classification flips (old: scientific below e<=-2; new: scientific
// only below e<=-4) - so no existing expectation changes.
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
                                           // finding 8: e=exp10+1=-2, not < -3 -> stays fixed. This
                                           // is the value the review's own bug report used
                                           // (`FloatToStr(0.001)` gave `1E-3` before this fix).
                                           FloatToStrCase{0.001L, "0.001"}));
// The ticket's other requested row, 0.00001 -> "the rule yields", is not a new row: it is exactly
// the existing `1e-5L, "1E-5"` golden above (e=exp10+1=-4, < -3, so the review's threshold - not
// the ticket's own restatement, which disagree here - sends it scientific; see the note above).
// Adding a byte-identical duplicate row would be exactly the redundancy finding 7 asks to remove.
