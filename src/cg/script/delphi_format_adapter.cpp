// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/delphi_format_adapter.h"

namespace cg::script {

namespace {

core::FormatArg toFormatArg(const Value& v) {
    switch (v.kind()) {
    case ValueKind::Int:
        return *v.toInt();
    case ValueKind::Real:
        return *v.toReal();
    case ValueKind::Str:
        return v.toStr();
    case ValueKind::Bool:
        return *v.toBool();
    case ValueKind::Empty:
        return false; // converts to 0 for `%*`, rejected elsewhere - same as Bool
    default:
        return std::monostate{};
    }
}

} // namespace

core::Result<QByteArray> delphiFormat(const QByteArray& fmt, std::span<const Value> args,
                                      char decimalSeparator, char thousandsSeparator,
                                      const QByteArray& currencyString) {
    std::vector<core::FormatArg> converted;
    converted.reserve(args.size());
    for (const Value& v : args) {
        converted.push_back(toFormatArg(v));
    }
    return core::delphiFormat(fmt, converted, decimalSeparator, thousandsSeparator, currencyString);
}

} // namespace cg::script
