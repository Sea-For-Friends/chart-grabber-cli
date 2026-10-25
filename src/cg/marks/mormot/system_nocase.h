// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// FEAT-GEO-034 (held open). Ports: nothing of the SAS tree -- the SYSTEMNOCASE collation of the
// mORMot 2 ORM library, which legacy registers on every Marks.db3 connection; the one port file of
// src/cg/marks/mormot/ without a SAS unit (docs/specs/marks.md §2.9.4, §5.3). mORMot checkout
// 2.4.17538:
//   db/mormot.db.raw.sqlite3.pas:6328-6341   Utf8_SYSTEMNOCASE (the SQLite callback)
//   core/mormot.core.unicode.pas:7683-7798   Utf8ILComp
//   core/mormot.core.unicode.pas:84-107      UTF8_TABLE
//   core/mormot.core.unicode.pas:12572-12581 WinAnsiToUp, copied into NormToUpper at :12604-12612
//   db/mormot.db.raw.sqlite3.pas:7582-7583   registration (SQLITE_UTF8)
// The collation orders the on-disk indexes of uName, cName and miName, so the result must have the
// sign the Windows program computes for every pair of byte strings.

#include <QByteArrayView>

namespace cg::marks {

// sqlite3 collation callback contract: <0, 0, >0. Lengths are honoured (no terminator is looked
// for), so an embedded NUL is an ordinary character below every other one.
//
// Integer model: the 64-bit Windows build (the comparison runs in a 64-bit PtrInt and the callback
// returns its low 32 bits).
// Deviation D-69: a 6-byte sequence (lead byte FC/FD) whose low 32 decoded bits are below 0x100
// makes legacy index the 256-entry upper-case table far out of bounds in that build; the port
// indexes with the low 8 bits, which is what the 32-bit Windows build computes.
[[nodiscard]] int systemNoCaseCompare(QByteArrayView a, QByteArrayView b) noexcept;

} // namespace cg::marks
