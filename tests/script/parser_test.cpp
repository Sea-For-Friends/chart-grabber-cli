// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT yet (T-0013 slice A). docs/specs/script.md 3, 11.3 (parser half only -
// interpreter_test.cpp lands with T-0014).
#include "cg/script/lexer.h"
#include "cg/script/parser.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <memory>

using namespace cg::script;

namespace {

struct ParseOutcome {
    cg::core::Result<Program*> result;
    DiagnosticList diagnostics;
    std::unique_ptr<SymbolTable> symbols; // keeps the AST arena alive for the caller
};

ParseOutcome parseSource(const QByteArray& source) {
    ParseOutcome outcome;
    outcome.symbols = std::make_unique<SymbolTable>();
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize(outcome.diagnostics);
    Parser parser(std::move(tokens), *outcome.symbols);
    outcome.result = parser.parse(outcome.diagnostics);
    return outcome;
}

// F1 rework: returns the parsed Program* (or nullptr on failure), keeping `outcome` alive so the
// caller can walk the tree and query `outcome.symbols` for slot/name facts.
Program* mustParse(const QByteArray& source, ParseOutcome& outcome) {
    outcome = parseSource(source);
    return outcome.result.has_value() ? *outcome.result : nullptr;
}

::testing::AssertionResult parses(const QByteArray& source) {
    ParseOutcome outcome = parseSource(source);
    if (outcome.result.has_value()) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << outcome.result.error().what().toStdString();
}

} // namespace

#define EXPECT_PARSES(src) EXPECT_TRUE(parses(QByteArrayLiteral(src)))

// --- one test per grammar production (spec 3.1), ~27 constructs ---------------------------------

