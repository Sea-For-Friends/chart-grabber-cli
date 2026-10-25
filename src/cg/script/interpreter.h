// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/uPSRuntime.pas (TPSExec, the tree/bytecode-walking VM).
//
// T-0014 slice: the pure interpreter core - value model consumer, VM frame management
// (spec 4.4 I1-I5), expression and statement evaluation (spec 4.1/4.2/4.3 N1-N11/T1-T9/A1-A6),
// ExecBudget (spec 4.5), and the built-ins that need no host binding.
//
// T-0014B slice: declared-type range-wrap/rounding on every slot write (spec 4.1 N1/N2, via
// `Value::wrappedTo` + `CompiledScript::declaredType`), and the 6 built-in record types - default
// init at frame entry, `Field`/`FieldAssign`, `with` (spec 2.6/3.5/4.3 A5/4.4 I1). Still OUT this
// slice (fails loudly with ErrorCode::Unsupported, never silently): static/dynamic arrays
// (`Index`/`IndexAssign` on one, `ArrayLit`-as-aggregate - out of this ticket's scope). See
// docs/handoffs/T-0014.md / T-0014B.md.
//
// T-0032 slice: PD-1 - static arrays (`array[lo..hi] of char|byte|integer|longint`, spec 3.6).
// `evalIndex`/the `IndexAssign` case of `execStmt` now also handle `ValueKind::Array` (still OUT:
// dynamic arrays, arrays of records/multi-dim - zero corpus uses, T-0032 ticket scope). Every
// index is range-checked against the array's declared bounds (`arrayElementOffset`,
// uPSRuntime.pas:7127-7135/:7289-7297) before the read/write. PD-103 - `const` declarations now
// fold to a compile-time `Value` (parser.cpp's `Parser::parseConstExpr`) and `defaultValueForSlot`
// returns it instead of the ordinary zero default.
//
// T-0015A slice: `run()` now takes the host and the 26 bound variables (spec 2.11/5.1) - see
// script_host.h. `checkNotBound` (still named for its T-0014 role) no longer rejects anything;
// every bound-variable slot is a live, host-backed value for the duration of a run. The 7
// host-backed builtins (Random, RandomRange, FileExists, GetUnixTime, SaveToLocalFile, WriteLn,
// TemplateToUrl) forward to `IScriptHost` (spec 5.3 F1/F2/F19/F35-F38). `UrlScript`'s
// buffer-only-on-success write-back *policy* is T-0015C; this slice's `run()` writes back
// ResultURL/PostData/RequestHead/ScriptBuffer together, only when the run itself succeeds (spec
// 2.11: "On success the out-fields of vars have been written back").
//
// T-0015B slice: `evalCall`'s `CallKind::Method` case now dispatches to the 7 script-visible
// interfaces (spec 2.7/5.6) instead of failing with `ErrorCode::Unsupported`. `evalMethodCall`
// decodes `Call::callee` (parser.h's `boundSlot * kMethodSlotStride + methodIndex`), reads the
// live `InterfaceRef` out of the bound slot's `Value` and casts `InterfaceRef::obj` back to the
// concrete interface named by `InterfaceRef::id`. A method call through a nil (unassigned)
// interface is `ErrorCode::IoError` (spec 7's generic runtime-error row - the exact legacy
// exception text is unverifiable, no compiler/oracle exists; see docs/handoffs/T-0015B.md). One
// helper method per interface (`callCoordConverter`/`callProjConverter`/
// `callProjConverterFactory`/`callHttpDownloader`/`callScriptGlobal`/`callScriptLogger`/
// `callScriptTileCache`) keeps each interface's argument marshalling together; `IHttpDownloader`'s
// two `var` output parameters are handled like `Insert`/`Delete`/`SetLength` in `callBuiltin` -
// from the raw `Call::args` nodes, not the by-value-evaluated vector the other 6 interfaces use.

#include "cg/core/result.h"
#include "cg/script/ast.h"
#include "cg/script/compiled_script.h"
#include "cg/script/script_host.h"
#include "cg/script/value.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cg::script {

struct ExecBudget {
    std::uint64_t maxSteps = 50'000'000; // ~1s of tree-walking on the dev box (spec 4.5)
    std::chrono::milliseconds maxWall{5000};
};

