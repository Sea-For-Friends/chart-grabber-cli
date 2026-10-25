// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_orm_model.h"

#include "cg/storage/sqlite/sqlite_db.h"

#include <QByteArray>
#include <QStringList>

#include <array>
#include <bitset>
#include <tuple>

namespace cg::marks {

namespace {

using storage::sqlite::SqliteDb;

constexpr QByteArrayView kText = " TEXT COLLATE SYSTEMNOCASE, "; // RawUtf8
constexpr QByteArrayView kInteger = " INTEGER, "; // TID, Boolean, Byte, Cardinal, Integer, enum
constexpr QByteArrayView kBlob = " BLOB, ";       // RawBlob
constexpr QByteArrayView kFloat = " FLOAT, ";     // Double

// The published properties, in declaration order (:38-178).
constexpr std::array<OrmFieldDef, 1> kUserFields = {{{"uName", kText}}};
constexpr std::array<OrmFieldDef, 1> kCategoryFields = {{{"cName", kText}}};
constexpr std::array<OrmFieldDef, 5> kCategoryViewFields = {{
    {"cvUser", kInteger},
    {"cvCategory", kInteger},
    {"cvVisible", kInteger},
    {"cvMinZoom", kInteger},
    {"cvMaxZoom", kInteger},
}};
constexpr std::array<OrmFieldDef, 1> kMarkImageFields = {{{"miName", kText}}};
constexpr std::array<OrmFieldDef, 10> kMarkFields = {{
    {"mCategory", kInteger},
    {"mImage", kInteger},
    {"mAppearance", kInteger},
    {"mName", kText},
    {"mDesc", kText},
    {"mGeoType", kInteger},
    {"mGeoCount", kInteger},
    {"mGeoLonSize", kInteger},
    {"mGeoLatSize", kInteger},
    {"mGeoWKB", kBlob},
}};
constexpr std::array<OrmFieldDef, 4> kMarkViewFields = {{
    {"mvUser", kInteger},
    {"mvMark", kInteger},
    {"mvCategory", kInteger},
    {"mvVisible", kInteger},
}};
constexpr std::array<OrmFieldDef, 4> kMarkAppearanceFields = {{
    {"maColor1", kInteger},
    {"maColor2", kInteger},
    {"maScale1", kInteger},
    {"maScale2", kInteger},
}};
constexpr std::array<OrmFieldDef, 2> kMarkFtsFields = {{{"mName", kText}, {"mDesc", kText}}};
constexpr std::array<OrmFieldDef, 4> kMarkRTreeFields = {{
    {"mLeft", kFloat},
    {"mRight", kFloat},
    {"mBottom", kFloat},
    {"mTop", kFloat},
}};
constexpr std::array<OrmFieldDef, 2> kMarkMetaFields = {{{"mMark", kInteger, true}, {"mMeta", kBlob}}};

// CreateModelSQLite3 (:190-207). The CREATE texts are what TOrm.GetSqlCreate returns for each class
// (an ordinary table ends in ");", a virtual one has no terminator); SQLite stores them in
// sqlite_master without the ";".
constexpr std::array<OrmTableDef, 10> kModel = {{
    {"User", "CREATE TABLE User(ID INTEGER PRIMARY KEY AUTOINCREMENT, uName TEXT COLLATE SYSTEMNOCASE);",
     kUserFields},
    {"Category",
     "CREATE TABLE Category(ID INTEGER PRIMARY KEY AUTOINCREMENT, cName TEXT COLLATE SYSTEMNOCASE);",
     kCategoryFields},
    {"CategoryView",
     "CREATE TABLE CategoryView(ID INTEGER PRIMARY KEY AUTOINCREMENT, cvUser INTEGER, cvCategory INTEGER, "
     "cvVisible INTEGER, cvMinZoom INTEGER, cvMaxZoom INTEGER);",
     kCategoryViewFields},
    {"MarkImage",
     "CREATE TABLE MarkImage(ID INTEGER PRIMARY KEY AUTOINCREMENT, miName TEXT COLLATE SYSTEMNOCASE);",
     kMarkImageFields},
    {"Mark",
     "CREATE TABLE Mark(ID INTEGER PRIMARY KEY AUTOINCREMENT, mCategory INTEGER, mImage INTEGER, "
     "mAppearance INTEGER, mName TEXT COLLATE SYSTEMNOCASE, mDesc TEXT COLLATE SYSTEMNOCASE, mGeoType "
     "INTEGER, "
     "mGeoCount INTEGER, mGeoLonSize INTEGER, mGeoLatSize INTEGER, mGeoWKB BLOB);",
     kMarkFields},
    {"MarkView",
     "CREATE TABLE MarkView(ID INTEGER PRIMARY KEY AUTOINCREMENT, mvUser INTEGER, mvMark INTEGER, "
     "mvCategory INTEGER, mvVisible INTEGER);",
     kMarkViewFields},
    {"MarkAppearance",
     "CREATE TABLE MarkAppearance(ID INTEGER PRIMARY KEY AUTOINCREMENT, maColor1 INTEGER, maColor2 INTEGER, "
     "maScale1 INTEGER, maScale2 INTEGER);",
     kMarkAppearanceFields},
    {"MarkFTS", "CREATE VIRTUAL TABLE MarkFTS USING fts4(mName,mDesc, tokenize=simple)", kMarkFtsFields},
    {"MarkRTree", "CREATE VIRTUAL TABLE MarkRTree USING rtree(RowID,mLeft,mRight,mBottom,mTop)",
     kMarkRTreeFields},
    {"MarkMeta",
     "CREATE TABLE MarkMeta(ID INTEGER PRIMARY KEY AUTOINCREMENT, mMark INTEGER UNIQUE, mMeta BLOB);",
     kMarkMetaFields},
}};

// _CreateMissingIndexes (:264-323), as CreateSqlMultiIndex words each request.
constexpr std::array<QByteArrayView, 10> kMissingIndexes = {{
    "CREATE UNIQUE INDEX IF NOT EXISTS IndexUser_uName ON User(uName);",
    "CREATE INDEX IF NOT EXISTS IndexCategory_cName ON Category(cName);",
    "CREATE INDEX IF NOT EXISTS IndexCategoryView_cvUser ON CategoryView(cvUser);",
    "CREATE UNIQUE INDEX IF NOT EXISTS IndexCategoryView_cvCategoryUser ON CategoryView(cvCategory,cvUser);",
    "CREATE UNIQUE INDEX IF NOT EXISTS IndexMarkImage_miName ON MarkImage(miName);",
    "CREATE UNIQUE INDEX IF NOT EXISTS IndexMarkAppearance_maC1C2S1S2 ON "
    "MarkAppearance(maColor1,maColor2,maScale1,maScale2);",
    "CREATE INDEX IF NOT EXISTS IndexMark_mCategory ON Mark(mCategory);",
    "CREATE INDEX IF NOT EXISTS IndexMarkView_mvUser ON MarkView(mvUser);",
    "CREATE INDEX IF NOT EXISTS IndexMarkView_mvCategoryUser ON MarkView(mvCategory,mvUser);",
    "CREATE UNIQUE INDEX IF NOT EXISTS IndexMarkView_mvMarkUser ON MarkView(mvMark,mvUser);",
}};

// The membership test of CreateMissingTables: StrIComp, i.e. ASCII case-insensitive.
template <class Names> bool containsName(const Names& names, QByteArrayView name) {
    for (const auto& candidate : names) {
        if (QByteArrayView(candidate).compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

// SqlAddField (orm/mormot.orm.base.pas:11178-11188): the "," of the type text becomes ";".
QByteArray sqlAddField(const OrmTableDef& table, const OrmFieldDef& field) {
    QByteArray sql = "ALTER TABLE " + table.name.toByteArray() + " ADD COLUMN " + field.name.toByteArray() +
                     field.sqlType.toByteArray();
    if (field.unique) {
        sql.insert(sql.size() - 2, " UNIQUE");
    }
    sql[sql.size() - 2] = ';';
    return sql;
}

// TOrm.InitializeTable with [itoNoIndex4TID] (orm/mormot.orm.core.pas:6715-6747): a plain index
// over every unique field, named Index<Table><Field>. It duplicates the index SQLite builds for the
// UNIQUE column, and it is in every Windows-written file.
QByteArray sqlAutomaticIndex(const OrmTableDef& table, const OrmFieldDef& field) {
    return "CREATE INDEX IF NOT EXISTS Index" + table.name.toByteArray() + field.name.toByteArray() + " ON " +
           table.name.toByteArray() + "(" + field.name.toByteArray() + ");";
}

} // namespace

std::span<const OrmTableDef> createModelSqlite3() noexcept {
    return kModel;
}

core::Result<int> createMissingTablesSqlite3(SqliteDb& db) {
    int statements = 0;
    bool transactionActive = false;
    std::bitset<kModel.size()> tableCreated;

    // DB.RollBack + raise of the `except` block (orm/mormot.orm.sqlite3.pas:1594-1599).
    const auto rollBack = [&](const core::Error& error) {
        if (transactionActive) {
            std::ignore = db.exec("ROLLBACK TRANSACTION;");
        }
        return core::fail(error.code(), error.what());
    };
    // DB.Execute preceded by "if not DB.TransactionActive then DB.TransactionBegin".
    const auto execInTransaction = [&](QByteArrayView sql) -> core::Result<void> {
        if (!transactionActive) {
            if (const auto begun = db.exec("BEGIN TRANSACTION;"); !begun) {
                return rollBack(begun.error());
            }
            transactionActive = true;
            ++statements;
        }
        if (const auto done = db.exec(sql); !done) {
            return rollBack(done.error());
        }
        ++statements;
        return {};
    };

    const auto tablesAtCreation = db.tableNames();
    if (!tablesAtCreation) {
        return core::fail(tablesAtCreation.error().code(), tablesAtCreation.error().what());
    }
    QByteArrayList existingTables;
    for (const QString& name : *tablesAtCreation) {
        existingTables.append(name.toUtf8());
    }

    for (std::size_t t = 0; t < kModel.size(); ++t) {
        const OrmTableDef& table = kModel[t];
        if (!containsName(existingTables, table.name)) { // B-9
            if (const auto created = execInTransaction(table.sqlCreate); !created) {
                return std::unexpected(created.error());
            }
            tableCreated.set(t);
            continue;
        }
        // B-10: the table exists -- add the model columns it lacks. A column added this way never
        // gets an automatic index: the only unique field is MarkMeta.mMark, and SQLite refuses to
        // add a UNIQUE column, which ends the whole call.
        const auto fieldsAtCreation = db.tableColumns(table.name);
        if (!fieldsAtCreation) {
            return rollBack(fieldsAtCreation.error());
        }
        for (const OrmFieldDef& field : table.fields) {
            if (containsName(*fieldsAtCreation, field.name)) {
                continue;
            }
            if (const auto added = execInTransaction(sqlAddField(table, field)); !added) {
                return std::unexpected(added.error());
            }
        }
    }
    if (!transactionActive) {
        return 0;
    }
    // B-11: "initialize new tables AFTER creation of ALL tables". A failing index statement is
    // ignored (CreateSqlMultiIndex returns false and InitializeTable does not look). user_version
    // is not written: legacy passes 0.
    for (std::size_t t = 0; t < kModel.size(); ++t) {
        if (!tableCreated.test(t)) {
            continue;
        }
        for (const OrmFieldDef& field : kModel[t].fields) {
            if (field.unique) {
                std::ignore = db.exec(sqlAutomaticIndex(kModel[t], field));
                ++statements;
            }
        }
    }
    if (const auto committed = db.exec("COMMIT TRANSACTION;"); !committed) {
        return rollBack(committed.error());
    }
    ++statements;
    return statements;
}

void createMissingIndexesSqlite3(SqliteDb& db) {
    for (const QByteArrayView sql : kMissingIndexes) {
        std::ignore = db.exec(sql);
    }
}

} // namespace cg::marks
