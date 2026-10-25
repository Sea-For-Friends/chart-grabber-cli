// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/parser.h"

#include <QByteArray>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>

namespace cg::script {

namespace {

// --- the 6 registered types + TTileInfo (spec 5.2) --------------------------------------------
// Each type's fields live in their own named array so `RegisteredType::fields` (a `std::span`)
// can view them without owning anything - keeps every table below usable as a `constexpr` global.
constexpr const char* kPointFields[] = {"X", "Y"};
constexpr const char* kDoublePointFields[] = {"X", "Y"};
constexpr const char* kRectFields[] = {"Left", "Top", "Right", "Bottom"};
constexpr const char* kDoubleRectFields[] = {"Left", "Top", "Right", "Bottom"};
constexpr const char* kTileInfoFields[] = {"IsExists", "IsExistsTne", "LoadDate", "Size",
                                           "Version",  "ContentType", "Data"};
constexpr RegisteredType kPoint{"TPoint", kPointFields, DeclaredType::TPoint};
constexpr RegisteredType kDoublePoint{"TDoublePoint", kDoublePointFields, DeclaredType::TDoublePoint};
constexpr RegisteredType kRect{"TRect", kRectFields, DeclaredType::TRect};
constexpr RegisteredType kDoubleRect{"TDoubleRect", kDoubleRectFields, DeclaredType::TDoubleRect};
constexpr RegisteredType kTileInfo{"TTileInfo", kTileInfoFields, DeclaredType::TTileInfo};
constexpr std::array<const RegisteredType*, 5> kRegisteredTypes{&kPoint, &kDoublePoint, &kRect, &kDoubleRect,
                                                                &kTileInfo};

// Primitive type names the v1 grammar accepts after a `:` (spec 2.6/4.1 N1/N2) - resolved to a
// `DeclaredType` so `SymbolTable::setSlotType` can record it; a name absent from this table (or
// not a registered record type) leaves the slot `DeclaredType::Unknown`, i.e. today's unwrapped
// behaviour (T-0014B, no regression for anything this table doesn't name).
struct PrimitiveTypeName {
    const char* name;
    DeclaredType type;
};
constexpr PrimitiveTypeName kPrimitiveTypeNames[] = {
    {"Byte", DeclaredType::Byte},
    {"Word", DeclaredType::Word},
    {"ShortInt", DeclaredType::ShortInt},
    {"SmallInt", DeclaredType::SmallInt},
    {"Integer", DeclaredType::Integer},
    {"Longint", DeclaredType::Integer}, // Delphi synonym
    {"Cardinal", DeclaredType::Cardinal},
    {"Int64", DeclaredType::Int64},
    {"Boolean", DeclaredType::Boolean},
    {"Char", DeclaredType::Char},
    {"AnsiString", DeclaredType::AnsiString},
    {"String", DeclaredType::AnsiString}, // spec 4.2 T1: string == AnsiString
    {"WideString", DeclaredType::WideString},
    {"Single", DeclaredType::Single},
    {"Double", DeclaredType::Double},
    {"Extended", DeclaredType::Extended},
    {"Variant", DeclaredType::Variant},
    {"TReplaceFlags", DeclaredType::TReplaceFlags},
};

// --- the 7 script-visible interfaces (spec 2.7/5.6) ----------------------------------------------
constexpr InterfaceMethod kCoordConverterMethods[] = {
    {"Pos2LonLat", 2},  {"LonLat2Pos", 2},   {"LonLat2Metr", 1},      {"Metr2LonLat", 1},
    {"TilesAtZoom", 1}, {"PixelsAtZoom", 1}, {"TilePos2PixelPos", 2}, {"TilePos2PixelRect", 2},
};
constexpr InterfaceMethod kProjConverterMethods[] = {{"LonLat2XY", 1}, {"XY2LonLat", 1}};
constexpr InterfaceMethod kProjConverterFactoryMethods[] = {{"GetByEPSG", 1}, {"GetByInitString", 1}};
// The script-visible signature is `DoHttpRequest(url, requestHeader, postData, var responseHeader,
// var responseData): Cardinal` - 5 arguments, not the 3 of the host-side C++ `IHttpDownloader`
// (script_host.h), which returns an `HttpReply` record instead of using two var parameters.
// Corpus-verified (GGC/*, Google/GoogleSat.zmp, Navionics/*, Yandex/YaTraf.zmp all pass 5 args) -
// legacy is truth over the inferred spec mapping; see handoff open questions.
constexpr InterfaceMethod kHttpDownloaderMethods[] = {{"DoHttpRequest", 5}};
constexpr InterfaceMethod kScriptGlobalMethods[] = {
    {"SetVar", 2},   {"GetVar", 1}, {"Exists", 1}, {"SetVarTS", 2}, {"GetVarTS", 1},
    {"ExistsTS", 1}, {"Lock", 0},   {"Unlock", 0}, {"LockRead", 0}, {"UnlockRead", 0},
};
constexpr InterfaceMethod kScriptLoggerMethods[] = {{"Write", 1}, {"WriteFmt", 2}};
constexpr InterfaceMethod kScriptTileCacheMethods[] = {
    {"Read", 5}, {"Write", 7}, {"WriteTne", 4}, {"Delete", 4}};

constexpr InterfaceType kCoordConverter{"ICoordConverter", kCoordConverterMethods};
constexpr InterfaceType kProjConverter{"IProjConverter", kProjConverterMethods};
constexpr InterfaceType kProjConverterFactory{"IProjConverterFactory", kProjConverterFactoryMethods};
constexpr InterfaceType kHttpDownloader{"ISimpleHttpDownloader", kHttpDownloaderMethods};
constexpr InterfaceType kScriptGlobal{"IPascalScriptGlobal", kScriptGlobalMethods};
constexpr InterfaceType kScriptLogger{"IPascalScriptLogger", kScriptLoggerMethods};
constexpr InterfaceType kScriptTileCache{"IPascalScriptTileCache", kScriptTileCacheMethods};

struct BoundVariable {
    const char* name;
    const InterfaceType* boundInterface; // nullptr for the 19 non-interface bound variables
    bool nonDeterministic;
};

// spec 5.1, in registration order: TileCache is slot 0, GetBMetr is slot 25.
constexpr std::array<BoundVariable, 26> kBoundVariables{{
    {"TileCache", &kScriptTileCache, true},
    {"Logger", &kScriptLogger, false},
    {"Global", &kScriptGlobal, true},
    {"Downloader", &kHttpDownloader, true},
    {"DefProjConverter", &kProjConverter, false},
    {"ProjFactory", &kProjConverterFactory, false},
    {"Converter", &kCoordConverter, false},
    {"ResultURL", nullptr, false},
    {"PostData", nullptr, false},
    {"GetURLBase", nullptr, false},
    {"RequestHead", nullptr, false},
    {"ResponseHead", nullptr, true},
    {"ScriptBuffer", nullptr, true},
    {"Version", nullptr, false},
    {"Lang", nullptr, false},
    {"GetX", nullptr, false},
    {"GetY", nullptr, false},
    {"GetZ", nullptr, false},
    {"GetLlon", nullptr, false},
    {"GetTLat", nullptr, false},
    {"GetBLat", nullptr, false},
    {"GetRLon", nullptr, false},
    {"GetLMetr", nullptr, false},
    {"GetRMetr", nullptr, false},
    {"GetTMetr", nullptr, false},
    {"GetBMetr", nullptr, false},
}};

// The 38 registered free functions (spec 5.3) followed by the intrinsics the interpreter must
// supply itself (spec 5.4, minus True/False/nil/Result which the grammar/parser handle
// structurally rather than as callables). `Call::callee` indexes this array for
// `CallKind::Registered`/`CallKind::Builtin` - see parser.h.
constexpr std::array<ExternalSymbol, 72> kExternals{{
    // --- registered functions (F1-F38) ---
    {"Random", ExternalKind::Function, 1, 1, false, true},
    {"RandomRange", ExternalKind::Function, 2, 2, false, false},
    {"Power", ExternalKind::Function, 2, 2, false, false},
    {"IntPower", ExternalKind::Function, 2, 2, false, false},
    {"Ceil", ExternalKind::Function, 1, 1, false, false},
    {"Floor", ExternalKind::Function, 1, 1, false, false},
    {"Log2", ExternalKind::Function, 1, 1, false, false},
    {"Ln", ExternalKind::Function, 1, 1, false, false},
    {"Max", ExternalKind::Function, 2, 2, false, false},
    {"Min", ExternalKind::Function, 2, 2, false, false},
    {"MaxExt", ExternalKind::Function, 2, 2, false, false},
    {"MinExt", ExternalKind::Function, 2, 2, false, false},
    {"RegExprGetMatchSubStr", ExternalKind::Function, 3, 3, false, false},
    {"RegExprReplaceMatchSubStr", ExternalKind::Function, 3, 3, false, false},
    {"Base64Encode", ExternalKind::Function, 1, 1, false, false},
    {"Base64UrlEncode", ExternalKind::Function, 1, 1, false, false},
    {"Base64Decode", ExternalKind::Function, 1, 1, false, false},
    {"IntToHex", ExternalKind::Function, 2, 2, false, false},
    {"FileExists", ExternalKind::Function, 1, 1, false, false},
    {"Format", ExternalKind::Function, 1, 0xFF, false, false},
    {"FormatEx", ExternalKind::Function, 2, 0xFF, false, false},
    {"PosEx", ExternalKind::Function, 3, 3, false, false},
    {"StringReplace", ExternalKind::Function, 4, 4, false, false},
    {"MD5String", ExternalKind::Function, 1, 1, false, false},
    {"RoundEx", ExternalKind::Function, 2, 2, false, false},
    {"GetAfter", ExternalKind::Function, 2, 2, false, false},
    {"GetBefore", ExternalKind::Function, 2, 2, false, false},
    {"GetBetween", ExternalKind::Function, 3, 3, false, false},
    {"SetHeaderValue", ExternalKind::Function, 3, 3, false, false},
    {"GetHeaderValue", ExternalKind::Function, 2, 2, false, false},
    {"DeleteHeaderEntry", ExternalKind::Function, 2, 2, false, false},
    {"SubStrPos", ExternalKind::Function, 3, 3, false, false},
    {"GetNumberAfter", ExternalKind::Function, 2, 2, false, false},
    {"GetDiv3Path", ExternalKind::Function, 1, 1, false, false},
    {"GetUnixTime", ExternalKind::Function, 0, 0, false, true},
    {"SaveToLocalFile", ExternalKind::Function, 2, 2, false, true},
    {"WriteLn", ExternalKind::Function, 1, 1, true, false},
    {"TemplateToUrl", ExternalKind::Function, 1, 1, false, false},
    // --- intrinsics (spec 5.4) ---
    {"IntToStr", ExternalKind::Intrinsic, 1, 1, false, false},
    {"StrToInt", ExternalKind::Intrinsic, 1, 1, false, false},
    {"StrToIntDef", ExternalKind::Intrinsic, 2, 2, false, false},
    {"StrToFloat", ExternalKind::Intrinsic, 1, 1, false, false},
    {"FloatToStr", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Copy", ExternalKind::Intrinsic, 3, 3, false, false},
    {"Pos", ExternalKind::Intrinsic, 2, 2, false, false},
    {"Insert", ExternalKind::Intrinsic, 3, 3, true, false},
    {"Delete", ExternalKind::Intrinsic, 3, 3, true, false},
    {"Length", ExternalKind::Intrinsic, 1, 1, false, false},
    {"SetLength", ExternalKind::Intrinsic, 2, 2, true, false},
    {"Chr", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Ord", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Round", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Trunc", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Int", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Frac", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Abs", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Sqr", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Sqrt", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Sin", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Cos", ExternalKind::Intrinsic, 1, 1, false, false},
    {"ArcTan", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Exp", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Assigned", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Uppercase", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Lowercase", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Trim", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Inc", ExternalKind::Intrinsic, 1, 2, true, false},
    {"Dec", ExternalKind::Intrinsic, 1, 2, true, false},
    {"High", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Low", ExternalKind::Intrinsic, 1, 1, false, false},
    {"VarType", ExternalKind::Intrinsic, 1, 1, false, false},
    {"Pi", ExternalKind::Intrinsic, 0, 0, false, false},
}};

struct NamedConstant {
    const char* name;
    qint64 value;
};
// spec 5.2 `TReplaceFlag = (rfReplaceAll, rfIgnoreCase)` (F2): resolved to their Delphi ordinal,
// the same way `true`/`false` lower straight to a literal in parseDesignatorOrLiteral - not a
// callable, so not part of kExternals. `[rfReplaceAll]` (StringReplace's 4th argument, spec 3.4)
// is then a G6 set-constructor of these ordinals; the interpreter (T-0014) is what turns such an
// ArrayLit into a set bitmask when it drives a set-typed parameter.
constexpr std::array<NamedConstant, 2> kNamedConstants{{
    {"rfReplaceAll", 0},
    {"rfIgnoreCase", 1},
}};

bool equalsFolded(const QByteArray& a, const char* b) {
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

// Reserved-in-effect words that are not real `Keyword` values (spec 3.4) because the v1 grammar
// never accepts them anywhere - so the lexer hands them back as plain identifiers and the parser
// recognises the spelling itself, only where a declaration or statement is expected.
bool isRejectedClassKeyword(const QByteArray& text) {
    return equalsFolded(text, "class") || equalsFolded(text, "object") || equalsFolded(text, "constructor") ||
           equalsFolded(text, "destructor") || equalsFolded(text, "property");
}

bool isIntDeclared(DeclaredType t) {
    return t >= DeclaredType::Byte && t <= DeclaredType::Int64;
}

// The three integer types a literal / folded constant can have (ReadInteger, uPSCompiler.pas:5720).
bool isConstInt(DeclaredType t) {
    return t == DeclaredType::Integer || t == DeclaredType::Cardinal || t == DeclaredType::Int64;
}

enum class FoldStatus : std::uint8_t { Ok, Mismatch, DivZero };

// T-0035: TPSPascalCompiler.PreCalc (uPSCompiler.pas:3325-3770). `l` is both left operand and
// destination: the arithmetic runs at the LEFT operand's type (s32/u32/s64 wrap), an integer left
// with a real right is first converted to Extended (:3331-3332), a failure is "Type mismatch"
// (:3764-3770). Without PS_DELPHIDIV `/` and `div` are one operator (:3436-3470 `otDiv, otIntDiv`):
// truncating on integers, real on reals.
FoldStatus preCalc(BinaryOp op, ConstVal& l, const ConstVal& r) {
    const bool relational = op == BinaryOp::Eq || op == BinaryOp::Ne || op == BinaryOp::Lt ||
                            op == BinaryOp::Gt || op == BinaryOp::Le || op == BinaryOp::Ge;
    auto compare = [&](const auto& a, const auto& b) {
        const bool res = op == BinaryOp::Eq   ? a == b
                         : op == BinaryOp::Ne ? a != b
                         : op == BinaryOp::Lt ? a < b
                         : op == BinaryOp::Gt ? a > b
                         : op == BinaryOp::Le ? a <= b
                                              : a >= b;
        l = ConstVal{Value::fromBool(res), DeclaredType::Boolean}; // ConvertToBoolean
        return FoldStatus::Ok;
    };
    if (r.type == DeclaredType::Extended && isConstInt(l.type)) {
        l = ConstVal{Value::fromReal(static_cast<long double>(*l.value.toInt())), DeclaredType::Extended};
    }
    if (isConstInt(l.type)) {
        // GetInt/GetUint/GetInt64 take a btChar right operand as its ord and a btEnum (Boolean) one
        // as 0/1 (uPSCompiler.pas:2913, :2917, :2939, :2943, :2963, :2967).
        qint64 rightRaw = 0;
        if (isConstInt(r.type)) {
            rightRaw = *r.value.toInt();
        } else if (r.type == DeclaredType::Char) {
            rightRaw = r.value.toStr().isEmpty() ? 0 : static_cast<unsigned char>(r.value.toStr().at(0));
        } else if (r.type == DeclaredType::Boolean) {
            rightRaw = *r.value.toBool() ? 1 : 0;
        } else {
            return FoldStatus::Mismatch;
        }
        const qint64 a = *l.value.toInt();
        const qint64 b = *Value::fromInt(rightRaw).wrappedTo(l.type).toInt(); // GetInt/GetUint
        if (relational) {
            return compare(a, b);
        }
        const auto ua = static_cast<quint64>(a);
        const auto ub = static_cast<quint64>(b);
        const quint64 shiftMask = l.type == DeclaredType::Int64 ? 63u : 31u; // x86 CL masking
        const quint64 valueMask = l.type == DeclaredType::Int64 ? ~quint64{0} : 0xFFFFFFFFULL;
        qint64 res = 0;
        switch (op) {
        case BinaryOp::Add:
            res = static_cast<qint64>(ua + ub);
            break;
        case BinaryOp::Sub:
            res = static_cast<qint64>(ua - ub);
            break;
        case BinaryOp::Mul:
            res = static_cast<qint64>(ua * ub);
            break;
        case BinaryOp::RealDiv:
        case BinaryOp::IntDiv:
            if (b == 0) {
                return FoldStatus::DivZero;
            }
            res = b == -1 ? static_cast<qint64>(0 - ua) : a / b;
            break;
        case BinaryOp::Mod:
            if (b == 0) {
                return FoldStatus::DivZero;
            }
            res = b == -1 ? 0 : a % b;
            break;
        case BinaryOp::Shl:
            res = static_cast<qint64>(ua << (ub & shiftMask));
            break;
        case BinaryOp::Shr:
            res = static_cast<qint64>((ua & valueMask) >> (ub & shiftMask));
            break;
        case BinaryOp::And:
            res = a & b;
            break;
        case BinaryOp::Or:
            res = a | b;
            break;
        case BinaryOp::Xor:
            res = a ^ b;
            break;
        default:
            return FoldStatus::Mismatch;
        }
        l.value = Value::fromInt(res).wrappedTo(l.type);
        return FoldStatus::Ok;
    }
    if (l.type == DeclaredType::Extended) {
        if (!isConstInt(r.type) && r.type != DeclaredType::Extended) {
            return FoldStatus::Mismatch;
        }
        const long double a = *l.value.toReal();
        const long double b = *r.value.toReal();
        if (relational) {
            return compare(a, b);
        }
        switch (op) {
        case BinaryOp::Add:
            l.value = Value::fromReal(a + b);
            break;
        case BinaryOp::Sub:
            l.value = Value::fromReal(a - b);
            break;
        case BinaryOp::Mul:
            l.value = Value::fromReal(a * b);
            break;
        case BinaryOp::RealDiv:
        case BinaryOp::IntDiv:
            if (b == 0.0L) {
                return FoldStatus::DivZero; // EZeroDivide -> ecDivideByZero (:3735-3740)
            }
            l.value = Value::fromReal(a / b);
            break;
        default:
            return FoldStatus::Mismatch;
        }
        return FoldStatus::Ok;
    }
    if (l.type == DeclaredType::Char || l.type == DeclaredType::AnsiString) {
        const bool rightIsText = r.type == DeclaredType::Char || r.type == DeclaredType::AnsiString;
        // A Char left of `+` never fails: ConvertToString's flag goes to a local, GetString of a
        // non-string is '' (uPSCompiler.pas:3358-3361, :3040-3044). A string left still fails (:3362).
        if (op == BinaryOp::Add && (rightIsText || l.type == DeclaredType::Char)) {
            l = ConstVal{Value::fromStr(l.value.toStr() + (rightIsText ? r.value.toStr() : QByteArray())),
                         DeclaredType::AnsiString};
            return FoldStatus::Ok;
        }
        if (!rightIsText) {
            return FoldStatus::Mismatch;
        }
        // Strings and chars fold only `=` and `<>` (:3640-3700); `<` `>` `<=` `>=` are a mismatch.
        return op == BinaryOp::Eq || op == BinaryOp::Ne ? compare(l.value.toStr(), r.value.toStr())
                                                        : FoldStatus::Mismatch;
    }
    if (l.type == DeclaredType::Boolean) {
        // A Boolean (btEnum) left operand folds on ordinals with an integer, Char or Boolean right
        // operand (GetUint/GetInt, uPSCompiler.pas:2900-2948): `+ -` at u32 (:3343, :3381), `and or xor`
        // at s32 (:3522, :3539, :3554), `= <>` compare (:3708, :3671). `* / div mod shl shr` and
        // `< > <= >=` have no btEnum case: Type mismatch. The engine keeps the ordinal (`True + 1` is
        // ordinal 2); a Boolean Value cannot, so the result is stored as `n <> 0` (L-121).
        qint64 rightOrd = 0;
        if (isConstInt(r.type)) {
            rightOrd = *r.value.toInt();
        } else if (r.type == DeclaredType::Char) {
            rightOrd = r.value.toStr().isEmpty() ? 0 : static_cast<unsigned char>(r.value.toStr().at(0));
        } else if (r.type == DeclaredType::Boolean) {
            rightOrd = *r.value.toBool() ? 1 : 0;
        } else {
            return FoldStatus::Mismatch;
        }
        const qint64 a = *l.value.toBool() ? 1 : 0;
        const qint64 b = *Value::fromInt(rightOrd).wrappedTo(DeclaredType::Integer).toInt();
        qint64 res = 0;
        switch (op) {
        case BinaryOp::Eq:
        case BinaryOp::Ne:
            return compare(a, b);
        case BinaryOp::Add:
            res = a + b;
            break;
        case BinaryOp::Sub:
            res = a - b;
            break;
        case BinaryOp::And:
            res = a & b;
            break;
        case BinaryOp::Or:
            res = a | b;
            break;
        case BinaryOp::Xor:
            res = a ^ b;
            break;
        default:
            return FoldStatus::Mismatch;
        }
        l.value = Value::fromBool(Value::fromInt(res).wrappedTo(DeclaredType::Cardinal).toInt() != 0);
        return FoldStatus::Ok;
    }
    return FoldStatus::Mismatch;
}

// EvalConst's unary half (:13328-13385). `not` is bitwise on integers, logical on Boolean; unary
// minus negates integers (wrapping at their own type) and reals; anything else is a mismatch.
FoldStatus foldUnary(bool isNot, ConstVal& v) {
    if (isConstInt(v.type)) {
        const qint64 a = *v.value.toInt();
        v.value =
            Value::fromInt(isNot ? ~a : static_cast<qint64>(0 - static_cast<quint64>(a))).wrappedTo(v.type);
        return FoldStatus::Ok;
    }
    if (isNot && v.type == DeclaredType::Boolean) {
        v.value = Value::fromBool(!*v.value.toBool());
        return FoldStatus::Ok;
    }
    if (!isNot && v.type == DeclaredType::Extended) {
        v.value = Value::fromReal(-*v.value.toReal());
        return FoldStatus::Ok;
    }
    return FoldStatus::Mismatch;
}

// The first bound variable that is a Double (GetLlon .. GetBMetr, spec 5.1 order).
constexpr int kFirstDoubleBoundSlot = 18;

} // namespace

// ==================================================================================================
// SymbolTable
// ==================================================================================================

static_assert(kBoundVariables.size() == static_cast<std::size_t>(SymbolTable::kBoundVariableCount));

SymbolTable::SymbolTable() {
    scopes_.emplace_back();
    for (const BoundVariable& bound : kBoundVariables) {
        Entry entry;
        entry.name = QByteArray(bound.name);
        entry.slot = nextSlot_++;
        entry.boundInterface = bound.boundInterface;
        entry.nonDeterministic = bound.nonDeterministic;
        scopes_.front().entries.push_back(std::move(entry));
        // Bound variables are the host's ScriptVars, not a `var`-declared primitive - Unknown
        // (no range-wrap) is correct and matches N1's "today's behaviour, unchanged".
        declaredTypes_.push_back(DeclaredType::Unknown);
    }
}

void SymbolTable::pushScope() {
    scopes_.emplace_back();
}

void SymbolTable::popScope() {
    scopes_.pop_back();
}

int SymbolTable::declareLocal(const QByteArray& name) {
    Scope& scope = scopes_.back();
    for (const Entry& entry : scope.entries) {
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0) {
            return -1;
        }
    }
    Entry entry;
    entry.name = name;
    entry.slot = nextSlot_++;
    scope.entries.push_back(entry);
    declaredTypes_.push_back(DeclaredType::Unknown); // refined by setSlotType, if the declaration has one
    return entry.slot;
}

int SymbolTable::declareRoutine(const QByteArray& name) {
    for (const RoutineEntry& existing : routines_) {
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
            return -1;
        }
    }
    const int slot = nextSlot_++;
    routines_.push_back(RoutineEntry{name, slot, -1});
    declaredTypes_.push_back(DeclaredType::Unknown); // a routine's own slot is never written as a value
    return slot;
}

void SymbolTable::setSlotType(int slot, const RegisteredType* type, DeclaredType declared) {
    if (slot >= 0 && static_cast<std::size_t>(slot) < declaredTypes_.size()) {
        declaredTypes_[static_cast<std::size_t>(slot)] = declared;
    }
    for (Scope& scope : scopes_) {
        for (Entry& entry : scope.entries) {
            if (entry.slot == slot) {
                entry.declaredType = type;
                return;
            }
        }
    }
}

void SymbolTable::setArrayInfo(int slot, ArrayInfo info) {
    arrayInfos_.emplace_back(slot, info);
}

const ArrayInfo* SymbolTable::arrayInfoForSlot(int slot) const {
    for (const auto& [s, info] : arrayInfos_) {
        if (s == slot) {
            return &info;
        }
    }
    return nullptr;
}

void SymbolTable::setConstValue(int slot, Value value, DeclaredType type) {
    constValues_.push_back(ConstEntry{slot, std::move(value), type});
}

DeclaredType SymbolTable::constTypeForSlot(int slot) const {
    for (const ConstEntry& entry : constValues_) {
        if (entry.slot == slot) {
            return entry.type;
        }
    }
    return DeclaredType::Unknown;
}

const Value* SymbolTable::constValueForSlot(int slot) const {
    for (const ConstEntry& entry : constValues_) {
        if (entry.slot == slot) {
            return &entry.value;
        }
    }
    return nullptr;
}

void SymbolTable::setRoutineArity(int slot, int paramCount) {
    for (RoutineEntry& routine : routines_) {
        if (routine.slot == slot) {
            routine.paramCount = paramCount;
            return;
        }
    }
}

SymbolTable::Resolution SymbolTable::resolve(const QByteArray& name) {
    for (const RoutineEntry& routine : routines_) {
        if (routine.name.compare(name, Qt::CaseInsensitive) == 0) {
            Resolution resolution;
            resolution.kind = ResolutionKind::LocalRoutine;
            resolution.slot = routine.slot;
            resolution.routineParamCount = routine.paramCount;
            return resolution;
        }
    }
    for (const Scope& scope : scopes_ | std::views::reverse) {
        for (const Entry& entry : scope.entries) {
            if (entry.name.compare(name, Qt::CaseInsensitive) == 0) {
                if (entry.nonDeterministic) {
                    nonDeterministic_ = true;
                }
                Resolution resolution;
                resolution.kind = ResolutionKind::LocalVariable;
                resolution.slot = entry.slot;
                resolution.declaredType = entry.declaredType;
                resolution.boundInterface = entry.boundInterface;
                return resolution;
            }
        }
    }
    for (const ExternalSymbol& symbol : kExternals) {
        if (equalsFolded(name, symbol.name)) {
            if (symbol.nonDeterministic) {
                nonDeterministic_ = true;
            }
            Resolution resolution;
            resolution.kind = ResolutionKind::ExternalFunction;
            resolution.function = &symbol;
            return resolution;
        }
    }
    return Resolution{};
}

int SymbolTable::fieldIndex(const RegisteredType& type, const QByteArray& fieldName) {
    for (std::size_t i = 0; i < type.fields.size(); ++i) {
        if (equalsFolded(fieldName, type.fields[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const RegisteredType* SymbolTable::registeredTypeNamed(const QByteArray& name) {
    for (const RegisteredType* type : kRegisteredTypes) {
        if (equalsFolded(name, type->name)) {
            return type;
        }
    }
    return nullptr;
}

const RegisteredType* SymbolTable::declaredTypeOfSlot(int slot) const {
    for (const Scope& scope : scopes_) {
        for (const Entry& entry : scope.entries) {
            if (entry.slot == slot) {
                return entry.declaredType;
            }
        }
    }
    return nullptr;
}

const InterfaceType* SymbolTable::interfaceForBoundSlot(int slot) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= kBoundVariables.size()) {
        return nullptr;
    }
    return kBoundVariables[static_cast<std::size_t>(slot)].boundInterface;
}

std::span<const ExternalSymbol> SymbolTable::externalSymbols() {
    return kExternals;
}

// ==================================================================================================
// Parser - token stream helpers
// ==================================================================================================

Parser::Parser(std::vector<Token> tokens, SymbolTable& symbols)
    : tokens_(std::move(tokens)), symbols_(symbols) {}

const Token& Parser::current() const {
    return tokens_[static_cast<std::size_t>(pos_)];
}

const Token& Parser::lookAhead(int offset) const {
    const int p = pos_ + offset;
    if (p < 0 || static_cast<std::size_t>(p) >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[static_cast<std::size_t>(p)];
}

void Parser::advance() {
    if (static_cast<std::size_t>(pos_) + 1 < tokens_.size()) {
        ++pos_;
    }
}

bool Parser::atKeyword(Keyword keyword) const {
    return current().kind == TokenKind::Identifier && current().keyword == keyword;
}

bool Parser::atKind(TokenKind kind) const {
    return current().kind == kind;
}

void Parser::error(QString message) {
    errorAt(current().line, current().column, std::move(message));
}

void Parser::errorAt(int line, int column, QString message) {
    diagnostics_->push_back(Diagnostic{DiagnosticSeverity::Error, line, column, std::move(message)});
}

bool Parser::expectKind(TokenKind kind, const char* what) {
    if (atKind(kind)) {
        advance();
        return true;
    }
    error(QStringLiteral("expected %1").arg(QString::fromUtf8(what)));
    return false;
}

bool Parser::expectKeyword(Keyword keyword, const char* what) {
    if (atKeyword(keyword)) {
        advance();
        return true;
    }
    error(QStringLiteral("expected %1").arg(QString::fromUtf8(what)));
    return false;
}

void Parser::recover() {
    while (!atKind(TokenKind::End)) {
        if (atKind(TokenKind::Semicolon)) {
            advance();
            return;
        }
        if (current().kind == TokenKind::Identifier) {
            switch (current().keyword) {
            case Keyword::End:
            case Keyword::Else:
            case Keyword::Until:
            case Keyword::Begin:
            case Keyword::If:
            case Keyword::While:
            case Keyword::For:
            case Keyword::Case:
            case Keyword::Repeat:
            case Keyword::Var:
            case Keyword::Const:
            case Keyword::Function:
            case Keyword::Procedure:
                return;
            default:
                break;
            }
        }
        advance();
    }
}

// ==================================================================================================
// Parser - declarations
// ==================================================================================================

core::Result<Program*> Parser::parse(DiagnosticList& diagnostics) {
    diagnostics_ = &diagnostics;
    Program* program = symbols_.make<Program>();
    parseDeclSections(program);
    program->main = parseCompoundStmt();
    expectKind(TokenKind::Dot, "'.'");
    program->globalFrameSize = symbols_.frameSize();
    diagnostics_ = nullptr;
    const bool hasError =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
    if (hasError) {
        return core::fail(core::ErrorCode::ParseError, formatDiagnostics(diagnostics));
    }
    return program;
}

void Parser::parseDeclSections(Program* program) {
    for (;;) {
        if (atKeyword(Keyword::Const)) {
            parseConstSection();
            continue;
        }
        if (atKeyword(Keyword::Var)) {
            parseVarSection();
            continue;
        }
        if (atKeyword(Keyword::Function) || atKeyword(Keyword::Procedure)) {
            parseRoutineDecl(program);
            continue;
        }
        if (atKeyword(Keyword::Type)) {
            error(QStringLiteral("'type' declarations are not supported"));
            recover();
            continue;
        }
        if (atKeyword(Keyword::Uses) || atKeyword(Keyword::Unit) || atKeyword(Keyword::Program)) {
            error(QStringLiteral("unit and uses clauses are not supported"));
            recover();
            continue;
        }
        if (atKeyword(Keyword::Label)) {
            error(QStringLiteral("goto is not supported"));
            recover();
            continue;
        }
        if (current().kind == TokenKind::Identifier && current().keyword == Keyword::None &&
            isRejectedClassKeyword(current().text)) {
            error(QStringLiteral("classes and objects are not supported"));
            recover();
            continue;
        }
        break;
    }
}

void Parser::parseConstSection() {
    advance(); // 'const'
    do {
        if (current().kind != TokenKind::Identifier || current().keyword != Keyword::None) {
            error(QStringLiteral("expected identifier"));
            recover();
            return;
        }
        const QByteArray name = current().text;
        const int line = current().line;
        const int column = current().column;
        advance();
        // T-0035 (L-68): no typed constants - DoConstBlock goes straight from the identifier to
        // `=` (uPSCompiler.pas:12162-12167).
        if (!atKind(TokenKind::Equal)) {
            error(QStringLiteral("is ('=') expected"));
            recover();
            return;
        }
        advance();
        // T-0035: the engine's own const-expr grammar and folding (ReadConstant, :13056-13430).
        const std::optional<ConstVal> folded = parseConstExpr();
        if (!folded.has_value()) {
            recover();
            return;
        }
        const int slot = symbols_.declareLocal(name);
        if (slot < 0) {
            errorAt(line, column, QStringLiteral("duplicate identifier '%1'").arg(QString::fromUtf8(name)));
        } else {
            if (isConstInt(folded->type)) {
                symbols_.setSlotType(slot, nullptr, folded->type); // leftIntType: fold at the const's type
            }
            symbols_.setConstValue(slot, folded->value, folded->type);
        }
        if (!expectKind(TokenKind::Semicolon, "';'")) {
            recover();
            return;
        }
    } while (current().kind == TokenKind::Identifier && current().keyword == Keyword::None);
}

void Parser::parseVarSection() {
    advance(); // 'var'
    do {
        std::vector<std::pair<QByteArray, std::pair<int, int>>> names;
        for (;;) {
            if (current().kind != TokenKind::Identifier || current().keyword != Keyword::None) {
                error(QStringLiteral("expected identifier"));
                recover();
                return;
            }
            names.emplace_back(current().text, std::pair{current().line, current().column});
            advance();
            if (atKind(TokenKind::Comma)) {
                advance();
                continue;
            }
            break;
        }
        if (!expectKind(TokenKind::Colon, "':'")) {
            recover();
            return;
        }
        const ParsedType type = parseTypeName();
        if (atKind(TokenKind::Equal)) {
            advance();
            (void)parseExpr(); // initialiser; slice A parses but does not evaluate it
        }
        for (const auto& [name, pos] : names) {
            const int slot = symbols_.declareLocal(name);
            if (slot < 0) {
                errorAt(pos.first, pos.second,
                        QStringLiteral("duplicate identifier '%1'").arg(QString::fromUtf8(name)));
            } else {
                symbols_.setSlotType(slot, type.recordType, type.kind);
                // T-0032 (PD-1): a static-array slot also gets its element type + bounds, so
                // defaultValueForSlot/evalIndex/execIndexAssign (interpreter.cpp) can build and
                // range-check its Value::fromArray.
                if (type.kind == DeclaredType::StaticArray) {
                    symbols_.setArrayInfo(slot, ArrayInfo{type.elementKind, type.lowBound, type.highBound});
                }
            }
        }
        if (!expectKind(TokenKind::Semicolon, "';'")) {
            recover();
            return;
        }
    } while (current().kind == TokenKind::Identifier && current().keyword == Keyword::None);
}

Parser::ParsedType Parser::parseTypeName() {
    if (atKeyword(Keyword::Array)) {
        advance(); // 'array'
        if (atKind(TokenKind::LBracket)) {
            // Static array (spec 3.6, T-0032/PD-1): parse and keep the declared bounds + element
            // type, unlike the dynamic branch below (zero corpus uses - parsed, not tracked).
            advance();
            const std::optional<ConstVal> loVal = parseConstBound();
            expectKind(TokenKind::DotDot, "'..'");
            const std::optional<ConstVal> hiVal = parseConstBound();
            const qint64 lo = loVal.has_value() ? *loVal->value.toInt() : 0;
            const qint64 hi = hiVal.has_value() ? *hiVal->value.toInt() : -1;
            // T-0035 (L-70): length = hi - lo + 1 must be in [0, MaxInt div 4] (:4200-4205); an
            // empty array[lo..lo-1] is accepted.
            if (loVal.has_value() && hiVal.has_value() && (hi < lo - 1 || hi - lo + 1 > 0x1FFFFFFF)) {
                error(QStringLiteral("Type mismatch"));
            }
            expectKind(TokenKind::RBracket, "']'");
            expectKeyword(Keyword::Of, "'of'");
            const ParsedType element = parseTypeName();
            ParsedType result;
            result.kind = DeclaredType::StaticArray;
            result.elementKind = element.kind;
            result.lowBound = lo;
            result.highBound = hi;
            return result;
        }
        // `array of T` - dynamic (spec 3.6): zero corpus uses, out of this ticket's scope.
        expectKeyword(Keyword::Of, "'of'");
        (void)parseTypeName();
        return {nullptr, DeclaredType::DynamicArray};
    }
    if (atKeyword(Keyword::Record)) {
        error(QStringLiteral("user-defined record types are not supported"));
        skipRecordType();
        return {};
    }
    if (atKeyword(Keyword::Set)) {
        error(QStringLiteral("set types are not supported"));
        advance();
        if (atKeyword(Keyword::Of)) {
            advance();
            if (current().kind == TokenKind::Identifier) {
                advance();
            }
        }
        return {};
    }
    if (current().kind != TokenKind::Identifier) {
        error(QStringLiteral("expected type"));
        return {};
    }
    if (isRejectedClassKeyword(current().text)) {
        error(QStringLiteral("classes and objects are not supported"));
        advance();
        return {};
    }
    const QByteArray name = current().text;
    advance();
    if (const RegisteredType* recordType = SymbolTable::registeredTypeNamed(name)) {
        return {recordType, recordType->kind};
    }
    for (const PrimitiveTypeName& primitive : kPrimitiveTypeNames) {
        if (equalsFolded(name, primitive.name)) {
            return {nullptr, primitive.type};
        }
    }
    return {}; // an unresolvable type name - Unknown, same as today's behaviour (N1: no wrap)
}

void Parser::skipRecordType() {
    if (atKeyword(Keyword::Record)) {
        advance();
        while (!atKeyword(Keyword::End) && !atKind(TokenKind::End)) {
            if (current().kind == TokenKind::Identifier && current().keyword == Keyword::None) {
                advance();
                while (atKind(TokenKind::Comma)) {
                    advance();
                    if (current().kind == TokenKind::Identifier) {
                        advance();
                    }
                }
                if (atKind(TokenKind::Colon)) {
                    advance();
                    (void)parseTypeName();
                }
                if (atKind(TokenKind::Semicolon)) {
                    advance();
                }
            } else {
                advance(); // defensive: never loop forever on unexpected input
            }
        }
        expectKeyword(Keyword::End, "'end'");
    }
}

void Parser::parseRoutineDecl(Program* program) {
    const bool isFunction = atKeyword(Keyword::Function);
    advance(); // 'function' | 'procedure'
    if (current().kind != TokenKind::Identifier || current().keyword != Keyword::None) {
        error(QStringLiteral("expected routine name"));
        recover();
        return;
    }
    const QByteArray name = current().text;
    const int nameLine = current().line;
    const int nameColumn = current().column;
    advance();

    Routine routine;
    routine.name = QString::fromUtf8(name);
    const int routineSlot = symbols_.declareRoutine(name);
    if (routineSlot < 0) {
        errorAt(nameLine, nameColumn,
                QStringLiteral("duplicate identifier '%1'").arg(QString::fromUtf8(name)));
    }

    symbols_.pushScope();
    const int firstParamSlot = symbols_.frameSize();
    std::vector<ParamMode> modes;
    int paramCount = 0;
    if (atKind(TokenKind::LParen)) {
        advance();
        if (!atKind(TokenKind::RParen)) { // spec 3.5 G5: `()` may be empty
            for (;;) {
                ParamMode mode = ParamMode::Value;
                if (atKeyword(Keyword::Const)) {
                    mode = ParamMode::Const;
                    advance();
                } else if (atKeyword(Keyword::Var)) {
                    mode = ParamMode::Var;
                    advance();
                } else if (atKeyword(Keyword::Out)) {
                    mode = ParamMode::Out;
                    advance();
                }
                std::vector<std::pair<QByteArray, std::pair<int, int>>> paramNames;
                for (;;) {
                    if (current().kind != TokenKind::Identifier || current().keyword != Keyword::None) {
                        error(QStringLiteral("expected parameter name"));
                        break;
                    }
                    paramNames.emplace_back(current().text, std::pair{current().line, current().column});
                    advance();
                    if (atKind(TokenKind::Comma)) {
                        advance();
                        continue;
                    }
                    break;
                }
                expectKind(TokenKind::Colon, "':'");
                const ParsedType type = parseTypeName();
                for (const auto& [paramName, pos] : paramNames) {
                    const int slot = symbols_.declareLocal(paramName);
                    if (slot < 0) {
                        errorAt(
                            pos.first, pos.second,
                            QStringLiteral("duplicate identifier '%1'").arg(QString::fromUtf8(paramName)));
                    } else {
                        symbols_.setSlotType(slot, type.recordType, type.kind);
                    }
                    modes.push_back(mode);
                    ++paramCount;
                }
                if (atKind(TokenKind::Semicolon)) {
                    advance();
                    continue;
                }
                break;
            }
        }
        expectKind(TokenKind::RParen, "')'");
    }
    int resultSlot = -1;
    if (isFunction) {
        expectKind(TokenKind::Colon, "':'");
        const ParsedType resultType = parseTypeName();
        resultSlot = symbols_.declareLocal(QByteArrayLiteral("Result"));
        if (resultSlot >= 0) {
            symbols_.setSlotType(resultSlot, resultType.recordType, resultType.kind); // N1/N2 on `Result :=`
        }
    }
    if (routineSlot >= 0) {
        // Set before the body is parsed so a recursive call inside it resolves its own arity.
        symbols_.setRoutineArity(routineSlot, paramCount);
        routineSigs_.push_back(RoutineSig{routineSlot, firstParamSlot, modes});
    }
    expectKind(TokenKind::Semicolon, "';'");
    if (current().kind == TokenKind::Identifier && current().keyword == Keyword::None &&
        equalsFolded(current().text, "external")) {
        // An external routine has no `begin ... end` body at all - unlike every other rejection
        // in this function, there is nothing well-formed to resume parsing into, so stop this
        // declaration here instead of falling through to the body/trailing-';' parsing below
        // (which would desynchronise the token stream against whatever follows, corpus evidence
        // or not - spec 3.4 is zero-occurrence for this row, so this path is purely defensive).
        error(QStringLiteral("external declarations are not supported"));
        recover();
        routine.firstParamSlot = firstParamSlot;
        routine.paramCount = paramCount;
        routine.paramModes = std::move(modes);
        routine.resultSlot = resultSlot;
        routine.frameSize = symbols_.frameSize() - firstParamSlot;
        routine.body = symbols_.make<Compound>();
        symbols_.popScope();
        program->routines.push_back(std::move(routine));
        return;
    }

    parseDeclSections(program);
    Node* body = parseCompoundStmt();
    expectKind(TokenKind::Semicolon, "';'");

    routine.firstParamSlot = firstParamSlot;
    routine.paramCount = paramCount;
    routine.paramModes = std::move(modes);
    routine.resultSlot = resultSlot;
    routine.frameSize = symbols_.frameSize() - firstParamSlot;
    routine.body = body;
    symbols_.popScope();
    program->routines.push_back(std::move(routine));
}

// ==================================================================================================
// Parser - statements
// ==================================================================================================

Node* Parser::parseCompoundStmt() {
    const int line = current().line;
    const int column = current().column;
    expectKeyword(Keyword::Begin, "'begin'");
    auto* compound = symbols_.make<Compound>(line, column);
    for (;;) {
        if (atKeyword(Keyword::End) || atKind(TokenKind::End)) {
            break;
        }
        Node* stmt = parseStmt();
        if (stmt) {
            compound->stmts.push_back(stmt);
        }
        if (atKind(TokenKind::Semicolon)) {
            advance();
            continue;
        }
        break;
    }
    expectKeyword(Keyword::End, "'end'");
    return compound;
}

Node* Parser::parseStmt() {
    if (atKeyword(Keyword::Begin)) {
        return parseCompoundStmt();
    }
    if (atKeyword(Keyword::If)) {
        return parseIfStmt();
    }
    if (atKeyword(Keyword::Case)) {
        return parseCaseStmt();
    }
    if (atKeyword(Keyword::For)) {
        return parseForStmt();
    }
    if (atKeyword(Keyword::While)) {
        return parseWhileStmt();
    }
    if (atKeyword(Keyword::Repeat)) {
        return parseRepeatStmt();
    }
    if (atKeyword(Keyword::Try)) {
        return parseTryStmt();
    }
    if (atKeyword(Keyword::With)) {
        return parseWithStmt();
    }
    if (atKeyword(Keyword::Exit)) {
        const int line = current().line;
        const int column = current().column;
        advance();
        return symbols_.make<Exit>(line, column);
    }
    if (atKeyword(Keyword::Break)) {
        const int line = current().line;
        const int column = current().column;
        advance();
        if (loopDepth_ == 0) {
            errorAt(line, column, QStringLiteral("'break' is not supported outside a loop"));
        }
        return symbols_.make<Break>(line, column);
    }
    if (atKeyword(Keyword::Continue)) {
        const int line = current().line;
        const int column = current().column;
        advance();
        if (loopDepth_ == 0) {
            errorAt(line, column, QStringLiteral("'continue' is not supported outside a loop"));
        }
        return symbols_.make<Continue>(line, column);
    }
    if (atKeyword(Keyword::Goto)) {
        error(QStringLiteral("goto is not supported"));
        recover();
        return nullptr;
    }
    if (atKeyword(Keyword::Inherited)) {
        error(QStringLiteral("classes and objects are not supported"));
        recover();
        return nullptr;
    }
    if (current().kind == TokenKind::Identifier && current().keyword == Keyword::None) {
        if (equalsFolded(current().text, "asm")) {
            error(QStringLiteral("inline assembly is not supported"));
            recover();
            return nullptr;
        }
        if (equalsFolded(current().text, "raise")) {
            error(QStringLiteral("raise is not supported"));
            recover();
            return nullptr;
        }
        if (isRejectedClassKeyword(current().text)) {
            error(QStringLiteral("classes and objects are not supported"));
            recover();
            return nullptr;
        }
        return parseSimpleStmtOrAssign();
    }
    // stmt = ... | (* empty *) - legal wherever a delimiter follows immediately (spec 3.3 S1).
    if (atKind(TokenKind::Semicolon) || atKeyword(Keyword::End) || atKeyword(Keyword::Else) ||
        atKeyword(Keyword::Until) || atKind(TokenKind::End)) {
        return symbols_.make<Empty>(current().line, current().column);
    }
    error(QStringLiteral("unexpected token"));
    recover();
    return nullptr;
}

Node* Parser::parseIfStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'if'
    auto* node = symbols_.make<If>(line, column);
    node->cond = parseExpr();
    expectKeyword(Keyword::Then, "'then'");
    node->thenBranch = parseStmt();
    // spec 3.3 S2: a ';' immediately before 'else' is a syntax error in Delphi but a common
    // hand-edit RemObjects tolerated - accept it with a warning rather than reject a real script.
    if (atKind(TokenKind::Semicolon) && lookAhead(1).kind == TokenKind::Identifier &&
        lookAhead(1).keyword == Keyword::Else) {
        diagnostics_->push_back(
            Diagnostic{DiagnosticSeverity::Warning, current().line, current().column,
                       QStringLiteral("';' before 'else' is tolerated (Delphi rejects it)")});
        advance();
    }
    if (atKeyword(Keyword::Else)) {
        advance();
        node->elseBranch = parseStmt();
    }
    return node;
}

Node* Parser::parseCaseStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'case'
    auto* node = symbols_.make<Case>(line, column);
    node->subject = parseExpr();
    expectKeyword(Keyword::Of, "'of'");
    while (!atKeyword(Keyword::Else) && !atKeyword(Keyword::End) && !atKind(TokenKind::End)) {
        CaseArm arm;
        for (;;) {
            CaseLabel label;
            label.lo = parseConstIntExpr();
            label.hi = label.lo;
            if (atKind(TokenKind::DotDot)) {
                advance();
                label.hi = parseConstIntExpr();
            }
            arm.labels.push_back(label);
            if (atKind(TokenKind::Comma)) {
                advance();
                continue;
            }
            break;
        }
        expectKind(TokenKind::Colon, "':'");
        arm.body = parseStmt();
        node->arms.push_back(std::move(arm));
        if (atKind(TokenKind::Semicolon)) {
            advance();
            continue;
        }
        break;
    }
    if (atKeyword(Keyword::Else)) {
        advance();
        auto* elseCompound = symbols_.make<Compound>();
        for (;;) {
            if (atKeyword(Keyword::End) || atKind(TokenKind::End)) {
                break;
            }
            Node* stmt = parseStmt();
            if (stmt) {
                elseCompound->stmts.push_back(stmt);
            }
            if (atKind(TokenKind::Semicolon)) {
                advance();
                continue;
            }
            break;
        }
        node->elseBranch = elseCompound;
    }
    expectKeyword(Keyword::End, "'end'");
    return node;
}

Node* Parser::parseForStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'for'
    if (current().kind != TokenKind::Identifier || current().keyword != Keyword::None) {
        error(QStringLiteral("expected loop variable"));
        recover();
        return nullptr;
    }
    const QByteArray varName = current().text;
    const int varLine = current().line;
    const int varColumn = current().column;
    advance();
    expectKind(TokenKind::Assign, "':='");
    auto* node = symbols_.make<ForTo>(line, column);
    const SymbolTable::Resolution resolution = symbols_.resolve(varName);
    if (resolution.kind == SymbolTable::ResolutionKind::LocalVariable) {
        node->slot = resolution.slot;
    } else if (resolution.kind == SymbolTable::ResolutionKind::Unknown) {
        errorAt(varLine, varColumn,
                QStringLiteral("unknown identifier '%1'").arg(QString::fromUtf8(varName)));
    } else {
        errorAt(varLine, varColumn, QStringLiteral("'%1' is not a variable").arg(QString::fromUtf8(varName)));
    }
    node->from = parseExpr();
    if (atKeyword(Keyword::Downto)) {
        node->downto = true;
        advance();
    } else {
        expectKeyword(Keyword::To, "'to' or 'downto'");
    }
    node->to = parseExpr();
    expectKeyword(Keyword::Do, "'do'");
    ++loopDepth_;
    node->body = parseStmt();
    --loopDepth_;
    return node;
}

Node* Parser::parseWhileStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'while'
    auto* node = symbols_.make<While>(line, column);
    node->cond = parseExpr();
    expectKeyword(Keyword::Do, "'do'");
    ++loopDepth_;
    node->body = parseStmt();
    --loopDepth_;
    return node;
}

Node* Parser::parseRepeatStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'repeat'
    auto* node = symbols_.make<RepeatUntil>(line, column);
    ++loopDepth_;
    for (;;) {
        if (atKeyword(Keyword::Until)) {
            break;
        }
        Node* stmt = parseStmt();
        if (stmt) {
            node->body.push_back(stmt);
        }
        if (atKind(TokenKind::Semicolon)) {
            advance();
            continue;
        }
        break;
    }
    --loopDepth_;
    expectKeyword(Keyword::Until, "'until'");
    node->cond = parseExpr();
    return node;
}

Node* Parser::parseTryStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'try'
    auto* node = symbols_.make<TryFinally>(line, column);
    for (;;) {
        if (atKeyword(Keyword::Finally) || atKeyword(Keyword::Except)) {
            break;
        }
        Node* stmt = parseStmt();
        if (stmt) {
            node->body.push_back(stmt);
        }
        if (atKind(TokenKind::Semicolon)) {
            advance();
            continue;
        }
        break;
    }
    if (atKeyword(Keyword::Except)) {
        node->isExcept = true;
        advance();
    } else {
        expectKeyword(Keyword::Finally, "'finally' or 'except'");
    }
    for (;;) {
        if (atKeyword(Keyword::End)) {
            break;
        }
        Node* stmt = parseStmt();
        if (stmt) {
            node->handler.push_back(stmt);
        }
        if (atKind(TokenKind::Semicolon)) {
            advance();
            continue;
        }
        break;
    }
    expectKeyword(Keyword::End, "'end'");
    return node;
}