// One enumerator per entry of SymbolTable::externalSymbols() (parser.cpp's `kExternals`), in the
// same order, so `static_cast<BuiltinId>(Call::callee)` is exact for CallKind::Builtin/Registered.
// Ruling 2026-09-18 (T-0013 review): lives here, not a separate builtins.h ("no new header").
// `kExternals` is a TU-local `constexpr` array (parser.cpp), so `externalSymbols()` (the only way
// another TU can see it) is a runtime function - the ORDER can only be checked at runtime
// (BuiltinIdMatchesExternalSymbolOrder, interpreter_test.cpp); the COUNT is checked below with a
// real static_assert.
enum class BuiltinId : std::uint8_t {
    // --- registered functions (F1-F38), spec 5.3 order ---
    Random,
    RandomRange,
    Power,
    IntPower,
    Ceil,
    Floor,
    Log2,
    Ln,
    Max,
    Min,
    MaxExt,
    MinExt,
    RegExprGetMatchSubStr,
    RegExprReplaceMatchSubStr,
    Base64Encode,
    Base64UrlEncode,
    Base64Decode,
    IntToHex,
    FileExists,
    Format,
    FormatEx,
    PosEx,
    StringReplace,
    MD5String,
    RoundEx,
    GetAfter,
    GetBefore,
    GetBetween,
    SetHeaderValue,
    GetHeaderValue,
    DeleteHeaderEntry,
    SubStrPos,
    GetNumberAfter,
    GetDiv3Path,
    GetUnixTime,
    SaveToLocalFile,
    WriteLn,
    TemplateToUrl,
    // --- intrinsics, spec 5.4 order ---
    IntToStr,
    StrToInt,
    StrToIntDef,
    StrToFloat,
    FloatToStr,
    Copy,
    Pos,
    Insert,
    Delete,
    Length,
    SetLength,
    Chr,
    Ord,
    Round,
    Trunc,
    Int,
    Frac,
    Abs,
    Sqr,
    Sqrt,
    Sin,
    Cos,
    ArcTan,
    Exp,
    Assigned,
    Uppercase,
    Lowercase,
    Trim,
    Inc,
    Dec,
    High,
    Low,
    VarType,
    Pi,
    Count, // sentinel - not a real builtin
};

inline constexpr std::array<const char*, static_cast<std::size_t>(BuiltinId::Count)> kBuiltinNames{{
    "Random",
    "RandomRange",
    "Power",
    "IntPower",
    "Ceil",
    "Floor",
    "Log2",
    "Ln",
    "Max",
    "Min",
    "MaxExt",
    "MinExt",
    "RegExprGetMatchSubStr",
    "RegExprReplaceMatchSubStr",
    "Base64Encode",
    "Base64UrlEncode",
    "Base64Decode",
    "IntToHex",
    "FileExists",
    "Format",
    "FormatEx",
    "PosEx",
    "StringReplace",
    "MD5String",
    "RoundEx",
    "GetAfter",
    "GetBefore",
    "GetBetween",
    "SetHeaderValue",
    "GetHeaderValue",
    "DeleteHeaderEntry",
    "SubStrPos",
    "GetNumberAfter",
    "GetDiv3Path",
    "GetUnixTime",
    "SaveToLocalFile",
    "WriteLn",
    "TemplateToUrl",
    "IntToStr",
    "StrToInt",
    "StrToIntDef",
    "StrToFloat",
    "FloatToStr",
    "Copy",
    "Pos",
    "Insert",
    "Delete",
    "Length",
    "SetLength",
    "Chr",
    "Ord",
    "Round",
    "Trunc",
    "Int",
    "Frac",
    "Abs",
    "Sqr",
    "Sqrt",
    "Sin",
    "Cos",
    "ArcTan",
    "Exp",
    "Assigned",
    "Uppercase",
    "Lowercase",
    "Trim",
    "Inc",
    "Dec",
    "High",
    "Low",
    "VarType",
    "Pi",
}};
static_assert(kBuiltinNames.size() == static_cast<std::size_t>(BuiltinId::Count),
              "one BuiltinId enumerator per kBuiltinNames entry");

// True iff `kBuiltinNames[i]` case-insensitively equals `SymbolTable::externalSymbols()[i].name`
// for every i, AND the counts match - the runtime half of the BuiltinId/externalSymbols pairing
// static_assert above cannot express (see the enum's doc comment). Checked once by
// interpreter_test.cpp's BuiltinIdMatchesExternalSymbolOrder.
[[nodiscard]] bool builtinIdsMatchExternalSymbolOrder();

// Tree-walking interpreter over one `CompiledScript`.
class Interpreter {
public:
    // Takes shared ownership of `script` (T-0014 rework Major 7): the interpreter must keep the
    // AST arena alive for its own lifetime rather than trust the caller to outlive it.
    explicit Interpreter(std::shared_ptr<const CompiledScript> script, ExecBudget budget = {});

