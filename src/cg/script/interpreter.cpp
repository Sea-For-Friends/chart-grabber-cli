// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/PascalScript/uPSRuntime.pas (TPSExec). See interpreter.h for this slice's scope.
#include "cg/script/interpreter.h"

#include "cg/core/delphi_format.h" // roundEx (F25) + intToStr/intToHex/delphiRound/delphiTrunc (core, T-0092)
#include "cg/core/header_text.h" // T-0028: scriptGetHeaderValue/scriptSetHeaderValue/scriptDeleteHeaderEntry (F29-F31)
#include "cg/core/platform/qt_compat.h"
#include "cg/script/delphi_format_adapter.h" // delphiFormat over script::Value (F20/F21)
#include "cg/script/parser.h"                // SymbolTable::kBoundVariableCount, externalSymbols()
#include "cg/script/regex_compat.h"          // compileTRegExpr/tRegExprMatch/tRegExprReplace (F13/F14)

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QString>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <type_traits>

namespace cg::script {
namespace {

long double intPower(long double base, qint64 exponent) {
    const bool negative = exponent < 0;
    qint64 n = negative ? -exponent : exponent;
    long double result = 1.0L;
    long double b = base;
    while (n > 0) {
        if ((n & 1) != 0) {
            result *= b;
        }
        b *= b;
        n >>= 1;
    }
    return negative ? 1.0L / result : result;
}

// T-0031 rework (review finding 7 / ruling 3): Low(T) div -1 overflows a signed IDIV at T's own
// width - the same x86 hardware fault class as Low(Integer) div -1, generalised to every signed
// integer width the port models (leftIntType never returns anything narrower than Integer for an
// Unknown-typed operand, so this only fires for an explicitly ShortInt/SmallInt/Integer/Int64-typed
// left operand). Byte/Word/Cardinal are unsigned - unsigned division never overflows this way.
bool isIntDivOverflow(qint64 a, qint64 b, DeclaredType t) {
    if (b != -1) {
        return false;
    }
    switch (t) {
    case DeclaredType::ShortInt:
        return a == -128;
    case DeclaredType::SmallInt:
        return a == -32768;
    case DeclaredType::Integer:
        return a == std::numeric_limits<qint32>::min();
    case DeclaredType::Int64:
        return a == std::numeric_limits<qint64>::min();
    default:
        return false;
    }
}

char asciiLowerChar(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

QByteArray asciiUpper(QByteArray s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 32);
        }
    }
    return s;
}
QByteArray asciiLower(QByteArray s) {
    for (char& c : s) {
        c = asciiLowerChar(c);
    }
    return s;
}