Node* Parser::parseWithStmt() {
    const int line = current().line;
    const int column = current().column;
    advance(); // 'with'
    auto* node = symbols_.make<With>(line, column);
    // spec 3.5: `with` resolves only against the 6 built-in record types (no scope-chain beyond
    // that); each valid designator pushes a `WithScope` that `parseDesignatorTail` checks
    // (innermost first) while parsing `body` below, then all of them pop once the statement ends.
    // `with A, B do` is equivalent to nested `with A do with B do` - B (pushed last) wins a name
    // both share. Corpus never uses `with`, so only the first designator is kept as `node->subject`
    // for the interpreter to (trivially) evaluate; a later designator is guaranteed to be a plain
    // variable reference (records here have no nested record/interface fields to make it anything
    // else), so it has nothing further to evaluate at run time.
    int pushedCount = 0;
    for (;;) {
        const int desLine = current().line;
        const int desColumn = current().column;
        Node* designator = parseExpr();
        if (node->subject == nullptr) {
            node->subject = designator;
        }
        const RegisteredType* type = (designator->kind == NodeKind::VarRef)
                                         ? symbols_.declaredTypeOfSlot(static_cast<VarRef*>(designator)->slot)
                                         : nullptr;
        if (type == nullptr) {
            // A designator that is not a variable of one of the 6 built-in record types stays a
            // parse-time reject, unchanged from T-0013 (spec 3.5).
            errorAt(desLine, desColumn,
                    QStringLiteral("'with' requires a variable of a built-in record type"));
        } else {
            withScopes_.push_back(WithScope{static_cast<VarRef*>(designator)->slot, type});
            ++pushedCount;
        }
        if (atKind(TokenKind::Comma)) {
            advance();
            continue;
        }
        break;
    }
    expectKeyword(Keyword::Do, "'do'");
    node->body = parseStmt();
    for (int i = 0; i < pushedCount; ++i) {
        withScopes_.pop_back();
    }
    return node;
}

