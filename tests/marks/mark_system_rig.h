// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared rig of the SQLite system tests (mark_system_impl_orm_test.cpp, T-0133; mark_system_test.cpp,
// T-0137): the collaborators a system is built from, the impl config of the SQLite backend, plain SQL on a
// closed database file, and the sorted name lists the tests compare. Hoisted from
// mark_system_impl_orm_test.cpp unchanged.

#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_impl_factory_changeable.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/mormot/system_nocase.h"
#include "cg/storage/sqlite/sqlite_db.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/vector_item_subset_builder.h"

#include <QByteArray>
#include <QFile>
#include <QStringList>

#include <gtest/gtest.h>

#include <memory>
#include <tuple>
#include <vector>

#include "mark_db_impl_orm_helper_test_util.h"
#include "mark_test_util.h"

namespace cg::marks::testutil {

// The collaborators every system of these tests is built from.
struct Parts {
    std::shared_ptr<FakePictureList> pictures = std::make_shared<FakePictureList>();
    vector::AppearanceOfMarkFactoryPtr appearances = std::make_shared<vector::AppearanceOfMarkFactory>();
    vector::VectorItemSubsetBuilderFactoryPtr subsets =
        std::make_shared<vector::VectorItemSubsetBuilderFactory>();
    std::shared_ptr<IMarkFactory> markFactory =
        std::make_shared<MarkFactory>(nullptr, nullptr, appearances, testutil::converter());
    vector::HtmlToHintTextConverterPtr converter = testutil::converter();
};

inline std::shared_ptr<MarkSystemImplFactoryListStatic> makeList(const Parts& p) {
    return std::make_shared<MarkSystemImplFactoryListStatic>(p.pictures, p.appearances, kFactory, p.subsets,
                                                             p.markFactory, p.converter);
}

inline MarkSystemImplConfigStaticPtr ormConfig(bool readOnly, const QString& fileName = {}) {
    return std::make_shared<MarkSystemImplConfigORM>(fileName, readOnly, QString(), QString(), QString());
}

// Opens a closed database file for plain SQL; the collation the schema needs is registered.
inline QStringList sqlRows(const QString& path, const char* sql) {
    QStringList out;
    auto opened = storage::sqlite::SqliteDb::open(path, storage::sqlite::SqliteDb::OpenMode::ReadWrite);
    if (!opened) {
        ADD_FAILURE() << opened.error().what().toStdString();
        return out;
    }
    std::ignore = opened->createCollation("SYSTEMNOCASE", &systemNoCaseCompare);
    auto stmt = opened->prepare(sql);
    if (!stmt) {
        ADD_FAILURE() << stmt.error().what().toStdString();
        return out;
    }
    for (auto step = stmt->step(); step && *step == storage::sqlite::SqliteStmt::StepResult::Row;
         step = stmt->step()) {
        QStringList columns;
        for (int i = 0; i < stmt->columnCount(); ++i) {
            columns.append(stmt->columnText(i));
        }
        out.append(columns.join(QLatin1Char('|')));
    }
    return out;
}

inline QByteArray fileBytes(const QString& path) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

inline QStringList sorted(QStringList list) {
    list.sort();
    return list;
}

// The names of a subset, sorted (the backend answers in id order; the tests compare sets).
inline QStringList namesOf(const vector::VectorItemSubsetPtr& subset) {
    QStringList names;
    for (int i = 0; subset && i < subset->count(); ++i) {
        names.append(subset->item(i)->name());
    }
    names.sort();
    return names;
}

} // namespace cg::marks::testutil