    // Binds `host` and the 26 `vars` (spec 5.1, registration order) into their slots, then runs
    // the program. On success the out-fields of `vars` (ResultURL, PostData, RequestHead,
    // ScriptBuffer) have been written back; on failure `vars` is left untouched (T-0015C's
    // UrlScript decides what a caller does with a partially-run map).
    //
    // Errors: ErrorCode::Unsupported for a construct that compiled but cannot execute this slice
    // (an interface method call - T-0015B; records/arrays; `with`; an unimplemented builtin);
    // ErrorCode::IoError for a Delphi runtime error (division by zero, bad conversion,
    // out-of-range string write); ErrorCode::Cancelled when `ExecBudget` is exhausted (spec 4.5).
    // Never ErrorCode::ParseError.
    [[nodiscard]] core::Result<void> run(IScriptHost& host, ScriptVars& vars);

    // Reads back a slot after a successful run() - exposed for interpreter_test.cpp; there is no
    // ScriptVars out-binding this slice (T-0014B).
    [[nodiscard]] const Value& slot(int index) const { return storage_[static_cast<std::size_t>(index)]; }

private:
    enum class Signal : std::uint8_t { Normal, Break, Continue, Exit };

    [[nodiscard]] core::Result<Value> evalExpr(Node* node);
    // T-0031 rework (review finding 3): like `evalExpr`, but when `node` is exactly a `Unary`
    // '-' node, computes it in `slot`'s declared type instead of the operand's - `DoUnCalc`
    // (uPSCompiler.pas:6230-6242) is handed the REAL destination register directly whenever the
    // unary node is not itself nested inside a further calculation (`WriteCalculation`,
    // `:9604-9688`'s `CheckOutReg`/`DoUnCalc(InData, OutReg)` branch), so e.g. `R64 := -I` with
    // `I = Low(Integer)` widens I into R64 *before* negating, giving 2147483648, not
    // -2147483648. Only the direct assignment RHS gets this; a `Unary` nested inside a further
    // expression still goes through `evalUnary`'s operand-width path via `evalExpr`.
    [[nodiscard]] core::Result<Value> evalExprForSlot(Node* node, int slot);
    [[nodiscard]] core::Result<Value> evalBinary(Binary* node);
    [[nodiscard]] core::Result<Value> evalUnary(Unary* node);
    [[nodiscard]] core::Result<Value> evalIndex(Index* node);
    // T-0032 (PD-1): the 0-based element offset of `index` into `array` (a ValueKind::Array
    // Value), range-checked the way the engine's compiler/runtime do it for a static array - the
    // compiler itself subtracts the declared low bound into a Cardinal temp before the generic
    // array-access opcode runs (uPSCompiler.pas ~6965-6988), so a negative or too-large offset
    // wraps to a huge unsigned magnitude rather than going negative (uPSRuntime.pas:7127-7135
    // read / :7289-7297 write). Shared by evalIndex and the IndexAssign case of execStmt so both
    // report the identical "Static array index out of bounds (%1). Length is %2." text.
    [[nodiscard]] core::Result<std::size_t> arrayElementOffset(const Value& array, qint64 index,
                                                               int line) const;
    [[nodiscard]] core::Result<Value> evalCall(Call* node);
    [[nodiscard]] core::Result<Value> callUserRoutine(const Routine& routine, Call* node);
    [[nodiscard]] core::Result<Value> callBuiltin(BuiltinId id, Call* node);

    // T-0015B: CallKind::Method dispatch (spec 2.7/5.6) - see the header block above.
    [[nodiscard]] core::Result<Value> evalMethodCall(Call* node);
    [[nodiscard]] core::Result<Value> evalMethodCallUnguarded(Call* node);
    [[nodiscard]] core::Result<Value> callCoordConverter(const InterfaceRef& ref, int methodIndex,
                                                         const std::vector<Value>& args, int line);
    [[nodiscard]] core::Result<Value> callProjConverter(const InterfaceRef& ref, int methodIndex,
                                                        const std::vector<Value>& args, int line);
    [[nodiscard]] core::Result<Value> callProjConverterFactory(const InterfaceRef& ref, int methodIndex,
                                                               const std::vector<Value>& args, int line);
    [[nodiscard]] core::Result<Value> callHttpDownloader(const InterfaceRef& ref, Call* node);
    [[nodiscard]] core::Result<Value> callScriptGlobal(const InterfaceRef& ref, int methodIndex,
                                                       const std::vector<Value>& args, int line);
    [[nodiscard]] core::Result<Value> callScriptLogger(const InterfaceRef& ref, Call* node);
    [[nodiscard]] core::Result<Value> callScriptTileCache(const InterfaceRef& ref, int methodIndex,
                                                          const std::vector<Value>& args, int line);

    [[nodiscard]] core::Result<Signal> execStmt(Node* node);
    [[nodiscard]] core::Result<Signal> execCompound(Compound* node);
    [[nodiscard]] core::Result<Signal> execIf(If* node);
    [[nodiscard]] core::Result<Signal> execCase(Case* node);
    [[nodiscard]] core::Result<Signal> execForTo(ForTo* node);
    [[nodiscard]] core::Result<Signal> execWhile(While* node);
    [[nodiscard]] core::Result<Signal> execRepeatUntil(RepeatUntil* node);
    [[nodiscard]] core::Result<Signal> execTryFinally(TryFinally* node);
    [[nodiscard]] core::Result<Signal> execStmtList(const std::vector<Node*>& stmts);