Node* Parser::parseSimpleStmtOrAssign() {
    const int line = current().line;
    const int column = current().column;
    const QByteArray name = current().text;
    advance();
    Node* designator = parseDesignatorTail(name, line, column);
    if (atKind(TokenKind::Assign)) {
        advance();
        Node* value = parseExpr();
        // T-0035: the engine's compile-time checks on the target. A constant is not a TPSValueVar
        // (L-67, ProcessIdentifier :10653-10658); a Real into an integer-typed target fails
        // IsCompatibleType (:3271-3272).
        DeclaredType destType = DeclaredType::Unknown;
        if (designator->kind == NodeKind::VarRef) {
            const int slot = static_cast<VarRef*>(designator)->slot;
            if (symbols_.constValueForSlot(slot) != nullptr) {
                errorAt(line, column, QStringLiteral("Internal error (20)"));
            }
            destType = symbols_.declaredTypes()[static_cast<std::size_t>(slot)];
        } else if (designator->kind == NodeKind::Index &&
                   static_cast<Index*>(designator)->base->kind == NodeKind::VarRef) {
            const ArrayInfo* info =
                symbols_.arrayInfoForSlot(static_cast<VarRef*>(static_cast<Index*>(designator)->base)->slot);
            destType = info != nullptr ? info->elementKind : DeclaredType::Unknown;
        } else if (designator->kind == NodeKind::Field &&
                   static_cast<Field*>(designator)->base->kind == NodeKind::VarRef) {
            const DeclaredType record = symbols_.declaredTypes()[static_cast<std::size_t>(
                static_cast<VarRef*>(static_cast<Field*>(designator)->base)->slot)];
            destType = record == DeclaredType::TPoint || record == DeclaredType::TRect
                           ? DeclaredType::Integer
                           : DeclaredType::Unknown;
        }
        checkRealToInt(destType, value);
        if (designator->kind == NodeKind::VarRef) {
            auto* assign = symbols_.make<Assign>(line, column);
            assign->slot = static_cast<VarRef*>(designator)->slot;
            assign->value = value;
            return assign;
        }
        if (designator->kind == NodeKind::Field) {
            auto* field = static_cast<Field*>(designator);
            auto* assign = symbols_.make<FieldAssign>(line, column);
            assign->target = field->base;
            assign->fieldIndex = field->fieldIndex;
            assign->value = value;
            return assign;
        }
        if (designator->kind == NodeKind::Index) {
            auto* index = static_cast<Index*>(designator);
            auto* assign = symbols_.make<IndexAssign>(line, column);
            assign->target = index->base;
            assign->index = index->index;
            assign->value = value;
            return assign;
        }
        errorAt(line, column, QStringLiteral("left side of ':=' is not assignable"));
        return nullptr;
    }
    if (designator->kind == NodeKind::Call) {
        auto* stmt = symbols_.make<CallStmt>(line, column);
        stmt->call = static_cast<Call*>(designator);
        return stmt;
    }
    return designator;
}