// Byte-wise `indexOf`, optionally ASCII case-insensitive (D-5: no code-page folding in the port).
int findByteString(const QByteArray& hay, const QByteArray& needle, int from, bool ignoreCase) {
    if (needle.isEmpty()) {
        return -1;
    }
    if (!ignoreCase) {
        return static_cast<int>(hay.indexOf(needle, from));
    }
    for (int i = std::max(from, 0); i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (int j = 0; j < needle.size(); ++j) {
            if (asciiLowerChar(hay[i + j]) != asciiLowerChar(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return -1;
}

// Delphi StrToInt grammar (spec 5.4): optional sign, then decimal digits or a `$`-prefixed hex
// run; no leading/trailing spaces, no partial parse.
core::Result<qint64> parseDelphiInt(const QByteArray& s) {
    auto fail = [] { return core::fail(core::ErrorCode::IoError, QStringLiteral("invalid integer")); };
    if (s.isEmpty()) {
        return fail();
    }
    int i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = (s[0] == '-');
        i = 1;
    }
    if (i >= s.size()) {
        return fail();
    }
    bool ok = false;
    qint64 v = 0;
    if (s[i] == '$') {
        const QByteArray digits = s.mid(i + 1);
        if (digits.isEmpty()) {
            return fail();
        }
        v = digits.toLongLong(&ok, 16);
    } else {
        for (int j = i; j < s.size(); ++j) {
            if (s[j] < '0' || s[j] > '9') {
                return fail();
            }
        }
        v = s.mid(i).toLongLong(&ok, 10);
    }
    if (!ok) {
        return fail();
    }
    return neg ? -v : v;
}

// T-0032 (PD-1, spec I1): the zero value of one element of a static array, by the array's declared
// element type (ticket scope: char|byte|integer|longint - the corpus's four). Byte/ShortInt/
// SmallInt/Cardinal/Int64 are included for free since Value::wrappedTo already narrows an Int the
// same way a scalar slot of that type would. Char defaults to Empty, matching this interpreter's
// existing (pre-T-0032) scalar-Char-slot gap - see docs/handoffs/T-0032.md "Legacy defects noticed".
Value defaultElementValue(DeclaredType elementKind) {
    switch (elementKind) {
    case DeclaredType::Byte:
    case DeclaredType::Word:
    case DeclaredType::ShortInt:
    case DeclaredType::SmallInt:
    case DeclaredType::Integer:
    case DeclaredType::Cardinal:
    case DeclaredType::Int64:
        return Value::fromInt(0);
    case DeclaredType::Single:
    case DeclaredType::Double:
    case DeclaredType::Extended:
        return Value::fromReal(0.0L);
    case DeclaredType::Boolean:
        return Value::fromBool(false);
    case DeclaredType::Char:
        return Value::fromStr(QByteArray(1, '\0')); // T-0035: InitializeVariant zero-fills (F5)
    case DeclaredType::AnsiString:
    case DeclaredType::WideString:
        return Value::fromStr(QByteArray());
    default:
        return Value();
    }
}

QByteArray getDiv3Path(const QByteArray& number) {
    const int n = static_cast<int>(number.size());
    if (n == 0) {
        return QByteArray();
    }
    QByteArray result;
    int idx = n % 3;
    for (; idx < n; idx += 3) {
        result += number.mid(idx, 3);
        result += '\\';
    }
    return result;
}

// F17 Base64Decode strictness gap (docs/specs/script.md:1086, T-0014 deviation note): standard
// alphabet only (A-Za-z0-9+/), padding required, no URL alphabet. Any invalid input -> ''.
// Hand-validated rather than relying on QByteArray::fromBase64Encoding's exact error semantics
// for malformed/misplaced padding, then decoded with the now-guaranteed-well-formed input.
QByteArray base64DecodeStrict(const QByteArray& data) {
    if (data.isEmpty()) {
        return QByteArray();
    }
    if (data.size() % 4 != 0) {
        return QByteArray();
    }
    auto isDataChar = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
               c == '/';
    };
    int i = 0;
    const int n = static_cast<int>(data.size());
    while (i < n && isDataChar(data[i])) {
        ++i;
    }
    const int padLen = n - i;
    if (padLen > 2) {
        return QByteArray();
    }
    for (int j = i; j < n; ++j) {
        if (data[j] != '=') {
            return QByteArray();
        }
    }
    return QByteArray::fromBase64(data, QByteArray::Base64Encoding);
}

} // namespace

Interpreter::Interpreter(std::shared_ptr<const CompiledScript> script, ExecBudget budget)
    : script_(std::move(script)), program_(script_->program()), budget_(budget),
      storage_(static_cast<std::size_t>(script_->frameSize())) {}

Value Interpreter::defaultValueForSlot(int slot) const {
    // PD-103 (spec I1/I3): a const's "zero value" at frame entry IS its folded value, not 0/''/
    // Empty - checked before the declared-type switch below so an untyped const (DeclaredType
    // Unknown) still gets its fold instead of falling through to Empty.
    if (const Value* constant = script_->constValue(slot)) {
        return *constant;
    }
    // Field order matches parser.cpp's kPointFields/kDoublePointFields/kRectFields/
    // kDoubleRectFields/kTileInfoFields exactly - load-bearing (spec 2.6/5.2).
    switch (script_->declaredType(slot)) {
    case DeclaredType::TPoint:
        return Value::fromRecord({Value::fromInt(0), Value::fromInt(0)});
    case DeclaredType::TDoublePoint:
        return Value::fromRecord({Value::fromReal(0.0L), Value::fromReal(0.0L)});
    case DeclaredType::TRect:
        return Value::fromRecord(
            {Value::fromInt(0), Value::fromInt(0), Value::fromInt(0), Value::fromInt(0)});
    case DeclaredType::TDoubleRect:
        return Value::fromRecord(
            {Value::fromReal(0.0L), Value::fromReal(0.0L), Value::fromReal(0.0L), Value::fromReal(0.0L)});
    case DeclaredType::TTileInfo:
        // LoadDate has no ValueKind (spec 2.6 has no QDateTime kind) - Empty is its zero value;
        // nothing this slice reads or writes it (TTileInfo is otherwise host-binding surface,
        // T-0015).
        return Value::fromRecord({Value::fromBool(false), Value::fromBool(false), Value(), Value::fromInt(0),
                                  Value::fromStr(QByteArray()), Value::fromStr(QByteArray()),
                                  Value::fromStr(QByteArray())});
    case DeclaredType::Single:
    case DeclaredType::Double:
    case DeclaredType::Extended:
        return Value::fromReal(0.0L); // PD-2/I1: a real-declared scalar slot zero-inits to 0.0, not Empty
    case DeclaredType::StaticArray: {
        // PD-1 (spec 3.6/I1): a static array slot zero-inits to `count` zero-valued elements, one
        // per the declared element type. `arrayInfo` is null only for a slot this parser could not
        // resolve bounds for (never happens for a real `array[lo..hi] of T` declaration) - Empty,
        // same defensive fallback as an unresolved primitive type.
        const ArrayInfo* info = script_->arrayInfo(slot);
        if (info == nullptr) { // an empty array[lo..lo-1] (L-70) is a zero-length array: every access raises
                               // the range error
            return Value();
        }
        const auto count = static_cast<std::size_t>(info->highBound - info->lowBound + 1);
        std::vector<Value> elems(count, defaultElementValue(info->elementKind));
        return Value::fromArray(std::move(elems), info->lowBound);
    }
    default:
        return Value();
    }
}

Value Interpreter::wrapForSlot(int slot, const Value& v) const {
    return v.wrappedTo(script_->declaredType(slot));
}

namespace {
// static_cast<void> a possibly-const-qualified shared_ptr into the shared_ptr<void> InterfaceRef
// expects (value.h). const_pointer_cast first: shared_ptr's converting constructor cannot itself
// drop the const the 3 read-only ScriptVars interface fields (converter/defProjConverter/
// projFactory) carry.
template <class T> std::shared_ptr<void> toVoidShared(const std::shared_ptr<T>& p) {
    return std::const_pointer_cast<std::remove_const_t<T>>(p);
}
} // namespace

void Interpreter::bindScriptVars(const ScriptVars& vars) {
    // spec 5.1 registration order == parser.cpp's kBoundVariables order == this list. Interface
    // slots hold an InterfaceRef (null obj => Assigned() is False); ResultURL/PostData are
    // cleared, never read from `vars`, exactly as ExecTimeSet does
    // (u_TileDownloadRequestBuilderPascalScriptVars.pas:225-226). GetURLBase is copied by value -
    // storage_[9] is a throwaway mutable copy that never aliases vars.getUrlBase (spec 2.7 tail).
    storage_[0] = Value::fromInterface(toVoidShared(vars.tileCache), InterfaceId::TileCache);
    storage_[1] = Value::fromInterface(toVoidShared(vars.logger), InterfaceId::Logger);
    storage_[2] = Value::fromInterface(toVoidShared(vars.global), InterfaceId::Global);
    storage_[3] = Value::fromInterface(toVoidShared(vars.downloader), InterfaceId::Downloader);
    storage_[4] = Value::fromInterface(toVoidShared(vars.defProjConverter), InterfaceId::ProjConverter);
    storage_[5] = Value::fromInterface(toVoidShared(vars.projFactory), InterfaceId::ProjFactory);
    storage_[6] = Value::fromInterface(toVoidShared(vars.converter), InterfaceId::Converter);
    storage_[7] = Value::fromStr(QByteArray());
    storage_[8] = Value::fromStr(QByteArray());
    storage_[9] = Value::fromStr(vars.getUrlBase);
    storage_[10] = Value::fromStr(vars.requestHead);
    storage_[11] = Value::fromStr(vars.responseHead);
    storage_[12] = Value::fromStr(vars.scriptBuffer);
    storage_[13] = Value::fromStr(vars.version);
    storage_[14] = Value::fromStr(vars.lang);
    storage_[15] = Value::fromInt(vars.getX);
    storage_[16] = Value::fromInt(vars.getY);
    storage_[17] = Value::fromInt(vars.getZ); // 1-based - the caller already added 1 (spec 5.1)
    storage_[18] = Value::fromReal(vars.getLlon);
    storage_[19] = Value::fromReal(vars.getTLat);
    storage_[20] = Value::fromReal(vars.getBLat);
    storage_[21] = Value::fromReal(vars.getRLon);
    storage_[22] = Value::fromReal(vars.getLMetr);
    storage_[23] = Value::fromReal(vars.getRMetr);
    storage_[24] = Value::fromReal(vars.getTMetr);
    storage_[25] = Value::fromReal(vars.getBMetr);
}

void Interpreter::writeBackScriptVars(ScriptVars& vars) const {
    // spec 2.13 read-back order: ResultUrl, PostData, RequestHead, ScriptBuffer.
    vars.resultUrl = storage_[7].toStr();
    vars.postData = storage_[8].toStr();
    vars.requestHead = storage_[10].toStr();
    vars.scriptBuffer = storage_[12].toStr();
}

core::Result<void> Interpreter::run(IScriptHost& host, ScriptVars& vars) {
    host_ = &host;
    // spec 4.4 I1/I3: zero-init once per run - a record-declared global starts as a zero-filled
    // record (Value::fromRecord), everything else as Value() (Empty).
    for (std::size_t i = 0; i < storage_.size(); ++i) {
        storage_[i] = defaultValueForSlot(static_cast<int>(i));
    }
    bindScriptVars(vars);
    steps_ = 0;
    deadline_ = std::chrono::steady_clock::now() + budget_.maxWall;
    auto signal = execStmt(program_.main);
    if (!signal.has_value()) {
        return std::unexpected(signal.error());
    }
    writeBackScriptVars(vars);
    return {};
}

std::unexpected<core::Error> Interpreter::unsupported(int line, const QString& what) const {
    return std::unexpected(
        core::Error{core::ErrorCode::Unsupported, QStringLiteral("%1 at line %2").arg(what).arg(line)});
}
std::unexpected<core::Error> Interpreter::runtimeError(int line, const QString& what) const {
    return std::unexpected(
        core::Error{core::ErrorCode::IoError, QStringLiteral("%1 at line %2").arg(what).arg(line)});
}

core::Result<void> Interpreter::tickBudget(int line) {
    (void)line;
    ++steps_;
    if (steps_ > budget_.maxSteps) {
        return core::fail(core::ErrorCode::Cancelled, QStringLiteral("script exceeded execution budget"));
    }
    if ((steps_ & 0xFFFF) == 0 && std::chrono::steady_clock::now() > deadline_) {
        return core::fail(core::ErrorCode::Cancelled, QStringLiteral("script exceeded execution budget"));
    }
    return {};
}

core::Result<Value> Interpreter::checkNotBound(int slotIndex, int line) const {
    // T-0015A: every bound-variable slot is a live value for the duration of run() (bindScriptVars
    // above) - nothing is rejected here any more. See interpreter.h for why the function stays.
    (void)slotIndex;
    (void)line;
    return Value();
}

const Routine* Interpreter::routineForSlot(int slot) const {
    for (const Routine& routine : program_.routines) {
        if (routine.firstParamSlot - 1 == slot) {
            return &routine;
        }
    }
    return nullptr;
}

DeclaredType Interpreter::leftIntType(const Node* node) const {
    switch (node->kind) {
    case NodeKind::IntLit: {
        // ReadInteger (uPSCompiler.pas:5720-5743): a lexed literal is never negative, so this is
        // exactly "r <= High(Integer)" / "r <= $FFFFFFFF" with r = value.
        const qint64 v = static_cast<const IntLit*>(node)->value;
        if (static_cast<quint64>(v) <= 0x7FFFFFFFULL) {
            return DeclaredType::Integer; // bts32
        }
        if (static_cast<quint64>(v) <= 0xFFFFFFFFULL) {
            return DeclaredType::Cardinal; // btu32 - UNSIGNED, review finding 1
        }
        return DeclaredType::Int64; // bts64
    }
    case NodeKind::VarRef: {
        const DeclaredType t = script_->declaredType(static_cast<const VarRef*>(node)->slot);
        return t == DeclaredType::Unknown ? DeclaredType::Integer : t;
    }
    case NodeKind::Unary:
        return leftIntType(static_cast<const Unary*>(node)->operand); // GetTypeNo(NewVar): unary
                                                                      // keeps the operand's type
    case NodeKind::Binary:
        return leftIntType(static_cast<const Binary*>(node)->lhs); // GetResultType: Result := t1
    case NodeKind::Call: {
        const auto* call = static_cast<const Call*>(node);
        if (call->callKind == CallKind::UserRoutine) {
            const Routine* routine = routineForSlot(call->callee);
            if (routine == nullptr || routine->resultSlot < 0) {
                return DeclaredType::Integer;
            }
            const DeclaredType t = script_->declaredType(routine->resultSlot);
            return t == DeclaredType::Unknown ? DeclaredType::Integer : t;
        }
        // Every registered/intrinsic function returns LongInt/Extended/string/... except
        // GetUnixTime (spec F35: `GetUnixTime: Int64`) - the one 64-bit-typed builtin.
        return static_cast<BuiltinId>(call->callee) == BuiltinId::GetUnixTime ? DeclaredType::Int64
                                                                              : DeclaredType::Integer;
    }
    default:
        return DeclaredType::Integer; // Field/Index/ArrayLit and anything else: no Int64 source here
    }
}

int Interpreter::intWidthBits(DeclaredType type) {
    switch (type) {
    case DeclaredType::Byte:
    case DeclaredType::ShortInt:
        return 8;
    case DeclaredType::Word:
    case DeclaredType::SmallInt:
        return 16;
    case DeclaredType::Int64:
        return 64;
    default:
        return 32; // Integer, Cardinal - and every other DeclaredType wrappedTo doesn't narrow
    }
}

// ==================================================================================================
// Expressions
// ==================================================================================================

core::Result<Value> Interpreter::evalExpr(Node* node) {
    if (auto ticked = tickBudget(node->line); !ticked.has_value()) {
        return std::unexpected(ticked.error());
    }
    switch (node->kind) {
    case NodeKind::IntLit:
        return Value::fromInt(static_cast<IntLit*>(node)->value);
    case NodeKind::RealLit:
        return Value::fromReal(static_cast<RealLit*>(node)->value);
    case NodeKind::StrLit:
        return Value::fromStr(static_cast<StrLit*>(node)->value);
    case NodeKind::BoolLit:
        return Value::fromBool(static_cast<BoolLit*>(node)->value);
    case NodeKind::NilLit:
        return Value();
    case NodeKind::VarRef: {
        auto* n = static_cast<VarRef*>(node);
        if (auto ok = checkNotBound(n->slot, n->line); !ok.has_value()) {
            return std::unexpected(ok.error());
        }
        return storage_[static_cast<std::size_t>(n->slot)];
    }
    case NodeKind::Unary:
        return evalUnary(static_cast<Unary*>(node));
    case NodeKind::Binary:
        return evalBinary(static_cast<Binary*>(node));
    case NodeKind::Call:
        return evalCall(static_cast<Call*>(node));
    case NodeKind::Index:
        return evalIndex(static_cast<Index*>(node));
    case NodeKind::Field: {
        auto* n = static_cast<Field*>(node);
        auto baseR = evalExpr(n->base);
        if (!baseR.has_value())
            return std::unexpected(baseR.error());
        const Value& base = *baseR;
        if (base.kind() != ValueKind::Record) {
            return unsupported(node->line, QStringLiteral("field access on a value that is not a record"));
        }
        const std::vector<Value>& flds = base.fields();
        if (n->fieldIndex < 0 || static_cast<std::size_t>(n->fieldIndex) >= flds.size()) {
            return runtimeError(node->line, QStringLiteral("internal error: field index out of range"));
        }
        return flds[static_cast<std::size_t>(n->fieldIndex)];
    }
    case NodeKind::ArrayLit: {
        // Set-or-array literal (ast.h G6): this slice only implements the set-of-ordinal use
        // (`[rfReplaceAll]`) - each element contributes bit `ord` to an Int bitmask.
        auto* n = static_cast<ArrayLit*>(node);
        qint64 mask = 0;
        for (Node* element : n->elements) {
            auto v = evalExpr(element);
            if (!v.has_value()) {
                return std::unexpected(v.error());
            }
            auto ord = v->toInt();
            if (!ord.has_value() || *ord < 0 || *ord > 62) {
                return unsupported(node->line, QStringLiteral("set/array literals are only supported "
                                                              "as a small ordinal set this slice"));
            }
            mask |= (qint64{1} << *ord);
        }
        return Value::fromInt(mask);
    }
    default:
        return unsupported(node->line, QStringLiteral("unsupported expression"));
    }
}

core::Result<Value> Interpreter::evalUnary(Unary* node) {
    auto operand = evalExpr(node->operand);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    switch (node->op) {
    case UnaryOp::Plus:
        return *operand;
    case UnaryOp::Minus:
        if (operand->kind() == ValueKind::Real) {
            return Value::fromReal(-*operand->toReal());
        } else {
            auto v = operand->toInt();
            if (!v.has_value()) {
                return unsupported(node->line, QStringLiteral("unary '-' needs a numeric operand"));
            }
            // Review finding 3 / T-0031 rework ruling 2: this is the NESTED path (the unary node
            // is not itself the value stored into a slot - see evalExprForSlot for that case), so
            // it still keeps the operand's own static type (uPSCompiler.pas:8148's
            // "NewVarU.aType := GetTypeNo(BlockInfo, NewVar)") and wraps there.
            return Value::fromInt(-*v).wrappedTo(leftIntType(node->operand));
        }
    case UnaryOp::Not:
        if (operand->kind() == ValueKind::Bool) {
            return Value::fromBool(!operand->isTruthy());
        } else {
            auto v = operand->toInt();
            if (!v.has_value()) {
                return unsupported(node->line, QStringLiteral("'not' needs an integer or boolean operand"));
            }
            return Value::fromInt(~*v);
        }
    }
    return unsupported(node->line, QStringLiteral("unsupported unary operator"));
}

core::Result<Value> Interpreter::evalExprForSlot(Node* node, int slot) {
    // Review finding 3 / T-0031 rework ruling 2: DoUnCalc (uPSCompiler.pas:6230-6242) is handed
    // the real destination register directly whenever a unary '-' node is not nested inside a
    // further calculation (WriteCalculation, :9604-9688) - so e.g. `R64 := -Low(Integer)` widens
    // the operand into R64's Int64 width *first*, then negates there: 2147483648, not
    // -2147483648. Only this exact shape (the Assign's RHS is directly a Unary '-' node) takes
    // this path; anything else - including `R64 := (-Low(Integer)) + 0` - falls through to
    // evalExpr/evalUnary's operand-width computation, unchanged.
    if (node->kind == NodeKind::Unary && static_cast<Unary*>(node)->op == UnaryOp::Minus) {
        auto* u = static_cast<Unary*>(node);
        auto operand = evalExpr(u->operand);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        if (operand->kind() == ValueKind::Real) {
            return Value::fromReal(-*operand->toReal());
        }
        auto raw = operand->toInt();
        if (!raw.has_value()) {
            return unsupported(node->line, QStringLiteral("unary '-' needs a numeric operand"));
        }
        const DeclaredType destType = script_->declaredType(slot);
        const Value widened = Value::fromInt(*raw).wrappedTo(destType);
        return Value::fromInt(-*widened.toInt()).wrappedTo(destType);
    }
    return evalExpr(node);
}

core::Result<Value> Interpreter::evalBinary(Binary* node) {
    auto lhsR = evalExpr(node->lhs);
    if (!lhsR.has_value()) {
        return std::unexpected(lhsR.error());
    }
    auto rhsR = evalExpr(node->rhs); // N9: both operands always evaluated, never short-circuit
    if (!rhsR.has_value()) {
        return std::unexpected(rhsR.error());
    }
    const Value& lhs = *lhsR;
    const Value& rhs = *rhsR;
    const bool eitherStr = lhs.kind() == ValueKind::Str || rhs.kind() == ValueKind::Str;
    const bool eitherReal = lhs.kind() == ValueKind::Real || rhs.kind() == ValueKind::Real;
    const bool eitherBool = lhs.kind() == ValueKind::Bool || rhs.kind() == ValueKind::Bool;
    const int line = node->line;
    // T-0031 rework (review findings 1/2): PD-104's "compute in the left operand's static integer
    // type" now goes through the exact type (`leftIntType`) and `Value::wrappedTo`, which already
    // implements every per-type wrap - Byte/Word/ShortInt/SmallInt at 8/16 bits, Cardinal
    // **unsigned** 32-bit, Integer signed 32-bit, Int64 none. `+ - *` compute the mathematically
    // exact qint64 result first: truncating to N<64 bits afterward gives the same low-N-bit pattern
    // as doing the op in an N-bit register throughout (two's-complement wraparound is associative
    // with truncation for +/-/*), so this is exact, not an approximation, for every width.
    switch (node->op) {
    case BinaryOp::Add:
        if (eitherStr) {
            return Value::fromStr(lhs.toStr() + rhs.toStr()); // T5: char/string concatenation
        }
        if (eitherReal) {
            auto a = lhs.toReal(), b = rhs.toReal();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("'+' needs numeric operands"));
            return Value::fromReal(*a + *b);
        }
        {
            auto a = lhs.toInt(), b = rhs.toInt();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("'+' needs numeric operands"));
            return Value::fromInt(*a + *b).wrappedTo(leftIntType(node->lhs));
        }
    case BinaryOp::Sub:
        if (eitherStr)
            return unsupported(line, QStringLiteral("'-' is not defined on strings"));
        if (eitherReal) {
            auto a = lhs.toReal(), b = rhs.toReal();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("'-' needs numeric operands"));
            return Value::fromReal(*a - *b);
        }
        {
            auto a = lhs.toInt(), b = rhs.toInt();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("'-' needs numeric operands"));
            return Value::fromInt(*a - *b).wrappedTo(leftIntType(node->lhs));
        }
    case BinaryOp::Mul:
        if (eitherStr)
            return unsupported(line, QStringLiteral("'*' is not defined on strings"));
        if (eitherReal) {
            auto a = lhs.toReal(), b = rhs.toReal();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("'*' needs numeric operands"));
            return Value::fromReal(*a * *b);
        }
        {
            auto a = lhs.toInt(), b = rhs.toInt();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("'*' needs numeric operands"));
            return Value::fromInt(*a * *b).wrappedTo(leftIntType(node->lhs));
        }
    // PD-3/PD-4 (T-0031, spec N5 corrected): legacy's PascalScript fork is built without
    // PS_DELPHIDIV (docs/handoffs/T-0015E-review.md; sasgis/pascalscript uPSCompiler.pas
    // ~8597-8645), so the compiler emits the SAME `otDiv` opcode for both `/` and `div` (codegen
    // ~6248: "if BVAL.Operator = otIntDiv then BlockWriteByte(..., Ord(otDiv))" - the VM never sees
    // a separate `div` instruction). GetResultType's rule for that opcode: int op int -> the
    // integer type of the left operand (real division is only picked `{$IFDEF PS_DELPHIDIV}`,
    // absent here); int/real mixed or real/real -> real. Runtime `DoCalc` case 3 (uPSRuntime.pas
    // ~6210-6400) matches: integer base types compute Pascal `div` (truncating), `btSingle`/
    // `btDouble`/`btExtended` compute real `/`. So `/` and `div` are IDENTICAL operators in legacy:
    // both are real division when either operand is real, and both are truncating integer division
    // when both operands are Int - reproduced here as one case, not two.
    case BinaryOp::RealDiv:
    case BinaryOp::IntDiv: {
        const QString opWord =
            node->op == BinaryOp::RealDiv ? QStringLiteral("'/'") : QStringLiteral("'div'");
        if (eitherReal) {
            auto a = lhs.toReal(), b = rhs.toReal();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, opWord + QStringLiteral(" needs numeric operands"));
            // Review finding 7 / ruling 3: real division by zero is EZeroDivide, whose message the
            // engine surfaces verbatim (uPSRuntime.pas:6679) - distinct from the integer EDivByZero
            // text below.
            if (*b == 0.0L)
                return runtimeError(line, QStringLiteral("Floating point division by zero"));
            return Value::fromReal(*a / *b);
        }
        {
            auto a = lhs.toInt(), b = rhs.toInt();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, opWord + QStringLiteral(" needs numeric operands"));
            if (*b == 0)
                return runtimeError(line, QStringLiteral("Division by zero")); // EDivByZero text
            const DeclaredType t = leftIntType(node->lhs);
            if (isIntDivOverflow(*a, *b, t)) {
                // Review finding 7 / ruling 3: Low(T) div -1 is a signed IDIV overflow on x86 - a
                // SEPARATE hardware fault from divide-by-zero (STATUS_INTEGER_OVERFLOW, not
                // STATUS_INTEGER_DIVIDE_BY_ZERO), which DoCalc's except block (uPSRuntime.pas
                // :6673-6684) does not special-case: it falls through to "Tmp is Exception" and
                // surfaces EIntOverflow's own message, not "Division by zero".
                return runtimeError(line, QStringLiteral("Integer overflow"));
            }
            // N3: truncates toward zero (matches C++'s integer '/'); wrapping the result is a
            // no-op here (division only shrinks magnitude, the overflow case is handled above) but
            // keeps this case uniform with +/-/*/mod/shl/shr per the ruling.
            return Value::fromInt(*a / *b).wrappedTo(t);
        }
    }
    case BinaryOp::Mod: { // N3: truncates toward zero (matches C++'s integer '%')
        auto a = lhs.toInt(), b = rhs.toInt();
        if (!a.has_value() || !b.has_value())
            return unsupported(line, QStringLiteral("'mod' needs integer operands"));
        if (*b == 0)
            return runtimeError(line, QStringLiteral("Division by zero"));
        return Value::fromInt(*a % *b).wrappedTo(leftIntType(node->lhs));
    }
    case BinaryOp::Shl: { // PD-104: shifts within the left operand's static integer type
        auto a = lhs.toInt(), b = rhs.toInt();
        if (!a.has_value() || !b.has_value())
            return unsupported(line, QStringLiteral("'shl' needs integer operands"));
        const DeclaredType t = leftIntType(node->lhs);
        // x86 SHL r/m8|16|32, CL masks CL to 5 bits (mod 32) regardless of the destination
        // operand's own size - only a 64-bit destination widens the mask to 6 bits (mod 64). Not
        // "mod the operand's own width": a Byte shl 9 shifts by 9 actual bit positions, not 1.
        const quint64 countMask = intWidthBits(t) <= 32 ? 31u : 63u;
        const quint64 shifted = static_cast<quint64>(*a) << (static_cast<quint64>(*b) & countMask);
        // Truncating the wide shift result to N bits afterward matches doing the shift in an N-bit
        // register throughout (bits shifted out past bit N-1 are simply discarded either way).
        return Value::fromInt(static_cast<qint64>(shifted)).wrappedTo(t);
    }
    case BinaryOp::Shr: { // N8: logical shift on the unsigned interpretation, at the static width
        auto a = lhs.toInt(), b = rhs.toInt();
        if (!a.has_value() || !b.has_value())
            return unsupported(line, QStringLiteral("'shr' needs integer operands"));
        const DeclaredType t = leftIntType(node->lhs);
        const int width = intWidthBits(t);
        const quint64 countMask = width <= 32 ? 31u : 63u; // same x86 CL-masking rule as Shl above
        const quint64 valueMask = width < 64 ? ((quint64{1} << width) - 1) : ~quint64{0};
        // Unlike Shl, the vacated high bits matter: mask the operand down to its OWN width first so
        // shr zero-fills from that width's top bit, not from qint64's - e.g. Byte(0xFF) shr 10 must
        // shift 10 real positions within an 8-bit register (giving 0), not fill in from bit 63.
        const quint64 operand = static_cast<quint64>(*a) & valueMask;
        const quint64 shifted = operand >> (static_cast<quint64>(*b) & countMask);
        return Value::fromInt(static_cast<qint64>(shifted)).wrappedTo(t);
    }
    case BinaryOp::And:
    case BinaryOp::Or:
    case BinaryOp::Xor: { // N9: bitwise on integers, logical on booleans
        if (eitherBool) {
            auto a = lhs.toBool(), b = rhs.toBool();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("boolean operator needs boolean operands"));
            const bool result = node->op == BinaryOp::And  ? (*a && *b)
                                : node->op == BinaryOp::Or ? (*a || *b)
                                                           : (*a != *b);
            return Value::fromBool(result);
        }
        auto a = lhs.toInt(), b = rhs.toInt();
        if (!a.has_value() || !b.has_value())
            return unsupported(line, QStringLiteral("bitwise operator needs integer operands"));
        const qint64 result = node->op == BinaryOp::And  ? (*a & *b)
                              : node->op == BinaryOp::Or ? (*a | *b)
                                                         : (*a ^ *b);
        return Value::fromInt(result);
    }
    case BinaryOp::Eq:
    case BinaryOp::Ne:
    case BinaryOp::Lt:
    case BinaryOp::Gt:
    case BinaryOp::Le:
    case BinaryOp::Ge: {
        int cmp = 0;
        if (eitherStr) { // T8: byte-wise
            cmp = lhs.toStr().compare(rhs.toStr());
        } else if (eitherReal) { // N10
            auto a = lhs.toReal(), b = rhs.toReal();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("comparison needs numeric operands"));
            cmp = (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
        } else {
            auto a = lhs.toInt(), b = rhs.toInt();
            if (!a.has_value() || !b.has_value())
                return unsupported(line, QStringLiteral("comparison needs comparable operands"));
            cmp = (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
        }
        bool result = false;
        switch (node->op) {
        case BinaryOp::Eq:
            result = cmp == 0;
            break;
        case BinaryOp::Ne:
            result = cmp != 0;
            break;
        case BinaryOp::Lt:
            result = cmp < 0;
            break;
        case BinaryOp::Gt:
            result = cmp > 0;
            break;
        case BinaryOp::Le:
            result = cmp <= 0;
            break;
        case BinaryOp::Ge:
            result = cmp >= 0;
            break;
        default:
            break;
        }
        return Value::fromBool(result);
    }
    }
    return unsupported(line, QStringLiteral("unsupported binary operator"));
}

