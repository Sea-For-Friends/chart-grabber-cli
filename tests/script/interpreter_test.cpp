// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-003 (the run half, now with the host binding). docs/specs/script.md 2.11,
// 2.7, 4, 5.1, 5.3, 7, 8. Acceptance item #0's parser tests live in parser_test.cpp; this file is
// the "value model, VM/global scopes, expression + statement evaluation, built-ins" half of
// T-0014/T-0014B/T-0015A.
#include "cg/script/compiled_script.h"
#include "cg/script/interpreter.h"
#include "cg/script/parser.h" // SymbolTable::kBoundVariableCount

#include <QDateTime>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <time.h> // tzset
#include <utility>

#include "script_host_fake.h"

using namespace cg::script;
using cg::script::test::FakeCoordConverter;
using cg::script::test::FakeHttpDownloader;
using cg::script::test::FakeProjConverter;
using cg::script::test::FakeProjConverterFactory;
using cg::script::test::FakeScriptGlobal;
using cg::script::test::FakeScriptHost;
using cg::script::test::FakeScriptLogger;
using cg::script::test::FakeScriptTileCache;

namespace {

// First user-declared slot in a script with no routines before its `var` section: bound variables
// always occupy [0, kBoundVariableCount) - see SymbolTable's constructor (parser.cpp).
constexpr int kV0 = SymbolTable::kBoundVariableCount;
constexpr int kV1 = kV0 + 1;
constexpr int kV2 = kV0 + 2;

struct Outcome {
    std::shared_ptr<const CompiledScript> compiled;
    std::unique_ptr<Interpreter> interpreter;
    std::unique_ptr<FakeScriptHost> host;
    ScriptVars vars;
    cg::core::Result<void> runResult;
};

// `vars` is passed by value (the caller's copy is never touched - `run(IScriptHost&, ScriptVars&)`
// only ever mutates the Outcome's own copy) and `host` defaults to a freshly-seeded FakeScriptHost
// so the ~90 pre-T-0015A call sites (`run(source)`) are unaffected by the host binding.
Outcome run(const QByteArray& source, ScriptVars vars = {}, std::unique_ptr<FakeScriptHost> host = nullptr) {
    Outcome outcome;
    outcome.vars = std::move(vars);
    outcome.host = host ? std::move(host) : std::make_unique<FakeScriptHost>();
    auto compiled = CompiledScript::compile(source, QStringLiteral("test"));
    if (!compiled.has_value()) {
        outcome.runResult = cg::core::fail(compiled.error().code(), compiled.error().what());
        return outcome;
    }
    outcome.compiled = *compiled;
    outcome.interpreter = std::make_unique<Interpreter>(outcome.compiled);
    outcome.runResult = outcome.interpreter->run(*outcome.host, outcome.vars);
    return outcome;
}

::testing::AssertionResult ranOk(const Outcome& outcome) {
    if (outcome.runResult.has_value()) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << outcome.runResult.error().what().toStdString();
}

} // namespace

#define ASSERT_RAN_OK(outcome) ASSERT_TRUE(ranOk(outcome))

// --- BuiltinId / externalSymbols pairing (ruling 2026-09-18) --------------------------------------

TEST(Interpreter, BuiltinIdMatchesExternalSymbolOrder) {
    EXPECT_TRUE(builtinIdsMatchExternalSymbolOrder());
}

// --- expressions: numeric model (spec 4.1) --------------------------------------------------------