// ==================================================================================================
// Parser - expressions (Delphi precedence, spec 3.2)
// ==================================================================================================

Node* Parser::parseExpr() {
    Node* lhs = parseSimpleExpr();
    struct RelOp {
        TokenKind kind;
        BinaryOp op;
    };
    static constexpr RelOp kRelOps[] = {
        {TokenKind::Equal, BinaryOp::Eq},  {TokenKind::NotEqual, BinaryOp::Ne},
        {TokenKind::Less, BinaryOp::Lt},   {TokenKind::Greater, BinaryOp::Gt},
        {TokenKind::LessEq, BinaryOp::Le}, {TokenKind::GreaterEq, BinaryOp::Ge},
    };
    for (const RelOp& relOp : kRelOps) {
        if (atKind(relOp.kind)) {
            const int line = current().line;
            const int column = current().column;
            advance();
            Node* rhs = parseSimpleExpr();
            auto* bin = symbols_.make<Binary>(line, column);
            bin->op = relOp.op;
            bin->lhs = lhs;
            bin->rhs = rhs;
            return bin; // level 4 is non-chaining (spec 3.2)
        }
    }
    if (atKeyword(Keyword::In)) {
        error(QStringLiteral("set types are not supported"));
        advance();
        (void)parseSimpleExpr();
    }
    return lhs;
}