core::Result<std::size_t> Interpreter::arrayElementOffset(const Value& array, qint64 index, int line) const {
    const std::size_t size = array.fields().size();
    // uPSCompiler.pas ~6965-6988: the compiler subtracts the declared low bound from a non-
    // constant index into a Cardinal (btU32) temp BEFORE the generic array-access opcode runs, so
    // the bounds check below (uPSRuntime.pas:7127-7135/:7289-7297) sees that already-wrapped
    // 32-bit magnitude, not the signed offset. `static_cast<qint32>` first keeps the reinterpret
    // exact for the in-range values every corpus script actually computes.
    const auto offset = static_cast<quint32>(static_cast<qint32>(index - array.lowBound()));
    if (offset >= static_cast<quint32>(size)) {
        return runtimeError(
            line,
            QStringLiteral("Static array index out of bounds (%1). Length is %2.").arg(offset).arg(size));
    }
    return static_cast<std::size_t>(offset);
}

core::Result<Value> Interpreter::evalIndex(Index* node) {
    auto baseR = evalExpr(node->base);
    if (!baseR.has_value())
        return std::unexpected(baseR.error());
    const Value& base = *baseR;
    if (base.kind() == ValueKind::Array) {
        auto idxR = evalExpr(node->index);
        if (!idxR.has_value())
            return std::unexpected(idxR.error());
        auto idx = idxR->toInt();
        if (!idx.has_value())
            return unsupported(node->line, QStringLiteral("array index must be an integer"));
        auto offset = arrayElementOffset(base, *idx, node->line);
        if (!offset.has_value())
            return std::unexpected(offset.error());
        return base.fields()[*offset];
    }
    if (base.kind() != ValueKind::Str && base.kind() != ValueKind::Empty) {
        return unsupported(node->line, QStringLiteral("indexing is only supported on strings this slice"));
    }
    auto idxR = evalExpr(node->index);
    if (!idxR.has_value())
        return std::unexpected(idxR.error());
    auto idx = idxR->toInt();
    if (!idx.has_value())
        return unsupported(node->line, QStringLiteral("string index must be an integer"));
    const QByteArray s = base.toStr();
    const qint64 i = *idx;
    if (i >= 1 && i <= s.size()) {
        return Value::fromStr(QByteArray(1, s.at(static_cast<int>(i - 1))));
    }
    if (i > s.size() && i <= s.size() + 16) { // T3: bug-compatible read-past-end window
        return Value::fromStr(QByteArray(1, '\0'));
    }
    return runtimeError(node->line, QStringLiteral("string index out of range"));
}

// ==================================================================================================
// Calls
// ==================================================================================================

core::Result<Value> Interpreter::evalCall(Call* node) {
    if (auto ticked = tickBudget(node->line); !ticked.has_value()) {
        return std::unexpected(ticked.error());
    }
    switch (node->callKind) {
    case CallKind::Method:
        return evalMethodCall(node);
    case CallKind::UserRoutine: {
        const Routine* routine = routineForSlot(node->callee);
        if (routine == nullptr) {
            return runtimeError(node->line, QStringLiteral("internal error: unresolved routine"));
        }
        return callUserRoutine(*routine, node);
    }
    case CallKind::Builtin:
    case CallKind::Registered:
        try {
            return callBuiltin(static_cast<BuiltinId>(node->callee), node);
        } catch (const std::exception& e) { // IScriptHost free functions: external procs too
            return runtimeError(node->line, QString::fromUtf8(e.what()));
        }
    }
    return unsupported(node->line, QStringLiteral("unsupported call kind"));
}