TEST(Parser, ConstSection) {
    EXPECT_PARSES("const A = 1;\nbegin end.");
}
TEST(Parser, VarSectionSingle) {
    EXPECT_PARSES("var A: Integer;\nbegin end.");
}
TEST(Parser, VarSectionList) {
    EXPECT_PARSES("var A, B, C: Integer;\nbegin end.");
}
TEST(Parser, MultipleVarSectionsInterleave) {
    // spec 3.3 S11: `Navionics/navionics.zmp` has const, const, function, function, var, begin.
    EXPECT_PARSES("const A = 1;\nconst B = 2;\nvar C: Integer;\nbegin end.");
}
TEST(Parser, FunctionDecl) {
    EXPECT_PARSES("function F(A: Integer): Integer;\nbegin Result := A; end;\nbegin end.");
}
TEST(Parser, ProcedureDecl) {
    EXPECT_PARSES("procedure P(A: Integer);\nbegin end;\nbegin end.");
}
TEST(Parser, ProcedureEmptyParams) {
    // spec 3.5 G5: `procedure GetMaxZoom();` (zoomify_usage_example.zmp).
    EXPECT_PARSES("procedure P();\nbegin end;\nbegin P(); end.");
}
TEST(Parser, ParamModesConstVarOut) {
    // spec 3.5 G5: `out` parameters (Google/GoogleSat.zmp's `_RequestVersion`).
    EXPECT_PARSES("function F(const A: Integer; var B: Integer; out C: Integer): Integer;\n"
                  "begin B := A; C := A; Result := A; end;\nbegin end.");
}
TEST(Parser, ArrayTypeBounded) {
    // `array[0..35] of char` (megafonpro.zmp).
    EXPECT_PARSES("var A: array[0..35] of Integer;\nbegin end.");
}
TEST(Parser, ArrayTypeOpen) {
    EXPECT_PARSES("var A: array of Integer;\nbegin end.");
}
TEST(Parser, VarOfRegisteredRecordType) {
    // Maps.by.zmp: `var TL, M: TDoublePoint;`.
    EXPECT_PARSES("var TL, M: TDoublePoint;\nbegin TL.X := 1.0; M.Y := 2.0; end.");
}
TEST(Parser, CompoundStmtNested) {
    EXPECT_PARSES("begin begin begin end end end.");
}
TEST(Parser, IfThen) {
    EXPECT_PARSES("begin if True then ResultURL := 'a'; end.");
}
TEST(Parser, IfThenElse) {
    EXPECT_PARSES("begin if True then ResultURL := 'a' else ResultURL := 'b'; end.");
}
TEST(Parser, ChainedElseIf) {
    EXPECT_PARSES("begin if True then ResultURL := 'a' else if False then ResultURL := 'b' else ResultURL := "
                  "'c'; end.");
}
TEST(Parser, CaseSingleLabel) {
    EXPECT_PARSES("var X: Integer;\nbegin X := 1; case X of 1: ResultURL := 'a'; end; end.");
}
TEST(Parser, CaseCommaList) {
    // spec 3.3 S4: `GGC/karta_ggc_250m.zmp` (`case r of 0,1,2`).
    EXPECT_PARSES("var X: Integer;\nbegin X := 1; case X of 0, 1, 2: ResultURL := 'a'; end; end.");
}
TEST(Parser, CaseRangeLabel) {
    // spec 3.5 G4: `Tatar.ru.zmp` (`case Y1 of 0..9: ... 10..99: ...`).
    EXPECT_PARSES("var X: Integer;\nbegin X := 1; case X of 0..9: ResultURL := 'a'; 10..99: ResultURL := "
                  "'b'; end; end.");
}
TEST(Parser, CaseElseBranch) {
    EXPECT_PARSES(
        "var X: Integer;\nbegin X := 1; case X of 1: ResultURL := 'a'; else ResultURL := 'b'; end; end.");
}
TEST(Parser, ForTo) {
    EXPECT_PARSES("var I: Integer;\nbegin for I := 1 to 10 do ResultURL := 'a'; end.");
}
TEST(Parser, ForDownto) {
    EXPECT_PARSES("var I: Integer;\nbegin for I := 10 downto 1 do ResultURL := 'a'; end.");
}
TEST(Parser, WhileDo) {
    EXPECT_PARSES("var I: Integer;\nbegin I := 0; while I < 10 do I := I + 1; end.");
}
TEST(Parser, RepeatUntilRunsAtLeastOnce) {
    EXPECT_PARSES("var I: Integer;\nbegin I := 0; repeat I := I + 1; until I = 10; end.");
}
TEST(Parser, TryFinallyAlwaysRuns) {
    // spec 3.3 S9: `Navionics/*`, `Google/GoogleSat.zmp`, `Yandex/*` use `finally`.
    EXPECT_PARSES("begin try ResultURL := 'a'; finally ResultURL := 'b'; end; end.");
}
TEST(Parser, TryExceptSwallows) {
    EXPECT_PARSES("begin try ResultURL := 'a'; except ResultURL := 'b'; end; end.");
}
TEST(Parser, WithStmtOverRegisteredType) {
    // spec 3.5: kept in the grammar though the corpus never uses it.
    EXPECT_PARSES("var P: TDoublePoint;\nbegin with P do begin end; end.");
}
TEST(Parser, WithStmtResolvesFieldNamesAgainstTheSixBuiltInTypes) {
    // T-0014B: `X` inside the `with` body is `P`'s field, not an unknown identifier.
    EXPECT_PARSES("var P: TPoint;\nbegin with P do X := 1; end.");
}
TEST(Parser, WithStmtOverNonRecordVariableIsAParseTimeReject) {
    // spec 3.5: a `with` designator that is not one of the 6 built-in record types stays a
    // parse-time reject, unchanged from T-0013 (here, a plain Integer local).
    ParseOutcome outcome = parseSource("var N: Integer;\nbegin with N do begin end; end.");
    ASSERT_FALSE(outcome.result.has_value());
    bool found = false;
    for (const Diagnostic& diagnostic : outcome.diagnostics) {
        if (diagnostic.message.contains(
                QStringLiteral("'with' requires a variable of a built-in record type"))) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
TEST(Parser, ExitBreakContinue) {
    EXPECT_PARSES(
        "var I: Integer;\nbegin for I := 1 to 10 do begin if I = 5 then break; if I = 3 then continue; end; "
        "exit; end.");
}
TEST(Parser, AssignTargetsVarFieldAndStringIndex) {
    // spec 3.3 S14: `GetURLBase[8] := chr(...)` even on a bound variable.
    EXPECT_PARSES("var P: TDoublePoint;\nbegin P.X := 1.0; GetURLBase[1] := 'x'; end.");
}
TEST(Parser, CallWithAndWithoutParens) {
    // spec 3.3 S10: `GetMaxZoom();` and `InitScriptBuffer;` both occur.
    EXPECT_PARSES("procedure P();\nbegin end;\nbegin P(); P; end.");
}
TEST(Parser, LiteralFormsIntHexRealStringBoolNil) {
    EXPECT_PARSES("var X: Integer; B: Boolean;\n"
                  "begin X := $30; X := 5; X := 5; B := True; B := False; "
                  "if DefProjConverter = nil then B := False; ResultURL := 'lit'; end.");
}
TEST(Parser, EmptyStatementsAndTrailingSemicolon) {
    // spec 3.3 S1: `eAtlas.zmp` ends a chained `if` with `then mn:='Yaroslavl' ;` before `end`.
    EXPECT_PARSES("begin ;; if True then ResultURL := 'a' ; end.");
}
TEST(Parser, RoutineShadowsRegisteredFunction) {
    // spec 2.8: `_broken_maps/Local/MAmet.zmp` declares its own `Exp`/`ArcTan`.
    EXPECT_PARSES("function Exp(X: Extended): Extended;\nbegin Result := X; end;\n"
                  "begin ResultURL := RoundEx(Exp(1.0), 3); end.");
}
TEST(Parser, RecursionIsLegal) {
    // spec 3.3 S13: `MAmet.zmp`'s `ArcTan` calls itself.
    EXPECT_PARSES(
        "function F(X: Integer): Integer;\nbegin if X <= 0 then Result := 0 else Result := F(X - 1); end;\n"
        "begin end.");
}
TEST(Parser, G6ArrayOrSetLiteral) {
    // spec 3.1 G6 (F2/D-3): `[e1, e2, ...]` open-array/set-constructor literal.
    EXPECT_PARSES("begin ResultURL := Format('%d,%d', [1, 2]); end.");
    EXPECT_PARSES("begin ResultURL := Format('%d', []); end.");
}

TEST(Parser, StructuralReplaceFlagsResolveToTheirOrdinal) {
    // spec 5.2 TReplaceFlag (F2): `rfReplaceAll`/`rfIgnoreCase` are not callables (not in
    // kExternals) - they must resolve, inside a G6 set literal, to their Delphi ordinal (0/1) so
    // `StringReplace(s, a, b, [rfReplaceAll])` (corpus-shaped call) is not a false "unknown
    // identifier".
    ParseOutcome outcome;
    Program* program = mustParse(
        "begin ResultURL := StringReplace(ResultURL, 'a', 'b', [rfReplaceAll, rfIgnoreCase]); end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Call);
    auto* call = static_cast<Call*>(assign->value);
    ASSERT_EQ(call->args.size(), 4u);
    ASSERT_EQ(call->args[3]->kind, NodeKind::ArrayLit);
    auto* set = static_cast<ArrayLit*>(call->args[3]);
    ASSERT_EQ(set->elements.size(), 2u);
    ASSERT_EQ(set->elements[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(set->elements[0])->value, 0); // rfReplaceAll
    ASSERT_EQ(set->elements[1]->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(set->elements[1])->value, 1); // rfIgnoreCase
}

TEST(Parser, InterfaceMethodCallOnBoundVariable) {
    // Maps.by.zmp: `DefProjConverter.LonLat2XY(TL)`.
    EXPECT_PARSES("var TL: TDoublePoint;\nbegin if Assigned(DefProjConverter) then TL := "
                  "DefProjConverter.LonLat2XY(TL); end.");
}

// --- F1 rework (2026-09-18): structural AST assertions ------------------------------------------
// Every test above only proves the source *parses*; none inspects the returned tree, so a
// mis-wired AST (wrong BinaryOp, swapped precedence levels, dropped downto/range/arg, a
// miscomputed Call::callee) stayed invisible (review F1). These pin the shape.

TEST(Parser, StructuralPrecedenceAdditiveAndOrVsMultiplicative) {
    // spec 3.2: `and` binds like `*`/`div` (parseTerm), `+`/`or` bind one level looser
    // (parseSimpleExpr). A swap of the two levels (review F1's named regression) would produce
    // Or(And(Add(1,2),3),4) instead of the correct Or(Add(1,And(2,3)),4).
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin X := 1 + 2 and 3 or 4; end.", outcome);
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->main->kind, NodeKind::Compound);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* orNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(orNode->op, BinaryOp::Or);
    ASSERT_EQ(orNode->rhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(orNode->rhs)->value, 4);
    ASSERT_EQ(orNode->lhs->kind, NodeKind::Binary);
    auto* addNode = static_cast<Binary*>(orNode->lhs);
    EXPECT_EQ(addNode->op, BinaryOp::Add);
    ASSERT_EQ(addNode->lhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(addNode->lhs)->value, 1);
    ASSERT_EQ(addNode->rhs->kind, NodeKind::Binary);
    auto* andNode = static_cast<Binary*>(addNode->rhs);
    EXPECT_EQ(andNode->op, BinaryOp::And);
    ASSERT_EQ(andNode->lhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(andNode->lhs)->value, 2);
    ASSERT_EQ(andNode->rhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(andNode->rhs)->value, 3);
}

TEST(Parser, StructuralRelationalIsLoosestAndNonChaining) {
    // spec 3.2 level 4: relational wraps a whole additive expression on each side and does not
    // itself chain - `1 + 2 > 3` must be Gt(Add(1,2), 3), not Add(1, Gt(2,3)).
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin X := 1 + 2 > 3; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* gtNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(gtNode->op, BinaryOp::Gt);
    ASSERT_EQ(gtNode->lhs->kind, NodeKind::Binary);
    EXPECT_EQ(static_cast<Binary*>(gtNode->lhs)->op, BinaryOp::Add);
    ASSERT_EQ(gtNode->rhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(gtNode->rhs)->value, 3);
}

TEST(Parser, StructuralUnaryNotAndMinus) {
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin if not (X = 1) then X := -X; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* ifNode = static_cast<If*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(ifNode->cond->kind, NodeKind::Unary);
    auto* notNode = static_cast<Unary*>(ifNode->cond);
    EXPECT_EQ(notNode->op, UnaryOp::Not);
    ASSERT_EQ(notNode->operand->kind, NodeKind::Binary);
    EXPECT_EQ(static_cast<Binary*>(notNode->operand)->op, BinaryOp::Eq);
    ASSERT_EQ(ifNode->thenBranch->kind, NodeKind::Assign);
    auto* assign = static_cast<Assign*>(ifNode->thenBranch);
    ASSERT_EQ(assign->value->kind, NodeKind::Unary);
    auto* minusNode = static_cast<Unary*>(assign->value);
    EXPECT_EQ(minusNode->op, UnaryOp::Minus);
    EXPECT_EQ(minusNode->operand->kind, NodeKind::VarRef);
}

TEST(Parser, StructuralLeftAssociativity) {
    // `10 - 3 - 2` must be Sub(Sub(10,3),2) (left-assoc, value 5), never Sub(10,Sub(3,2)) (value 9).
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin X := 10 - 3 - 2; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* outer = static_cast<Binary*>(assign->value);
    EXPECT_EQ(outer->op, BinaryOp::Sub);
    ASSERT_EQ(outer->rhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(outer->rhs)->value, 2);
    ASSERT_EQ(outer->lhs->kind, NodeKind::Binary);
    auto* inner = static_cast<Binary*>(outer->lhs);
    EXPECT_EQ(inner->op, BinaryOp::Sub);
    EXPECT_EQ(static_cast<IntLit*>(inner->lhs)->value, 10);
    EXPECT_EQ(static_cast<IntLit*>(inner->rhs)->value, 3);
}

TEST(Parser, StructuralDivModShlShrProduceDistinctBinaryOps) {
    // Guards `parseTerm` mapping every one of these to its own BinaryOp (review F1's named
    // regression: `div` silently landing on RealDiv).
    ParseOutcome outcome;
    Program* program = mustParse(
        "var X: Integer;\nbegin X := 7 div 2; X := 7 mod 2; X := 7 shl 2; X := 7 shr 2; X := 7 / 2; end.",
        outcome);
    ASSERT_NE(program, nullptr);
    auto& stmts = static_cast<Compound*>(program->main)->stmts;
    ASSERT_EQ(stmts.size(), 5u);
    static constexpr BinaryOp kExpected[] = {BinaryOp::IntDiv, BinaryOp::Mod, BinaryOp::Shl, BinaryOp::Shr,
                                             BinaryOp::RealDiv};
    for (std::size_t i = 0; i < 5; ++i) {
        auto* assign = static_cast<Assign*>(stmts.at(i));
        ASSERT_EQ(assign->value->kind, NodeKind::Binary) << i;
        EXPECT_EQ(static_cast<Binary*>(assign->value)->op, kExpected[i]) << i;
    }
}

TEST(Parser, StructuralForDowntoFlagAndSlot) {
    ParseOutcome outcome;
    Program* program =
        mustParse("var I: Integer;\nbegin for I := 10 downto 1 do ResultURL := 'a'; end.", outcome);
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(static_cast<Compound*>(program->main)->stmts.at(0)->kind, NodeKind::ForTo);
    auto* forNode = static_cast<ForTo*>(static_cast<Compound*>(program->main)->stmts.at(0));
    EXPECT_TRUE(forNode->downto);
    EXPECT_EQ(forNode->slot, outcome.symbols->resolve(QByteArrayLiteral("I")).slot);
    ASSERT_EQ(forNode->from->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(forNode->from)->value, 10);
    ASSERT_EQ(forNode->to->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(forNode->to)->value, 1);
}

TEST(Parser, StructuralForToClearsDowntoFlag) {
    ParseOutcome outcome;
    Program* program =
        mustParse("var I: Integer;\nbegin for I := 1 to 10 do ResultURL := 'a'; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* forNode = static_cast<ForTo*>(static_cast<Compound*>(program->main)->stmts.at(0));
    EXPECT_FALSE(forNode->downto);
}

TEST(Parser, StructuralCaseRangeAndSingleLabels) {
    ParseOutcome outcome;
    Program* program = mustParse(
        "var X: Integer;\nbegin case X of 1: ResultURL := 'a'; 2..5: ResultURL := 'b'; end; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* caseNode = static_cast<Case*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(caseNode->arms.size(), 2u);
    ASSERT_EQ(caseNode->arms[0].labels.size(), 1u);
    EXPECT_EQ(caseNode->arms[0].labels[0].lo, 1);
    EXPECT_EQ(caseNode->arms[0].labels[0].hi, 1); // single label: hi == lo
    ASSERT_EQ(caseNode->arms[1].labels.size(), 1u);
    EXPECT_EQ(caseNode->arms[1].labels[0].lo, 2);
    EXPECT_EQ(caseNode->arms[1].labels[0].hi, 5); // range: hi must survive, not collapse to lo
}

TEST(Parser, StructuralAssignSlotMatchesDeclaredVariable) {
    // Guards "Assign::slot taken from the wrong scope" (review F1).
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin X := 5; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    EXPECT_EQ(assign->slot, outcome.symbols->resolve(QByteArrayLiteral("X")).slot);
}

TEST(Parser, StructuralCallCalleeAndArgOrderUserRoutine) {
    // Guards "Call::callee off by one against kExternals" / swapped argument order.
    ParseOutcome outcome;
    Program* program = mustParse("function F(A: Integer; B: Integer): Integer;\nbegin Result := A; end;\n"
                                 "var X: Integer;\nbegin X := F(10, 20); end.",
                                 outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Call);
    auto* call = static_cast<Call*>(assign->value);
    EXPECT_EQ(call->callKind, CallKind::UserRoutine);
    EXPECT_EQ(call->callee, outcome.symbols->resolve(QByteArrayLiteral("F")).slot);
    ASSERT_EQ(call->args.size(), 2u);
    ASSERT_EQ(call->args[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(call->args[0])->value, 10); // order, not just count
    ASSERT_EQ(call->args[1]->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(call->args[1])->value, 20);
}

TEST(Parser, StructuralCallCalleeIndexesExternalSymbolsInOrder) {
    // `Call::callee` for CallKind::Builtin/Registered indexes SymbolTable::externalSymbols() -
    // pin that it is the *matching* index, not just any in-range value.
    ParseOutcome outcome;
    Program* program = mustParse("begin ResultURL := IntToStr(5); end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Call);
    auto* call = static_cast<Call*>(assign->value);
    EXPECT_EQ(call->callKind, CallKind::Builtin);
    const auto externals = SymbolTable::externalSymbols();
    ASSERT_GE(call->callee, 0);
    ASSERT_LT(static_cast<std::size_t>(call->callee), externals.size());
    EXPECT_STRCASEEQ(externals[static_cast<std::size_t>(call->callee)].name, "IntToStr");
}

TEST(Parser, StructuralCallCalleeEncodesMethodSlotAndIndex) {
    // `Call::callee = boundSlot * kMethodSlotStride + methodIndex` for CallKind::Method.
    ParseOutcome outcome;
    Program* program = mustParse("var TL: TDoublePoint;\nbegin if Assigned(DefProjConverter) then TL := "
                                 "DefProjConverter.LonLat2XY(TL); end.",
                                 outcome);
    ASSERT_NE(program, nullptr);
    auto* ifNode = static_cast<If*>(static_cast<Compound*>(program->main)->stmts.at(0));
    auto* assign = static_cast<Assign*>(ifNode->thenBranch);
    ASSERT_EQ(assign->value->kind, NodeKind::Call);
    auto* call = static_cast<Call*>(assign->value);
    EXPECT_EQ(call->callKind, CallKind::Method);
    const int boundSlot = outcome.symbols->resolve(QByteArrayLiteral("DefProjConverter")).slot;
    EXPECT_EQ(call->callee / kMethodSlotStride, boundSlot);
    const InterfaceType* iface = SymbolTable::interfaceForBoundSlot(boundSlot);
    ASSERT_NE(iface, nullptr);
    int expectedIndex = -1;
    for (std::size_t i = 0; i < iface->methods.size(); ++i) {
        if (QByteArray(iface->methods[i].name).compare("LonLat2XY", Qt::CaseInsensitive) == 0) {
            expectedIndex = static_cast<int>(i);
        }
    }
    ASSERT_GE(expectedIndex, 0);
    EXPECT_EQ(call->callee % kMethodSlotStride, expectedIndex);
}

// --- T-0014 acceptance item #0 (tech-lead-rulings.md 2026-09-18 T-0013 escalation): dedicated
// Mul/not/and/or precedence and F4 field-on-unknown-base coverage, before any interpreter work. ---

TEST(Parser, StructuralMulBindsTighterThanAdd) {
    // `a*b+c` must be Add(Mul(a,b), c), never Mul(a, Add(b,c)).
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin X := 2 * 3 + 4; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* addNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(addNode->op, BinaryOp::Add);
    ASSERT_EQ(addNode->rhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(addNode->rhs)->value, 4);
    ASSERT_EQ(addNode->lhs->kind, NodeKind::Binary);
    auto* mulNode = static_cast<Binary*>(addNode->lhs);
    EXPECT_EQ(mulNode->op, BinaryOp::Mul);
    EXPECT_EQ(static_cast<IntLit*>(mulNode->lhs)->value, 2);
    EXPECT_EQ(static_cast<IntLit*>(mulNode->rhs)->value, 3);
}

TEST(Parser, StructuralAddThenMulBindsTighterThanAdd) {
    // `a+b*c` must be Add(a, Mul(b,c)), never Mul(Add(a,b), c).
    ParseOutcome outcome;
    Program* program = mustParse("var X: Integer;\nbegin X := 2 + 3 * 4; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* addNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(addNode->op, BinaryOp::Add);
    ASSERT_EQ(addNode->lhs->kind, NodeKind::IntLit);
    EXPECT_EQ(static_cast<IntLit*>(addNode->lhs)->value, 2);
    ASSERT_EQ(addNode->rhs->kind, NodeKind::Binary);
    auto* mulNode = static_cast<Binary*>(addNode->rhs);
    EXPECT_EQ(mulNode->op, BinaryOp::Mul);
    EXPECT_EQ(static_cast<IntLit*>(mulNode->lhs)->value, 3);
    EXPECT_EQ(static_cast<IntLit*>(mulNode->rhs)->value, 4);
}

TEST(Parser, StructuralUnparenthesizedNotBindsTighterThanAnd) {
    // `not a and b` must be And(Not(a), b) - `not` is a parseFactor unary, `and` is parseTerm.
    ParseOutcome outcome;
    Program* program = mustParse("var A, B: Boolean;\nbegin A := not A and B; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* andNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(andNode->op, BinaryOp::And);
    ASSERT_EQ(andNode->lhs->kind, NodeKind::Unary);
    EXPECT_EQ(static_cast<Unary*>(andNode->lhs)->op, UnaryOp::Not);
    EXPECT_EQ(andNode->rhs->kind, NodeKind::VarRef);
}

TEST(Parser, StructuralUnparenthesizedAndBindsTighterThanOr) {
    // `a and b or c` must be Or(And(a,b), c) - `and` is parseTerm, `or` is parseSimpleExpr.
    ParseOutcome outcome;
    Program* program = mustParse("var A, B, C: Boolean;\nbegin A := A and B or C; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* orNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(orNode->op, BinaryOp::Or);
    EXPECT_EQ(orNode->rhs->kind, NodeKind::VarRef);
    ASSERT_EQ(orNode->lhs->kind, NodeKind::Binary);
    EXPECT_EQ(static_cast<Binary*>(orNode->lhs)->op, BinaryOp::And);
}

TEST(Parser, StructuralNotBindsTighterThanRelational) {
    // `not a = b` must be Eq(Not(a), b) - `not` never reaches across a relational operator.
    ParseOutcome outcome;
    Program* program = mustParse("var A, B: Boolean;\nbegin A := not A = B; end.", outcome);
    ASSERT_NE(program, nullptr);
    auto* assign = static_cast<Assign*>(static_cast<Compound*>(program->main)->stmts.at(0));
    ASSERT_EQ(assign->value->kind, NodeKind::Binary);
    auto* eqNode = static_cast<Binary*>(assign->value);
    EXPECT_EQ(eqNode->op, BinaryOp::Eq);
    ASSERT_EQ(eqNode->lhs->kind, NodeKind::Unary);
    EXPECT_EQ(static_cast<Unary*>(eqNode->lhs)->op, UnaryOp::Not);
    EXPECT_EQ(eqNode->rhs->kind, NodeKind::VarRef);
}

TEST(Parser, RejectsFieldAccessOnStaticallyUnknownBase) {
    // F4 (T-0013 review escalation): `.field` on a base with no statically-known RegisteredType
    // (here a plain Integer local) is a compile error, not a silently wrong fieldIndex == -1.
    ParseOutcome outcome;
    outcome = parseSource("var X: Integer;\nbegin X := X.foo; end.");
    ASSERT_FALSE(outcome.result.has_value());
    bool found = false;
    for (const Diagnostic& diagnostic : outcome.diagnostics) {
        if (diagnostic.message.contains(QStringLiteral("cannot access 'foo'"))) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// --- the 12 rejections of spec 3.4, each asserting the exact message -----------------------------

::testing::AssertionResult rejectsWith(const QByteArray& source, const QString& expectedSubstring) {
    ParseOutcome outcome = parseSource(source);
    if (outcome.result.has_value()) {
        return ::testing::AssertionFailure() << "expected a diagnostic but parsing succeeded";
    }
    for (const Diagnostic& diagnostic : outcome.diagnostics) {
        if (diagnostic.message.contains(expectedSubstring)) {
            return ::testing::AssertionSuccess();
        }
    }
    return ::testing::AssertionFailure() << outcome.result.error().what().toStdString();
}

TEST(Parser, RejectsTypeSection) {
    EXPECT_TRUE(rejectsWith("type TFoo = Integer;\nbegin end.",
                            QStringLiteral("'type' declarations are not supported")));
}
TEST(Parser, RejectsUserDefinedRecordType) {
    EXPECT_TRUE(rejectsWith("var R: record A: Integer; end;\nbegin end.",
                            QStringLiteral("user-defined record types are not supported")));
}
TEST(Parser, RejectsUsesClause) {
    EXPECT_TRUE(
        rejectsWith("uses SysUtils;\nbegin end.", QStringLiteral("unit and uses clauses are not supported")));
}
TEST(Parser, RejectsGoto) {
    EXPECT_TRUE(rejectsWith("begin goto lbl; end.", QStringLiteral("goto is not supported")));
}
TEST(Parser, RejectsPointerOperators) {
    EXPECT_TRUE(rejectsWith("var X: Integer;\nbegin X := @X; end.",
                            QStringLiteral("pointer operators are not supported")));
}
TEST(Parser, RejectsClassesAndObjects) {
    EXPECT_TRUE(rejectsWith("constructor Create;\nbegin end.",
                            QStringLiteral("classes and objects are not supported")));
}
TEST(Parser, RejectsExternalDeclaration) {
    EXPECT_TRUE(rejectsWith("procedure P; external 'libfoo.dll';\nbegin end.",
                            QStringLiteral("external declarations are not supported")));
}
TEST(Parser, RejectsSetTypes) {
    EXPECT_TRUE(
        rejectsWith("var F: set of Byte;\nbegin end.", QStringLiteral("set types are not supported")));
}
TEST(Parser, RejectsInlineAssembly) {
    EXPECT_TRUE(rejectsWith("begin asm; end.", QStringLiteral("inline assembly is not supported")));
}
TEST(Parser, RejectsRaise) {
    EXPECT_TRUE(rejectsWith("begin try ResultURL := 'a'; except raise; end; end.",
                            QStringLiteral("raise is not supported")));
}
TEST(Parser, RejectsUnknownIdentifier) {
    EXPECT_TRUE(rejectsWith("begin ResultURL := NoSuchThing; end.",
                            QStringLiteral("unknown identifier 'NoSuchThing'")));
}
TEST(Parser, RejectsWrongArgumentCount) {
    EXPECT_TRUE(rejectsWith("procedure P(A: Integer);\nbegin end;\nbegin P(1, 2); end.",
                            QStringLiteral("expects 1 argument(s), got 2")));
}

// --- T-0035: what the engine rejects at compile time (FEAT-SCRIPT-001). One row per construct; an
// empty `expected` means the source must compile. Texts and citations: docs/handoffs/T-0035.md. -----

struct CompileCheckCase {
    const char* name;
    const char* source;
    const char* expected;
};

class Parser_CompileTimeChecks : public ::testing::TestWithParam<CompileCheckCase> {};

TEST_P(Parser_CompileTimeChecks, MatchesTheEngine) {
    const auto& c = GetParam();
    if (*c.expected == '\0') {
        EXPECT_TRUE(parses(c.source));
    } else {
        EXPECT_TRUE(rejectsWith(c.source, QString::fromLatin1(c.expected)));
    }
}

std::string compileCheckName(const ::testing::TestParamInfo<CompileCheckCase>& info) {
    return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    T0035, Parser_CompileTimeChecks,
    ::testing::Values(
        // L-68: DoConstBlock has no colon branch (uPSCompiler.pas:12162-12167).
        CompileCheckCase{"TypedConstIsRejected", "const A: Integer = 1;\nbegin end.", "is ('=') expected"},
        // :3271-3272 IsCompatibleType - a Real is not compatible with an integer target.
        CompileCheckCase{"RealLiteralToInteger", "var I: Integer;\nbegin I := 1.5; end.", "Type mismatch"},
        CompileCheckCase{"RealCallToInteger", "var I: Integer;\nbegin I := Sqrt(2); end.", "Type mismatch"},
        CompileCheckCase{"RealExprToByteElement",
                         "var D: Double; A: array[0..1] of byte;\nbegin A[0] := D * 2; end.",
                         "Type mismatch"},
        CompileCheckCase{"IntegerToDoubleIsFine", "var D: Double;\nbegin D := 1; D := Round(2.5); end.", ""},
        // :5473 IsVarInCompatible - a var parameter takes its own type exactly.
        CompileCheckCase{"RealToIntegerVarParam",
                         "procedure P(var A: Integer);\nbegin end;\nvar D: Double;\nbegin P(D); end.",
                         "Type mismatch"},
        CompileCheckCase{"ByteToIntegerVarParam",
                         "procedure P(var A: Integer);\nbegin end;\nvar B: Byte;\nbegin P(B); end.",
                         "Type mismatch"},
        // Forbidden direction: accepted by the engine, so accepted here (uPSCompiler.pas:8542-8554,
        // 2913-2917, 3358-3361).
        CompileCheckCase{
            "IntegerTargetsTheEngineAccepts",
            "var I: Integer; V: Variant; D: Double;\nfunction F: Double;\nbegin Result := 1.5; end;\n"
            "begin I := 7 / 2; I := Trunc(D) * 2; I := V; end.",
            ""},
        CompileCheckCase{"ConstCharAndBooleanOperandsFold",
                         "const A = 1 + 'a'; B = 'a' + 1; C = 1 + True; D = rfReplaceAll;\nbegin end.", ""},
        // :13064-13086 GetConstantIdentifier searches constants only.
        CompileCheckCase{"ConstExprNamesAVariable", "var V: Integer;\nconst C = V + 1;\nbegin end.",
                         "Unknown identifier ''"},
        CompileCheckCase{"ConstExprCallsAFunction", "const C = Random(3);\nbegin end.",
                         "Unknown identifier ''"},
        CompileCheckCase{"ConstExprUnaryPlusIsASyntaxError", "const C = +1;\nbegin end.", "Syntax error"},
        // L-67: the target of the assignment is not a variable (:10653-10658).
        CompileCheckCase{"AssignToConst", "const C = 1;\nbegin C := 2; end.", "Internal error (20)"},
        // :5438 ValidateParameters - an in/out argument must be a variable.
        CompileCheckCase{"ConstToInc", "const C = 1;\nbegin Inc(C); end.", "Variable Expected"},
        CompileCheckCase{"ConstToVarParam",
                         "procedure P(var A: Integer);\nbegin end;\nconst C = 1;\nbegin P(C); end.",
                         "Variable Expected"},
        // PreCalc (:3325-3770): folding failures are "Type mismatch" / "Divide by Zero".
        CompileCheckCase{"ConstBooleanTimesIntegerIsRejected", "const C = True * 2;\nbegin end.",
                         "Type mismatch"},
        CompileCheckCase{"ConstDivByZero", "const C = 1 div 0;\nbegin end.", "Divide by Zero"},
        CompileCheckCase{"ConstCharLessThanChar", "const C = 'a' < 'b';\nbegin end.", "Type mismatch"},
        CompileCheckCase{"ConstRelationalFolds", "const C = 3 > 2; D = 'a' = 'a';\nbegin end.", ""},
        // L-70 and :4143-4205: bounds are full const-exprs of integer type; length in [0, MaxInt/4].
        CompileCheckCase{"ArrayHiBelowLoMinusOne", "var A: array[5..3] of Integer;\nbegin end.",
                         "Type mismatch"},
        CompileCheckCase{"ArrayEmptyIsAccepted", "var A: array[3..2] of Integer;\nbegin end.", ""},
        CompileCheckCase{"ArrayCharBound", "var A: array['a'..'z'] of Integer;\nbegin end.", "Type mismatch"},
        CompileCheckCase{"ArrayBoundsAreConstExprs",
                         "const N = 4;\nvar A: array[-N+1 .. N*2-1] of Integer;\nbegin end.", ""}),
    compileCheckName);

// --- error recovery (spec 2.5 / 11.3) -------------------------------------------------------------

TEST(Parser, ReportsAllErrorsNotJustTheFirst) {
    ParseOutcome outcome = parseSource("begin ResultURL := NoSuchThing; PostData := AlsoUnknown; end.");
    ASSERT_FALSE(outcome.result.has_value());
    int unknownCount = 0;
    for (const Diagnostic& diagnostic : outcome.diagnostics) {
        if (diagnostic.message.contains(QStringLiteral("unknown identifier"))) {
            ++unknownCount;
        }
    }
    EXPECT_EQ(unknownCount, 2);
}

TEST(Parser, RecoversAtSemicolon) {
    // The first statement is broken (nothing follows ':='); recovery skips to the next ';' so the
    // second, well-formed statement parses cleanly instead of cascading into further diagnostics.
    ParseOutcome outcome = parseSource("begin ResultURL := ; ResultURL := 'ok'; end.");
    ASSERT_FALSE(outcome.result.has_value()); // still a compile failure - one bad statement is enough
    int diagnosticCount = 0;
    for (const Diagnostic& diagnostic : outcome.diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            ++diagnosticCount;
        }
    }
    // Exactly the one error from the broken statement - the recovered, well-formed second
    // statement must not add a spurious diagnostic of its own.
    EXPECT_EQ(diagnosticCount, 1);
}