Node* Parser::parseSimpleExpr() {
    Node* lhs = parseTerm();
    for (;;) {
        BinaryOp op;
        if (atKind(TokenKind::Plus)) {
            op = BinaryOp::Add;
        } else if (atKind(TokenKind::Minus)) {
            op = BinaryOp::Sub;
        } else if (atKeyword(Keyword::Or)) {
            op = BinaryOp::Or;
        } else if (atKeyword(Keyword::Xor)) {
            op = BinaryOp::Xor;
        } else {
            break;
        }
        const int line = current().line;
        const int column = current().column;
        advance();
        Node* rhs = parseTerm();
        auto* bin = symbols_.make<Binary>(line, column);
        bin->op = op;
        bin->lhs = lhs;
        bin->rhs = rhs;
        lhs = bin;
    }
    return lhs;
}

Node* Parser::parseTerm() {
    Node* lhs = parseFactor();
    for (;;) {
        BinaryOp op;
        if (atKind(TokenKind::Star)) {
            op = BinaryOp::Mul;
        } else if (atKind(TokenKind::Slash)) {
            op = BinaryOp::RealDiv;
        } else if (atKeyword(Keyword::Div)) {
            op = BinaryOp::IntDiv;
        } else if (atKeyword(Keyword::Mod)) {
            op = BinaryOp::Mod;
        } else if (atKeyword(Keyword::And)) {
            op = BinaryOp::And;
        } else if (atKeyword(Keyword::Shl)) {
            op = BinaryOp::Shl;
        } else if (atKeyword(Keyword::Shr)) {
            op = BinaryOp::Shr;
        } else {
            break;
        }
        const int line = current().line;
        const int column = current().column;
        advance();
        Node* rhs = parseFactor();
        auto* bin = symbols_.make<Binary>(line, column);
        bin->op = op;
        bin->lhs = lhs;
        bin->rhs = rhs;
        lhs = bin;
    }
    return lhs;
}