core::Result<Value> Interpreter::callUserRoutine(const Routine& routine, Call* node) {
    const int first = routine.firstParamSlot;
    const int count = routine.frameSize;
    const int line = node->line;

    // Evaluate every argument BEFORE the frame is touched (recursion reuses these exact slot
    // numbers on each call, spec 4.4 I4 - an argument that reads one of them must see the
    // caller's value, not a half-zeroed callee frame).
    std::vector<Value> ins(static_cast<std::size_t>(routine.paramCount));
    for (int i = 0; i < routine.paramCount; ++i) {
        const ParamMode mode = routine.paramModes[static_cast<std::size_t>(i)];
        Node* argNode = node->args[static_cast<std::size_t>(i)];
        if (mode == ParamMode::Out) {
            if (argNode->kind != NodeKind::VarRef) {
                return unsupported(line, QStringLiteral("an 'out' argument must be a simple variable"));
            }
            continue; // zero-initialised, never read from the caller
        }
        if (mode == ParamMode::Var) {
            if (argNode->kind != NodeKind::VarRef) {
                return unsupported(line, QStringLiteral("a 'var' argument must be a simple variable"));
            }
            ins[static_cast<std::size_t>(i)] =
                storage_[static_cast<std::size_t>(static_cast<VarRef*>(argNode)->slot)];
            continue;
        }
        auto v = evalExpr(argNode);
        if (!v.has_value()) {
            return std::unexpected(v.error());
        }
        ins[static_cast<std::size_t>(i)] = *v;
    }

    // spec 4.3 A5: `var`/`out` are by-reference in Delphi. This slice keeps copy-in/copy-out
    // (T-0014 rework Major 5 - true aliasing needs a slot-indirection rewrite of the flat-slot VM,
    // beyond this ticket's LOC budget); the one place that silently diverges from Delphi is two
    // `var`/`out` arguments naming the SAME slot (`P(x, x)`), where Delphi's writes-through-the-
    // same-cell give a different result than copy-in/copy-out's last-write-wins. Fail loudly
    // instead of diverging silently; true var-by-reference is recorded for T-0014B.
    for (int i = 0; i < routine.paramCount; ++i) {
        const ParamMode modeI = routine.paramModes[static_cast<std::size_t>(i)];
        if (modeI != ParamMode::Var && modeI != ParamMode::Out)
            continue;
        const int slotI = static_cast<VarRef*>(node->args[static_cast<std::size_t>(i)])->slot;
        for (int j = i + 1; j < routine.paramCount; ++j) {
            const ParamMode modeJ = routine.paramModes[static_cast<std::size_t>(j)];
            if (modeJ != ParamMode::Var && modeJ != ParamMode::Out)
                continue;
            const int slotJ = static_cast<VarRef*>(node->args[static_cast<std::size_t>(j)])->slot;
            if (slotI == slotJ) {
                return unsupported(line, QStringLiteral("aliased 'var'/'out' arguments are not "
                                                        "supported this slice (T-0014B)"));
            }
        }
    }

    std::vector<Value> saved(storage_.begin() + first, storage_.begin() + first + count);
    for (int i = 0; i < count; ++i) {
        storage_[static_cast<std::size_t>(first) + static_cast<std::size_t>(i)] =
            defaultValueForSlot(first + i); // I1: zero-init at frame entry
    }
    for (int i = 0; i < routine.paramCount; ++i) {
        if (routine.paramModes[static_cast<std::size_t>(i)] != ParamMode::Out) {
            const int slot = first + i;
            storage_[static_cast<std::size_t>(slot)] = wrapForSlot(
                slot, ins[static_cast<std::size_t>(i)]); // PD-2/N1/N2: bind through the declared type
        }
    }

    auto signal = execStmt(routine.body);

    core::Result<Value> outcome;
    if (!signal.has_value()) {
        outcome = std::unexpected(signal.error());
    } else {
        outcome = routine.resultSlot >= 0 ? storage_[static_cast<std::size_t>(routine.resultSlot)] : Value();
    }

    std::vector<Value> outs;
    if (signal.has_value()) {
        outs.resize(static_cast<std::size_t>(routine.paramCount));
        for (int i = 0; i < routine.paramCount; ++i) {
            const ParamMode mode = routine.paramModes[static_cast<std::size_t>(i)];
            if (mode == ParamMode::Var || mode == ParamMode::Out) {
                outs[static_cast<std::size_t>(i)] =
                    storage_[static_cast<std::size_t>(first) + static_cast<std::size_t>(i)];
            }
        }
    }

    for (int i = 0; i < count; ++i) { // restore the caller's/outer recursion level's frame
        storage_[static_cast<std::size_t>(first) + static_cast<std::size_t>(i)] =
            saved[static_cast<std::size_t>(i)];
    }

    if (signal.has_value()) {
        for (int i = 0; i < routine.paramCount; ++i) {
            const ParamMode mode = routine.paramModes[static_cast<std::size_t>(i)];
            if (mode == ParamMode::Var || mode == ParamMode::Out) {
                auto* vr = static_cast<VarRef*>(node->args[static_cast<std::size_t>(i)]);
                if (auto ok = checkNotBound(vr->slot, line); !ok.has_value())
                    return std::unexpected(ok.error());
                storage_[static_cast<std::size_t>(vr->slot)] =
                    wrapForSlot(vr->slot, outs[static_cast<std::size_t>(i)]); // N1/N2
            }
        }
    }
    return outcome;
}

