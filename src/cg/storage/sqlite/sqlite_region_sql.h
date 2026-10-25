// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/TileStorage/SQLite/u_TileStorageSQLiteFunc.pas (:69-164) and the
// GetOrderByVersion*/VersionFieldIsEqualOrMax/GetCheckOtherVersionsBy* fragments of
// Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas (:252-347), plus the default-version
// constants of Src/TileStorage/SQLite/c_TileStorageSQLite.pas.
//
// docs/specs/storage.md §2.16.3 "The statements". Pure text builders, no direct engine header here
// (the SQLITE3-LEAK rule restricts that include to sqlite_db.cpp). Legacy builds SQL by string
// concatenation with x/y/size/CRC inlined as literals; the port keeps that ONLY for `v` (its
// comparison is a text/integer coercion dance bound parameters would change) and uses `?`
// placeholders for everything else -- callers bind x, y, d, s, h, c, b themselves (T-0022B3/B4).
//
// FEAT-STO-018.

#include <QByteArray>
#include <QByteArrayView>

#include <cstdint>

namespace cg::storage::sqlite {

// Ports TVersionColMode (t_TileStorageSQLiteHandler.pas): how the region file's `t.v` column is
// declared. Discovered per file by `SELECT * FROM t WHERE 0=1` (spec §2.16.2); out of scope here.
enum class VersionColMode : std::uint8_t { Int, Text };

// Ports LocalTryStrToInt64 (u_TileStorageSQLiteFunc.pas:69-73): the FIRST character being 'x', 'X'
// or '$' makes the string "not a number" regardless of what follows -- "X701" and "$123" are
// versions, not integers (Delphi's TryStrToInt64/Val would otherwise read a leading '$' as its own
// hex-literal marker). Otherwise parses as a base-10 (optionally signed) 64-bit integer. `s` empty
// always fails (legacy never calls this on an empty string -- see versionFieldIsEqual).
[[nodiscard]] bool localTryStrToInt64(QByteArrayView s, qint64* value);

// Ports GetDefaultDBVersion (u_TileStorageSQLiteFunc.pas:75-81) / c_TileStorageSQLite.pas
// (cDefaultVersionAsIntValue = 0, cDefaultVersionAsStrValue = ''): the SQL literal an absent/empty
// requested version becomes.
[[nodiscard]] QByteArray defaultVersion(VersionColMode mode);

// Ports VersionFieldIsEqual (u_TileStorageSQLiteFunc.pas:124-161) folded together with the
// parsing/quoting decisions of ParseSQLiteDBVersion (:94-121) that produce its arguments.
// `requestedVersion` is the version's StoreString (IMapVersionInfo.StoreString); empty means "no
// version requested" and becomes defaultVersion(mode). Returns the full "<lhs>=<value>" predicate
// text (spec §2.16.3's comment block, the four-row table).
[[nodiscard]] QByteArray versionFieldIsEqual(VersionColMode mode, QByteArrayView requestedVersion);

// Ports ParseSQLiteDBVersion's `RequestedVersionToDB` output alone (u_TileStorageSQLiteFunc.pas:
// 94-121) -- the bare literal SQL value for a version (already quoted for a TEXT column, bare
// digits for INT), not a predicate. SaveTile (u_TileStorageSQLiteHandler.pas:1132-1139) splices
// this straight into the INSERT's VALUES list for column `v`; `versionFieldIsEqual` above builds
// the `v=<this>` predicate for SELECT/DELETE/UPDATE instead. Same empty-means-default rule.
[[nodiscard]] QByteArray requestedVersionToDb(VersionColMode mode, QByteArrayView requestedVersion);

// Ports GetOrderByVersion_ASC / GetOrderByVersion_DESC (u_TileStorageSQLiteHandler.pas:277-286),
// i.e. GetOrderByVersion(mode, AIsOrderDESC) with the two literal booleans folded in. Returns the
// fragment that follows "ORDER BY " -- callers prepend that keyword. The Text form appends
// `COLLATE LOGICALSORTING` (§2.15.1); the Int form sorts numerically and never touches it.
[[nodiscard]] QByteArray getOrderByVersionAsc(VersionColMode mode);
[[nodiscard]] QByteArray getOrderByVersionDesc(VersionColMode mode);

// Ports VersionFieldIsEqualOrMax (u_TileStorageSQLiteHandler.pas:290-315): the predicate for "this
// version, or the newest" (spec §2.16.3 table row "tile, this or newest"). `x`/`y` are `?`
// placeholders in the port (legacy inlines them as literals).
//
// Legacy quirk, preserved as written (not "fixed"): the left-hand side is `cast(v as TEXT)` only
// when the column is INTEGER and the request does not parse as one; for a TEXT column it is always
// `v`. This is NOT the same four-way rule as versionFieldIsEqual's left-hand side.
[[nodiscard]] QByteArray versionFieldIsEqualOrMax(VersionColMode mode, QByteArrayView requestedVersion);

// Ports GetCheckOtherVersionsBySizeSQLText / GetCheckOtherVersionsByCRC32SQLText
// (u_TileStorageSQLiteHandler.pas:317-347): the `stfSkipIfSameAsPrev` fragment ("do not store a new
// tile identical to the last one"), CRC-32 when the `h` column exists, size otherwise. Legacy
// inlines x, y and the size/CRC value as literals; the port binds all four as `?`, in order
// (compared-value, x, y, WHERE-value) -- the first and last placeholder are bound to the same
// runtime value.
[[nodiscard]] QByteArray getCheckOtherVersionsBySizeSqlText();
[[nodiscard]] QByteArray getCheckOtherVersionsByCrc32SqlText();

} // namespace cg::storage::sqlite