Node* Parser::parseFactor() {
    const int line = current().line;
    const int column = current().column;
    if (atKind(TokenKind::Plus)) {
        advance();
        auto* node = symbols_.make<Unary>(line, column);
        node->op = UnaryOp::Plus;
        node->operand = parseFactor();
        return node;
    }
    if (atKind(TokenKind::Minus)) {
        advance();
        auto* node = symbols_.make<Unary>(line, column);
        node->op = UnaryOp::Minus;
        node->operand = parseFactor();
        return node;
    }
    if (atKeyword(Keyword::Not)) {
        advance();
        auto* node = symbols_.make<Unary>(line, column);
        node->op = UnaryOp::Not;
        node->operand = parseFactor();
        return node;
    }
    if (atKind(TokenKind::At) || atKind(TokenKind::Caret)) {
        error(QStringLiteral("pointer operators are not supported"));
        advance();
        return parseFactor();
    }
    return parseDesignatorOrLiteral();
}

Node* Parser::parseDesignatorOrLiteral() {
    const Token token = current();
    switch (token.kind) {
    case TokenKind::IntLiteral: {
        advance();
        auto* node = symbols_.make<IntLit>(token.line, token.column);
        node->value = token.intValue;
        return node;
    }
    case TokenKind::RealLiteral: {
        advance();
        auto* node = symbols_.make<RealLit>(token.line, token.column);
        node->value = token.realValue;
        return node;
    }
    case TokenKind::StringLiteral: {
        advance();
        auto* node = symbols_.make<StrLit>(token.line, token.column);
        node->value = token.stringValue;
        return node;
    }
    case TokenKind::LParen: {
        advance();
        Node* inner = parseExpr();
        expectKind(TokenKind::RParen, "')'");
        return inner;
    }
    case TokenKind::LBracket: {
        // `[e1, e2, ...]` open-array/set constructor - see ast.h ArrayLit.
        advance();
        auto* node = symbols_.make<ArrayLit>(token.line, token.column);
        if (!atKind(TokenKind::RBracket)) {
            for (;;) {
                node->elements.push_back(parseExpr());
                if (atKind(TokenKind::Comma)) {
                    advance();
                    continue;
                }
                break;
            }
        }
        expectKind(TokenKind::RBracket, "']'");
        return node;
    }
    default:
        break;
    }
    if (atKeyword(Keyword::Nil)) {
        advance();
        return symbols_.make<NilLit>(token.line, token.column);
    }
    if (token.kind == TokenKind::Identifier && token.keyword == Keyword::None) {
        if (equalsFolded(token.text, "true")) {
            advance();
            auto* node = symbols_.make<BoolLit>(token.line, token.column);
            node->value = true;
            return node;
        }
        if (equalsFolded(token.text, "false")) {
            advance();
            auto* node = symbols_.make<BoolLit>(token.line, token.column);
            node->value = false;
            return node;
        }
        for (const NamedConstant& namedConst : kNamedConstants) {
            if (equalsFolded(token.text, namedConst.name)) {
                advance();
                auto* node = symbols_.make<IntLit>(token.line, token.column);
                node->value = namedConst.value;
                return node;
            }
        }
        advance();
        return parseDesignatorTail(token.text, token.line, token.column);
    }
    error(QStringLiteral("expected expression"));
    return symbols_.make<NilLit>(token.line, token.column);
}

std::vector<Node*> Parser::parseArgList() {
    std::vector<Node*> args;
    if (!expectKind(TokenKind::LParen, "'('")) {
        return args;
    }
    if (!atKind(TokenKind::RParen)) {
        for (;;) {
            args.push_back(parseExpr());
            if (atKind(TokenKind::Comma)) {
                advance();
                continue;
            }
            break;
        }
    }
    expectKind(TokenKind::RParen, "')'");
    return args;
}