TEST(Interpreter, MulBindsTighterThanAddAtRuntime) {
    Outcome o = run("var X: Integer;\nbegin X := 2 * 3 + 4; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 10);
}

TEST(Interpreter, DivAndModTruncateTowardZero) {
    Outcome o = run("var A, B: Integer;\nbegin A := (-7) div 2; B := (-7) mod 2; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), -3);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), -1);
}

TEST(Interpreter, DivisionByZeroIsARuntimeError) {
    Outcome o = run("var A: Integer;\nbegin A := 1 div 0; end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_TRUE(o.runResult.error().what().contains(QStringLiteral("Division by zero")));
}

// T-0031, spec N5 corrected: `4 / 2` is Int/Int, so the `/` itself yields Int(2) (PD-4 - see the
// DivisionAndRealSlots table below for the direct case). This still ends up Real(2.0) here only
// because PD-2 makes assignment into a `Double`-declared slot widen an Int - two rules composing,
// not N5's old "/ always yields a real". Was named RealDivisionAlwaysYieldsReal.
TEST(Interpreter, IntDivisionResultWidensWhenAssignedToARealSlot) {
    Outcome o = run("var A: Double;\nbegin A := 4 / 2; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).kind(), ValueKind::Real);
    EXPECT_DOUBLE_EQ(static_cast<double>(*o.interpreter->slot(kV0).toReal()), 2.0);
}

// --- T-0031: PD-2 (Int -> real slot widening), PD-3/PD-4 (`div`/`/` semantics), PD-104 (32-bit
// integer intermediates) - docs/backlog/T-0031-script-real-typed-slots-and-division-semantics.md.
// Expected values are derived from the engine (sasgis/pascalscript, commit 81202d0), not the port:
//  - PD-2: uPSRuntime.pas's assignment/parameter-bind/Result-store path converts an Int into a
//    real-declared slot (spec 4.3 A1); a real scalar slot zero-inits to 0.0 (spec 4.4 I1).
//  - PD-3/PD-4: uPSCompiler.pas's GetResultType (~8597-8645), no PS_DELPHIDIV: int op int -> the
//    left operand's integer type (truncating division); int/real mixed -> real division. `/` and
//    `div` compile to the SAME opcode (codegen ~6248) and are semantically identical.
//  - PD-104: GetResultType's "Result := t1" rule for +/-/*/div/mod/shl/shr and unary '-' makes an
//    entire left-associative arithmetic chain compute (and wrap) in its LEFTMOST leaf's static
//    type - S32 for an Integer-typed literal/slot (`{$R-}{$Q-}`, overflow wraps), S64 only when
//    that leftmost operand is declared Int64.
struct DivAndRealSlotCase {
    const char* name;
    const char* source;
    int slotOffset; // added to kV0 - the slot holding the value under test
    ValueKind expectedKind;
    const char* expectedStr; // Value::toStr() - IntToStr/FloatToStr form, exact
    // T-0031 rework (ruling 3): the two vectors that are a runtime error instead of a value -
    // expectedKind/expectedStr/slotOffset are unused when this is set.
    bool expectError = false;
    const char* expectedErrorSubstr = nullptr;
};

class Interpreter_DivAndRealSlots : public ::testing::TestWithParam<DivAndRealSlotCase> {};

TEST_P(Interpreter_DivAndRealSlots, MatchesTheEngine) {
    const auto& c = GetParam();
    Outcome o = run(c.source);
    if (c.expectError) {
        ASSERT_FALSE(o.runResult.has_value());
        EXPECT_TRUE(o.runResult.error().what().contains(QLatin1String(c.expectedErrorSubstr)))
            << o.runResult.error().what().toStdString();
        return;
    }
    ASSERT_RAN_OK(o);
    const Value& v = o.interpreter->slot(kV0 + c.slotOffset);
    EXPECT_EQ(v.kind(), c.expectedKind);
    EXPECT_EQ(v.toStr(), QByteArray(c.expectedStr));
}

std::string divAndRealSlotCaseName(const ::testing::TestParamInfo<DivAndRealSlotCase>& info) {
    return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    T0031, Interpreter_DivAndRealSlots,
    ::testing::Values(
        // PD-2: Int -> Extended by assignment/parameter/Result.
        DivAndRealSlotCase{"IntAssignedToExtendedSlotWidens", "var R: Extended;\nbegin R := 5; end.", 0,
                           ValueKind::Real, "5"},
        DivAndRealSlotCase{"IntPassedToExtendedParamWidens",
                           "function F(X: Extended): Extended;\nbegin Result := X * 2; end;\n"
                           "var R: Extended;\nbegin R := F(3); end.",
                           3, ValueKind::Real, "6"},
        DivAndRealSlotCase{"IntReturnedAsExtendedResultWidens",
                           "function G: Extended;\nbegin Result := 7; end;\n"
                           "var R: Extended;\nbegin R := G; end.",
                           2, ValueKind::Real, "7"},
        // PD-2/I1: a real scalar slot zero-inits to 0.0, not Empty.
        DivAndRealSlotCase{"RealSlotZeroInitialisesToZeroPointZero", "var Z: Double;\nbegin end.", 0,
                           ValueKind::Real, "0"},
        // PD-3: `div` with a real operand is REAL division.
        DivAndRealSlotCase{"DivWithRealRightOperandIsRealDivision",
                           "var R: Extended;\nbegin R := 7 div 2.0; end.", 0, ValueKind::Real, "3.5"},
        DivAndRealSlotCase{"DivWithRealLeftOperandIsRealDivision",
                           "var R: Extended;\nbegin R := 7.5 div 2; end.", 0, ValueKind::Real, "3.75"},
        // PD-4: `/` between two Integers is INTEGER division; `/` with a real operand stays real.
        DivAndRealSlotCase{"SlashBetweenIntegersIsIntegerDivision", "var R: Integer;\nbegin R := 7 / 2; end.",
                           0, ValueKind::Int, "3"},
        DivAndRealSlotCase{"SlashWithRealOperandIsRealDivision", "var R: Extended;\nbegin R := 7.0 / 2; end.",
                           0, ValueKind::Real, "3.5"},
        // PD-104: Integer-literal arithmetic wraps at 32 bits (2000000000+2000000000 = 4000000000,
        // which as signed S32 is 4000000000 - 2^32 = -294967296); the SAME expression on Int64
        // operands does not wrap. `R` is declared Int64 (T-0031 rework, review finding 6): both
        // operands are Integer literals, so the wrap happens in the `+` itself, before the
        // assignment - declaring R Integer would make N1's slot-wrap alone produce the same
        // -294967296 even without PD-104, so this row would pass either way. Int64 pins the
        // INTERMEDIATE instead.
        DivAndRealSlotCase{"IntegerArithmeticWrapsAt32Bits",
                           "var R: Int64;\nbegin R := 2000000000 + 2000000000; end.", 0, ValueKind::Int,
                           "-294967296"},
        DivAndRealSlotCase{"Int64ArithmeticDoesNotWrap",
                           "var A, B, R: Int64;\nbegin A := 2000000000; B := 2000000000; R := A + B; end.", 2,
                           ValueKind::Int, "4000000000"},
        // T-0031 rework (review findings 1/2, ruling 1): the width rule is the left operand's
        // EXACT static type, not just "32 or 64 bits" - Cardinal and a literal in 2^31..2^32-1 are
        // UNSIGNED 32-bit, and Byte/Word/ShortInt/SmallInt compute at 8/16 bits.
        DivAndRealSlotCase{"CardinalArithmeticIsUnsigned32Bit",
                           "var C: Cardinal; R: Int64;\nbegin C := 4000000000; R := C + 0; end.", 1,
                           ValueKind::Int, "4000000000"},
        DivAndRealSlotCase{"LargeLiteralArithmeticIsUnsigned32Bit",
                           "var R: Int64;\nbegin R := 3000000000 + 0; end.", 0, ValueKind::Int, "3000000000"},
        DivAndRealSlotCase{"ByteArithmeticComputesAt8Bits",
                           "var B: Byte; R: Integer;\nbegin B := 200; R := B + 100; end.", 1, ValueKind::Int,
                           "44"},
        // T-0031 rework (review finding 3, ruling 2): unary '-' as the direct assignment RHS
        // computes in the DESTINATION's width, not the operand's - I (Integer) holds Low(Integer);
        // negating it directly into R64 (Int64) widens I first, then negates at 64 bits.
        DivAndRealSlotCase{"UnaryMinusOfLowIntegerWidensIntoAWiderDestination",
                           "var I: Integer; R64: Int64;\nbegin I := -2147483648; R64 := -I; end.", 1,
                           ValueKind::Int, "2147483648"},
        // T-0031 rework (review finding 7, ruling 3): the two pre-existing error-text/class gaps.
        DivAndRealSlotCase{"RealDivisionByZeroReportsTheEngineText",
                           "var R: Extended;\nbegin R := 5.0 / 0; end.", 0, ValueKind::Real, "",
                           /*expectError=*/true, "Floating point division by zero"},
        DivAndRealSlotCase{"LowIntegerDivMinusOneIsAnIntegerOverflow",
                           "var I, R: Integer;\nbegin I := -2147483648; R := I div -1; end.", 0,
                           ValueKind::Int, "", /*expectError=*/true, "Integer overflow"}),
    divAndRealSlotCaseName);

TEST(Interpreter, AndOrXorAreBitwiseOnIntegers) {
    Outcome o = run("var A, B, C: Integer;\nbegin A := 6 and 3; B := 6 or 1; C := 6 xor 3; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 2);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 7);
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), 5);
}

TEST(Interpreter, AndOrAreLogicalOnBooleans) {
    Outcome o = run("var A, B: Boolean;\nbegin A := True and False; B := True or False; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toBool(), false);
    EXPECT_EQ(*o.interpreter->slot(kV1).toBool(), true);
}

TEST(Interpreter, MixedIntRealComparisonPromotesToReal) {
    Outcome o = run("var A: Boolean;\nbegin A := 1 + 2 > 2.5; end.");
    ASSERT_RAN_OK(o);
    EXPECT_TRUE(*o.interpreter->slot(kV0).toBool());
}

TEST(Interpreter, ShrIsLogicalOnUnsignedInterpretation) {
    Outcome o = run("var A: Integer;\nbegin A := (-1) shr 60; end."); // N8
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 15);
}

// --- expressions: string model (spec 4.2) ---------------------------------------------------------

TEST(Interpreter, StringIndexingIsOneBased) {
    Outcome o = run("var S: string; A: string;\nbegin S := 'Galileo'; A := S[1]; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("G"));
}

TEST(Interpreter, StringIndexPastEndWithinWindowReturnsNul) {
    // T3: RoscosmGeo.zmp reads one byte past Length() on its last loop iteration.
    Outcome o = run("var S: string; A: string;\nbegin S := 'ab'; A := S[3]; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArray(1, '\0'));
}

TEST(Interpreter, StringIndexFarPastEndIsARuntimeError) {
    Outcome o = run("var S: string; A: string;\nbegin S := 'ab'; A := S[50]; end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::IoError);
}

TEST(Interpreter, IndexedAssignmentWritesOneByteInPlace) {
    Outcome o = run("var S: string;\nbegin S := 'cat'; S[1] := 'b'; end."); // T4
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("bat"));
}

TEST(Interpreter, IndexedAssignmentOutOfRangeIsARuntimeError) {
    Outcome o = run("var S: string;\nbegin S := 'ab'; S[5] := 'x'; end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::IoError);
}

TEST(Interpreter, StringConcatenationWithAdd) {
    Outcome o = run("var A: string;\nbegin A := 'foo' + 'bar'; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("foobar"));
}

TEST(Interpreter, UninitialisedStringIsEmpty) {
    Outcome o = run("var S: string; A: Boolean;\nbegin A := (S = ''); end.");
    ASSERT_RAN_OK(o);
    EXPECT_TRUE(*o.interpreter->slot(kV1).toBool());
}

// --- statements ------------------------------------------------------------------------------------

TEST(Interpreter, IfThenElse) {
    Outcome o = run("var A: Integer;\nbegin if 1 > 2 then A := 1 else A := 2; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 2);
}

TEST(Interpreter, CaseRangeAndElse) {
    Outcome o = run("var A, B: Integer;\n"
                    "begin case 5 of 1..3: A := 1; 4..6: A := 2; else A := 3; end;\n"
                    "case 99 of 1..3: B := 1; else B := 9; end; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 2);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 9);
}

TEST(Interpreter, ForToAccumulates) {
    Outcome o = run("var I, Sum: Integer;\nbegin Sum := 0; for I := 1 to 5 do Sum := Sum + I; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 15);
}

TEST(Interpreter, ForDowntoRuns) {
    Outcome o = run("var I, Sum: Integer;\nbegin Sum := 0; for I := 3 downto 1 do Sum := Sum + I; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 6);
}

TEST(Interpreter, WhileLoop) {
    Outcome o = run("var I: Integer;\nbegin I := 0; while I < 5 do I := I + 1; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 5);
}

TEST(Interpreter, RepeatRunsAtLeastOnce) {
    Outcome o = run("var I: Integer;\nbegin I := 10; repeat I := I + 1; until I >= 5; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 11);
}

TEST(Interpreter, BreakStopsTheLoop) {
    Outcome o = run("var I: Integer;\nbegin for I := 1 to 10 do begin if I = 3 then break; end; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 3);
}

TEST(Interpreter, ContinueSkipsToNextIteration) {
    Outcome o =
        run("var I, Sum: Integer;\n"
            "begin Sum := 0; for I := 1 to 5 do begin if I = 3 then continue; Sum := Sum + I; end; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 12); // 1+2+4+5
}

TEST(Interpreter, TryFinallyAlwaysRuns) {
    Outcome o = run("var A: Integer;\nbegin A := 0; try A := 1; finally A := A + 10; end; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 11);
}

TEST(Interpreter, TryFinallyRunsThenReRaisesOnError) {
    // The failed `A := 1 div 0` never writes A (the assignment's RHS errors before the store), so
    // A is still 0 when `finally` runs: A becomes 10, then the division-by-zero error re-raises.
    Outcome o = run("var A: Integer;\nbegin A := 0; try A := 1 div 0; finally A := A + 10; end; end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.interpreter->slot(kV0).toInt().value_or(-1), 10); // finally ran before the re-raise
}

TEST(Interpreter, TryExceptSwallowsTheError) {
    Outcome o = run("var A: Integer;\nbegin A := 0; try A := 1 div 0; except A := 99; end; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 99);
}

// --- routines (spec 4.4 I2/I4) ----------------------------------------------------------------------

// Slot arithmetic for these: a routine's own name occupies a slot too (SymbolTable::declareRoutine
// precedes its params/Result/locals), so a `var` section declared AFTER a routine starts at
// kV0 + (that routine's own slot + params + Result/locals count), not kV0.

TEST(Interpreter, FunctionResultAndCall) {
    // Square=kV0, N=kV0+1, Result=kV0+2, A=kV0+3.
    Outcome o = run("function Square(N: Integer): Integer;\nbegin Result := N * N; end;\n"
                    "var A: Integer;\nbegin A := Square(7); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 3).toInt(), 49);
}

TEST(Interpreter, RecursionGetsAFreshFrameEachCall) {
    // _broken_maps/Local/MAmet.zmp-style recursion (spec notes). Fact=kV0, N=kV0+1, Result=kV0+2,
    // A=kV0+3.
    Outcome o = run("function Fact(N: Integer): Integer;\n"
                    "begin if N <= 1 then Result := 1 else Result := N * Fact(N - 1); end;\n"
                    "var A: Integer;\nbegin A := Fact(5); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 3).toInt(), 120);
}

TEST(Interpreter, VarParameterWritesBackToCaller) {
    // AddOne=kV0, X=kV0+1 (a procedure has no Result slot), A=kV0+2.
    Outcome o = run("procedure AddOne(var X: Integer);\nbegin X := X + 1; end;\n"
                    "var A: Integer;\nbegin A := 5; AddOne(A); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 2).toInt(), 6);
}

TEST(Interpreter, OutParameterIsZeroInitialisedThenWrittenBack) {
    // Give=kV0, X=kV0+1, A=kV0+2.
    Outcome o = run("procedure Give(out X: Integer);\nbegin X := 42; end;\n"
                    "var A: Integer;\nbegin A := 7; Give(A); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 2).toInt(), 42);
}

TEST(Interpreter, RoutineLocalsAreZeroInitialised) {
    // I1: MAmet.zmp reads `r` before assigning it. ReadBeforeSet=kV0 (no params), Result=kV0+1,
    // R=kV0+2 (declared in the function's own `var` section, after Result), A=kV0+3.
    Outcome o = run("function ReadBeforeSet: Integer;\nvar R: Integer;\nbegin Result := R; end;\n"
                    "var A: Integer;\nbegin A := ReadBeforeSet; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 3).toInt(), 0);
}

// --- built-ins (a representative slice, not the full 57 - see docs/handoffs/T-0014.md) ------------

TEST(Interpreter, RoundIsBankersRounding) {
    Outcome o = run("var A, B, C, D: Integer;\n"
                    "begin A := Round(0.5); B := Round(1.5); C := Round(2.5); D := Round(-0.5); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 0);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 2);
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), 2);
}

TEST(Interpreter, TruncIntFrac) {
    Outcome o =
        run("var A: Integer; B, C: Double;\nbegin A := Trunc(3.7); B := Int(3.7); C := Frac(3.7); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 3);
    EXPECT_NEAR(static_cast<double>(*o.interpreter->slot(kV1).toReal()), 3.0, 1e-9);
    EXPECT_NEAR(static_cast<double>(*o.interpreter->slot(kV2).toReal()), 0.7, 1e-9);
}

TEST(Interpreter, IntPowerIsOneForAnyBaseAtZero) {
    Outcome o = run("var A: Double;\nbegin A := IntPower(0, 0); end."); // spec F4
    ASSERT_RAN_OK(o);
    EXPECT_DOUBLE_EQ(static_cast<double>(*o.interpreter->slot(kV0).toReal()), 1.0);
}

TEST(Interpreter, CopyPosAndLength) {
    Outcome o = run("var S: string; A: string; P, L: Integer;\n"
                    "begin S := 'Galileo'; A := Copy(S, 1, 0); P := Pos('leo', S); L := Length(S); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArray());
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), 5);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 3).toInt(), 7);
}

TEST(Interpreter, IntToStrAndStrToIntDef) {
    Outcome o =
        run("var A: string; B, C: Integer;\n"
            "begin A := IntToStr(42); B := StrToIntDef('nope', -1); C := StrToIntDef('123', -1); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("42"));
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), -1);
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), 123);
}

TEST(Interpreter, StrToIntRejectsTrailingGarbage) {
    Outcome o = run("var A: Integer;\nbegin A := StrToInt('12x'); end.");
    ASSERT_FALSE(o.runResult.has_value());
}

TEST(Interpreter, UppercaseLowercaseTrim) {
    Outcome o = run(
        "var A, B, C: string;\nbegin A := Uppercase('AbC'); B := Lowercase('AbC'); C := Trim('  x  '); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("ABC"));
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("abc"));
    EXPECT_EQ(o.interpreter->slot(kV2).toStr(), QByteArrayLiteral("x"));
}

TEST(Interpreter, IncAndDec) {
    Outcome o = run("var A, B: Integer;\nbegin A := 5; Inc(A); B := 5; Dec(B, 2); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 6);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 3);
}

TEST(Interpreter, InsertAndDeleteMutateInPlace) {
    Outcome o = run("var S: string;\nbegin S := 'helloworld'; Insert(' ', S, 6); Delete(S, 1, 6); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("world"));
}

TEST(Interpreter, GetAfterGetBeforeGetBetween) {
    Outcome o = run("var A, B, C: string;\n"
                    "begin A := GetAfter('=', 'k=v'); B := GetBefore('=', 'k=v'); "
                    "C := GetBetween('[k]', '[', ']'); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("v"));
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("k"));
    EXPECT_EQ(o.interpreter->slot(kV2).toStr(), QByteArrayLiteral("k"));
}

TEST(Interpreter, GetDiv3PathGroupsFromTheRight) {
    Outcome o = run("var A: string;\nbegin A := GetDiv3Path('1234567'); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("234\\567\\"));
}

TEST(Interpreter, StringReplaceDefaultFlagsReplacesFirstOnly) {
    Outcome o = run("var A, B: string;\n"
                    "begin A := StringReplace('aaa', 'a', 'b', []); "
                    "B := StringReplace('aaa', 'a', 'b', [rfReplaceAll]); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("baa"));
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("bbb"));
}

TEST(Interpreter, PiIsAvailable) {
    Outcome o = run("var A: Double;\nbegin A := Pi; end.");
    ASSERT_RAN_OK(o);
    EXPECT_NEAR(static_cast<double>(*o.interpreter->slot(kV0).toReal()), 3.14159265358979, 1e-12);
}

// --- deliberately unsupported this slice (fails loudly, never silently) ---------------------------

TEST(Interpreter, BoundVariableReadReachesScriptVars) {
    // T-0015A: GetX now flows straight from the caller's ScriptVars (spec 5.1 #16) - see
    // AllBoundVariablesRoundTrip below for the full 26-variable table.
    ScriptVars vars;
    vars.getX = 42;
    Outcome o = run("var A: Integer;\nbegin A := GetX; end.", vars);
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 42);
}

TEST(Interpreter, InterfaceMethodCallOnNilInterfaceIsARuntimeError) {
    // T-0015B: method-call dispatch is now implemented (Maps.by.zmp: `DefProjConverter.LonLat2XY
    // (TL)`), so this default-constructed ScriptVars (every interface slot null) exercises the one
    // remaining failure mode for a method call - spec 7's generic runtime-error row, IoError, not
    // Unsupported. The exact legacy exception text is unverifiable (no compiler/oracle - see
    // docs/handoffs/T-0015B.md); only the error *class* is asserted.
    Outcome o = run("var TL: TDoublePoint;\nbegin TL := DefProjConverter.LonLat2XY(TL); end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::IoError);
}

TEST(Interpreter, ExecBudgetCancelsARunawayLoop) {
    ExecBudget tinyBudget;
    tinyBudget.maxSteps = 1000;
    auto compiled = CompiledScript::compile("var I: Integer;\nbegin I := 0; while True do I := I + 1; end.",
                                            QStringLiteral("runaway"));
    ASSERT_TRUE(compiled.has_value());
    Interpreter interp(*compiled, tinyBudget);
    FakeScriptHost host;
    ScriptVars vars;
    auto result = interp.run(host, vars);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::Cancelled);
}

// --- T-0014 rework (2026-09-18 review) -------------------------------------------------------------

TEST(Interpreter, AssignToBoundVariableSucceedsAndReadsBack) {
    // T-0015A: 349/350 corpus scripts assign a bound variable (ResultURL). Blocker 1 (T-0014
    // review) demanded this fail loudly rather than silently no-op before the host binding
    // existed; now it is the whole point - a plain `Assign` to ResultURL must reach `vars` after a
    // successful run (spec 2.11/2.13).
    Outcome o = run("begin ResultURL := 'http://x/'; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(7).toStr(), QByteArrayLiteral("http://x/"));
    EXPECT_EQ(o.vars.resultUrl, QByteArrayLiteral("http://x/"));
}

TEST(Interpreter, BuiltinArityIsCheckedBeforeAnyArgAccess) {
    // Blocker 2: the parser deliberately does not enforce arity for Builtin/Registered calls, so
    // callBuiltin must reject a bad argument count itself, before it ever indexes args[i] - a
    // diagnostic, never an out-of-bounds access.
    Outcome tooFew = run("var A: string;\nbegin A := Copy('abcdef'); end.");
    ASSERT_FALSE(tooFew.runResult.has_value());
    EXPECT_EQ(tooFew.runResult.error().code(), cg::core::ErrorCode::Unsupported);

    Outcome tooMany = run("var A: string;\nbegin A := Copy('abcdef', 1, 2, 3); end.");
    ASSERT_FALSE(tooMany.runResult.has_value());
    EXPECT_EQ(tooMany.runResult.error().code(), cg::core::ErrorCode::Unsupported);
}

TEST(Interpreter, OutParameterToNonLvalueFailsLoudly) {
    // Blocker 2: the 'out' write-back needs the same VarRef guard the 'var' path already has -
    // Give(5) must be a diagnostic, never a write through a garbage slot.
    Outcome o = run("procedure Give(out X: Integer);\nbegin X := 42; end;\nbegin Give(5); end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::Unsupported);
}

TEST(Interpreter, TryExceptDoesNotSwallowUnsupportedOrCancelled) {
    // Major 3: only a Delphi-equivalent runtime error (IoError) is catchable; Unsupported and
    // Cancelled are port-internal and must propagate through `except` unchanged. T-0015A: GetX
    // is now a live bound variable (no longer Unsupported); T-0015B: an interface method call is
    // now dispatched too (a *nil* interface call is IoError, catchable - see
    // InterfaceMethodCallOnNilInterfaceIsARuntimeError), so the still-Unsupported construct used
    // here is aliased `var` arguments (Major 5, T-0014B) instead.
    Outcome unsupportedInTry = run("procedure P(var a, b: Integer);\nbegin a := 1; b := 2; end;\n"
                                   "var X: Integer;\nbegin X := 0; try P(X, X); except X := 99; end; end.");
    ASSERT_FALSE(unsupportedInTry.runResult.has_value());
    EXPECT_EQ(unsupportedInTry.runResult.error().code(), cg::core::ErrorCode::Unsupported);

    ExecBudget tinyBudget;
    tinyBudget.maxSteps = 10;
    auto compiled = CompiledScript::compile(
        "var A: Integer;\nbegin try while True do A := A + 1; except A := 99; end; end.",
        QStringLiteral("budget-in-try"));
    ASSERT_TRUE(compiled.has_value());
    Interpreter interp(*compiled, tinyBudget);
    FakeScriptHost host;
    ScriptVars vars;
    auto result = interp.run(host, vars);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::Cancelled);
}

TEST(Interpreter, InterpreterOutlivesADroppedCompiledScript) {
    // Major 4: Interpreter must keep the CompiledScript (and the AST arena it owns) alive itself -
    // nothing should force the caller to outlive it.
    std::unique_ptr<Interpreter> interp;
    {
        auto compiled =
            CompiledScript::compile("var A: Integer;\nbegin A := 41 + 1; end.", QStringLiteral("lifetime"));
        ASSERT_TRUE(compiled.has_value());
        interp = std::make_unique<Interpreter>(*compiled);
        // *compiled (a shared_ptr<const CompiledScript>) goes out of scope right here.
    }
    FakeScriptHost host;
    ScriptVars vars;
    auto result = interp->run(host, vars);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*interp->slot(kV0).toInt(), 42);
}

TEST(Interpreter, AliasedVarArgumentsFailLoudly) {
    // Major 5 (spec 4.3 A5): Delphi's `var` is by-reference, so P(x, x) writes through the SAME
    // cell; this slice's copy-in/copy-out VM cannot reproduce that and must fail loudly instead of
    // silently diverging (true var-by-reference is T-0014B).
    Outcome o = run("procedure P(var a, b: Integer);\nbegin a := 1; b := 2; end;\n"
                    "var X: Integer;\nbegin X := 0; P(X, X); end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::Unsupported);
}

TEST(Interpreter, CopyEdgeCases) {
    // spec 4.2 T6/5.4.
    Outcome o = run("var A, B, C, D: string;\n"
                    "begin A := Copy('Galileo', 1, 0); B := Copy('Galileo', 0, 2); "
                    "C := Copy('Galileo', 99, 5); D := Copy('Galileo', 1, -1); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("Ga"));
    EXPECT_EQ(o.interpreter->slot(kV2).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArray());
}

TEST(Interpreter, InsertAndDeleteEdgeCases) {
    // spec 4.2 T6: Insert clamps an out-of-range position instead of erroring; Delete fully out of
    // range is a no-op.
    Outcome o = run("var S, T: string;\n"
                    "begin S := 'bc'; Insert('a', S, 0); T := 'bc'; Insert('d', T, 99); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("abc")); // i < 1 prepends
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("bcd")); // i > Length+1 appends

    Outcome d = run("var S: string;\nbegin S := 'hello'; Delete(S, 99, 3); end.");
    ASSERT_RAN_OK(d);
    EXPECT_EQ(d.interpreter->slot(kV0).toStr(), QByteArrayLiteral("hello")); // fully out of range
}

TEST(Interpreter, PosExAndSubStrPosEdgeCasesAndArgumentOrder) {
    // spec 5.3 F22/F32: PosEx(SubStr, S, Offset) vs SubStrPos(S, SubStr, From) - the reversed
    // argument order is exactly the kind of thing that silently changes a URL.
    Outcome o = run("var A, B, C: Integer;\n"
                    "begin A := PosEx('v', 'k=v', 99); "
                    "B := PosEx('v', 'k=v', 1); C := SubStrPos('k=v', 'v', 1); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 0); // Offset > Length(S) -> 0
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 3);
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), 3); // same match, opposite argument order
}

TEST(Interpreter, IntToHexEdgeCases) {
    // spec 5.3 F18.
    Outcome o = run("var A, B, C: string;\n"
                    "begin A := IntToHex(-1, 8); B := IntToHex(255, 2); C := IntToHex(4096, 2); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("FFFFFFFF"));
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("FF"));
    EXPECT_EQ(o.interpreter->slot(kV2).toStr(), QByteArrayLiteral("1000")); // never truncates
}

TEST(Interpreter, GetDiv3PathExactTable) {
    // spec F34's full table (the existing test only covers one shape).
    Outcome o = run("var A, B, C, D, E: string;\n"
                    "begin A := GetDiv3Path(''); B := GetDiv3Path('1'); C := GetDiv3Path('12'); "
                    "D := GetDiv3Path('123'); E := GetDiv3Path('12345678'); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV2).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArrayLiteral("123\\"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 4).toStr(), QByteArrayLiteral("345\\678\\"));
}

TEST(Interpreter, StringReplaceBothFlagsTogether) {
    Outcome o = run("var A: string;\nbegin A := StringReplace('AbAb', 'ab', 'x', "
                    "[rfReplaceAll, rfIgnoreCase]); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("xx"));
}

TEST(Interpreter, ForLoopBoundEvaluatedOnceAndLoopVarReassignmentIsIgnored) {
    // spec 3.3: the upper bound is evaluated once, before the loop starts, and iteration is driven
    // by a hidden counter, not a re-read of the loop variable's slot - a body that reassigns the
    // loop variable does not change how many times the loop runs.
    Outcome o = run("var I, N, Count: Integer;\n"
                    "begin N := 3; Count := 0; "
                    "for I := 1 to N do begin N := 100; I := 99; Count := Count + 1; end; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), 3); // ran exactly 3 times, not 100
}

// --- T-0014B: declared-type range-wrap/rounding on assignment (spec 4.1 N1/N2) -------------------

TEST(Interpreter, ByteSlotWrapsMod256OnOverflowAndUnderflow) {
    // megafonpro.zmp-style: `max: array[0..20] of byte` motivates N1; this is the scalar case.
    Outcome over = run("var B: Byte;\nbegin B := 255 + 10; end.");
    ASSERT_RAN_OK(over);
    EXPECT_EQ(*over.interpreter->slot(kV0).toInt(), 9); // 265 mod 256

    Outcome under = run("var B: Byte;\nbegin B := 0 - 1; end.");
    ASSERT_RAN_OK(under);
    EXPECT_EQ(*under.interpreter->slot(kV0).toInt(), 255);
}

TEST(Interpreter, WordCardinalAndSigned32SlotsWrapPerN1) {
    Outcome o = run("var W: Word; C: Cardinal; I: Integer;\n"
                    "begin W := 65537; C := 0 - 1; I := 2147483648; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 1);             // Word: mod 65536
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 4294967295LL);  // Cardinal: mod 2^32
    EXPECT_EQ(*o.interpreter->slot(kV2).toInt(), -2147483648LL); // Integer: signed 32-bit wrap
}

TEST(Interpreter, IncAndDecWrapTheirTargetSlot) {
    Outcome o = run("var B: Byte;\nbegin B := 250; Inc(B, 10); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 4); // 260 mod 256
}

TEST(Interpreter, VarByteParameterWrapsOnAssignment) {
    Outcome o = run("var B: Byte;\nprocedure GiveMax(var X: Byte);\nbegin X := 300; end;\n"
                    "begin B := 0; GiveMax(B); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 44); // 300 mod 256, wrapped to B's declared type
}

TEST(Interpreter, FunctionResultWraps) {
    // AsByte=kV0, N=kV0+1, Result=kV0+2, B=kV0+3 (routine's own slot/params/Result all consume
    // global slot numbers before the top-level `var` section - see the Square test above).
    Outcome o = run("function AsByte(N: Integer): Byte;\nbegin Result := N; end;\n"
                    "var B: Integer;\nbegin B := AsByte(300); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 3).toInt(), 44); // Result's declared type (Byte) wraps 300
}

TEST(Interpreter, DoubleSlotRoundsFromExtendedPrecisionOnAssignment) {
    // spec 4.1 N2: `Sqrt` (std::sqrtl) is genuine long double precision; a Double slot narrows it.
    Outcome o = run("var E: Extended; D: Double;\nbegin E := Sqrt(2); D := Sqrt(2); end.");
    ASSERT_RAN_OK(o);
    const long double extended = *o.interpreter->slot(kV0).toReal();
    const long double rounded = *o.interpreter->slot(kV1).toReal();
    EXPECT_EQ(rounded, static_cast<long double>(static_cast<double>(extended)));
    EXPECT_NE(rounded, extended); // precision was actually dropped on assignment to D
}

// Each builtin below returns genuine long double precision: the slot equals the long double libm
// result and differs from the same function evaluated in double and widened.
static void expectExtendedBuiltin(const char* call, long double expected, long double asDouble) {
    Outcome o = run(QByteArray("var E: Extended;\nbegin E := ") + call + "; end.");
    ASSERT_RAN_OK(o);
    const long double got = *o.interpreter->slot(kV0).toReal();
    EXPECT_EQ(got, expected) << call;
    EXPECT_NE(got, asDouble) << call;
}

TEST(Interpreter, ExpIsExtendedPrecision) {
    // spec 4.1 N2: Exp is long double precision (std::exp(long double))
    expectExtendedBuiltin("Exp(1)", std::exp(1.0L), static_cast<long double>(std::exp(1.0)));
}

TEST(Interpreter, LnIsExtendedPrecision) {
    // spec 4.1 N2: Ln is long double precision
    expectExtendedBuiltin("Ln(2)", std::log(2.0L), static_cast<long double>(std::log(2.0)));
}

TEST(Interpreter, SinIsExtendedPrecision) {
    // spec 4.1 N2: Sin is long double precision
    expectExtendedBuiltin("Sin(1)", std::sin(1.0L), static_cast<long double>(std::sin(1.0)));
}

TEST(Interpreter, CosIsExtendedPrecision) {
    // spec 4.1 N2: Cos is long double precision
    expectExtendedBuiltin("Cos(1)", std::cos(1.0L), static_cast<long double>(std::cos(1.0)));
}

TEST(Interpreter, ArcTanIsExtendedPrecision) {
    // spec 4.1 N2: ArcTan is long double precision
    expectExtendedBuiltin("ArcTan(1)", std::atan(1.0L), static_cast<long double>(std::atan(1.0)));
}

TEST(Interpreter, NonIntegralPowerIsExtendedPrecision) {
    // spec 4.1 N2: Power(b, non-integral e) = exp(e * log(b)) in long double
    expectExtendedBuiltin("Power(2, 0.5)", std::exp(0.5L * std::log(2.0L)),
                          static_cast<long double>(std::exp(0.5 * std::log(2.0))));
}

// --- T-0014B: the 6 built-in record types - default init, Field/FieldAssign, `with` --------------

TEST(Interpreter, TPointZeroInitsAtFrameEntryAndRoundTripsThroughXY) {
    Outcome o = run("var P: TPoint; Z: Integer;\nbegin Z := P.X; P.X := 5; P.Y := P.X + 1; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV1).toInt(), 0); // spec 4.4 I1: zero before any write
    const Value& p = o.interpreter->slot(kV0);
    ASSERT_EQ(p.kind(), ValueKind::Record);
    EXPECT_EQ(*p.fields()[0].toInt(), 5);
    EXPECT_EQ(*p.fields()[1].toInt(), 6);
}

TEST(Interpreter, TDoublePointZeroInitsAtFrameEntryAndRoundTripsThroughXY) {
    Outcome o = run("var P: TDoublePoint;\nbegin P.X := 1.5; P.Y := P.X + 0.5; end.");
    ASSERT_RAN_OK(o);
    const Value& p = o.interpreter->slot(kV0);
    ASSERT_EQ(p.kind(), ValueKind::Record);
    EXPECT_DOUBLE_EQ(static_cast<double>(*p.fields()[0].toReal()), 1.5);
    EXPECT_DOUBLE_EQ(static_cast<double>(*p.fields()[1].toReal()), 2.0);
}

TEST(Interpreter, AllRegisteredRecordTypesZeroInitAtFrameEntry) {
    Outcome o = run("var P: TPoint; DP: TDoublePoint; R: TRect; DR: TDoubleRect; T: TTileInfo;\n"
                    "var X: Boolean; N: Integer; S: string;\n"
                    "begin X := T.IsExists; N := R.Left; S := T.Version; end.");
    ASSERT_RAN_OK(o);
    EXPECT_FALSE(*o.interpreter->slot(kV0 + 5).toBool());
    EXPECT_EQ(*o.interpreter->slot(kV0 + 6).toInt(), 0);
    EXPECT_EQ(o.interpreter->slot(kV0 + 7).toStr(), QByteArray());
}

TEST(Interpreter, WithStmtResolvesFieldsAgainstTheSubjectRecord) {
    Outcome o = run("var P: TPoint;\nbegin with P do X := 1; end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).fields()[0].toInt(), 1);
}

TEST(Interpreter, RecordAssignmentCopiesByValueNotAlias) {
    // Maps.by.zmp-style `TL := BR`: ADR-0003 semantic table row 8 - mutating the copy must not
    // change the original (COW detach on the first field write after the slot-to-slot copy).
    Outcome o = run("var TL, BR: TDoubleRect;\n"
                    "begin BR.Left := 1.0; BR.Top := 2.0; BR.Right := 3.0; BR.Bottom := 4.0; "
                    "TL := BR; TL.Left := 99.0; end.");
    ASSERT_RAN_OK(o);
    EXPECT_DOUBLE_EQ(static_cast<double>(*o.interpreter->slot(kV1).fields()[0].toReal()),
                     1.0); // BR unchanged
    EXPECT_DOUBLE_EQ(static_cast<double>(*o.interpreter->slot(kV0).fields()[0].toReal()), 99.0); // TL mutated
}

// T-0014C1: `Format`/`RoundEx` are wired into callBuiltin (delphi_format_test.cpp has the
// specifier-level table-driven coverage); this is just the "reachable from a script" acceptance
// check, including the corpus-critical `Format('%d/%s', [1, 'x'])` shape (F20).
TEST(Interpreter, FormatAndRoundExAreReachableFromAScript) {
    Outcome o = run("var A, B: string;\n"
                    "begin A := Format('%d/%s', [1, 'x']); B := RoundEx(1.0, 3); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("1/x"));
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("1.000"));
}

// T-0092: pins script::Value -> core::FormatArg (delphi_format_adapter.cpp). Bool and Empty are
// integers only for `%*` (Value::toInt(): 1/0 and 0); every specifier rejects them, and a record
// is rejected by both. Expected texts are those of the pre-move code (same messages, moved verbatim).
struct FormatKindCase {
    const char* name;
    const char* source;
    const char* expected; // result text, or the error text when expectError
    bool expectError = false;
};

class Interpreter_FormatKinds : public ::testing::TestWithParam<FormatKindCase> {};

TEST_P(Interpreter_FormatKinds, AdapterPassesEachValueKindAsBefore) {
    const auto& c = GetParam();
    Outcome o = run(c.source);
    if (c.expectError) {
        ASSERT_FALSE(o.runResult.has_value());
        EXPECT_TRUE(o.runResult.error().what().contains(QString::fromLatin1(c.expected)))
            << o.runResult.error().what().toStdString();
    } else {
        ASSERT_RAN_OK(o);
        EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArray(c.expected));
    }
}

INSTANTIATE_TEST_SUITE_P(
    T0092, Interpreter_FormatKinds,
    ::testing::Values(
        FormatKindCase{"StarWidthTakesBoolAsOne",
                       "var A: string;\nbegin A := Format('%*d|', [True, 3]); end.", "3|"},
        FormatKindCase{"StarWidthTakesEmptyAsZero",
                       "var A, S: string;\nbegin A := Format('%*d|', [S, 3]); end.", "3|"},
        FormatKindCase{"DecimalRejectsBool", "var A: string;\nbegin A := Format('%d', [True]); end.",
                       "Format: '%d' invalid or incompatible with argument 0", true},
        FormatKindCase{"StringRejectsEmpty", "var A, S: string;\nbegin A := Format('%s', [S]); end.",
                       "Format: '%s' invalid or incompatible with argument 0", true},
        FormatKindCase{"DecimalRejectsRecord",
                       "var A: string; P: TDoublePoint;\nbegin A := Format('%d', [P]); end.",
                       "Format: '%d' invalid or incompatible with argument 0", true},
        FormatKindCase{"StarWidthRejectsRecord",
                       "var A: string; P: TDoublePoint;\nbegin A := Format('%*d', [P, 3]); end.",
                       "Format: '*' width needs an integer argument", true}),
    [](const ::testing::TestParamInfo<FormatKindCase>& paramInfo) { return paramInfo.param.name; });

// T-0014C2a: RegExprGetMatchSubStr/RegExprReplaceMatchSubStr are wired into callBuiltin
// (regex_compat_test.cpp has the R1-R16/substitution-template coverage); this is just the
// "reachable from a script" acceptance check for F13/F14.
TEST(Interpreter, RegExprBuiltinsAreReachableFromAScript) {
    Outcome o = run("var A, B: string;\n"
                    "begin A := RegExprGetMatchSubStr('abc123', '[0-9]+', 0); "
                    "B := RegExprReplaceMatchSubStr('abc123', '[0-9]+', 'X'); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("123"));
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("abcX"));
}

// --- T-0014C2b: SetHeaderValue/GetHeaderValue/DeleteHeaderEntry (F29-F31) and Base64Decode -------
// (F17). Src/Network/u_NetworkStrFunc.pas:69-258 (IdemPChar/GetHeaderValueUp/ReplaceHeaderValueUp/
// DeleteHeaderValueUp), Src/PascalScript/u_PascalScriptUtils.pas:88-119. The 350-script corpus
// under tests/fixtures/zmp has zero calls to any of these four functions (verified by grep), so
// these tables are entirely spec/legacy-derived, not corpus-derived.

TEST(Interpreter, SetHeaderValueTable) {
    Outcome o = run(
        "var A, B, C, D, E, F, G, H: string;\n"
        "begin "
        "A := SetHeaderValue('', 'X-Test', 'v1'); "     // absent, empty headers
        "B := SetHeaderValue('Y: 1', 'X-Test', 'v2'); " // absent, no match, unterminated
        "C := SetHeaderValue('x-Test: old'#13#10'Y: 1'#13#10, 'X-TEST', 'new'); " // found, 1st line,
                                                                                  // case-insensitive, CRLF
                                                                                  // preserved
        "D := SetHeaderValue('A: 1'#10'X-Test: old'#13'B: 2'#13#10, 'x-test', "
        "'mid'); " // found, middle line, bare LF/CR terminators
        "E := SetHeaderValue('A: 1'#13#10'X-Test: old', 'X-Test', 'end'); " // found, last unterminated line
        "F := SetHeaderValue('X: old val'#13#10, 'X', 'new value here'); "  // value with spaces, different
                                                                            // length
        "G := SetHeaderValue('X: 1'#13#10, 'X', ''); " // empty AValue: asserts, not-found path (duplicate)
        "H := SetHeaderValue('X: 1'#13#10, '', 'v'); " // empty AName: asserts, not-found path
        "end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("X-Test: v1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 1).toStr(), QByteArrayLiteral("X-Test: v2\r\nY: 1"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 2).toStr(), QByteArrayLiteral("x-Test: new\r\nY: 1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArrayLiteral("A: 1\nX-Test: mid\rB: 2\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 4).toStr(), QByteArrayLiteral("A: 1\r\nX-Test: end"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 5).toStr(), QByteArrayLiteral("X: new value here\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 6).toStr(), QByteArrayLiteral("X: \r\nX: 1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 7).toStr(), QByteArrayLiteral(": v\r\nX: 1\r\n"));
}

TEST(Interpreter, GetHeaderValueTable) {
    Outcome o = run(
        "var A, B, C, D, E, F, G, H: string;\n"
        "begin "
        "A := GetHeaderValue('', 'X'); "                                         // absent, empty headers
        "B := GetHeaderValue('Y: 1'#13#10, 'X'); "                               // absent, no match
        "C := GetHeaderValue('X-Test:   value here  '#13#10, 'x-test'); "        // found, not right-trimmed
        "D := GetHeaderValue('A: 1'#10'X-Test: old'#13'B: 2'#13#10, 'X-Test'); " // middle line, bare LF/CR
        "E := GetHeaderValue('A: 1'#13#10'X-Test: last', 'X-Test'); "            // last unterminated line
        "F := GetHeaderValue('Content-Type: text/html'#13#10, 'Content'); " // IdemPChar prefix match, not a
                                                                            // token match
        "G := GetHeaderValue('X: 1'#13#10, ''); "                           // empty AName
        "H := GetHeaderValue('A: 1'#0'X-Test: v'#13#10, 'X-Test'); " // literal NUL aborts the whole scan
        "end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 1).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 2).toStr(), QByteArrayLiteral("value here  "));
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArrayLiteral("old"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 4).toStr(), QByteArrayLiteral("last"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 5).toStr(), QByteArrayLiteral("-Type: text/html"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 6).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 7).toStr(), QByteArray());
}

TEST(Interpreter, DeleteHeaderEntryTable) {
    Outcome o =
        run("var A, B, C, D, E, F: string;\n"
            "begin "
            "A := DeleteHeaderEntry('A: 1'#13#10, 'X'); " // not found: unchanged
            "B := DeleteHeaderEntry('', 'X'); "           // empty headers: unchanged
            "C := DeleteHeaderEntry('A: 1'#13#10, ''); "  // empty AName: unchanged
            "D := DeleteHeaderEntry('A: 1'#13#10'X-Test: v'#13#10#13#10'B: 2'#13#10, "
            "'X-Test'); " // deletes entry + trailing blank line
            "E := DeleteHeaderEntry('A: 1'#13#10'X-Test: v', 'X-Test'); " // last unterminated line: whole
                                                                          // rest removed
            "F := DeleteHeaderEntry('CONTENT-TYPE: text/html'#13#10, 'Content'); " // prefix match deletes the
                                                                                   // whole line
            "end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("A: 1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 1).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 2).toStr(), QByteArrayLiteral("A: 1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArrayLiteral("A: 1\r\nB: 2\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 4).toStr(), QByteArrayLiteral("A: 1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 5).toStr(), QByteArray());
}

TEST(Interpreter, Base64DecodeStrictnessTable) {
    Outcome o = run("var A, B, C, D, E, F, G, H, I: string;\n"
                    "begin "
                    "A := Base64Decode(''); "     // empty -> empty
                    "B := Base64Decode('TWFu'); " // valid, no padding needed
                    "C := Base64Decode('TWE='); " // valid, 1 padding char
                    "D := Base64Decode('TQ=='); " // valid, 2 padding chars
                    "E := Base64Decode('TWE'); "  // invalid: length not a multiple of 4 (missing padding)
                    "F := Base64Decode('TWE-'); " // invalid: URL alphabet char instead of standard/padding
                    "G := Base64Decode('TW=u'); " // invalid: padding in the middle
                    "H := Base64Decode('A==='); " // invalid: more than 2 padding chars
                    "I := Base64Decode('TW!u'); " // invalid: character outside the standard alphabet
                    "end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 1).toStr(), QByteArrayLiteral("Man"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 2).toStr(), QByteArrayLiteral("Ma"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArrayLiteral("M"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 4).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 5).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 6).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 7).toStr(), QByteArray());
    EXPECT_EQ(o.interpreter->slot(kV0 + 8).toStr(), QByteArray());
}

// Reachability check (ticket policy: one script-level test covering the real-usage call shapes).
// The 350-script corpus has no calls to these four functions at all, so this uses the natural
// call shapes from spec F17/F29-F31 instead of a corpus excerpt.
TEST(Interpreter, HeaderAndBase64FunctionsAreReachableFromAScript) {
    Outcome o = run("var H, G, D, B: string;\n"
                    "begin H := SetHeaderValue('X: 1'#13#10, 'Y', '2'); "
                    "G := GetHeaderValue(H, 'Y'); "
                    "D := DeleteHeaderEntry(H, 'X'); "
                    "B := Base64Decode('TWFu'); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(kV0).toStr(), QByteArrayLiteral("Y: 2\r\nX: 1\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 1).toStr(), QByteArrayLiteral("2"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 2).toStr(), QByteArrayLiteral("Y: 2\r\n"));
    EXPECT_EQ(o.interpreter->slot(kV0 + 3).toStr(), QByteArrayLiteral("Man"));
}

// --- T-0015A: the host binding (spec 2.7/5.1/5.3) --------------------------------------------

// One table-driven case for all 26 bound variables (spec 5.1) - registration order == slot index,
// TileCache=0 .. GetBMetr=25 (parser.cpp's kBoundVariables). The 7 interface slots are covered for
// their "unassigned" shape here (this fixture's ScriptVars leaves them null - the fakes for a live
// Assigned()=true case arrive with T-0015B); UnassignedInterfaceGivesAssignedFalse below is the
// dedicated case ticket §Tests asks for. No script body is needed: bindScriptVars runs
// unconditionally at the start of run(), independently of whether the script names a variable
// (legacy's AddUsedVariable marks all 26 "used" regardless too).
TEST(Interpreter, AllBoundVariablesRoundTrip) {
    ScriptVars vars;
    vars.getX = 100;
    vars.getY = 200;
    vars.getZ = 5;
    vars.getLlon = 1.5;
    vars.getTLat = 2.5;
    vars.getBLat = 3.5;
    vars.getRLon = 4.5;
    vars.getLMetr = 5.5;
    vars.getRMetr = 6.5;
    vars.getTMetr = 7.5;
    vars.getBMetr = 8.5;
    vars.getUrlBase = QByteArrayLiteral("base");
    vars.responseHead = QByteArrayLiteral("resp");
    vars.version = QByteArrayLiteral("ver");
    vars.lang = QByteArrayLiteral("en");
    vars.requestHead = QByteArrayLiteral("reqhead");
    vars.scriptBuffer = QByteArrayLiteral("buf0");

    Outcome o = run("begin end.", vars);
    ASSERT_RAN_OK(o);
    const Interpreter& in = *o.interpreter;
    for (int slot = 0; slot < 7; ++slot) { // #1-7: TileCache..Converter, all null in this fixture
        EXPECT_FALSE(in.slot(slot).isAssigned()) << "interface slot " << slot;
    }
    EXPECT_EQ(in.slot(7).toStr(), QByteArray());                       // #8 ResultURL: cleared, not read
    EXPECT_EQ(in.slot(8).toStr(), QByteArray());                       // #9 PostData: cleared, not read
    EXPECT_EQ(in.slot(9).toStr(), QByteArrayLiteral("base"));          // #10 GetURLBase
    EXPECT_EQ(in.slot(10).toStr(), QByteArrayLiteral("reqhead"));      // #11 RequestHead
    EXPECT_EQ(in.slot(11).toStr(), QByteArrayLiteral("resp"));         // #12 ResponseHead
    EXPECT_EQ(in.slot(12).toStr(), QByteArrayLiteral("buf0"));         // #13 ScriptBuffer
    EXPECT_EQ(in.slot(13).toStr(), QByteArrayLiteral("ver"));          // #14 Version
    EXPECT_EQ(in.slot(14).toStr(), QByteArrayLiteral("en"));           // #15 Lang
    EXPECT_EQ(*in.slot(15).toInt(), 100);                              // #16 GetX
    EXPECT_EQ(*in.slot(16).toInt(), 200);                              // #17 GetY
    EXPECT_EQ(*in.slot(17).toInt(), 5);                                // #18 GetZ
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(18).toReal()), 1.5); // #19 GetLlon
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(19).toReal()), 2.5); // #20 GetTLat
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(20).toReal()), 3.5); // #21 GetBLat
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(21).toReal()), 4.5); // #22 GetRLon
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(22).toReal()), 5.5); // #23 GetLMetr
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(23).toReal()), 6.5); // #24 GetRMetr
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(24).toReal()), 7.5); // #25 GetTMetr
    EXPECT_DOUBLE_EQ(static_cast<double>(*in.slot(25).toReal()), 8.5); // #26 GetBMetr

    // spec 2.11/2.13: on success, ResultURL/PostData/RequestHead/ScriptBuffer are written back
    // onto the caller's ScriptVars (an empty script leaves them at their run-start value).
    EXPECT_EQ(o.vars.resultUrl, QByteArray());
    EXPECT_EQ(o.vars.postData, QByteArray());
    EXPECT_EQ(o.vars.requestHead, QByteArrayLiteral("reqhead"));
    EXPECT_EQ(o.vars.scriptBuffer, QByteArrayLiteral("buf0"));
}

// spec 5.1 row 18: GetZ is VZoom+1, already 1-based in the caller's ScriptVars - the interpreter
// must copy it through unchanged, never reinterpret or re-offset it.
TEST(Interpreter, GetZIsOneBased) {
    ScriptVars vars;
    vars.getZ = 9; // stands for an internal zoom of 8
    Outcome o = run("var A: Integer; begin A := GetZ; end.", vars);
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 9);
}

// spec 2.7 tail: GetURLBase is copied into a *mutable* slot that this Interpreter discards at the
// end of run() - a script mutating it (the corpus does this via `GetURLBase[8] := ...`, e.g.
// _osm/Thunderfores_cycle.zmp) must never leak the change back into the caller's ScriptVars, or
// the next tile request for the same map would see the previous request's mutation.
TEST(Interpreter, GetURLBaseMutationDoesNotReachScriptVars) {
    ScriptVars vars;
    vars.getUrlBase = QByteArrayLiteral("abcdefgh");
    Outcome o = run("begin GetURLBase[8] := 'X'; end.", vars);
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.interpreter->slot(9).toStr(), QByteArrayLiteral("abcdefgX")); // mutated locally
    EXPECT_EQ(vars.getUrlBase, QByteArrayLiteral("abcdefgh"));                // caller's copy intact
    EXPECT_EQ(o.vars.getUrlBase, QByteArrayLiteral("abcdefgh")); // not in the read-back list either
}

// An interface bound variable the host left null reads back as `nil`/Assigned() = False, for
// every one of the 7 interface-typed slots (spec 5.1 #1-7) - not just the one the caller happens
// to exercise. Method calls on a bound interface stay Unsupported until T-0015B.
TEST(Interpreter, UnassignedInterfaceGivesAssignedFalse) {
    Outcome o = run("var A, B, C, D, E, F, G: Boolean;\n"
                    "begin "
                    "A := Assigned(TileCache); B := Assigned(Logger); C := Assigned(Global); "
                    "D := Assigned(Downloader); E := Assigned(DefProjConverter); "
                    "F := Assigned(ProjFactory); G := Assigned(Converter); "
                    "end.");
    ASSERT_RAN_OK(o);
    for (int i = 0; i < 7; ++i) {
        EXPECT_FALSE(o.interpreter->slot(kV0 + i).isTruthy()) << "result var " << i;
    }
}

// --- T-0015B: interface method dispatch (spec 2.7/5.6) ------------------------------------------

// ICoordConverter: record argument + record result, plus the 0-based Byte zoom parameter (spec 5.1
// tail). FakeCoordConverter's spherical Mercator puts pixel (128,128) at zoom 0 exactly at the
// tiling's centre, lon/lat (0, 0).
TEST(Interpreter, ConverterMethodCallMarshalsRecordsAndByteZoom) {
    ScriptVars vars;
    vars.converter = std::make_shared<FakeCoordConverter>();
    Outcome o = run("var P: TPoint; LL: TDoublePoint;\n"
                    "begin P.X := 128; P.Y := 128; LL := Converter.Pos2LonLat(P, 0); end.",
                    vars);
    ASSERT_RAN_OK(o);
    const Value& ll = o.interpreter->slot(kV1);
    ASSERT_EQ(ll.kind(), ValueKind::Record);
    EXPECT_NEAR(static_cast<double>(*ll.fields()[0].toReal()), 0.0, 1e-9);
    EXPECT_NEAR(static_cast<double>(*ll.fields()[1].toReal()), 0.0, 1e-9);
}

// IProjConverter, via the corpus shape (Maps.by.zmp: `DefProjConverter.LonLat2XY(TL)`).
TEST(Interpreter, DefProjConverterMethodCallReturnsRecord) {
    ScriptVars vars;
    vars.defProjConverter = std::make_shared<FakeProjConverter>();
    Outcome o = run("var TL, XY: TDoublePoint;\n"
                    "begin TL.X := 1.0; TL.Y := 2.0; XY := DefProjConverter.LonLat2XY(TL); end.",
                    vars);
    ASSERT_RAN_OK(o);
    const Value& xy = o.interpreter->slot(kV1);
    EXPECT_DOUBLE_EQ(static_cast<double>(*xy.fields()[0].toReal()), 1000.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(*xy.fields()[1].toReal()), 2000.0);
}

// IProjConverterFactory: an unknown EPSG code returns nil (script_host.h/spec 5.6), so
// Assigned(ProjFactory.GetByEPSG(...)) is False - "GetByEPSG" is legacy's spelling
// (u_PascalScriptTypes.pas:68), which parser.cpp's method table now matches.
TEST(Interpreter, ProjFactoryByEpsgOfUnknownCodeGivesAssignedFalse) {
    ScriptVars vars;
    vars.projFactory = std::make_shared<FakeProjConverterFactory>();
    Outcome o = run("var A: Boolean;\nbegin A := Assigned(ProjFactory.GetByEPSG(999)); end.", vars);
    ASSERT_RAN_OK(o);
    EXPECT_FALSE(o.interpreter->slot(kV0).isTruthy());
}

// IHttpDownloader: the 2 `var` output arguments are written back independently of the Cardinal
// result, and a status of 0 is passed through unchanged (spec 5.6 - it also means "no connection").
TEST(Interpreter, HttpDownloaderWritesBackTheTwoOutParameters) {
    auto downloader = std::make_shared<FakeHttpDownloader>();
    downloader->enqueueReply(
        FakeHttpDownloader::Reply{0, QByteArrayLiteral("h: 1"), QByteArrayLiteral("body")});
    ScriptVars vars;
    vars.downloader = downloader;
    Outcome o = run("var Status: Integer; RH, RD: string;\n"
                    "begin Status := Downloader.DoHttpRequest('http://x/', 'rh', '', RH, RD); end.",
                    vars);
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 0); // status 0, never normalised away
    EXPECT_EQ(o.interpreter->slot(kV1).toStr(), QByteArrayLiteral("h: 1"));
    EXPECT_EQ(o.interpreter->slot(kV2).toStr(), QByteArrayLiteral("body"));
    ASSERT_EQ(downloader->calls().size(), 1);
    EXPECT_EQ(downloader->calls().front().url, QByteArrayLiteral("http://x/"));
    EXPECT_EQ(downloader->calls().front().requestHeader, QByteArrayLiteral("rh"));
}

// IScriptGlobal: GetVar on an absent id returns Value{} (Empty, legacy's Unassigned) - VarType of
// that result is 0, via the existing VarType builtin (no separate Variant<->Value mapping needed).
TEST(Interpreter, GlobalGetVarOnAbsentIdGivesVarTypeZero) {
    ScriptVars vars;
    vars.global = std::make_shared<FakeScriptGlobal>();
    Outcome o = run("var A: Integer;\nbegin A := VarType(Global.GetVar(0)); end.", vars);
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 0);
}

// IScriptLogger: an empty Write is dropped (spec 5.6) - by the implementation, not the dispatch
// site (FakeScriptLogger reproduces this the same way legacy's TPascalScriptLogger.Write does).
// WriteFmt dispatches as legacy does: Write(Format(fmt, args)) (u_PascalScriptLogger.pas:139-142).
TEST(Interpreter, LoggerEmptyWriteIsDropped) {
    auto logger = std::make_shared<FakeScriptLogger>();
    ScriptVars vars;
    vars.logger = logger;
    Outcome o =
        run("begin Logger.Write(''); Logger.Write('hi'); Logger.WriteFmt('%d/%s', [1, 'x']); end.", vars);
    ASSERT_RAN_OK(o);
    ASSERT_EQ(logger->writes().size(), 2);
    EXPECT_EQ(logger->writes().front(), QByteArrayLiteral("hi"));
    EXPECT_EQ(logger->writes().back(), QByteArrayLiteral("1/x"));
}

// IScriptTileCache: a Write-then-Read round-trip of the TTileInfo layout in declared field order
// (spec 5.2: IsExists, IsExistsTne, LoadDate, Size, Version, ContentType, Data). Zoom 259 on the
// Write also proves the dispatcher wraps it to a Byte (u_PascalScriptTypes.pas:159,169,179,187),
// like ICoordConverter's: 259 wraps to 3, so it round-trips with the Read at zoom 3.
TEST(Interpreter, TileCacheReadRoundTripsTileInfo) {
    ScriptVars vars;
    vars.tileCache = std::make_shared<FakeScriptTileCache>();
    Outcome o = run("var Written: Boolean; T: TTileInfo;\n"
                    "begin Written := TileCache.Write(1, 2, 259, 'v1', 'text/plain', 'hello', True); "
                    "T := TileCache.Read(1, 2, 3, 'v1', True); end.",
                    vars);
    ASSERT_RAN_OK(o);
    EXPECT_TRUE(o.interpreter->slot(kV0).isTruthy());
    const Value& t = o.interpreter->slot(kV1);
    ASSERT_EQ(t.kind(), ValueKind::Record);
    EXPECT_TRUE(t.fields()[0].isTruthy());                             // IsExists
    EXPECT_FALSE(t.fields()[1].isTruthy());                            // IsExistsTne
    EXPECT_EQ(*t.fields()[3].toInt(), 5);                              // Size == strlen("hello")
    EXPECT_EQ(t.fields()[5].toStr(), QByteArrayLiteral("text/plain")); // ContentType
    EXPECT_EQ(t.fields()[6].toStr(), QByteArrayLiteral("hello"));      // Data
}

// spec 5.3 F1: Delphi System.Random's exact process-global LCG, seeded to reproduce the golden
// harness's reference vector - 6 draws of Random(10) from seed 0 are 0, 0, 8, 2, 2, 6.
TEST(Interpreter, RandomUsesSeededLcgVector) {
    Outcome o = run("var A, B, C, D, E, F: Integer;\n"
                    "begin A := Random(10); B := Random(10); C := Random(10); "
                    "D := Random(10); E := Random(10); F := Random(10); end.",
                    ScriptVars{}, std::make_unique<FakeScriptHost>(0));
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 0);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 1).toInt(), 0);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 2).toInt(), 8);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 3).toInt(), 2);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 4).toInt(), 2);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 5).toInt(), 6);
}

