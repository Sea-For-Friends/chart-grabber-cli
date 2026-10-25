// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/uPSRuntime.pas (TPSVariantIFC / the boxed runtime cell RemObjects gives
// every VM slot) - `cg::script::Value` is this port's dynamically-typed equivalent (spec 2.6).
//
// T-0014B: `SymbolTable` (parser.h) now tracks each slot's `DeclaredType`, parsed from its
// `var`/parameter/`Result` type annotation. `Value::wrappedTo` implements spec 4.1 N1/N2 -
// declared-type range-wrapping/rounding on assignment - narrowing then widening back, exactly as
// Delphi's range-checked-off store into a smaller slot does; every integer is still held as a full
// `qint64` and every real as a full `long double` internally.

#include "cg/core/result.h"

#include <QByteArray>

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace cg::script {

enum class ValueKind : std::uint8_t { Empty, Int, Real, Bool, Str, Record, Array, Interface };

// A slot's *declared* Pascal type (spec 2.6). Tracked per slot by `SymbolTable` (parser.h) from
// its `var`/parameter/`Result` type annotation; `Unknown` where the parser cannot resolve one
// (an untyped `const`, or a bound-variable slot - those are the host's ScriptVars, not modelled
// here). Drives `Value::wrappedTo` (N1/N2) and record default-initialisation (I1) - everything
// else (StaticArray/DynamicArray/InterfaceRef and the string/bool/variant kinds) is recorded for
// completeness but not consulted by anything this slice builds.
enum class DeclaredType : std::uint8_t {
    Unknown,
    Byte,
    Word,
    ShortInt,
    SmallInt,
    Integer,
    Cardinal,
    Int64,
    Boolean,
    Char,
    AnsiString,
    WideString,
    Single,
    Double,
    Extended,
    Variant,
    TPoint,
    TDoublePoint,
    TRect,
    TDoubleRect,
    TTileInfo,
    TReplaceFlags,
    StaticArray,
    DynamicArray,
    InterfaceRef,
};

// Identity of the 7 script-visible interfaces (spec 5.1/5.6): which concrete type `interfaceRef()`
// below casts `InterfaceRef::obj` back to (interpreter.cpp's method-call dispatch, T-0015B).
enum class InterfaceId : std::uint8_t {
    TileCache,
    Logger,
    Global,
    Downloader,
    ProjFactory,
    ProjConverter,
    Converter,
};

// T-0032 (PD-1): a `StaticArray`-declared slot's element type and declared bounds (spec 3.6,
// `array[lo..hi] of T`). `SymbolTable`/`CompiledScript` track one per array-typed slot (parser.h/
// compiled_script.h); `Interpreter` uses it to build the slot's zero-initialised `Value::fromArray`
// (spec 4.4 I1) and to range-check every index (uPSRuntime.pas:7127-7135/:7289-7297).
struct ArrayInfo {
    DeclaredType elementKind = DeclaredType::Unknown;
    qint64 lowBound = 0;
    qint64 highBound = -1; // element count = highBound - lowBound + 1
};

struct InterfaceRef {
    std::shared_ptr<void> obj;
    InterfaceId id = InterfaceId::Converter;
};

// Value type with value-copy semantics for records and static arrays (ADR-0003 semantic table row
// 8): the `shared_ptr<vector<Value>>` behind Record/Array is copy-on-write - `fields()` (non-const)
// detaches before returning, so `TL := BR` copies a `TDoublePoint` without aliasing.
class Value {
public:
    Value() = default; // Empty

    [[nodiscard]] static Value fromInt(qint64 v);
    [[nodiscard]] static Value fromReal(long double v);
    [[nodiscard]] static Value fromBool(bool v);
    [[nodiscard]] static Value fromStr(QByteArray v);
    [[nodiscard]] static Value fromRecord(std::vector<Value> fields);
    [[nodiscard]] static Value fromArray(std::vector<Value> elems, qint64 lowBound);
    [[nodiscard]] static Value fromInterface(std::shared_ptr<void> obj, InterfaceId id);

    [[nodiscard]] ValueKind kind() const noexcept { return kind_; }

    // Delphi implicit conversions. Each returns Result because a failure is a *runtime* error the
    // interpreter must surface, never a silent default (spec 4.3).
    [[nodiscard]] core::Result<qint64> toInt() const;
    [[nodiscard]] core::Result<long double> toReal() const;
    [[nodiscard]] core::Result<bool> toBool() const;
    [[nodiscard]] QByteArray toStr() const; // total: every kind has a string form

    [[nodiscard]] bool isTruthy() const; // spec 2.6: Bool, or Int != 0

    // spec 4.1 N1/N2: range-wraps an Int to the declared integer width, or rounds a Real to
    // double/float precision. Every other (kind, DeclaredType) pair - including Int64/Extended/
    // Unknown, which never wrap - is returned unchanged. Called by the interpreter at every
    // slot-write path that has a declared type (parser.h's SymbolTable).
    [[nodiscard]] Value wrappedTo(DeclaredType type) const;

    [[nodiscard]] std::vector<Value>& fields();
    [[nodiscard]] const std::vector<Value>& fields() const;
    [[nodiscard]] qint64 lowBound() const noexcept { return lowBound_; }

    // Interface only: true when the bound object pointer is non-null (Assigned()/nil compares).
    [[nodiscard]] bool isAssigned() const;

    // Interface only (T-0015B): the bound object plus which of the 7 script-visible interfaces it
    // is - the interpreter's method-call dispatch (interpreter.cpp) casts `obj` back to the
    // concrete interface named by `id`. Precondition: kind() == ValueKind::Interface.
    [[nodiscard]] const InterfaceRef& interfaceRef() const;

private:
    ValueKind kind_ = ValueKind::Empty;
    std::variant<std::monostate, qint64, long double, bool, QByteArray, std::shared_ptr<std::vector<Value>>,
                 InterfaceRef>
        data_;
    qint64 lowBound_ = 0;
};

} // namespace cg::script