core::Result<Value> Interpreter::callBuiltin(BuiltinId id, Call* node) {
    if (auto ticked = tickBudget(node->line); !ticked.has_value()) {
        return std::unexpected(ticked.error());
    }
    const int line = node->line;

    // Arity must be checked before ANY node->args[i]/evalExpr(args[i]) access below - the parser
    // deliberately does not enforce kExternals' minArgs/maxArgs for Builtin/Registered calls
    // (parser.cpp ~1420-1430), so a malformed script can otherwise drive an out-of-bounds
    // std::vector access (T-0014 rework Blocker 2).
    const ExternalSymbol& sym = SymbolTable::externalSymbols()[static_cast<std::size_t>(id)];
    const std::size_t argc = node->args.size();
    if (argc < sym.minArgs || (sym.maxArgs != 0xFF && argc > sym.maxArgs)) {
        return unsupported(
            line, QStringLiteral("%1 expects %2 argument(s), got %3")
                      .arg(QString::fromUtf8(sym.name))
                      .arg(sym.maxArgs == 0xFF
                               ? QStringLiteral("at least %1").arg(sym.minArgs)
                               : (sym.minArgs == sym.maxArgs
                                      ? QString::number(sym.minArgs)
                                      : QStringLiteral("%1 to %2").arg(sym.minArgs).arg(sym.maxArgs)))
                      .arg(argc));
    }

    // --- by-reference intrinsics: need the raw argument node, not its evaluated value ---
    switch (id) {
    case BuiltinId::Inc:
    case BuiltinId::Dec: {
        if (node->args.empty() || node->args[0]->kind != NodeKind::VarRef) {
            return unsupported(line, QStringLiteral("Inc/Dec require a simple variable"));
        }
        auto* vr = static_cast<VarRef*>(node->args[0]);
        if (auto ok = checkNotBound(vr->slot, line); !ok.has_value())
            return std::unexpected(ok.error());
        qint64 delta = 1;
        if (node->args.size() > 1) {
            auto d = evalExpr(node->args[1]);
            if (!d.has_value())
                return std::unexpected(d.error());
            auto di = d->toInt();
            if (!di.has_value())
                return unsupported(line, QStringLiteral("Inc/Dec step must be an integer"));
            delta = *di;
        }
        auto cur = storage_[static_cast<std::size_t>(vr->slot)].toInt();
        if (!cur.has_value())
            return unsupported(line, QStringLiteral("Inc/Dec target must be an integer"));
        storage_[static_cast<std::size_t>(vr->slot)] =
            wrapForSlot(vr->slot, Value::fromInt(*cur + (id == BuiltinId::Inc ? delta : -delta))); // N1
        return Value();
    }
    case BuiltinId::SetLength: {
        if (node->args.size() != 2 || node->args[0]->kind != NodeKind::VarRef) {
            return unsupported(line, QStringLiteral("SetLength requires a simple variable"));
        }
        auto* vr = static_cast<VarRef*>(node->args[0]);
        if (auto ok = checkNotBound(vr->slot, line); !ok.has_value())
            return std::unexpected(ok.error());
        Value& target = storage_[static_cast<std::size_t>(vr->slot)];
        if (target.kind() != ValueKind::Str && target.kind() != ValueKind::Empty) {
            return unsupported(line, QStringLiteral("SetLength is only supported on strings this slice"));
        }
        auto n = evalExpr(node->args[1]);
        if (!n.has_value())
            return std::unexpected(n.error());
        auto ni = n->toInt();
        if (!ni.has_value() || *ni < 0)
            return unsupported(line, QStringLiteral("SetLength size must be a non-negative integer"));
        QByteArray s = target.toStr();
        s.resize(static_cast<int>(*ni), '\0');
        target = Value::fromStr(std::move(s));
        return Value();
    }
    case BuiltinId::Insert: { // Insert(src, var dst, i)
        if (node->args.size() != 3 || node->args[1]->kind != NodeKind::VarRef) {
            return unsupported(line, QStringLiteral("Insert requires a simple variable as the 2nd argument"));
        }
        auto src = evalExpr(node->args[0]);
        if (!src.has_value())
            return std::unexpected(src.error());
        auto* vr = static_cast<VarRef*>(node->args[1]);
        if (auto ok = checkNotBound(vr->slot, line); !ok.has_value())
            return std::unexpected(ok.error());
        auto iv = evalExpr(node->args[2]);
        if (!iv.has_value())
            return std::unexpected(iv.error());
        auto ii = iv->toInt();
        if (!ii.has_value())
            return unsupported(line, QStringLiteral("Insert position must be an integer"));
        QByteArray dst = storage_[static_cast<std::size_t>(vr->slot)].toStr();
        const qint64 pos = std::clamp<qint64>(*ii, 1, dst.size() + 1);
        dst.insert(static_cast<int>(pos - 1), src->toStr());
        storage_[static_cast<std::size_t>(vr->slot)] = Value::fromStr(std::move(dst));
        return Value();
    }
    case BuiltinId::Delete: { // Delete(var s, i, n)
        if (node->args.size() != 3 || node->args[0]->kind != NodeKind::VarRef) {
            return unsupported(line, QStringLiteral("Delete requires a simple variable as the 1st argument"));
        }
        auto* vr = static_cast<VarRef*>(node->args[0]);
        if (auto ok = checkNotBound(vr->slot, line); !ok.has_value())
            return std::unexpected(ok.error());
        auto iv = evalExpr(node->args[1]);
        auto nv = evalExpr(node->args[2]);
        if (!iv.has_value())
            return std::unexpected(iv.error());
        if (!nv.has_value())
            return std::unexpected(nv.error());
        auto ii = iv->toInt();
        auto nn = nv->toInt();
        if (!ii.has_value() || !nn.has_value())
            return unsupported(line, QStringLiteral("Delete arguments must be integers"));
        QByteArray s = storage_[static_cast<std::size_t>(vr->slot)].toStr();
        const qint64 pos = *ii;
        qint64 cnt = *nn;
        if (pos >= 1 && pos <= s.size() && cnt > 0) {
            cnt = std::min<qint64>(cnt, s.size() - pos + 1);
            s.remove(static_cast<int>(pos - 1), static_cast<int>(cnt));
        }
        storage_[static_cast<std::size_t>(vr->slot)] = Value::fromStr(std::move(s));
        return Value();
    }
    case BuiltinId::Format:
    case BuiltinId::FormatEx: {
        // Format(fmt, [args...]) / FormatEx(fmt, [args...], decimalSeparator): the 2nd argument is
        // an open-array literal (ast.h ArrayLit, "G6") whose elements must reach delphiFormat as
        // individual Values - NOT the ordinal-set bitmask the generic ArrayLit evaluation (used by
        // StringReplace's `[rfReplaceAll]`) produces. So this needs the raw node, like Inc/Dec/
        // Insert/Delete above, rather than the generic post-loop `args` vector.
        const bool isFormatEx = id == BuiltinId::FormatEx;
        if (node->args.size() != (isFormatEx ? 3u : 2u) || node->args[1]->kind != NodeKind::ArrayLit) {
            return unsupported(line, QStringLiteral("%1 requires (formatString, [args]%2)")
                                         .arg(QString::fromUtf8(isFormatEx ? "FormatEx" : "Format"))
                                         .arg(isFormatEx ? QStringLiteral(", decimalSeparator") : QString()));
        }
        auto fmtV = evalExpr(node->args[0]);
        if (!fmtV.has_value())
            return std::unexpected(fmtV.error());
        auto* lit = static_cast<ArrayLit*>(node->args[1]);
        std::vector<Value> fmtArgs;
        fmtArgs.reserve(lit->elements.size());
        for (Node* e : lit->elements) {
            auto v = evalExpr(e);
            if (!v.has_value())
                return std::unexpected(v.error());
            fmtArgs.push_back(*v);
        }
        char decimalSeparator = '.';
        char thousandsSeparator = ',';
        QByteArray currencyString;
        if (isFormatEx) {
            auto sepV = evalExpr(node->args[2]);
            if (!sepV.has_value())
                return std::unexpected(sepV.error());
            const QByteArray sepStr = sepV->toStr();
            decimalSeparator = sepStr.isEmpty() ? '.' : sepStr.at(0);
            // F21: a fresh, default-initialised TFormatSettings with only DecimalSeparator set -
            // ThousandSeparator is the zero char, CurrencyString is empty - "%n"/"%m" produce
            // garbage. Reproduced deliberately, not fixed up.
            thousandsSeparator = '\0';
            currencyString = QByteArray();
        }
        auto out = delphiFormat(fmtV->toStr(), fmtArgs, decimalSeparator, thousandsSeparator, currencyString);
        if (!out.has_value())
            return runtimeError(line, out.error().what());
        return Value::fromStr(*out);
    }
    default:
        break;
    }

    // --- everything else: evaluate all arguments by value ---
    std::vector<Value> args;
    args.reserve(node->args.size());
    for (Node* a : node->args) {
        auto v = evalExpr(a);
        if (!v.has_value())
            return std::unexpected(v.error());
        args.push_back(*v);
    }
    auto argInt = [&](std::size_t i) { return args[i].toInt(); };
    auto argReal = [&](std::size_t i) { return args[i].toReal(); };
    auto needNumeric = [&](const char* op) {
        return unsupported(line, QStringLiteral("'%1' needs numeric arguments").arg(op));
    };

    switch (id) {
    // ---- Math (registered, host-backed RNG) ---- F1/F2, spec :1053-1055
    case BuiltinId::Random: {
        auto x = argInt(0);
        if (!x.has_value())
            return needNumeric("Random");
        if (*x <= 0) // Delphi Random(X) for X <= 0: 0, never calls the generator
            return Value::fromInt(0);
        return Value::fromInt(host_->random(static_cast<qint32>(*x)));
    }
    case BuiltinId::RandomRange: {
        auto from = argInt(0), to = argInt(1);
        if (!from.has_value() || !to.has_value())
            return needNumeric("RandomRange");
        const bool swapped = *from > *to;
        const qint64 span = swapped ? *from - *to : *to - *from;
        const qint32 r = span <= 0 ? 0 : host_->random(static_cast<qint32>(span));
        return Value::fromInt(r + (swapped ? *to : *from));
    }
    case BuiltinId::Power: {
        auto b = argReal(0), e = argReal(1);
        if (!b.has_value() || !e.has_value())
            return needNumeric("Power");
        if (*e == 0.0L)
            return Value::fromReal(1.0L);
        if (*b == 0.0L && *e > 0.0L)
            return Value::fromReal(0.0L);
        if (*e == std::truncl(*e) &&
            std::fabs(*e) <= static_cast<long double>(std::numeric_limits<int>::max())) {
            return Value::fromReal(intPower(*b, static_cast<qint64>(*e)));
        }
        if (*b < 0.0L)
            return runtimeError(line, QStringLiteral("Power: negative base with a non-integral exponent"));
        return Value::fromReal(std::exp(*e * std::log(*b)));
    }
    case BuiltinId::IntPower: {
        auto b = argReal(0);
        auto e = argInt(1);
        if (!b.has_value() || !e.has_value())
            return needNumeric("IntPower");
        if (*e == 0)
            return Value::fromReal(1.0L);
        if (*b == 0.0L && *e < 0)
            return runtimeError(line, QStringLiteral("Division by zero"));
        return Value::fromReal(intPower(*b, *e));
    }
    case BuiltinId::Ceil: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Ceil");
        const long double t = std::truncl(*x);
        return Value::fromInt(static_cast<qint64>(t) + ((*x - t) > 0.0L ? 1 : 0));
    }
    case BuiltinId::Floor: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Floor");
        const long double t = std::truncl(*x);
        return Value::fromInt(static_cast<qint64>(t) - ((*x - t) < 0.0L ? 1 : 0));
    }
    case BuiltinId::Log2: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Log2");
        if (*x <= 0.0L)
            return runtimeError(line, QStringLiteral("Log2: argument must be positive"));
        return Value::fromReal(std::log2l(*x));
    }
    case BuiltinId::Ln: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Ln");
        if (*x <= 0.0L)
            return runtimeError(line, QStringLiteral("Ln: argument must be positive"));
        return Value::fromReal(std::log(*x));
    }
    case BuiltinId::Max: {
        auto a = argInt(0), b = argInt(1);
        if (!a.has_value() || !b.has_value())
            return needNumeric("Max");
        return Value::fromInt(std::max(*a, *b));
    }
    case BuiltinId::Min: {
        auto a = argInt(0), b = argInt(1);
        if (!a.has_value() || !b.has_value())
            return needNumeric("Min");
        return Value::fromInt(std::min(*a, *b));
    }
    case BuiltinId::MaxExt: {
        auto a = argReal(0), b = argReal(1);
        if (!a.has_value() || !b.has_value())
            return needNumeric("MaxExt");
        return Value::fromReal(std::max(*a, *b));
    }
    case BuiltinId::MinExt: {
        auto a = argReal(0), b = argReal(1);
        if (!a.has_value() || !b.has_value())
            return needNumeric("MinExt");
        return Value::fromReal(std::min(*a, *b));
    }
    // ---- Regex (registered, pure) ----  F13/F14, spec :1073-1078, §6 :1225-1324
    case BuiltinId::RegExprGetMatchSubStr: {
        const QByteArray str = args[0].toStr();
        const QByteArray pattern = args[1].toStr();
        auto matchId = argInt(2);
        if (!matchId.has_value())
            return needNumeric("RegExprGetMatchSubStr");
        const CompiledRegex cre = compileTRegExpr(pattern);
        if (!cre.ok) // R16: an invalid pattern is a runtime error, never a silent empty match
            return runtimeError(line, QStringLiteral("RegExprGetMatchSubStr: %1").arg(cre.error));
        return Value::fromStr(tRegExprMatch(str, pattern, static_cast<int>(*matchId)));
    }
    case BuiltinId::RegExprReplaceMatchSubStr: {
        const QByteArray str = args[0].toStr();
        const QByteArray pattern = args[1].toStr();
        const QByteArray replace = args[2].toStr();
        const CompiledRegex cre = compileTRegExpr(pattern);
        if (!cre.ok) // R16
            return runtimeError(line, QStringLiteral("RegExprReplaceMatchSubStr: %1").arg(cre.error));
        return Value::fromStr(tRegExprReplace(str, pattern, replace));
    }
    // ---- Base64 / MD5 (registered, pure) ----
    case BuiltinId::Base64Encode:
        return Value::fromStr(args[0].toStr().toBase64());
    case BuiltinId::Base64UrlEncode:
        return Value::fromStr(
            args[0].toStr().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    case BuiltinId::Base64Decode: // F17: standard alphabet only, padding required, else ''.
        return Value::fromStr(base64DecodeStrict(args[0].toStr()));
    case BuiltinId::MD5String:
        return Value::fromStr(
            QCryptographicHash::hash(args[0].toStr(), QCryptographicHash::Md5).toHex().toUpper());
    // ---- Utils (registered, pure) ----
    case BuiltinId::IntToHex: {
        auto v = argInt(0);
        auto d = argInt(1);
        if (!v.has_value() || !d.has_value())
            return needNumeric("IntToHex");
        return Value::fromStr(core::intToHex(*v, static_cast<int>(*d)));
    }
    case BuiltinId::FileExists: // F19
        return Value::fromBool(host_->fileExists(QString::fromUtf8(args[0].toStr())));
    case BuiltinId::RoundEx: { // F25
        auto v = argReal(0);
        auto p = argInt(1);
        if (!v.has_value() || !p.has_value())
            return needNumeric("RoundEx");
        return Value::fromStr(core::roundEx(static_cast<double>(*v), static_cast<int>(*p)));
    }
    case BuiltinId::PosEx: {
        const QByteArray needle = args[0].toStr(), hay = args[1].toStr();
        auto off = argInt(2);
        if (!off.has_value())
            return needNumeric("PosEx");
        if (needle.isEmpty() || *off < 1 || *off > hay.size())
            return Value::fromInt(0);
        const int idx = static_cast<int>(hay.indexOf(needle, static_cast<int>(*off - 1)));
        return Value::fromInt(idx < 0 ? 0 : idx + 1);
    }
    case BuiltinId::SubStrPos: { // (haystack, needle, from) - the opposite order of PosEx
        const QByteArray hay = args[0].toStr(), needle = args[1].toStr();
        auto from = argInt(2);
        if (!from.has_value())
            return needNumeric("SubStrPos");
        if (needle.isEmpty() || *from < 1 || *from > hay.size())
            return Value::fromInt(0);
        const int idx = static_cast<int>(hay.indexOf(needle, static_cast<int>(*from - 1)));
        return Value::fromInt(idx < 0 ? 0 : idx + 1);
    }
    case BuiltinId::GetAfter: { // (needle, haystack)
        const QByteArray sub = args[0].toStr(), s = args[1].toStr();
        if (sub.isEmpty())
            return Value::fromStr(QByteArray());
        const int i = static_cast<int>(s.indexOf(sub));
        return Value::fromStr(i < 0 ? QByteArray() : s.mid(i + sub.size()));
    }
    case BuiltinId::GetBefore: { // (needle, haystack)
        const QByteArray sub = args[0].toStr(), s = args[1].toStr();
        if (sub.isEmpty())
            return Value::fromStr(QByteArray());
        const int i = static_cast<int>(s.indexOf(sub));
        return Value::fromStr(i < 0 ? QByteArray() : s.left(i));
    }
    case BuiltinId::GetBetween: { // (haystack, after, before) = GetBefore(before, GetAfter(after, s))
        const QByteArray s = args[0].toStr(), after = args[1].toStr(), before = args[2].toStr();
        if (after.isEmpty() || before.isEmpty())
            return Value::fromStr(QByteArray());
        const int i = static_cast<int>(s.indexOf(after));
        if (i < 0)
            return Value::fromStr(QByteArray());
        const QByteArray mid = s.mid(i + after.size());
        const int j = static_cast<int>(mid.indexOf(before));
        return Value::fromStr(j < 0 ? QByteArray() : mid.left(j));
    }
    case BuiltinId::GetNumberAfter: {
        const QByteArray sub = args[0].toStr(), s = args[1].toStr();
        const int i = static_cast<int>(s.indexOf(sub));
        if (sub.isEmpty() || i < 0)
            return Value::fromStr(QByteArray());
        int start = i + static_cast<int>(sub.size());
        int end = start;
        while (end < s.size() && s.at(end) >= '0' && s.at(end) <= '9')
            ++end;
        return Value::fromStr(s.mid(start, end - start));
    }
    case BuiltinId::GetDiv3Path:
        return Value::fromStr(getDiv3Path(args[0].toStr()));
    // ---- host-backed (F35-F38) ----
    case BuiltinId::GetUnixTime: // F35: true UTC seconds (D-7 - legacy's local-as-UTC quirk is
                                 // not reproduced, see docs/parity/deviations.md)
        return Value::fromInt(host_->unixTime());
    case BuiltinId::SaveToLocalFile: // F36 (AFullLocalFilename, AData)
        return Value::fromInt(host_->saveToLocalFile(QString::fromUtf8(args[0].toStr()), args[1].toStr()));
    case BuiltinId::WriteLn: // F37: a no-op at tile-download time in legacy (Obj = nil); the
                             // production host mirrors that, the fake host records the call
        host_->writeLn(args[0].toStr());
        return Value();
    case BuiltinId::TemplateToUrl: // F38: delegates to the maptype-owned template engine
        return Value::fromStr(host_->templateToUrl(args[0].toStr()));
    case BuiltinId::SetHeaderValue: // F29 (AHeaders, AName, AValue)
        return Value::fromStr(core::scriptSetHeaderValue(args[0].toStr(), args[1].toStr(), args[2].toStr()));
    case BuiltinId::GetHeaderValue: // F30 (AHeaders, AName)
        return Value::fromStr(core::scriptGetHeaderValue(args[0].toStr(), args[1].toStr()));
    case BuiltinId::DeleteHeaderEntry: // F31 (AHeaders, AName)
        return Value::fromStr(core::scriptDeleteHeaderEntry(args[0].toStr(), args[1].toStr()));
    case BuiltinId::StringReplace: { // (S, OldPattern, NewPattern, [rfReplaceAll|rfIgnoreCase])
        const QByteArray s = args[0].toStr(), oldP = args[1].toStr(), newP = args[2].toStr();
        auto flags = argInt(3);
        if (!flags.has_value())
            return unsupported(line, QStringLiteral("StringReplace flags must be a set literal"));
        const bool replaceAll = (*flags & 0x1) != 0;
        const bool ignoreCase = (*flags & 0x2) != 0;
        if (oldP.isEmpty())
            return Value::fromStr(s);
        QByteArray result;
        int pos = 0;
        for (;;) {
            const int idx = findByteString(s, oldP, pos, ignoreCase);
            if (idx < 0) {
                result += s.mid(pos);
                break;
            }
            result += s.mid(pos, idx - pos);
            result += newP;
            pos = idx + static_cast<int>(oldP.size());
            if (!replaceAll) {
                result += s.mid(pos);
                break;
            }
        }
        return Value::fromStr(result);
    }
    // ---- Conversions (intrinsics) ----
    case BuiltinId::IntToStr: {
        auto v = argInt(0);
        if (!v.has_value())
            return needNumeric("IntToStr");
        return Value::fromStr(core::intToStr(*v));
    }
    case BuiltinId::StrToInt: {
        auto v = parseDelphiInt(args[0].toStr());
        if (!v.has_value())
            return runtimeError(line, QStringLiteral("StrToInt: invalid integer"));
        return Value::fromInt(*v);
    }
    case BuiltinId::StrToIntDef: {
        auto v = parseDelphiInt(args[0].toStr());
        return v.has_value() ? Value::fromInt(*v) : args[1];
    }
    case BuiltinId::StrToFloat: {
        bool ok = false;
        const double d = args[0].toStr().toDouble(&ok);
        if (!ok)
            return runtimeError(line, QStringLiteral("StrToFloat: invalid number"));
        return Value::fromReal(static_cast<long double>(d));
    }
    case BuiltinId::FloatToStr:
        return Value::fromStr(args[0].toStr());
    // ---- Strings (intrinsics) ----
    case BuiltinId::Copy: {
        const QByteArray s = args[0].toStr();
        auto idx = argInt(1), cnt = argInt(2);
        if (!idx.has_value() || !cnt.has_value())
            return needNumeric("Copy");
        const qint64 i = std::max<qint64>(*idx, 1);
        qint64 n = *cnt;
        if (n < 0 || i > s.size())
            return Value::fromStr(QByteArray());
        n = std::min(n, static_cast<qint64>(s.size()) - i + 1);
        return Value::fromStr(s.mid(static_cast<int>(i - 1), static_cast<int>(n)));
    }
    case BuiltinId::Pos: { // (needle, haystack)
        const QByteArray sub = args[0].toStr(), s = args[1].toStr();
        if (sub.isEmpty())
            return Value::fromInt(0);
        const int idx = static_cast<int>(s.indexOf(sub));
        return Value::fromInt(idx < 0 ? 0 : idx + 1);
    }
    case BuiltinId::Length:
        if (args[0].kind() == ValueKind::Str)
            return Value::fromInt(args[0].toStr().size());
        if (args[0].kind() == ValueKind::Empty)
            return Value::fromInt(0);
        return unsupported(line, QStringLiteral("Length is only supported on strings this slice"));
    case BuiltinId::Chr: {
        auto n = argInt(0);
        if (!n.has_value())
            return needNumeric("Chr");
        return Value::fromStr(QByteArray(1, static_cast<char>(*n & 0xFF)));
    }
    case BuiltinId::Ord:
        if (args[0].kind() == ValueKind::Bool)
            return Value::fromInt(args[0].isTruthy() ? 1 : 0);
        if (args[0].kind() == ValueKind::Str) {
            const QByteArray s = args[0].toStr();
            return Value::fromInt(s.isEmpty() ? 0 : static_cast<unsigned char>(s.at(0)));
        }
        {
            auto n = argInt(0);
            if (!n.has_value())
                return needNumeric("Ord");
            return Value::fromInt(*n);
        }
    case BuiltinId::Uppercase:
        return Value::fromStr(asciiUpper(args[0].toStr()));
    case BuiltinId::Lowercase:
        return Value::fromStr(asciiLower(args[0].toStr()));
    case BuiltinId::Trim: {
        const QByteArray s = args[0].toStr();
        int b = 0, e = static_cast<int>(s.size());
        while (b < e && static_cast<unsigned char>(s.at(b)) <= ' ')
            ++b;
        while (e > b && static_cast<unsigned char>(s.at(e - 1)) <= ' ')
            --e;
        return Value::fromStr(s.mid(b, e - b));
    }
    case BuiltinId::High:
        if (args[0].kind() != ValueKind::Str)
            return unsupported(line, QStringLiteral("High is only supported on strings this slice"));
        return Value::fromInt(args[0].toStr().size());
    case BuiltinId::Low:
        if (args[0].kind() != ValueKind::Str)
            return unsupported(line, QStringLiteral("Low is only supported on strings this slice"));
        return Value::fromInt(1);
    // ---- Numeric (intrinsics) ----
    case BuiltinId::Round: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Round");
        return Value::fromInt(core::delphiRound(*x));
    }
    case BuiltinId::Trunc: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Trunc");
        return Value::fromInt(core::delphiTrunc(*x));
    }
    case BuiltinId::Int: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Int");
        return Value::fromReal(std::truncl(*x));
    }
    case BuiltinId::Frac: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Frac");
        return Value::fromReal(*x - std::truncl(*x));
    }
    case BuiltinId::Abs:
        if (args[0].kind() == ValueKind::Real) {
            auto x = argReal(0);
            return Value::fromReal(std::fabs(*x));
        } else {
            auto x = argInt(0);
            if (!x.has_value())
                return needNumeric("Abs");
            return Value::fromInt(*x < 0 ? -*x : *x);
        }
    case BuiltinId::Sqr:
        if (args[0].kind() == ValueKind::Real) {
            auto x = argReal(0);
            return Value::fromReal(*x * *x);
        } else {
            auto x = argInt(0);
            if (!x.has_value())
                return needNumeric("Sqr");
            return Value::fromInt(*x * *x);
        }
    case BuiltinId::Sqrt: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Sqrt");
        if (*x < 0.0L)
            return runtimeError(line, QStringLiteral("Sqrt: argument must be non-negative"));
        return Value::fromReal(std::sqrt(*x));
    }
    case BuiltinId::Sin: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Sin");
        return Value::fromReal(std::sin(*x));
    }
    case BuiltinId::Cos: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Cos");
        return Value::fromReal(std::cos(*x));
    }
    case BuiltinId::ArcTan: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("ArcTan");
        return Value::fromReal(std::atan(*x));
    }
    case BuiltinId::Exp: {
        auto x = argReal(0);
        if (!x.has_value())
            return needNumeric("Exp");
        return Value::fromReal(std::exp(*x));
    }
    // ---- Misc (intrinsics) ----
    case BuiltinId::Assigned:
        if (args[0].kind() == ValueKind::Interface || args[0].kind() == ValueKind::Array)
            return Value::fromBool(args[0].isAssigned());
        if (args[0].kind() == ValueKind::Empty)
            return Value::fromBool(false);
        return unsupported(line, QStringLiteral("Assigned() requires an interface or array"));
    case BuiltinId::VarType:
        switch (args[0].kind()) {
        case ValueKind::Empty:
            return Value::fromInt(0);
        case ValueKind::Int:
            return Value::fromInt(3);
        case ValueKind::Real:
            return Value::fromInt(5);
        case ValueKind::Bool:
            return Value::fromInt(11);
        case ValueKind::Str:
            return Value::fromInt(256);
        default:
            return unsupported(line, QStringLiteral("VarType is not supported for this value kind"));
        }
    case BuiltinId::Pi:
        return Value::fromReal(3.14159265358979323846L);
    default:
        return unsupported(line,
                           QStringLiteral("'%1' is not implemented this slice - see docs/handoffs/T-0014.md")
                               .arg(QString::fromUtf8(kBuiltinNames[static_cast<std::size_t>(id)])));
    }
}

