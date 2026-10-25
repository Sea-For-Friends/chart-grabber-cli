// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PSPascalCompilerEx.pas (RemObjects grammar -> the v1 EBNF of spec
// section 3), Src/PascalScript/u_PascalScriptTypes.pas (the 6 registered types + TTileInfo,
// consulted here for field layout so `p.x` resolves to a slot at compile time, and (T-0014B) for
// each slot's primitive `DeclaredType` (spec 2.6) so the interpreter can range-wrap/round on
// assignment (spec 4.1 N1/N2) without re-deriving it from the AST.
//
// Recursive-descent parser producing a `Program*` plus every diagnostic it found in one pass
// (legacy aggregated every RemObjects message into one exception - ADR-0003's error table keeps
// that behaviour). Compile-time name resolution (local scope, then the fixed SAS API namespace)
// happens inline, in the shadowing order the corpus proves (spec 2.8): a routine declared in the
// script, then a variable/parameter in scope, then the fixed set of 26 bound variables, then a
// registered function or intrinsic (spec 5.1/5.3/5.4). Only the *names* and arities of the fixed
// namespace are needed here; their runtime behaviour is the interpreter's job (T-0014/T-0015) -
// see the handoff for how `Call::callee` encodes a reference into that fixed namespace so the
// interpreter can decode it without re-parsing.

#include "cg/core/result.h"
#include "cg/script/ast.h"
#include "cg/script/diagnostic.h"
#include "cg/script/token.h"
#include "cg/script/value.h" // DeclaredType (spec 2.6) - T-0014B

#include <QByteArray>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cg::script {

// A registered free function (spec 5.3) or an interpreter intrinsic (spec 5.4): callable, never
// assignable. `nonDeterministic` flags the three of the eight names compiled_script.h's
// isDeterministic() must track that are calls (Random, GetUnixTime, SaveToLocalFile); the other
// five are bound variables and are flagged where they are declared, see `SymbolTable`.
enum class ExternalKind : std::uint8_t { Function, Intrinsic };

struct ExternalSymbol {
    const char* name = "";
    ExternalKind kind = ExternalKind::Function;
    std::uint8_t minArgs = 0;
    std::uint8_t maxArgs = 0; // 0xFF marks an open/variadic argument list (Format, FormatEx)
    bool isProcedure = false;
    bool nonDeterministic = false;
};

// One of the 6 registered record types (spec 5.2): field order is load-bearing (PascalScript maps
// a record by layout, and so does `Value::fromRecord`, T-0014/T-0014B). `fields` views a static
// constexpr array (parser.cpp) rather than owning a `std::vector`, so the whole table of these can
// be `constexpr` - no static-initialization-order or throwing-static-init concerns. `kind` is this
// type's own `DeclaredType` enumerator (T-0014B), so `SymbolTable::setSlotType` has one thing to
// stash per slot rather than two.
struct RegisteredType {
    const char* name = "";
    std::span<const char* const> fields;
    DeclaredType kind = DeclaredType::Unknown;
};

// One method of one of the 7 script-visible interfaces (spec 2.7/5.6).
struct InterfaceMethod {
    const char* name = "";
    std::uint8_t argCount = 0;
};
struct InterfaceType {
    const char* name = "";
    std::span<const InterfaceMethod> methods;
};

// Number of interface-method slots reserved per ScriptVars bound variable when encoding a
// `Call::callee` for `CallKind::Method` (ast.h): `callee = boundSlot * kMethodSlotStride +
// methodIndex`. 16 comfortably covers every interface's method count (the largest,
// ICoordConverter, has 8). `SymbolTable::interfaceForBoundSlot` decodes the interface half.
inline constexpr int kMethodSlotStride = 16;