// Random(X) for X <= 0 is 0 without ever calling the host (spec 5.3 F1) - proven by using a seed
// that would otherwise produce a non-zero first draw.
TEST(Interpreter, RandomOfNonPositiveIsZero) {
    Outcome o = run("var A, B: Integer; begin A := Random(0); B := Random(-5); end.", ScriptVars{},
                    std::make_unique<FakeScriptHost>(1));
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 0);
    EXPECT_EQ(*o.interpreter->slot(kV0 + 1).toInt(), 0);
}

// Reachability check (ticket policy: one script-level test per the remaining 6 host-backed
// builtins - RandomRange, FileExists, GetUnixTime, SaveToLocalFile, WriteLn, TemplateToUrl - not
// a semantics suite for each). RandomRange(5, 5) is deterministic by F2's own rule (equal args ->
// AFrom) without depending on the LCG, so it also doubles as a spec-F2 spot check.
TEST(Interpreter, RemainingHostBuiltinsAreReachableFromAScript) {
    auto host = std::make_unique<FakeScriptHost>();
    host->setUnixTime(1'700'000'000);
    host->setTile(3, 4, 5);
    host->addExistingFile(QStringLiteral("/tmp/exists.txt"));
    FakeScriptHost* rawHost = host.get();

    Outcome o = run("var R, T, N: Integer; F1, F2: Boolean; S: string;\n"
                    "begin "
                    "R := RandomRange(5, 5); "
                    "F1 := FileExists('/tmp/exists.txt'); "
                    "F2 := FileExists('/tmp/missing.txt'); "
                    "T := GetUnixTime; "
                    "N := SaveToLocalFile('/tmp/out.bin', 'hello'); "
                    "WriteLn('hi'); "
                    "S := TemplateToUrl('a{x}/{y}/{Z}/{q}'); "
                    "end.",
                    ScriptVars{}, std::move(host));
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 5);                 // RandomRange(5,5) = 5 (F2)
    EXPECT_TRUE(o.interpreter->slot(kV0 + 3).isTruthy());            // FileExists on a known path
    EXPECT_FALSE(o.interpreter->slot(kV0 + 4).isTruthy());           // FileExists on an unknown path
    EXPECT_EQ(*o.interpreter->slot(kV0 + 1).toInt(), 1'700'000'000); // GetUnixTime
    EXPECT_EQ(*o.interpreter->slot(kV0 + 2).toInt(), 5);             // SaveToLocalFile: byte count
    EXPECT_EQ(o.interpreter->slot(kV0 + 5).toStr(), QByteArrayLiteral("a3/4/5/{q}")); // TemplateToUrl

    ASSERT_EQ(rawHost->saveCalls().size(), 1);
    EXPECT_EQ(rawHost->saveCalls().front().path, QStringLiteral("/tmp/out.bin"));
    EXPECT_EQ(rawHost->saveCalls().front().data, QByteArrayLiteral("hello"));
    ASSERT_EQ(rawHost->writtenLines().size(), 1);
    EXPECT_EQ(rawHost->writtenLines().front(), QByteArrayLiteral("hi"));
    EXPECT_EQ(rawHost->fileExistsCalls().size(), 2);
}

// --- T-0032 rework (review F1): the two tests the ticket's own Tests section asked for and the
// developer left out. Corpus/golden coverage is real (megafonpro/eatlas_new for char/byte,
// odessa.ua.zmp/Rambler_maps.zmp for integer, belgeodesy.zmp for longint - see
// docs/handoffs/T-0032-review.md finding 1), but no corpus script stores an out-of-range value
// into a byte/char element, so `Value::wrappedTo`'s narrowing (value.cpp) on the IndexAssign write
// path (interpreter.cpp, NodeKind::IndexAssign) was untested; the PD-103 const repro
// (docs/handoffs/T-0015D-review.md "Repro (reviewer)") was likewise never turned into a test.
// Expected values are derived from the engine (sasgis-pascalscript, uPSRuntime.pas), not from
// running the port.

struct StaticArrayElementCase {
    const char* name;
    const char* source;
    int slotOffset; // added to kV0 - the slot holding the value under test
    ValueKind expectedKind;
    const char* expectedStr; // Value::toStr()
};

class Interpreter_StaticArrayElementTypes : public ::testing::TestWithParam<StaticArrayElementCase> {};

TEST_P(Interpreter_StaticArrayElementTypes, DeclareWriteAndReadBack) {
    const auto& c = GetParam();
    Outcome o = run(c.source);
    ASSERT_RAN_OK(o);
    const Value& v = o.interpreter->slot(kV0 + c.slotOffset);
    EXPECT_EQ(v.kind(), c.expectedKind);
    EXPECT_EQ(v.toStr(), QByteArray(c.expectedStr));
}

std::string staticArrayElementCaseName(const ::testing::TestParamInfo<StaticArrayElementCase>& info) {
    return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    T0032, Interpreter_StaticArrayElementTypes,
    ::testing::Values(
        // char: megafonpro.zmp-style `a: array[0..35] of char`, written/read as single-char strings
        // (`strx3 := a[x3];` in the corpus script) - declare/write/read round trip.
        StaticArrayElementCase{"CharElementDeclareWriteRead",
                               "var A: array[0..2] of char; S: string;\nbegin A[1] := 'Q'; S := A[1]; end.",
                               1, ValueKind::Str, "Q"},
        // byte: megafonpro.zmp-style `max: array[0..20] of byte` - in-range declare/write/read round trip.
        StaticArrayElementCase{"ByteElementDeclareWriteRead",
                               "var A: array[0..2] of byte; N: Integer;\nbegin A[1] := 200; N := A[1]; end.",
                               1, ValueKind::Int, "200"},
        // byte wrap-on-store: uPSRuntime.pas's element-store narrows exactly like a scalar Byte
        // slot does (N1; see ByteSlotWrapsMod256OnOverflowAndUnderflow above) - 300 mod 256 = 44.
        StaticArrayElementCase{"ByteElementWrapsOnStore",
                               "var A: array[0..2] of byte; N: Integer;\nbegin A[0] := 300; N := A[0]; end.",
                               1, ValueKind::Int, "44"},
        // integer: odessa.ua.zmp/Rambler_maps.zmp-style `sh_x,sh_y: array[0..24] of integer`.
        StaticArrayElementCase{"IntegerElementDeclareWriteRead",
                               "var A: array[0..24] of integer; N: Integer;\n"
                               "begin A[24] := 12345; N := A[24]; end.",
                               1, ValueKind::Int, "12345"},
        // longint: belgeodesy.zmp-style `dx,dy: array[1..50] of longint` (Longint is Integer's
        // Delphi synonym - parser.cpp's kTypeNames - same 32-bit representation, tested separately
        // because the ticket names it as one of the four element types).
        StaticArrayElementCase{"LongintElementDeclareWriteRead",
                               "var A: array[1..50] of longint; N: Integer;\n"
                               "begin A[50] := 2000000000; N := A[50]; end.",
                               1, ValueKind::Int, "2000000000"},
        // T-0035 (review F5): InitializeVariant zero-fills, so an unassigned char element is #0
        // and an unassigned string element is '' - `'x' + a[k]` is 2 characters / 1 character.
        StaticArrayElementCase{"CharElementUnassignedIsNul",
                               "var A: array[0..2] of char; N: Integer;\nbegin N := Length('x' + A[1]); end.",
                               1, ValueKind::Int, "2"},
        StaticArrayElementCase{
            "StringElementUnassignedIsEmpty",
            "var A: array[0..2] of string; N: Integer;\nbegin N := Length('x' + A[1]); end.", 1,
            ValueKind::Int, "1"},
        // T-0035 (review F4): bounds are full const-exprs.
        StaticArrayElementCase{"ArrayBoundsFromANamedConst",
                               "const N = 3;\nvar A: array[0..N-1] of byte; M: Integer;\n"
                               "begin A[2] := 7; M := A[2]; end.",
                               2, ValueKind::Int, "7"},
        // T-0035 (review F3a/b, L-69): const folding as ReadConstant/PreCalc (uPSCompiler.pas:3325,
        // :13104-13118, :5720-5743). 65536*65536 wraps to 0 at s32; 4000000000 is u32 so C + 0
        // does not wrap; `-8 shr 1` is -(8 shr 1); `/` on integers is `div`; `>` folds to Boolean.
        StaticArrayElementCase{"ConstFoldsAtTheLeftOperandType",
                               "const C = 65536 * 65536;\nvar N: Int64;\nbegin N := C; end.", 1,
                               ValueKind::Int, "0"},
        StaticArrayElementCase{"ConstU32KeepsItsTypeInArithmetic",
                               "const C = 4000000000;\nvar N: Int64;\nbegin N := C + 0; end.", 1,
                               ValueKind::Int, "4000000000"},
        StaticArrayElementCase{"ConstUnaryMinusTakesAWholeTerm",
                               "const C = -8 shr 1;\nvar N: Integer;\nbegin N := C; end.", 1, ValueKind::Int,
                               "-4"},
        StaticArrayElementCase{"ConstIntegerPlusCharIsItsOrd",
                               "const C = 1 + 'a';\nvar N: Integer;\nbegin N := C; end.", 1, ValueKind::Int,
                               "98"},
        // T-0035 confirmation (F1d): Boolean-left folds on ordinals (uPSCompiler.pas:3343, :3522, :3708).
        StaticArrayElementCase{
            "ConstTruePlusOneIsTrue",
            "const C = True + 1;\nvar N: Integer;\nbegin if C then N := 1 else N := 2; end.", 1,
            ValueKind::Int, "1"},
        StaticArrayElementCase{
            "ConstTrueAndZeroIsFalse",
            "const C = True and 0;\nvar N: Integer;\nbegin if C then N := 1 else N := 2; end.", 1,
            ValueKind::Int, "2"},
        StaticArrayElementCase{
            "ConstFalseEqualsZeroIsTrue",
            "const C = False = 0;\nvar N: Integer;\nbegin if C then N := 1 else N := 2; end.", 1,
            ValueKind::Int, "1"},
        StaticArrayElementCase{"ConstSlashOnIntegersIsIntegerDivision",
                               "const C = 7 / 2;\nvar N: Integer;\nbegin N := C; end.", 1, ValueKind::Int,
                               "3"},
        StaticArrayElementCase{"ConstRelationalFoldsToBoolean",
                               "const C = 3 > 2;\nvar N: Integer;\nbegin if C then N := 1 else N := 2; end.",
                               1, ValueKind::Int, "1"}),
    staticArrayElementCaseName);

TEST(Interpreter, StaticArrayIndexOutOfRangeIsARuntimeError) {
    // belgeodesy.zmp-style bounds case (spec 3.6): uPSRuntime.pas:7127-7135 range-checks every
    // static-array index at run time - an out-of-range index ends the script in a runtime error,
    // never a silent Empty/zero read.
    Outcome o = run("var A: array[0..2] of byte; N: Integer;\nbegin N := A[5]; end.");
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_TRUE(o.runResult.error().what().contains(QStringLiteral("Static array index out of bounds")));
    // T-0035 (L-70): array[lo..lo-1] is a zero-length array - every access raises the same error.
    Outcome empty = run("var A: array[3..2] of byte; N: Integer;\nbegin N := A[3]; end.");
    ASSERT_FALSE(empty.runResult.has_value());
    EXPECT_TRUE(empty.runResult.error().what().contains(QStringLiteral("Length is 0")));
}

TEST(Interpreter, ConstDeclarationsFoldIntegerStringAndExpression) {
    // PD-103 repro, verbatim (docs/handoffs/T-0015D-review.md "Repro (reviewer)"): an integer
    // const, a string const, and an expression built from a const must all read their compile-
    // time-folded value, not the zero-initialised slot default that was the PD-103 bug (the port
    // produced "0||1" before the fix landed in this ticket).
    Outcome o = run("const X = 42; S = 'abc';\nvar A: Integer;\n"
                    "begin A := X; ResultURL := IntToStr(A)+'|'+S+'|'+IntToStr(X+1); end.");
    ASSERT_RAN_OK(o);
    EXPECT_EQ(o.vars.resultUrl, QByteArrayLiteral("42|abc|43"));
}

// --- host-object exceptions (uPSRuntime.pas:8285-8340: a Delphi exception from an external procedure
// becomes a script exception, text "Exception: <Message>") and the tile load date ---------------

namespace {
class ThrowingGlobal : public FakeScriptGlobal {
public:
    void setVar(int, const Value&) override { throw std::runtime_error("boom"); }
};

class ThrowingHost : public FakeScriptHost {
public:
    void writeLn(const QByteArray&) override { throw std::runtime_error("host boom"); }
};

class FixedDateTileCache : public FakeScriptTileCache {
public:
    [[nodiscard]] TileInfo read(int, int, int, const QByteArray&, bool) const override {
        TileInfo t;
        t.isExists = true;
        t.loadDate = QDateTime(QDate(2020, 6, 15), QTime(12, 0, 0));
        return t;
    }
};
} // namespace

TEST(Interpreter, HostExceptionIsCaughtByScriptExcept) {
    ScriptVars vars;
    vars.global = std::make_shared<ThrowingGlobal>();
    Outcome o = run(
        "var A: Integer;\nbegin A := 0; try Global.SetVar(1, 2); A := 1; except A := 99; end; end.", vars);
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).toInt(), 99);
}