Node* Parser::parseDesignatorTail(const QByteArray& name, int line, int column) {
    Node* base = nullptr;
    const RegisteredType* currentType = nullptr;
    const InterfaceType* currentInterface = nullptr;
    bool pendingCall = false;
    CallKind pendingCallKind = CallKind::UserRoutine;
    int pendingRoutineSlot = -1;
    int pendingRoutineParamCount = -1;
    const QByteArray& pendingRoutineName = name;
    const ExternalSymbol* pendingExternal = nullptr;

    // `with` scopes shadow ordinary resolution for their field names (spec 3.5): checked innermost
    // first, matching nested `with A do with B do`. Corpus never uses `with` so this is never hit
    // outside the dedicated test, but the check itself is O(active with depth), i.e. O(1) here.
    const RegisteredType* withType = nullptr;
    int withSlot = -1;
    int withFieldIdx = -1;
    for (const WithScope& scope : withScopes_ | std::views::reverse) {
        const int idx = SymbolTable::fieldIndex(*scope.type, name);
        if (idx >= 0) {
            withType = scope.type;
            withSlot = scope.slot;
            withFieldIdx = idx;
            break;
        }
    }

    if (withType != nullptr) {
        auto* withBase = symbols_.make<VarRef>(line, column);
        withBase->slot = withSlot;
        auto* field = symbols_.make<Field>(line, column);
        field->base = withBase;
        field->fieldIndex = withFieldIdx;
        base = field;
        // The 6 built-in record types are flat (no field is itself a record or interface), so
        // there is nothing further to chain a `.field`/method access from here.
    } else {
        const SymbolTable::Resolution resolution = symbols_.resolve(name);
        switch (resolution.kind) {
        case SymbolTable::ResolutionKind::LocalRoutine:
            pendingCall = true;
            pendingCallKind = CallKind::UserRoutine;
            pendingRoutineSlot = resolution.slot;
            pendingRoutineParamCount = resolution.routineParamCount;
            break;
        case SymbolTable::ResolutionKind::LocalVariable: {
            auto* varRef = symbols_.make<VarRef>(line, column);
            varRef->slot = resolution.slot;
            base = varRef;
            currentType = resolution.declaredType;
            currentInterface = resolution.boundInterface;
            break;
        }
        case SymbolTable::ResolutionKind::ExternalFunction:
            pendingCall = true;
            pendingCallKind = resolution.function->kind == ExternalKind::Intrinsic ? CallKind::Builtin
                                                                                   : CallKind::Registered;
            pendingExternal = resolution.function;
            break;
        case SymbolTable::ResolutionKind::Unknown:
        default: {
            errorAt(line, column, QStringLiteral("unknown identifier '%1'").arg(QString::fromUtf8(name)));
            base = symbols_.make<NilLit>(line, column);
            break;
        }
        }
    }

    auto materialiseCall = [&](std::vector<Node*> args) {
        auto* call = symbols_.make<Call>(line, column);
        call->callKind = pendingCallKind;
        if (pendingCallKind == CallKind::UserRoutine) {
            call->callee = pendingRoutineSlot;
            // Arity is exact and fully under this compiler's control, so it is worth enforcing
            // (spec 3.4 "wrong argument count"). Registered functions/intrinsics are deliberately
            // NOT checked this strictly here: several have host-side optional/overloaded forms
            // (Format's open array, Inc/Dec's optional step) that this slice does not model in
            // full, and getting one arity wrong must never turn into a false Corpus.AllParse
            // failure - see the handoff open questions.
            if (pendingRoutineParamCount >= 0 && static_cast<int>(args.size()) != pendingRoutineParamCount) {
                errorAt(line, column,
                        QStringLiteral("'%1' expects %2 argument(s), got %3")
                            .arg(QString::fromUtf8(pendingRoutineName))
                            .arg(pendingRoutineParamCount)
                            .arg(args.size()));
            }
        } else {
            call->callee = static_cast<int>(pendingExternal - kExternals.data());
        }
        call->args = std::move(args);
        checkVarArgs(call, call->args, pendingCallKind == CallKind::UserRoutine ? "" : pendingExternal->name);
        return call;
    };

    if (pendingCall && !atKind(TokenKind::LParen)) {
        base = materialiseCall({}); // S10: a routine may be called without parentheses
        pendingCall = false;
    }

    for (;;) {
        if (atKind(TokenKind::Dot)) {
            advance();
            if (current().kind != TokenKind::Identifier) {
                error(QStringLiteral("expected field or method name"));
                break;
            }
            const QByteArray member = current().text;
            const int memberLine = current().line;
            const int memberColumn = current().column;
            advance();
            if (currentInterface != nullptr) {
                const InterfaceMethod* method = nullptr;
                for (const InterfaceMethod& candidate : currentInterface->methods) {
                    if (equalsFolded(member, candidate.name)) {
                        method = &candidate;
                        break;
                    }
                }
                // spec 3.3 S10 applies to interface methods too - `Global.Lock;`/`Global.Unlock;`
                // (Google/GoogleSat.zmp, Navionics/*) call a zero-arg method with no parentheses.
                std::vector<Node*> args = atKind(TokenKind::LParen) ? parseArgList() : std::vector<Node*>{};
                if (method == nullptr) {
                    errorAt(memberLine, memberColumn,
                            QStringLiteral("unknown identifier '%1'").arg(QString::fromUtf8(member)));
                    base = symbols_.make<NilLit>();
                } else {
                    auto* call = symbols_.make<Call>(memberLine, memberColumn);
                    call->callKind = CallKind::Method;
                    const int boundSlot = static_cast<VarRef*>(base)->slot;
                    const int methodIndex = static_cast<int>(method - currentInterface->methods.data());
                    call->callee = boundSlot * kMethodSlotStride + methodIndex;
                    call->args = std::move(args);
                    if (call->args.size() != method->argCount) {
                        errorAt(memberLine, memberColumn,
                                QStringLiteral("'%1' expects %2 argument(s), got %3")
                                    .arg(QString::fromUtf8(member))
                                    .arg(method->argCount)
                                    .arg(call->args.size()));
                    }
                    base = call;
                }
                currentType = nullptr;
                currentInterface = nullptr;
                continue;
            }
            auto* field = symbols_.make<Field>(memberLine, memberColumn);
            field->base = base;
            if (currentType != nullptr) {
                const int fieldIdx = SymbolTable::fieldIndex(*currentType, member);
                if (fieldIdx < 0) {
                    errorAt(memberLine, memberColumn,
                            QStringLiteral("unknown identifier '%1'").arg(QString::fromUtf8(member)));
                }
                field->fieldIndex = fieldIdx;
            } else {
                // F4: a statically-unknown base (slice A does not do full type inference through
                // call results) can no longer be trusted to resolve `.field` - reject it with a
                // diagnostic instead of silently emitting fieldIndex == -1 for T-0014 to chase.
                errorAt(memberLine, memberColumn,
                        QStringLiteral("cannot access '%1': base expression has no known field type")
                            .arg(QString::fromUtf8(member)));
                field->fieldIndex = -1;
            }
            base = field;
            currentType = nullptr;
            currentInterface = nullptr;
            continue;
        }
        if (atKind(TokenKind::LBracket)) {
            advance();
            Node* indexExpr = parseExpr();
            expectKind(TokenKind::RBracket, "']'");
            auto* index = symbols_.make<Index>(line, column);
            index->base = base;
            index->index = indexExpr;
            base = index;
            currentType = nullptr;
            currentInterface = nullptr;
            continue;
        }
        if (atKind(TokenKind::LParen)) {
            std::vector<Node*> args = parseArgList();
            if (pendingCall) {
                base = materialiseCall(std::move(args));
                pendingCall = false;
            } else {
                error(QStringLiteral("only a routine can be called"));
            }
            continue;
        }
        if (atKind(TokenKind::Caret)) {
            error(QStringLiteral("pointer operators are not supported"));
            advance();
            continue;
        }
        break;
    }
    if (pendingCall) {
        base = materialiseCall({});
    }
    return base;
}

qint64 Parser::parseConstIntExpr() {
    bool negative = false;
    if (atKind(TokenKind::Minus)) {
        negative = true;
        advance();
    } else if (atKind(TokenKind::Plus)) {
        advance();
    }
    if (!atKind(TokenKind::IntLiteral)) {
        error(QStringLiteral("expected a constant integer"));
        return 0;
    }
    const qint64 value = current().intValue;
    advance();
    return negative ? -value : value;
}

std::optional<BinaryOp> Parser::constOpAt(int level) const {
    const TokenKind kind = current().kind;
    if (level == 0) { // ReadExpression: relational operators (:13265-13305)
        switch (kind) {
        case TokenKind::Equal:
            return BinaryOp::Eq;
        case TokenKind::NotEqual:
            return BinaryOp::Ne;
        case TokenKind::Less:
            return BinaryOp::Lt;
        case TokenKind::Greater:
            return BinaryOp::Gt;
        case TokenKind::LessEq:
            return BinaryOp::Le;
        case TokenKind::GreaterEq:
            return BinaryOp::Ge;
        default:
            return std::nullopt;
        }
    }
    if (level == 1) { // ReadSimpleExpression
        if (kind == TokenKind::Plus) {
            return BinaryOp::Add;
        }
        if (kind == TokenKind::Minus) {
            return BinaryOp::Sub;
        }
        return atKeyword(Keyword::Or)    ? std::optional(BinaryOp::Or)
               : atKeyword(Keyword::Xor) ? std::optional(BinaryOp::Xor)
                                         : std::nullopt;
    }
    // level 2, ReadTerm
    if (kind == TokenKind::Star) {
        return BinaryOp::Mul;
    }
    if (kind == TokenKind::Slash) {
        return BinaryOp::RealDiv;
    }
    static constexpr std::pair<Keyword, BinaryOp> kTermKeywords[] = {{Keyword::Div, BinaryOp::IntDiv},
                                                                     {Keyword::Mod, BinaryOp::Mod},
                                                                     {Keyword::And, BinaryOp::And},
                                                                     {Keyword::Shl, BinaryOp::Shl},
                                                                     {Keyword::Shr, BinaryOp::Shr}};
    for (const auto& [keyword, op] : kTermKeywords) {
        if (atKeyword(keyword)) {
            return op;
        }
    }
    return std::nullopt;
}

