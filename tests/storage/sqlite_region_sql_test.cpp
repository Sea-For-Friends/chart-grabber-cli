// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-018. Ports the pure-text parts of Src/TileStorage/SQLite/u_TileStorageSQLiteFunc.pas
// (:69-164) and the SQL-builder slice of Src/TileStorage/SQLite/u_TileStorageSQLiteHandler.pas
// (:252-347); see docs/specs/storage.md §2.16.3 "The statements".
#include "cg/storage/sqlite/sqlite_region_sql.h"

#include <QByteArray>
#include <QByteArrayView>

#include <gtest/gtest.h>

using cg::storage::sqlite::VersionColMode;
using cg::storage::sqlite::versionFieldIsEqual;

namespace {

// The version-predicate matrix of §2.16.3: both column modes crossed with a requested version that
// (a) is absent, (b) parses as an integer, (c) does not parse because of a leading 'x'/'X'/'$'
// (LocalTryStrToInt64's own exclusion, not a digit-grammar failure), or (d) does not parse because
// it simply is not numeric. Ports VersionFieldIsEqual (u_TileStorageSQLiteFunc.pas:124-161) together
// with ParseSQLiteDBVersion's (:94-121) quoting decisions.
struct Case {
    VersionColMode mode;
    const char* requested; // "" means absent/empty
    const char* expectedSql;
    const char* note;
};

const Case kCases[] = {
    // vcm_Int column
    {VersionColMode::Int, "", "v=0", "absent -> default version 0, which counts as int -> v=<n>"},
    {VersionColMode::Int, "701", "v=701", "parses as int -> v=<n>, unquoted"},
    {VersionColMode::Int, "X701", "cast(v as TEXT)='X701'",
     "leading 'X' rejected by LocalTryStrToInt64 -> treated as text -> cast(v as TEXT)='<s>'"},
    {VersionColMode::Int, "$123", "cast(v as TEXT)='$123'",
     "leading '$' rejected by LocalTryStrToInt64 -> treated as text -> cast(v as TEXT)='<s>'"},
    {VersionColMode::Int, "RUS", "cast(v as TEXT)='RUS'", "not numeric -> cast(v as TEXT)='<s>'"},
    // vcm_Text column
    {VersionColMode::Text, "", "cast(v as TEXT)=''",
     "absent -> default version '', which counts as int (ParseSQLiteDBVersion always sets "
     "RequestedVersionIsInt for the default) -> cast(v as TEXT)='<s>'"},
    {VersionColMode::Text, "701", "cast(v as TEXT)='701'",
     "parses as int -> quoted for a TEXT column -> cast(v as TEXT)='<s>'"},
    {VersionColMode::Text, "X701", "v='X701'",
     "leading 'X' rejected by LocalTryStrToInt64 -> treated as text -> v='<s>'"},
    {VersionColMode::Text, "$123", "v='$123'",
     "leading '$' rejected by LocalTryStrToInt64 -> treated as text -> v='<s>'"},
    {VersionColMode::Text, "RUS", "v='RUS'", "not numeric -> v='<s>'"},
};

} // namespace

TEST(SqliteRegionSql, VersionFieldIsEqualMatrix) {
    for (const auto& c : kCases) {
        SCOPED_TRACE(c.note);
        EXPECT_EQ(versionFieldIsEqual(c.mode, QByteArrayView(c.requested)), QByteArray(c.expectedSql));
    }
}