// ==================================================================================================
// Interface method calls (T-0015B, spec 2.7/5.6)
// ==================================================================================================

namespace {
// Marshals TPoint/TDoublePoint/TRect between the generic record Value (field order: parser.cpp's
// kPointFields/kDoublePointFields/kRectFields - load-bearing, spec 5.2) and the 7 interfaces'
// script_host.h C++ signatures. A record-typed argument that is not actually a Record Value (a
// malformed script - never true of a corpus script, every call site here is const-typed in the
// grammar) degrades to the zero record rather than throwing std::bad_variant_access: "no exception
// ever crosses a cg::script API boundary" (spec 7) applies here too.
Point valueToPoint(const Value& v) {
    if (v.kind() != ValueKind::Record || v.fields().size() < 2) {
        return Point{};
    }
    const std::vector<Value>& f = v.fields();
    return Point{static_cast<qint32>(f[0].toInt().value_or(0)),
                 static_cast<qint32>(f[1].toInt().value_or(0))};
}
Value pointToValue(Point p) {
    return Value::fromRecord({Value::fromInt(p.x), Value::fromInt(p.y)});
}
DoublePoint valueToDoublePoint(const Value& v) {
    if (v.kind() != ValueKind::Record || v.fields().size() < 2) {
        return DoublePoint{};
    }
    const std::vector<Value>& f = v.fields();
    return DoublePoint{static_cast<double>(f[0].toReal().value_or(0.0L)),
                       static_cast<double>(f[1].toReal().value_or(0.0L))};
}
Value doublePointToValue(DoublePoint p) {
    return Value::fromRecord({Value::fromReal(p.x), Value::fromReal(p.y)});
}
Value rectToValue(Rect r) {
    return Value::fromRecord(
        {Value::fromInt(r.left), Value::fromInt(r.top), Value::fromInt(r.right), Value::fromInt(r.bottom)});
}
// spec 5.2: TTileInfo field order IsExists/IsExistsTne/LoadDate/Size/Version/ContentType/Data
// (parser.cpp's kTileInfoFields) - TileInfo only ever flows host -> script (IScriptTileCache::read
// is the sole record-result site that isn't one of the 4 plain point/rect shapes above), so only
// this direction is needed. LoadDate is `Int64` on the script side (a Unix timestamp,
// i_PascalScriptTileCache.pas:30) but `QDateTime` in script_host.h; an invalid/default-constructed
// QDateTime (never touched by a fake/host that reports IsExists=False) becomes 0.
Value tileInfoToValue(const TileInfo& t) {
    // u_PascalScriptTileCache.pas:159 `DateTimeToUnix(VInfo.LoadDate)` on a LOCAL TDateTime (default
    // AInputIsUTC) reads the local wall clock as if it were UTC - the same value as storage's
    // toLegacyUnixSeconds (script links only core, so it is spelled out here).
    const qint64 loadDate =
        t.loadDate.isValid()
            ? core::platform::utcDateTime(t.loadDate.date(), t.loadDate.time()).toSecsSinceEpoch()
            : 0;
    return Value::fromRecord({Value::fromBool(t.isExists), Value::fromBool(t.isExistsTne),
                              Value::fromInt(loadDate), Value::fromInt(static_cast<qint64>(t.size)),
                              Value::fromStr(t.version), Value::fromStr(t.contentType),
                              Value::fromStr(t.data)});
}
// The `AZoom: Byte` parameters (spec 5.1 tail: "the zoom argument is 0-based Byte") - used by both
// ICoordConverter and IScriptTileCache (u_PascalScriptTypes.pas:159,169,179,187), whose `AZoom` is
// a plain `int` on the C++ side (script_host.h) but still wraps as a Byte, see callScriptTileCache.
quint8 toByteArg(const Value& v) {
    return static_cast<quint8>(v.toInt().value_or(0));
}
} // namespace

// A C++ exception from a host object is what a Delphi exception from an external procedure is to the
// engine: uPSRuntime.pas:8285-8310 catches it around ProcPtr and stores the exception object
// (:8336); BuildRequest re-raises that original object (RaiseCurrentException, :11786-11790), so the
// outward message is the plain Message, not PSErrorToString's "Exception: %s" (:1133, :1736). It then unwinds
// like any script error (ExceptionProc, :2181-2230), so try..except / try..finally see it. Here that path is
// the ErrorCode::IoError Result.
core::Result<Value> Interpreter::evalMethodCall(Call* node) {
    try {
        return evalMethodCallUnguarded(node);
    } catch (const std::exception& e) {
        return runtimeError(node->line, QString::fromUtf8(e.what()));
    }
}