// Compile-time symbol table: local scopes (routines/vars/params), the 26 bound variables
// pre-declared as slots 0..25 (spec 5.1's order) and the fixed external-function namespace. Also
// owns the AST `Arena` so `CompiledScript` frees the whole tree in one call by destroying the
// `SymbolTable` (see compiled_script.h).
class SymbolTable {
public:
    // The 26 bound variables (spec 5.1) always occupy slots [0, kBoundVariableCount) - see the
    // constructor. T-0014's interpreter uses this to recognise a VarRef into ScriptVars/the host
    // interfaces (unimplemented this slice - no IScriptHost/ScriptVars binding yet) and fail with
    // a diagnostic instead of silently treating it as an ordinary zero-initialised local.
    static constexpr int kBoundVariableCount = 26;

    SymbolTable();

    template <class T, class... Args> T* make(Args&&... args) {
        return arena_.make<T>(std::forward<Args>(args)...);
    }

    void pushScope();
    void popScope();

    // Declares a name in the innermost scope. Returns the new slot, or -1 when `name` is already
    // declared in that same scope (duplicate identifier).
    int declareLocal(const QByteArray& name);
    int declareRoutine(const QByteArray& name);
    // `type` is non-null only for one of the 6 registered record types (`.field` resolution,
    // T-0013); `declared` is always set (T-0014B), including `DeclaredType::Unknown` for a slot
    // whose type this parser cannot resolve - see `declaredTypes()`.
    void setSlotType(int slot, const RegisteredType* type, DeclaredType declared);
    // T-0032 (PD-1): the element type + declared bounds of a `DeclaredType::StaticArray` slot -
    // set right after `setSlotType` in `parseVarSection`. `arrayInfoForSlot` is nullptr for every
    // other slot.
    void setArrayInfo(int slot, ArrayInfo info);
    [[nodiscard]] const ArrayInfo* arrayInfoForSlot(int slot) const;
    // T-0032 (PD-103): the compile-time-folded `Value` of a `const`-declared slot (grammar
    // `const-decl`, parseConstSection). Looked up two ways: by `Parser::parseConstExpr` when a
    // later const-expr references an earlier constant (`GetConstantIdentifier`,
    // uPSCompiler.pas:13072-13084), and by `CompiledScript::constValue` for the interpreter's
    // default-value-at-frame-entry (spec 4.4 I1/I3 - a const's "zero value" IS its folded value).
    // nullptr for every slot that is not a `const`.
    // T-0035: `type` is the engine's static type of the folded value (Integer = s32, Cardinal = u32,
    // Int64 = s64, Char, AnsiString, Extended, Boolean) - a const is also the target of the
    // assignment / `var`-argument rejections (uPSCompiler.pas:10653-10658, :5438).
    void setConstValue(int slot, Value value, DeclaredType type);
    [[nodiscard]] const Value* constValueForSlot(int slot) const;
    [[nodiscard]] DeclaredType constTypeForSlot(int slot) const;
    // Recorded right after a routine's parameter list is parsed (before its body), so a
    // recursive call inside that same body resolves with a known arity (spec 3.4's "wrong
    // argument count" diagnostic is only meaningful once this has been set).
    void setRoutineArity(int slot, int paramCount);

    enum class ResolutionKind : std::uint8_t { LocalRoutine, LocalVariable, ExternalFunction, Unknown };
    struct Resolution {
        ResolutionKind kind = ResolutionKind::Unknown;
        int slot = -1;                                 // LocalRoutine / LocalVariable
        int routineParamCount = -1;                    // LocalRoutine, once known (see above)
        const ExternalSymbol* function = nullptr;      // ExternalFunction
        const RegisteredType* declaredType = nullptr;  // LocalVariable, when statically known
        const InterfaceType* boundInterface = nullptr; // LocalVariable, when it is one of the 7
    };
    // The shadowing order of spec 2.8. Has the side effect of latching
    // `sawNonDeterministicSymbol()` when the resolved name is one of the eight spec-2.12 names.
    [[nodiscard]] Resolution resolve(const QByteArray& name);

    [[nodiscard]] int frameSize() const noexcept { return nextSlot_; }

    // True once `resolve()` has returned a bound variable or external function flagged
    // `nonDeterministic` (spec 2.12 / 3.7 `Corpus.DeterministicCountMatches`).
    [[nodiscard]] bool sawNonDeterministicSymbol() const noexcept { return nonDeterministic_; }

