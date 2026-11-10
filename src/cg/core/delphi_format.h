// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptUtils.pas:212-215 (Format_P), :217-224
// (FormatEx_P) - both delegate to Delphi RTL SysUtils.Format (not vendored in this tree;
// reproduced from its documented behavior), and Src/u_GeoToStrFunc.pas:56-68 (RoundEx).
//
// `delphiFormat` and `roundEx` are new work. `intToStr`/
// `intToHex`/`floatToStr`/`delphiRound`/`delphiTrunc` were already implemented inline in
// interpreter.cpp's callBuiltin (IntToStr/IntToHex/Round/Trunc cases) and value.cpp's
// Value::toStr(); this header re-declares them here and their bodies moved to
// delphi_format.cpp so both `delphiFormat` and the original call sites share one
// implementation - no duplication.

#include "cg/core/result.h"

#include <QByteArray>

#include <cstdint>
#include <span>
#include <variant>

namespace cg::core {

// One `Format` argument. Alternatives are exactly what Delphi's `Format` tells apart: qint64 (%d %u %x),
// long double (%e %f %g %n %m), QByteArray (%s). `bool` only matters for the `%*` width/precision
// forms, which accept it as 1/0 (the script interpreter's implicit integer conversion); a script
// Empty value is passed as `false` (it converts to 0 there and is rejected by every other specifier,
// exactly like Bool). std::monostate stands for any value no specifier accepts (script record, array,
// interface): every specifier and `%*` reject it. Anything but the matching alternative is the
// "'%x' invalid or incompatible with argument N" error.
using FormatArg = std::variant<std::monostate, bool, qint64, long double, QByteArray>;

// Delphi `Format`/`FormatEx`. Supported specifiers: %d %u %x %s %e %f %g %n %m, plus the
// `%<index>:` (0-based explicit argument index), `%-<width>`, `%.<prec>`, `%*` (width/prec taken
// from the next argument) and `%%` (literal '%') forms. `%p` and anything else
// -> a runtime error naming the specifier (%p is rejected on purpose, not treated as a real
// Delphi Format type; an unknown specifier is never passed through silently).
//
// `thousandsSeparator`/`currencyString` reproduce the "FormatEx uses a local,
// default-initialised TFormatSettings with only DecimalSeparator assigned" quirk ("%n and %m
// produce garbage - #0 thousands separator, empty currency string") without a second entry
// point: `Format` calls this with the defaults below (',' thousands, empty currency, which is
// what the port's forced-C-locale "process-global FormatSettings" amounts to); `FormatEx` passes
// thousandsSeparator='\0' and an empty currencyString explicitly.
[[nodiscard]] Result<QByteArray> delphiFormat(const QByteArray& fmt, std::span<const FormatArg> args,
                                              char decimalSeparator = '.', char thousandsSeparator = ',',
                                              const QByteArray& currencyString = QByteArray());

[[nodiscard]] QByteArray intToStr(qint64 v);
[[nodiscard]] QByteArray intToHex(qint64 v, int digits);
[[nodiscard]] QByteArray floatToStr(long double v); // Delphi FloatToStr, ffGeneral, 15 sig digits
[[nodiscard]] QByteArray roundEx(double v, int precision);
[[nodiscard]] qint64 delphiRound(long double v); // Delphi Round: banker's rounding
[[nodiscard]] qint64 delphiTrunc(long double v); // Delphi Trunc: toward zero

} // namespace cg::core
