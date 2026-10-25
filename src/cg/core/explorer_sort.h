// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: ExplorerSort.pas, CompareStringOrdinal only (the list/tree-view callbacks are VCL).
//
// Case-insensitive "natural" order: digit runs compare as numbers, so "Map (3)" < "Map (103)".

#include <QStringView>

namespace cg::core {

// FEAT-NET-001
// <0, 0, >0 (legacy returns -1, 0, 1). Digit runs accumulate in a 32-bit signed integer that
// wraps on overflow, as legacy does (L-118).
[[nodiscard]] int compareStringOrdinal(QStringView s1, QStringView s2);

} // namespace cg::core