core::Result<Value> Interpreter::evalMethodCallUnguarded(Call* node) {
    const int line = node->line;
    const int boundSlot = node->callee / kMethodSlotStride;
    const int methodIndex = node->callee % kMethodSlotStride;

    const Value& ifaceSlot = storage_[static_cast<std::size_t>(boundSlot)];
    if (!ifaceSlot.isAssigned()) {
        // spec 7's generic runtime-error row (division by zero, bad conversion, ... "invalid regex,
        // unknown MIME in TileCache.Write" - a method call through a nil interface belongs in the
        // same family). No compiler/oracle exists to pin legacy's exact exception text for this
        // specific case (unverifiable - see docs/handoffs/T-0015B.md); this implements the spec's
        // "Runtime error -> IoError" contract with a message naming the construct.
        return runtimeError(line, QStringLiteral("method call through an unassigned (nil) interface"));
    }
    const InterfaceRef& ref = ifaceSlot.interfaceRef();

    // IHttpDownloader::doHttpRequest is the one method with `var` (by-reference) parameters - like
    // Insert/Delete/SetLength/Format in callBuiltin, it needs the raw argument nodes, not the
    // generic by-value vector every other interface's methods use below.
    if (ref.id == InterfaceId::Downloader) {
        return callHttpDownloader(ref, node);
    }

    // IScriptLogger::WriteFmt's 2nd argument is an open-array literal (ast.h ArrayLit, "G6") that
    // must reach delphiFormat as individual Values, like Format/FormatEx in callBuiltin - not the
    // generic by-value vector below, which would evaluate it as the ordinal-set bitmask instead.
    if (ref.id == InterfaceId::Logger) {
        return callScriptLogger(ref, node);
    }

    std::vector<Value> args;
    args.reserve(node->args.size());
    for (Node* a : node->args) {
        auto v = evalExpr(a);
        if (!v.has_value()) {
            return std::unexpected(v.error());
        }
        args.push_back(*v);
    }

    switch (ref.id) {
    case InterfaceId::Converter:
        return callCoordConverter(ref, methodIndex, args, line);
    case InterfaceId::ProjConverter:
        return callProjConverter(ref, methodIndex, args, line);
    case InterfaceId::ProjFactory:
        return callProjConverterFactory(ref, methodIndex, args, line);
    case InterfaceId::Global:
        return callScriptGlobal(ref, methodIndex, args, line);
    case InterfaceId::TileCache:
        return callScriptTileCache(ref, methodIndex, args, line);
    case InterfaceId::Downloader:
    case InterfaceId::Logger:
        break; // handled above, before argument evaluation
    }
    return runtimeError(line, QStringLiteral("internal error: unrecognised interface id"));
}

// parser.cpp's kCoordConverterMethods, in order: Pos2LonLat/LonLat2Pos/LonLat2Metr/Metr2LonLat/
// TilesAtZoom/PixelsAtZoom/TilePos2PixelPos/TilePos2PixelRect.
core::Result<Value> Interpreter::callCoordConverter(const InterfaceRef& ref, int methodIndex,
                                                    const std::vector<Value>& args, int line) {
    const auto* conv = static_cast<const ICoordConverter*>(ref.obj.get());
    switch (methodIndex) {
    case 0:
        return doublePointToValue(conv->pos2LonLat(valueToPoint(args[0]), toByteArg(args[1])));
    case 1:
        return pointToValue(conv->lonLat2Pos(valueToDoublePoint(args[0]), toByteArg(args[1])));
    case 2:
        return doublePointToValue(conv->lonLat2Metr(valueToDoublePoint(args[0])));
    case 3:
        return doublePointToValue(conv->metr2LonLat(valueToDoublePoint(args[0])));
    case 4:
        return Value::fromInt(conv->tilesAtZoom(toByteArg(args[0])));
    case 5:
        return Value::fromInt(conv->pixelsAtZoom(toByteArg(args[0])));
    case 6:
        return pointToValue(conv->tilePos2PixelPos(valueToPoint(args[0]), toByteArg(args[1])));
    case 7:
        return rectToValue(conv->tilePos2PixelRect(valueToPoint(args[0]), toByteArg(args[1])));
    default:
        return runtimeError(line, QStringLiteral("internal error: unknown ICoordConverter method"));
    }
}

// parser.cpp's kProjConverterMethods: LonLat2XY/XY2LonLat.
core::Result<Value> Interpreter::callProjConverter(const InterfaceRef& ref, int methodIndex,
                                                   const std::vector<Value>& args, int line) {
    const auto* conv = static_cast<const IProjConverter*>(ref.obj.get());
    switch (methodIndex) {
    case 0:
        return doublePointToValue(conv->lonLat2XY(valueToDoublePoint(args[0])));
    case 1:
        return doublePointToValue(conv->xy2LonLat(valueToDoublePoint(args[0])));
    default:
        return runtimeError(line, QStringLiteral("internal error: unknown IProjConverter method"));
    }
}

// parser.cpp's kProjConverterFactoryMethods: GetByEPSG/GetByInitString, matching legacy's
// registration (u_PascalScriptTypes.pas:68-69). Both return nullptr for an unknown code/
// init-string; a null result becomes a nil InterfaceRef, so the script's Assigned() is False,
// exactly as legacy (script_host.h).
core::Result<Value> Interpreter::callProjConverterFactory(const InterfaceRef& ref, int methodIndex,
                                                          const std::vector<Value>& args, int line) {
    const auto* factory = static_cast<const IProjConverterFactory*>(ref.obj.get());
    std::shared_ptr<const IProjConverter> result;
    switch (methodIndex) {
    case 0:
        result = factory->byEpsg(static_cast<int>(args[0].toInt().value_or(0)));
        break;
    case 1:
        result = factory->byInitString(args[0].toStr());
        break;
    default:
        return runtimeError(line, QStringLiteral("internal error: unknown IProjConverterFactory method"));
    }
    return Value::fromInterface(toVoidShared(result), InterfaceId::ProjConverter);
}

// IHttpDownloader::doHttpRequest (kHttpDownloaderMethods' sole entry, 5 script-visible arguments -
// script_host.h/spec 5.6): the parser's arity check (parser.cpp:1591) guarantees node->args.size()
// == 5 for any Method call that reached here with this InterfaceId. The 2 `var` output arguments
// must be simple variables (spec 4.3 A5, same rule as callUserRoutine's var/out parameters); status
// 0 is passed through unchanged (spec 5.6 - status 0 also means "no connection"/a masked HTTP 403,
// and must never be normalised away).
core::Result<Value> Interpreter::callHttpDownloader(const InterfaceRef& ref, Call* node) {
    const int line = node->line;
    Node* responseHeaderNode = node->args[3];
    Node* responseDataNode = node->args[4];
    if (responseHeaderNode->kind != NodeKind::VarRef || responseDataNode->kind != NodeKind::VarRef) {
        return unsupported(line, QStringLiteral("DoHttpRequest's 'out' arguments must be simple variables"));
    }
    auto url = evalExpr(node->args[0]);
    if (!url.has_value()) {
        return std::unexpected(url.error());
    }
    auto requestHeader = evalExpr(node->args[1]);
    if (!requestHeader.has_value()) {
        return std::unexpected(requestHeader.error());
    }
    auto postData = evalExpr(node->args[2]);
    if (!postData.has_value()) {
        return std::unexpected(postData.error());
    }

    auto* downloader = static_cast<IHttpDownloader*>(ref.obj.get());
    QByteArray responseHeader;
    QByteArray responseData;
    const quint32 status = downloader->doHttpRequest(url->toStr(), requestHeader->toStr(), postData->toStr(),
                                                     responseHeader, responseData);

    auto* responseHeaderVar = static_cast<VarRef*>(responseHeaderNode);
    auto* responseDataVar = static_cast<VarRef*>(responseDataNode);
    if (auto ok = checkNotBound(responseHeaderVar->slot, line); !ok.has_value()) {
        return std::unexpected(ok.error());
    }
    if (auto ok = checkNotBound(responseDataVar->slot, line); !ok.has_value()) {
        return std::unexpected(ok.error());
    }
    storage_[static_cast<std::size_t>(responseHeaderVar->slot)] =
        wrapForSlot(responseHeaderVar->slot, Value::fromStr(responseHeader));
    storage_[static_cast<std::size_t>(responseDataVar->slot)] =
        wrapForSlot(responseDataVar->slot, Value::fromStr(responseData));
    return Value::fromInt(static_cast<qint64>(status));
}

// parser.cpp's kScriptGlobalMethods, in order: SetVar/GetVar/Exists/SetVarTS/GetVarTS/ExistsTS/
// Lock/Unlock/LockRead/UnlockRead. Values are Delphi Variants on the script side -> Value here
// (script_host.h); GetVar/GetVarTS on an absent id return Value() (Empty), matching legacy's
// Unassigned (spec 5.6) - VarType(Global.GetVar(id)) is then 0 via the existing VarType builtin,
// no separate mapping needed here. SetVar/GetVar/Exists do no locking of their own (spec 5.6: the
// script brackets them with Lock/Unlock itself); the …TS methods self-lock inside the host
// implementation - the interpreter is not involved in that decision either way.
core::Result<Value> Interpreter::callScriptGlobal(const InterfaceRef& ref, int methodIndex,
                                                  const std::vector<Value>& args, int line) {
    auto* global = static_cast<IScriptGlobal*>(ref.obj.get());
    switch (methodIndex) {
    case 0:
        global->setVar(static_cast<int>(args[0].toInt().value_or(0)), args[1]);
        return Value();
    case 1:
        return global->getVar(static_cast<int>(args[0].toInt().value_or(0)));
    case 2:
        return Value::fromBool(global->exists(static_cast<int>(args[0].toInt().value_or(0))));
    case 3:
        global->setVarTs(static_cast<int>(args[0].toInt().value_or(0)), args[1]);
        return Value();
    case 4:
        return global->getVarTs(static_cast<int>(args[0].toInt().value_or(0)));
    case 5:
        return Value::fromBool(global->existsTs(static_cast<int>(args[0].toInt().value_or(0))));
    case 6:
        global->lock();
        return Value();
    case 7:
        global->unlock();
        return Value();
    case 8:
        global->lockRead();
        return Value();
    case 9:
        global->unlockRead();
        return Value();
    default:
        return runtimeError(line, QStringLiteral("internal error: unknown IScriptGlobal method"));
    }
}

// parser.cpp's kScriptLoggerMethods: Write/WriteFmt. WriteFmt(AFormat, AArgs) dispatches exactly
// as legacy's implementation does (u_PascalScriptLogger.pas:139-142: `Write(Format(fmt, args))`),
// reusing the same array-literal marshalling as the `Format` builtin in callBuiltin - AArgs is an
// open-array literal, not a generically-evaluated argument (it would otherwise evaluate as the
// ordinal-set bitmask StringReplace's `[rfReplaceAll]` uses). The "empty string is dropped" rule
// (spec 5.6) is the *implementation's* responsibility (legacy u_PascalScriptLogger.pas:121-127
// checks inside TPascalScriptLogger.Write itself, not at the registration/dispatch layer) - the
// interpreter always forwards the argument unconditionally.
core::Result<Value> Interpreter::callScriptLogger(const InterfaceRef& ref, Call* node) {
    const int line = node->line;
    auto* logger = static_cast<IScriptLogger*>(ref.obj.get());
    const int methodIndex = node->callee % kMethodSlotStride;
    switch (methodIndex) {
    case 0: {
        auto str = evalExpr(node->args[0]);
        if (!str.has_value())
            return std::unexpected(str.error());
        logger->write(str->toStr());
        return Value();
    }
    case 1: {
        if (node->args.size() != 2 || node->args[1]->kind != NodeKind::ArrayLit) {
            return unsupported(line, QStringLiteral("WriteFmt requires (formatString, [args])"));
        }
        auto fmtV = evalExpr(node->args[0]);
        if (!fmtV.has_value())
            return std::unexpected(fmtV.error());
        auto* lit = static_cast<ArrayLit*>(node->args[1]);
        std::vector<Value> fmtArgs;
        fmtArgs.reserve(lit->elements.size());
        for (Node* e : lit->elements) {
            auto v = evalExpr(e);
            if (!v.has_value())
                return std::unexpected(v.error());
            fmtArgs.push_back(*v);
        }
        auto out = delphiFormat(fmtV->toStr(), fmtArgs, '.', ',', QByteArray());
        if (!out.has_value())
            return runtimeError(line, out.error().what());
        logger->write(*out);
        return Value();
    }
    default:
        return runtimeError(line, QStringLiteral("internal error: unknown IScriptLogger method"));
    }
}