TEST(Interpreter, HostExceptionRunsFinallyThenPropagates) {
    ScriptVars vars;
    auto global = std::make_shared<ThrowingGlobal>();
    vars.global = global;
    Outcome o = run("begin Global.Lock; try Global.SetVar(1, 2); finally Global.Unlock; end; end.", vars);
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_EQ(global->lockDepth(), 0);
}

TEST(Interpreter, HostFreeFunctionExceptionRunsFinally) {
    ScriptVars vars;
    auto global = std::make_shared<FakeScriptGlobal>();
    vars.global = global;
    Outcome o = run("begin Global.Lock; try WriteLn('x'); finally Global.Unlock; end; end.", vars,
                    std::make_unique<ThrowingHost>());
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().what(), QStringLiteral("host boom at line 1"));
    EXPECT_EQ(global->lockDepth(), 0);
}

TEST(Interpreter, UncaughtHostExceptionFailsTheRunWithoutThrowing) {
    ScriptVars vars;
    vars.global = std::make_shared<ThrowingGlobal>();
    Outcome o;
    EXPECT_NO_THROW(o = run("begin\nGlobal.SetVar(1, 2);\nend.", vars));
    ASSERT_FALSE(o.runResult.has_value());
    EXPECT_EQ(o.runResult.error().what(), QStringLiteral("boom at line 2"));
}

// u_PascalScriptTileCache.pas:159: DateTimeToUnix of a local TDateTime = the wall clock read as UTC,
// whatever the zone: 2020-06-15 12:00:00 -> 1592222400 even with TZ=America/New_York.
TEST(Interpreter, TileLoadDateIsLocalWallClockReadAsUtc) {
    const QByteArray oldTz = qgetenv("TZ");
    qputenv("TZ", "America/New_York");
    tzset();
    const bool zoneWorks =
        QDateTime(QDate(2020, 6, 15), QTime(12, 0)).toSecsSinceEpoch() == 1592222400 + 4 * 3600;
    ScriptVars vars;
    vars.tileCache = std::make_shared<FixedDateTileCache>();
    Outcome o = run("var T: TTileInfo;\nbegin T := TileCache.Read(1, 2, 3, '', False); end.", vars);
    if (oldTz.isNull()) {
        qunsetenv("TZ");
    } else {
        qputenv("TZ", oldTz);
    }
    tzset();
    if (!zoneWorks) {
        GTEST_SKIP() << "America/New_York zone data unavailable";
    }
    ASSERT_RAN_OK(o);
    EXPECT_EQ(*o.interpreter->slot(kV0).fields()[2].toInt(), 1592222400);
}