    // T-0031 rework (review finding 1/2): `node`'s PascalScript static integer type, i.e. the
    // engine's `t1` (uPSCompiler.pas `GetResultType`, otAdd `:8459`, otSub/otMul/otIntDiv/otDiv
    // `:8514-8560`, otMod/otShl/otShr `:8672-8686` - every int-int case is "Result := t1", the
    // LEFT operand's own type, propagated recursively). `IntLit` follows `ReadInteger`
    // (`:5720-5743`): a literal in `Low(Integer)..High(Integer)` is `Integer` (S32), up to
    // `$FFFFFFFF` is `Cardinal` (**unsigned** U32), else `Int64`. A `VarRef`/routine `Result` whose
    // declared type is `Unknown` (the 19 non-interface bound variables, GetX/GetY/GetZ included)
    // maps to `Integer` - they are S32 in the engine
    // (u_TileDownloadRequestBuilderPascalScriptVars.pas:141-144). `Unary` keeps the operand's type
    // (`NewVarU.aType := GetTypeNo(BlockInfo, NewVar)`, uPSCompiler.pas:8148/8148) - this is the
    // node's *static* type for use as a further left operand, not the destination-register
    // exception unary '-' gets when it is the direct value stored into a slot (see
    // `evalExprForSlot`). Never returns `DeclaredType::Unknown` - callers pass the result straight
    // to `Value::wrappedTo`, which does not wrap that case. Called only where both operands are
    // already known Int-kind at runtime (evalBinary/evalUnary).
    [[nodiscard]] DeclaredType leftIntType(const Node* node) const;
    // Bit width `leftIntType` computes at: 8 (Byte/ShortInt), 16 (Word/SmallInt), 64 (Int64), else
    // 32 (Integer/Cardinal and everything else `wrappedTo` narrows) - uPSRuntime.pas `DoCalc`'s
    // per-basetype cases (`:5610-5636` add, `:6202-6205` div, `:6471-6474`/`:6506` shl/shr).
    [[nodiscard]] static int intWidthBits(DeclaredType type);

    [[nodiscard]] core::Result<void> tickBudget(int line);
    // T-0015A: kept for its original call sites (VarRef/Assign/IndexAssign/FieldAssign/ForTo/
    // Inc/Dec/SetLength/Insert/Delete/var-out copy-back), but now a no-op - every bound-variable
    // slot (index < kBoundVariableCount) is a live value once `run()` has bound `vars`, so nothing
    // is rejected here any more. The one remaining host-binding rejection is a method call on a
    // bound interface variable (T-0015B), handled directly by evalCall's CallKind::Method case.
    [[nodiscard]] core::Result<Value> checkNotBound(int slot, int line) const;
    // spec 5.1: copies the 26 bound variables into their slots [0, kBoundVariableCount) in
    // registration order. GetURLBase becomes a mutable copy in `storage_` that this Interpreter
    // discards at the end of `run()` - it never aliases `vars.getUrlBase`.
    void bindScriptVars(const ScriptVars& vars);
    // spec 2.13 read-back order: ResultUrl, PostData, RequestHead, ScriptBuffer. Called once, only
    // after a successful run.
    void writeBackScriptVars(ScriptVars& vars) const;
    [[nodiscard]] std::unexpected<core::Error> unsupported(int line, const QString& what) const;
    [[nodiscard]] std::unexpected<core::Error> runtimeError(int line, const QString& what) const;
    [[nodiscard]] const Routine* routineForSlot(int slot) const;

    // spec 4.4 I1: the zero value for a frame slot at entry - `Value()` (Empty) for every
    // primitive, or a zero-filled `Value::fromRecord` for one of the 6 built-in record types
    // (script_host.h/parser.cpp field order).
    [[nodiscard]] Value defaultValueForSlot(int slot) const;
    // spec 4.1 N1/N2: `v.wrappedTo(script_->declaredType(slot))` - every slot-write path
    // (Assign, Inc/Dec, var/out copy-back, function Result) goes through this.
    [[nodiscard]] Value wrapForSlot(int slot, const Value& v) const;

    std::shared_ptr<const CompiledScript> script_; // keeps the AST arena alive; declared before
                                                   // program_ so it is constructed first
    const Program& program_;
    ExecBudget budget_;
    std::vector<Value> storage_;
    std::uint64_t steps_ = 0;
    std::chrono::steady_clock::time_point deadline_;
    IScriptHost* host_ = nullptr; // set for the duration of run(); never owned
};

} // namespace cg::script