    [[nodiscard]] static int fieldIndex(const RegisteredType& type, const QByteArray& fieldName);
    [[nodiscard]] static const RegisteredType* registeredTypeNamed(const QByteArray& name);
    // The registered record type bound to a still-in-scope local variable's slot, or nullptr for
    // any other slot (a primitive, a bound variable, or one already out of scope). Parse-time
    // only, used to resolve a `with` designator (T-0014B) - unlike `declaredTypes()` below it does
    // not survive `popScope()`, so callers must query it before the declaring scope closes.
    [[nodiscard]] const RegisteredType* declaredTypeOfSlot(int slot) const;
    // The interface bound at ScriptVars slot `slot` (spec 5.1: TileCache=0 .. Converter=6), or
    // nullptr for the other 19 (non-interface) bound variables and any local slot.
    [[nodiscard]] static const InterfaceType* interfaceForBoundSlot(int slot);
    // The fixed registered-function/intrinsic table, in the order `Call::callee` indexes it for
    // `CallKind::Builtin`/`CallKind::Registered` (spec 5.3 order, then spec 5.4 order).
    [[nodiscard]] static std::span<const ExternalSymbol> externalSymbols();

    // One `DeclaredType` per slot, indexed [0, frameSize()) - `Unknown` for a bound variable, a
    // routine's own slot, or any local whose type this parser did not resolve. Unlike the per-
    // `Entry` `declaredType`/`declaredKind` above, this flat vector survives `popScope()` (T-0014B:
    // the interpreter needs a routine's parameter/local types long after that routine's own parse
    // scope has closed), so it is maintained directly by `declareLocal`/`declareRoutine`/
    // `setSlotType` rather than derived from `scopes_` after the fact.
    [[nodiscard]] const std::vector<DeclaredType>& declaredTypes() const noexcept { return declaredTypes_; }

private:
    struct Entry {
        QByteArray name;
        int slot = -1;
        const RegisteredType* declaredType = nullptr;
        const InterfaceType* boundInterface = nullptr;
        bool nonDeterministic = false;
    };
    struct Scope {
        std::vector<Entry> entries;
    };
    struct RoutineEntry {
        QByteArray name;
        int slot = -1;
        int paramCount = -1; // -1 until `setRoutineArity` runs (params parsed, body not yet)
    };

    std::vector<Scope> scopes_;
    std::vector<RoutineEntry> routines_;
    std::vector<DeclaredType> declaredTypes_; // parallel to slot numbers, see declaredTypes()
    // Sparse (slot, value) pairs - a script declares at most a handful of consts/arrays, so a
    // linear scan costs nothing and neither needs to grow in lockstep with declaredTypes_.
    struct ConstEntry {
        int slot;
        Value value;
        DeclaredType type;
    };
    std::vector<ConstEntry> constValues_;
    std::vector<std::pair<int, ArrayInfo>> arrayInfos_;
    int nextSlot_ = 0;
    bool nonDeterministic_ = false;
    Arena arena_;
};

// A folded constant with the engine's static type (see SymbolTable::setConstValue).
struct ConstVal {
    Value value;
    DeclaredType type = DeclaredType::Integer;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, SymbolTable& symbols);

    // Recursive descent, one token of lookahead, no backtracking. On an error the parser records
    // a diagnostic and skips to the next ';' or the next statement keyword, so one script yields
    // all its errors in one pass. Fails (returning every diagnostic, not just the first) as soon
    // as `diagnostics` is non-empty when parsing ends - including diagnostics the lexer already
    // recorded before the parser ever ran.
    [[nodiscard]] core::Result<Program*> parse(DiagnosticList& diagnostics);

