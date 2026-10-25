// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// FEAT-GEO-034 (held open). Ports: Src/MarksDB/mORMot/u_MarkSystemORMModel.pas
// (docs/specs/marks.md §2.9.1, §2.9.3 steps B-9...B-12, §2.9.14).
//
// Legacy declares ten ORM row classes and lets the mORMot library derive the schema of Marks.db3
// from them. No ORM is ported: this unit holds the model as the statements mORMot issues for it
// (orm/mormot.orm.core.pas:7430-7552 TOrm.GetSqlCreate, orm/mormot.orm.base.pas:11178-11188
// SqlAddField, orm/mormot.orm.server.pas:1732-1791 CreateSqlMultiIndex), in the order it issues
// them, so that a database created here has the sqlite_master of one created on Windows.
//
// Not ported: CreateModelDBMS (:209-225), CreateModelMongoDB (:227-243), TOrmMarkDBMS /
// TOrmMarkMongoDB (:123-134), CreateMissingIndexesDBMS / ...MongoDB (:345-353) -- other database
// engines; the TOrm* row classes as types (their fields are the columns listed here).

#include "cg/core/result.h"

#include <QByteArrayView>
#include <QtGlobal>

#include <span>

namespace cg::storage::sqlite {
class SqliteDb;
}

namespace cg::marks {

// TOrmGeoType (:68), stored in Mark.mGeoType.
enum class OrmGeoType : quint8 { Undef = 0, Point, Line, Poly };

// One published property of a TOrm* class: the column name and the type text mORMot appends to it
// (orm/mormot.orm.base.pas:4004-4032, with its trailing ", ").
struct OrmFieldDef {
    QByteArrayView name;
    QByteArrayView sqlType;
    bool unique = false; // `stored AS_UNIQUE`
};

struct OrmTableDef {
    QByteArrayView name;      // class name without "TOrm"
    QByteArrayView sqlCreate; // TOrmModel.GetSqlCreate
    std::span<const OrmFieldDef> fields;
};

// CreateModelSQLite3 (:190-207): the ten tables in model order.
[[nodiscard]] std::span<const OrmTableDef> createModelSqlite3() noexcept;

// B-9...B-11. Ports the call TRestServerDB.CreateMissingTables(0, [itoNoIndex4TID]) of
// u_MarkSystemImplORMClientProvider.pas:191 (mORMot orm/mormot.orm.sqlite3.pas:1520-1600): inside
// one transaction creates every model table that is missing, adds every model column an existing
// table lacks, then the automatic index of the unique field of each table it created. Nothing is
// missing -> no statement at all. On a failing statement: ROLLBACK TRANSACTION and the SQLite
// error. Returns the number of statements issued (BEGIN and COMMIT included).
[[nodiscard]] core::Result<int> createMissingTablesSqlite3(storage::sqlite::SqliteDb& db);

// B-12. Ports CreateMissingIndexesSQLite3 (:326-343) with _CreateMissingIndexes (:245-324): the
// ten SAS indexes, each in autocommit, a failing statement ignored. Legacy defect (LC-8): the
// "does it exist already" test never matches, so all ten are requested on every call; IF NOT
// EXISTS makes that harmless. The index-name query feeding that test is therefore not ported.
void createMissingIndexesSqlite3(storage::sqlite::SqliteDb& db);

} // namespace cg::marks
