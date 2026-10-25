// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Categories/u_MarkCategoryDbImplORMHelper.pas (docs/specs/marks.md §2.9.6,
// statements S-10 ... S-1C of §2.9.5). Line numbers in the sources are those of that unit.
//
// Every method except countCategorySql is called with the write lock of the owning category database held
// (§2.9.12); countCategorySql runs under the read lock (u_MarkCategoryDbImplORM.pas:425) and touches no
// cache. The helper and its caches have no lock of their own. The helper never starts or commits a
// transaction (legacy does not either: the caller, u_MarkCategoryDbImplORM, owns them); each statement runs
// under the connection's dbMutex.

#include "cg/marks/mormot/categories/mark_category_db_impl_orm_cache.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QString>
#include <QtGlobal>

#include <vector>

namespace cg::marks {

// TMarkCategoryDbImplORMHelper (:43-88)
class MarkCategoryDbImplORMHelper {
public:
    MarkCategoryDbImplORMHelper(bool isReadOnly, IMarkSystemImplORMClientProvider& provider); // :98-110

    bool deleteCategorySql(qint64 categoryId);                                    // :118-141
    bool insertCategorySql(OrmCategoryRec& rec);                                  // :143-189
    bool updateCategorySql(const OrmCategoryRec& oldRec, OrmCategoryRec& newRec); // :191-345
    bool readCategorySql(OrmCategoryRec& rec, qint64 id, const QString& name);    // :347-432
    int countCategorySql(const QString& name);                                    // :434-457
    void setAllCategoriesVisibleSql(bool visible);                                // :523-591
    int getCategoriesRecArray(std::vector<OrmCategoryRec>& out);                  // :593-632

    [[nodiscard]] bool isReadOnly() const { return isReadOnly_; }
    void setReadOnly(bool value); // :634-642

private:
    int fillPrepareCategoryCache();     // :459-486  S-13 once, then the cache
    int fillPrepareCategoryViewCache(); // :488-521  S-18 once, then the cache

    qint64 userId_;
    IMarkSystemImplORMClientProvider& provider_;
    OrmCategoryDbCache cache_;
    bool isReadOnly_;
};

} // namespace cg::marks