private:
    // --- token stream ----------------------------------------------------------------------
    [[nodiscard]] const Token& current() const;
    [[nodiscard]] const Token& lookAhead(int offset) const;
    void advance();
    [[nodiscard]] bool atKeyword(Keyword keyword) const;
    [[nodiscard]] bool atKind(TokenKind kind) const;
    bool expectKind(TokenKind kind, const char* what);
    bool expectKeyword(Keyword keyword, const char* what);
    void error(QString message);
    void errorAt(int line, int column, QString message);
    void recover(); // skip to the next ';' or a statement/section keyword

    // A `var`/parameter/`Result` type annotation, resolved as far as this parser goes: `recordType`
    // is non-null only for one of the 6 registered record types (`.field` resolution, unchanged
    // from T-0013); `kind` is the slot's `DeclaredType` (T-0014B), `Unknown` when unresolved.
    // T-0032 (PD-1): `elementKind`/`lowBound`/`highBound` are populated only when
    // `kind == DeclaredType::StaticArray` (spec 3.6).
    struct ParsedType {
        const RegisteredType* recordType = nullptr;
        DeclaredType kind = DeclaredType::Unknown;
        DeclaredType elementKind = DeclaredType::Unknown;
        qint64 lowBound = 0;
        qint64 highBound = -1;
    };

    // --- declarations ------------------------------------------------------------------------
    void parseDeclSections(Program* program);
    void parseConstSection();
    void parseVarSection();
    void parseRoutineDecl(Program* program);
    [[nodiscard]] ParsedType parseTypeName();
    void skipRecordType();
    // T-0035: the engine's ReadConstant (uPSCompiler.pas:13056-13430) over the token stream - its own
    // grammar (not the statement parser's), typed values, PreCalc folding. Reports its own
    // diagnostic and returns nullopt on failure.
    [[nodiscard]] std::optional<ConstVal> parseConstExpr(int level = 0);
    [[nodiscard]] std::optional<BinaryOp> constOpAt(int level) const;
    [[nodiscard]] std::optional<ConstVal> parseConstBound(); // array bound: integer-typed const-expr
    enum class NumKind : std::uint8_t { Unknown, Int, Real };
    [[nodiscard]] NumKind numericKind(const Node* node) const;
    void checkRealToInt(DeclaredType dest, const Node* value);
    void checkVarArgs(const Call* call, const std::vector<Node*>& args, const char* name);
    struct RoutineSig {
        int slot;
        int firstParamSlot;
        std::vector<ParamMode> modes;
    };
    std::vector<RoutineSig> routineSigs_;

    // --- statements --------------------------------------------------------------------------
    [[nodiscard]] Node* parseCompoundStmt();
    [[nodiscard]] Node* parseStmt();
    [[nodiscard]] Node* parseIfStmt();
    [[nodiscard]] Node* parseCaseStmt();
    [[nodiscard]] Node* parseForStmt();
    [[nodiscard]] Node* parseWhileStmt();
    [[nodiscard]] Node* parseRepeatStmt();
    [[nodiscard]] Node* parseTryStmt();
    [[nodiscard]] Node* parseWithStmt();
    [[nodiscard]] Node* parseSimpleStmtOrAssign();

    // --- expressions (Delphi precedence, spec 3.2) --------------------------------------------
    [[nodiscard]] Node* parseExpr();
    [[nodiscard]] Node* parseSimpleExpr();
    [[nodiscard]] Node* parseTerm();
    [[nodiscard]] Node* parseFactor();
    [[nodiscard]] Node* parseDesignatorOrLiteral();
    [[nodiscard]] Node* parseDesignatorTail(const QByteArray& name, int line, int column);
    [[nodiscard]] std::vector<Node*> parseArgList();
    [[nodiscard]] qint64 parseConstIntExpr(); // for case labels

    // One active `with` scope (spec 3.5, T-0014B): `slot` is the with-subject variable (always a
    // simple local of one of the 6 registered record types, enforced by `parseWithStmt`), `type`
    // its record layout. `parseDesignatorTail` checks these innermost-first, before ordinary name
    // resolution, so a bare field name inside the `with` body lowers straight to a `Field` node.
    struct WithScope {
        int slot = -1;
        const RegisteredType* type = nullptr;
    };

    std::vector<Token> tokens_;
    int pos_ = 0;
    SymbolTable& symbols_;
    DiagnosticList* diagnostics_ = nullptr;
    int loopDepth_ = 0; // break/continue must be inside a loop (spec 3.3 S8)
    std::vector<WithScope> withScopes_;
};

} // namespace cg::script