// parser.cpp's kScriptTileCacheMethods, in order: Read/Write/WriteTne/Delete. Legacy declares
// every TileCache.* zoom as `AZoom: Byte` too (u_PascalScriptTypes.pas:159,169,179,187), so the
// script value is wrapped with toByteArg, exactly as ICoordConverter's does; the host type stays a
// plain `int` (script_host.h). All four resolve an empty `version` argument as "default version"
// host-side (spec 5.6); the interpreter passes it through unchanged either way.
core::Result<Value> Interpreter::callScriptTileCache(const InterfaceRef& ref, int methodIndex,
                                                     const std::vector<Value>& args, int line) {
    auto* cache = static_cast<IScriptTileCache*>(ref.obj.get());
    switch (methodIndex) {
    case 0: {
        const int x = static_cast<int>(args[0].toInt().value_or(0));
        const int y = static_cast<int>(args[1].toInt().value_or(0));
        const int zoom = toByteArg(args[2]);
        return tileInfoToValue(cache->read(x, y, zoom, args[3].toStr(), args[4].isTruthy()));
    }
    case 1: {
        const int x = static_cast<int>(args[0].toInt().value_or(0));
        const int y = static_cast<int>(args[1].toInt().value_or(0));
        const int zoom = toByteArg(args[2]);
        return Value::fromBool(
            cache->write(x, y, zoom, args[3].toStr(), args[4].toStr(), args[5].toStr(), args[6].isTruthy()));
    }
    case 2: {
        const int x = static_cast<int>(args[0].toInt().value_or(0));
        const int y = static_cast<int>(args[1].toInt().value_or(0));
        const int zoom = toByteArg(args[2]);
        return Value::fromBool(cache->writeTne(x, y, zoom, args[3].toStr()));
    }
    case 3: {
        const int x = static_cast<int>(args[0].toInt().value_or(0));
        const int y = static_cast<int>(args[1].toInt().value_or(0));
        const int zoom = toByteArg(args[2]);
        return Value::fromBool(cache->remove(x, y, zoom, args[3].toStr()));
    }
    default:
        return runtimeError(line, QStringLiteral("internal error: unknown IScriptTileCache method"));
    }
}

// ==================================================================================================
// Statements
// ==================================================================================================

core::Result<Interpreter::Signal> Interpreter::execStmtList(const std::vector<Node*>& stmts) {
    for (Node* s : stmts) {
        auto sig = execStmt(s);
        if (!sig.has_value())
            return std::unexpected(sig.error());
        if (*sig != Signal::Normal)
            return sig;
    }
    return Signal::Normal;
}

core::Result<Interpreter::Signal> Interpreter::execCompound(Compound* node) {
    return execStmtList(node->stmts);
}

core::Result<Interpreter::Signal> Interpreter::execIf(If* node) {
    auto cond = evalExpr(node->cond);
    if (!cond.has_value())
        return std::unexpected(cond.error());
    if (cond->isTruthy())
        return execStmt(node->thenBranch);
    if (node->elseBranch != nullptr)
        return execStmt(node->elseBranch);
    return Signal::Normal;
}

core::Result<Interpreter::Signal> Interpreter::execCase(Case* node) {
    auto subject = evalExpr(node->subject);
    if (!subject.has_value())
        return std::unexpected(subject.error());
    auto sv = subject->toInt();
    if (!sv.has_value())
        return unsupported(node->line, QStringLiteral("case subject must be an integer"));
    for (const CaseArm& arm : node->arms) {
        for (const CaseLabel& label : arm.labels) {
            if (*sv >= label.lo && *sv <= label.hi)
                return execStmt(arm.body);
        }
    }
    if (node->elseBranch != nullptr)
        return execStmt(node->elseBranch);
    return Signal::Normal;
}

core::Result<Interpreter::Signal> Interpreter::execForTo(ForTo* node) {
    auto fromR = evalExpr(node->from);
    if (!fromR.has_value())
        return std::unexpected(fromR.error());
    auto toR = evalExpr(node->to);
    if (!toR.has_value())
        return std::unexpected(toR.error());
    auto fromI = fromR->toInt(), toI = toR->toInt();
    if (!fromI.has_value() || !toI.has_value())
        return unsupported(node->line, QStringLiteral("for bounds must be integers"));
    if (auto ok = checkNotBound(node->slot, node->line); !ok.has_value())
        return std::unexpected(ok.error());

    qint64 i = *fromI;
    const qint64 end = *toI;
    for (;;) {
        if (node->downto ? i < end : i > end)
            break;
        storage_[static_cast<std::size_t>(node->slot)] = Value::fromInt(i);
        auto sig = execStmt(node->body);
        if (!sig.has_value())
            return std::unexpected(sig.error());
        if (*sig == Signal::Break)
            break;
        if (*sig == Signal::Exit)
            return sig;
        if (i == end)
            break; // about to over/underflow past the last iteration
        node->downto ? --i : ++i;
    }
    return Signal::Normal;
}

core::Result<Interpreter::Signal> Interpreter::execWhile(While* node) {
    for (;;) {
        auto cond = evalExpr(node->cond);
        if (!cond.has_value())
            return std::unexpected(cond.error());
        if (!cond->isTruthy())
            break;
        auto sig = execStmt(node->body);
        if (!sig.has_value())
            return std::unexpected(sig.error());
        if (*sig == Signal::Break)
            break;
        if (*sig == Signal::Exit)
            return sig;
    }
    return Signal::Normal;
}

core::Result<Interpreter::Signal> Interpreter::execRepeatUntil(RepeatUntil* node) {
    for (;;) {
        auto sig = execStmtList(node->body);
        if (!sig.has_value())
            return std::unexpected(sig.error());
        if (*sig == Signal::Break)
            break;
        if (*sig == Signal::Exit)
            return sig;
        auto cond = evalExpr(node->cond);
        if (!cond.has_value())
            return std::unexpected(cond.error());
        if (cond->isTruthy())
            break;
    }
    return Signal::Normal;
}

core::Result<Interpreter::Signal> Interpreter::execTryFinally(TryFinally* node) {
    auto bodySig = execStmtList(node->body);
    if (node->isExcept) {
        // Only a Delphi-equivalent runtime error (ErrorCode::IoError) is catchable here.
        // Unsupported (a deliberate fail-loud diagnostic) and Cancelled (a budget/abort signal)
        // are port-internal, not script-level, and must propagate through `except` unchanged
        // (T-0014 rework Major 3).
        if (!bodySig.has_value() && bodySig.error().code() == core::ErrorCode::IoError) {
            return execStmtList(node->handler);
        }
        return bodySig;
    }
    auto handlerSig = execStmtList(node->handler); // finally: handler always runs
    if (!handlerSig.has_value())
        return std::unexpected(handlerSig.error());
    if (!bodySig.has_value())
        return std::unexpected(bodySig.error()); // re-raise (spec 3.3 S9)
    if (*handlerSig != Signal::Normal)
        return handlerSig;
    return bodySig;
}

core::Result<Interpreter::Signal> Interpreter::execStmt(Node* node) {
    if (auto ticked = tickBudget(node->line); !ticked.has_value()) {
        return std::unexpected(ticked.error());
    }
    switch (node->kind) {
    case NodeKind::Assign: {
        auto* n = static_cast<Assign*>(node);
        if (auto ok = checkNotBound(n->slot, n->line); !ok.has_value())
            return std::unexpected(ok.error());
        auto v = evalExprForSlot(n->value, n->slot); // review finding 3: unary '-' RHS special case
        if (!v.has_value())
            return std::unexpected(v.error());
        storage_[static_cast<std::size_t>(n->slot)] = wrapForSlot(n->slot, *v); // N1/N2, incl. Result
        return Signal::Normal;
    }
    case NodeKind::IndexAssign: {
        auto* n = static_cast<IndexAssign*>(node);
        if (n->target->kind != NodeKind::VarRef) {
            return unsupported(
                n->line, QStringLiteral("indexed assignment target must be a simple variable this slice"));
        }
        auto* vr = static_cast<VarRef*>(n->target);
        if (auto ok = checkNotBound(vr->slot, n->line); !ok.has_value())
            return std::unexpected(ok.error());
        Value& target = storage_[static_cast<std::size_t>(vr->slot)];
        if (target.kind() == ValueKind::Array) {
            // PD-1 (spec 3.6/A5): `arr[i] := v` range-checks `i` exactly like a read
            // (arrayElementOffset) then stores `v` narrowed to the array's declared element type -
            // the same wrappedTo an ordinary scalar slot write goes through (N1/N2).
            auto idxR = evalExpr(n->index);
            if (!idxR.has_value())
                return std::unexpected(idxR.error());
            auto idx = idxR->toInt();
            if (!idx.has_value())
                return unsupported(n->line, QStringLiteral("array index must be an integer"));
            auto offset = arrayElementOffset(target, *idx, n->line);
            if (!offset.has_value())
                return std::unexpected(offset.error());
            auto valR = evalExpr(n->value);
            if (!valR.has_value())
                return std::unexpected(valR.error());
            const ArrayInfo* info = script_->arrayInfo(vr->slot);
            const DeclaredType elementKind = info != nullptr ? info->elementKind : DeclaredType::Unknown;
            target.fields()[*offset] = valR->wrappedTo(elementKind); // COW detach - this is the write
            return Signal::Normal;
        }
        if (target.kind() != ValueKind::Str && target.kind() != ValueKind::Empty) {
            return unsupported(n->line,
                               QStringLiteral("indexed assignment is only supported on strings this slice"));
        }
        auto idxR = evalExpr(n->index);
        if (!idxR.has_value())
            return std::unexpected(idxR.error());
        auto idx = idxR->toInt();
        if (!idx.has_value())
            return unsupported(n->line, QStringLiteral("string index must be an integer"));
        auto valR = evalExpr(n->value);
        if (!valR.has_value())
            return std::unexpected(valR.error());
        QByteArray s = target.toStr();
        const qint64 i = *idx;
        if (i < 1 || i > s.size())
            return runtimeError(n->line, QStringLiteral("string index out of range")); // T4
        const QByteArray ch = valR->toStr();
        s[static_cast<int>(i - 1)] = ch.isEmpty() ? '\0' : ch.at(0);
        target = Value::fromStr(std::move(s));
        return Signal::Normal;
    }
    case NodeKind::FieldAssign: {
        auto* n = static_cast<FieldAssign*>(node);
        if (n->target->kind != NodeKind::VarRef) {
            return unsupported(
                n->line, QStringLiteral("field assignment target must be a simple variable this slice"));
        }
        auto* vr = static_cast<VarRef*>(n->target);
        if (auto ok = checkNotBound(vr->slot, n->line); !ok.has_value())
            return std::unexpected(ok.error());
        Value& target = storage_[static_cast<std::size_t>(vr->slot)];
        if (target.kind() != ValueKind::Record) {
            return unsupported(n->line, QStringLiteral("field assignment target is not a record"));
        }
        auto v = evalExpr(n->value);
        if (!v.has_value())
            return std::unexpected(v.error());
        std::vector<Value>& flds = target.fields(); // COW detach - this is the write
        if (n->fieldIndex < 0 || static_cast<std::size_t>(n->fieldIndex) >= flds.size()) {
            return runtimeError(n->line, QStringLiteral("internal error: field index out of range"));
        }
        // Record fields keep their declared C++ type (qint32/double per script_host.h) rather than
        // a per-field DeclaredType this slice tracks - N1/N2 range-wrap/round applies to named
        // slots (spec's own write-path list), not into a record field.
        flds[static_cast<std::size_t>(n->fieldIndex)] = *v;
        return Signal::Normal;
    }
    case NodeKind::CallStmt: {
        auto* n = static_cast<CallStmt*>(node);
        auto v = evalCall(n->call);
        if (!v.has_value())
            return std::unexpected(v.error());
        return Signal::Normal;
    }
    case NodeKind::If:
        return execIf(static_cast<If*>(node));
    case NodeKind::Case:
        return execCase(static_cast<Case*>(node));
    case NodeKind::ForTo:
        return execForTo(static_cast<ForTo*>(node));
    case NodeKind::While:
        return execWhile(static_cast<While*>(node));
    case NodeKind::RepeatUntil:
        return execRepeatUntil(static_cast<RepeatUntil*>(node));
    case NodeKind::TryFinally:
        return execTryFinally(static_cast<TryFinally*>(node));
    case NodeKind::Compound:
        return execCompound(static_cast<Compound*>(node));
    case NodeKind::Exit:
        return Signal::Exit;
    case NodeKind::Break:
        return Signal::Break;
    case NodeKind::Continue:
        return Signal::Continue;
    case NodeKind::Empty:
        return Signal::Normal;
    case NodeKind::With: {
        // Every field access in `body` was already lowered to an ordinary Field/VarRef node at
        // compile time (parser.cpp's with-scope, spec 3.5) - all that is left to do at run time is
        // evaluate the subject (a plain variable reference, so this is just the bound-variable
        // check) and run the body.
        auto* n = static_cast<With*>(node);
        auto subject = evalExpr(n->subject);
        if (!subject.has_value())
            return std::unexpected(subject.error());
        return execStmt(n->body);
    }
    default:
        return unsupported(node->line, QStringLiteral("unsupported statement"));
    }
}

bool builtinIdsMatchExternalSymbolOrder() {
    const auto externals = SymbolTable::externalSymbols();
    if (externals.size() != kBuiltinNames.size())
        return false;
    for (std::size_t i = 0; i < externals.size(); ++i) {
        if (QByteArray(externals[i].name).compare(kBuiltinNames[i], Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace cg::script