// Levels 0..2 are the engine's ReadExpression / ReadSimpleExpression / ReadTerm (left-associative
// chains, relational operators chain too); level 3 is ReadFactor (:13089-13190).
std::optional<ConstVal> Parser::parseConstExpr(int level) {
    if (level < 3) {
        std::optional<ConstVal> lhs = parseConstExpr(level + 1);
        while (lhs.has_value()) {
            const std::optional<BinaryOp> op = constOpAt(level);
            if (!op.has_value()) {
                break;
            }
            const int line = current().line;
            const int column = current().column;
            advance();
            const std::optional<ConstVal> rhs = parseConstExpr(level + 1);
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            const FoldStatus status = preCalc(*op, *lhs, *rhs);
            if (status != FoldStatus::Ok) {
                errorAt(line, column,
                        status == FoldStatus::DivZero ? QStringLiteral("Divide by Zero")
                                                      : QStringLiteral("Type mismatch"));
                return std::nullopt;
            }
        }
        return lhs;
    }
    const Token token = current();
    if (atKeyword(Keyword::Not) || atKind(TokenKind::Minus)) {
        const bool isNot = atKeyword(Keyword::Not);
        advance();
        // `-` applies to a whole TERM, `not` to a factor (L-69, :13104-13118).
        std::optional<ConstVal> operand = parseConstExpr(isNot ? 3 : 2);
        if (operand.has_value() && foldUnary(isNot, *operand) != FoldStatus::Ok) {
            errorAt(token.line, token.column, QStringLiteral("Type mismatch"));
            return std::nullopt;
        }
        return operand;
    }
    if (atKind(TokenKind::LParen)) {
        advance();
        std::optional<ConstVal> inner = parseConstExpr();
        if (inner.has_value() && !expectKind(TokenKind::RParen, "')'")) {
            return std::nullopt;
        }
        return inner;
    }
    if (token.kind == TokenKind::IntLiteral) { // ReadInteger: s32, else u32, else s64 (:5720-5743)
        advance();
        return ConstVal{Value::fromInt(token.intValue), token.intValue <= 0x7FFFFFFF ? DeclaredType::Integer
                                                        : token.intValue <= 0xFFFFFFFFLL
                                                            ? DeclaredType::Cardinal
                                                            : DeclaredType::Int64};
    }
    if (token.kind == TokenKind::RealLiteral) { // ReadReal: Extended (:5592-5599)
        advance();
        return ConstVal{Value::fromReal(token.realValue), DeclaredType::Extended};
    }
    if (token.kind == TokenKind::StringLiteral) { // a one-character literal is a Char (:5705-5709)
        advance();
        return ConstVal{Value::fromStr(token.stringValue),
                        token.stringValue.size() == 1 ? DeclaredType::Char : DeclaredType::AnsiString};
    }
    if (token.kind == TokenKind::Identifier && token.keyword == Keyword::None) {
        // GetConstantIdentifier (:13064-13086) searches constants only: a variable or a call is an
        // unknown identifier. True/False are the Boolean enum's constants.
        const bool isTrue = equalsFolded(token.text, "true");
        if (isTrue || equalsFolded(token.text, "false")) {
            advance();
            return ConstVal{Value::fromBool(isTrue), DeclaredType::Boolean};
        }
        const SymbolTable::Resolution resolution = symbols_.resolve(token.text);
        if (resolution.kind == SymbolTable::ResolutionKind::LocalVariable) {
            if (const Value* value = symbols_.constValueForSlot(resolution.slot)) {
                advance();
                return ConstVal{*value, symbols_.constTypeForSlot(resolution.slot)};
            }
        }
        // rfReplaceAll / rfIgnoreCase are host enum constants, i.e. FConstants entries (:4109).
        for (const NamedConstant& named : kNamedConstants) {
            if (equalsFolded(token.text, named.name)) {
                advance();
                return ConstVal{Value::fromInt(named.value), DeclaredType::Integer};
            }
        }
        // The engine's text has an EMPTY name: MakeError('', ecUnknownIdentifier, '') (:13084, L-120).
        error(QStringLiteral("Unknown identifier ''"));
        return std::nullopt;
    }
    error(QStringLiteral("Syntax error"));
    return std::nullopt;
}

// An array bound must have an integer type (:4143-4163, :4175-4195).
std::optional<ConstVal> Parser::parseConstBound() {
    const int line = current().line;
    const int column = current().column;
    std::optional<ConstVal> bound = parseConstExpr();
    if (bound.has_value() && !isConstInt(bound->type)) {
        errorAt(line, column, QStringLiteral("Type mismatch"));
        return std::nullopt;
    }
    return bound;
}

// ==================================================================================================
// T-0035 - the engine's compile-time type checks that need a static numeric class of an expression
// ==================================================================================================

Parser::NumKind Parser::numericKind(const Node* node) const {
    const auto declared = [&](int slot) {
        return slot >= kFirstDoubleBoundSlot && slot < SymbolTable::kBoundVariableCount
                   ? DeclaredType::Double
                   : symbols_.declaredTypes()[static_cast<std::size_t>(slot)];
    };
    const auto ofType = [](DeclaredType t) {
        return isIntDeclared(t) ? NumKind::Int
               : (t == DeclaredType::Single || t == DeclaredType::Double || t == DeclaredType::Extended)
                   ? NumKind::Real
                   : NumKind::Unknown;
    };
    switch (node->kind) {
    case NodeKind::IntLit:
        return NumKind::Int;
    case NodeKind::RealLit:
        return NumKind::Real;
    case NodeKind::VarRef: {
        const int slot = static_cast<const VarRef*>(node)->slot;
        if (const Value* value = symbols_.constValueForSlot(slot)) {
            return value->kind() == ValueKind::Real  ? NumKind::Real
                   : value->kind() == ValueKind::Int ? NumKind::Int
                                                     : NumKind::Unknown;
        }
        return ofType(declared(slot));
    }
    case NodeKind::Field: {
        const Node* base = static_cast<const Field*>(node)->base;
        if (base->kind != NodeKind::VarRef) {
            return NumKind::Unknown;
        }
        const DeclaredType record = declared(static_cast<const VarRef*>(base)->slot);
        return record == DeclaredType::TPoint || record == DeclaredType::TRect ? NumKind::Int
               : record == DeclaredType::TDoublePoint || record == DeclaredType::TDoubleRect
                   ? NumKind::Real
                   : NumKind::Unknown;
    }
    case NodeKind::Unary: {
        const auto* unary = static_cast<const Unary*>(node);
        return unary->op == UnaryOp::Not ? NumKind::Unknown : numericKind(unary->operand);
    }
    case NodeKind::Binary: { // GetResultType (:8456-8500): int op int keeps t1, anything real is real
        const auto* binary = static_cast<const Binary*>(node);
        if (binary->op != BinaryOp::Add && binary->op != BinaryOp::Sub && binary->op != BinaryOp::Mul &&
            binary->op != BinaryOp::RealDiv && binary->op != BinaryOp::IntDiv) {
            return NumKind::Unknown;
        }
        const NumKind a = numericKind(binary->lhs);
        const NumKind b = numericKind(binary->rhs);
        return a == NumKind::Unknown || b == NumKind::Unknown ? NumKind::Unknown
               : (a == NumKind::Real || b == NumKind::Real)   ? NumKind::Real
                                                              : NumKind::Int;
    }
    case NodeKind::Call: {
        const auto* call = static_cast<const Call*>(node);
        if (call->callKind == CallKind::UserRoutine) {
            return NumKind::Unknown;
        }
        const char* name = kExternals[static_cast<std::size_t>(call->callee)].name;
        static constexpr const char* kReal[] = {"Power",  "IntPower",   "Log2",   "Ln",   "MaxExt",
                                                "MinExt", "StrToFloat", "Int",    "Frac", "Sqrt",
                                                "Sin",    "Cos",        "ArcTan", "Exp",  "Pi"};
        static constexpr const char* kInt[] = {
            "Random", "RandomRange", "Ceil",  "Floor", "Max",      "Min",         "PosEx", "SubStrPos",
            "Length", "Pos",         "Round", "Trunc", "StrToInt", "StrToIntDef", "Ord",   "GetUnixTime"};
        for (const char* real : kReal) {
            if (equalsFolded(QByteArray(name), real)) {
                return NumKind::Real;
            }
        }
        for (const char* integer : kInt) {
            if (equalsFolded(QByteArray(name), integer)) {
                return NumKind::Int;
            }
        }
        if ((equalsFolded(QByteArray(name), "Abs") || equalsFolded(QByteArray(name), "Sqr")) &&
            call->args.size() == 1) {
            return numericKind(call->args[0]);
        }
        return NumKind::Unknown;
    }
    default:
        return NumKind::Unknown;
    }
}

void Parser::checkRealToInt(DeclaredType dest, const Node* value) {
    if (isIntDeclared(dest) && numericKind(value) == NumKind::Real) {
        errorAt(value->line, value->column, QStringLiteral("Type mismatch"));
    }
}

// ValidateParameters (uPSCompiler.pas:5414-5480): an argument for a var/out parameter must be a
// variable (a constant is not: "Variable Expected", :5438), and its type must be the parameter's
// type exactly (IsVarInCompatible :5407-5412) - checked here for a Real argument only.
void Parser::checkVarArgs(const Call* call, const std::vector<Node*>& args, const char* name) {
    const RoutineSig* sig = nullptr;
    if (call->callKind == CallKind::UserRoutine) {
        for (const RoutineSig& candidate : routineSigs_) {
            if (candidate.slot == call->callee) {
                sig = &candidate;
            }
        }
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool isVarArg =
            sig != nullptr
                ? (i < sig->modes.size() &&
                   (sig->modes[i] == ParamMode::Var || sig->modes[i] == ParamMode::Out))
                : ((equalsFolded(QByteArray(name), "Inc") || equalsFolded(QByteArray(name), "Dec") ||
                    equalsFolded(QByteArray(name), "SetLength") ||
                    equalsFolded(QByteArray(name), "Delete")) &&
                   i == 0) ||
                      (equalsFolded(QByteArray(name), "Insert") && i == 1);
        if (!isVarArg || args[i]->kind != NodeKind::VarRef) {
            continue;
        }
        const int slot = static_cast<const VarRef*>(args[i])->slot;
        if (symbols_.constValueForSlot(slot) != nullptr) {
            errorAt(args[i]->line, args[i]->column, QStringLiteral("Variable Expected"));
        } else if (sig != nullptr) {
            const DeclaredType paramType =
                symbols_.declaredTypes()[static_cast<std::size_t>(sig->firstParamSlot) + i];
            const DeclaredType argType =
                slot >= kFirstDoubleBoundSlot && slot < SymbolTable::kBoundVariableCount
                    ? DeclaredType::Double
                    : symbols_.declaredTypes()[static_cast<std::size_t>(slot)];
            const auto isArray = [](DeclaredType t) {
                return t == DeclaredType::StaticArray || t == DeclaredType::DynamicArray;
            };
            // IsVarInCompatible (:5407-5412): two known, different types are two engine type objects
            // (the port merges Integer/LongInt, which are one engine object, and string/AnsiString, which are
            // two: lenient); arrays compare element types instead (:5463-5470), so they are left alone.
            if (paramType != DeclaredType::Unknown && argType != DeclaredType::Unknown &&
                paramType != argType && !isArray(paramType) && !isArray(argType)) {
                errorAt(args[i]->line, args[i]->column, QStringLiteral("Type mismatch"));
            }
        }
    }
}

} // namespace cg::script
