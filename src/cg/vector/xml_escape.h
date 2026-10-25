// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_XmlEscape.pas
//
// XML text and attribute escaping with the legacy tables (spec vector.md §2.13). Characters above
// U+003F are never touched; U+0000 ends the scan (legacy defect LV-8, docs/parity/legacy-defects.md).

#include <QString>

namespace cg::vector {

// useNumericReference == true (the default) selects the numeric table: &#60; &#34; ...
// false selects the named table: &lt; &quot; ... Control characters U+0001..U+001F other than
// TAB, LF and CR are dropped in both tables.
[[nodiscard]] QString xmlEscapeText(const QString& text, bool useNumericReference = true);
[[nodiscard]] QString xmlEscapeAttr(const QString& value, bool useNumericReference = true);

} // namespace cg::vector
