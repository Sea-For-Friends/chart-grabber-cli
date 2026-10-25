// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (adapter; the Format port is cg/core/delphi_format.h).
// Adapter from script::Value to the core Delphi `Format` argument (T-0092). Maps by ValueKind so
// core sees exactly the kinds the interpreter's Format/FormatEx/WriteFmt call sites passed before the move.

#include "cg/core/delphi_format.h"
#include "cg/script/value.h"

#include <span>
#include <vector>

namespace cg::script {

[[nodiscard]] core::Result<QByteArray> delphiFormat(const QByteArray& fmt, std::span<const Value> args,
                                                    char decimalSeparator, char thousandsSeparator,
                                                    const QByteArray& currencyString);

} // namespace cg::script
