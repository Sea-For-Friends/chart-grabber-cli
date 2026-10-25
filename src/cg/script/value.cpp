// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/PascalScript/uPSRuntime.pas (variant coercion rules used by the RemObjects VM for
// every implicit conversion, including the declared-type range-wrap/rounding `wrappedTo` ports -
// see value.h).
#include "cg/script/value.h"

#include "cg/core/delphi_format.h" // floatToStr (spec 5.4/4.1 N2, moved to core T-0092)

namespace cg::script {

Value Value::fromInt(qint64 v) {
    Value value;
    value.kind_ = ValueKind::Int;
    value.data_ = v;
    return value;
}
Value Value::fromReal(long double v) {
    Value value;
    value.kind_ = ValueKind::Real;
    value.data_ = v;
    return value;
}
Value Value::fromBool(bool v) {
    Value value;
    value.kind_ = ValueKind::Bool;
    value.data_ = v;
    return value;
}
Value Value::fromStr(QByteArray v) {
    Value value;
    value.kind_ = ValueKind::Str;
    value.data_ = std::move(v);
    return value;
}
Value Value::fromRecord(std::vector<Value> fields) {
    Value value;
    value.kind_ = ValueKind::Record;
    value.data_ = std::make_shared<std::vector<Value>>(std::move(fields));
    return value;
}
Value Value::fromArray(std::vector<Value> elems, qint64 lowBound) {
    Value value;
    value.kind_ = ValueKind::Array;
    value.data_ = std::make_shared<std::vector<Value>>(std::move(elems));
    value.lowBound_ = lowBound;
    return value;
}
Value Value::fromInterface(std::shared_ptr<void> obj, InterfaceId id) {
    Value value;
    value.kind_ = ValueKind::Interface;
    value.data_ = InterfaceRef{std::move(obj), id};
    return value;
}

core::Result<qint64> Value::toInt() const {
    switch (kind_) {
    case ValueKind::Int:
        return std::get<qint64>(data_);
    case ValueKind::Bool:
        return std::get<bool>(data_) ? 1 : 0;
    case ValueKind::Empty:
        return 0;
    default:
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("cannot convert this value to an integer"));
    }
}

core::Result<long double> Value::toReal() const {
    switch (kind_) {
    case ValueKind::Real:
        return std::get<long double>(data_);
    case ValueKind::Int:
        return static_cast<long double>(std::get<qint64>(data_));
    case ValueKind::Empty:
        return 0.0L;
    default:
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("cannot convert this value to a real"));
    }
}

core::Result<bool> Value::toBool() const {
    switch (kind_) {
    case ValueKind::Bool:
        return std::get<bool>(data_);
    case ValueKind::Empty:
        return false;
    default:
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("cannot convert this value to a boolean"));
    }
}

QByteArray Value::toStr() const {
    switch (kind_) {
    case ValueKind::Str:
        return std::get<QByteArray>(data_);
    case ValueKind::Int:
        return QByteArray::number(static_cast<qlonglong>(std::get<qint64>(data_)));
    case ValueKind::Real:
        return core::floatToStr(std::get<long double>(data_));
    case ValueKind::Bool:
        return std::get<bool>(data_) ? QByteArrayLiteral("TRUE") : QByteArrayLiteral("FALSE");
    default:
        return QByteArray();
    }
}

Value Value::wrappedTo(DeclaredType type) const {
    // PD-2 (T-0031): a slot declared Extended/Double/Single holds a Real (spec 4.3 A1: "Int ->
    // real widens"). An Int - or an Empty slot read as its zero value - assigned/passed/returned
    // into such a slot converts to Real before any narrowing, exactly like the Real branch below
    // narrows an already-Real value. uPSRuntime.pas has no separate "int -> real" opcode: the
    // RemObjects VM's assignment/parameter-bind/Result-store path calls the same
    // ConvertToRealAndBack-style coercion this mirrors (see docs/handoffs/T-0015E-review.md PD-2).
    if (type == DeclaredType::Single || type == DeclaredType::Double || type == DeclaredType::Extended) {
        if (kind_ == ValueKind::Int || kind_ == ValueKind::Empty) {
            const long double v =
                kind_ == ValueKind::Int ? static_cast<long double>(std::get<qint64>(data_)) : 0.0L;
            switch (type) {
            case DeclaredType::Double:
                return Value::fromReal(static_cast<long double>(static_cast<double>(v)));
            case DeclaredType::Single:
                return Value::fromReal(static_cast<long double>(static_cast<float>(v)));
            default:
                return Value::fromReal(v); // Extended: full long double precision
            }
        }
    }
    if (kind_ == ValueKind::Int) {
        qint64 v = std::get<qint64>(data_);
        switch (type) {
        case DeclaredType::Byte:
            v = static_cast<quint8>(v);
            break;
        case DeclaredType::Word:
            v = static_cast<quint16>(v);
            break;
        case DeclaredType::ShortInt:
            // Sign extension is deliberate here - Delphi ShortInt is a signed 8-bit range
            // (-128..127) and N1 wraparound must preserve the sign, not reinterpret the low byte
            // as unsigned.
            v = static_cast<qint64>(static_cast<qint8>(v)); // NOLINT(bugprone-signed-char-misuse)
            break;
        case DeclaredType::SmallInt:
            v = static_cast<qint16>(v);
            break;
        case DeclaredType::Cardinal:
            v = static_cast<quint32>(v);
            break;
        case DeclaredType::Integer:
            v = static_cast<qint32>(v);
            break;
        default:
            return *this; // Int64, Unknown, everything else: no wrap (spec 4.1 N1)
        }
        return Value::fromInt(v);
    }
    if (kind_ == ValueKind::Real) {
        const long double v = std::get<long double>(data_);
        switch (type) {
        case DeclaredType::Double:
            return Value::fromReal(static_cast<long double>(static_cast<double>(v)));
        case DeclaredType::Single:
            return Value::fromReal(static_cast<long double>(static_cast<float>(v)));
        default:
            return *this; // Extended, Unknown: full long double precision (spec 4.1 N2)
        }
    }
    return *this;
}

bool Value::isTruthy() const {
    if (kind_ == ValueKind::Bool) {
        return std::get<bool>(data_);
    }
    if (kind_ == ValueKind::Int) {
        return std::get<qint64>(data_) != 0;
    }
    return false;
}

std::vector<Value>& Value::fields() {
    auto& shared = std::get<std::shared_ptr<std::vector<Value>>>(data_);
    if (shared.use_count() > 1) {
        shared = std::make_shared<std::vector<Value>>(*shared); // COW detach
    }
    return *shared;
}

const std::vector<Value>& Value::fields() const {
    return *std::get<std::shared_ptr<std::vector<Value>>>(data_);
}

bool Value::isAssigned() const {
    if (kind_ == ValueKind::Interface) {
        return std::get<InterfaceRef>(data_).obj != nullptr;
    }
    if (kind_ == ValueKind::Array) {
        return std::get<std::shared_ptr<std::vector<Value>>>(data_) != nullptr;
    }
    return false;
}

const InterfaceRef& Value::interfaceRef() const {
    return std::get<InterfaceRef>(data_);
}

} // namespace cg::script
