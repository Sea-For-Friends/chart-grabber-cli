// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Proof of ticket T-0067: find by id / by name (case-insensitive), prepared semantics.
// Ports u_MarkCategoryDbImplORMCache.pas; docs/specs/marks.md §2.9.6.
#include "cg/marks/mormot/categories/mark_category_db_impl_orm_cache.h"

#include <QString>

#include <gtest/gtest.h>

#include <vector>

namespace cg::marks {
namespace {

OrmCategoryRec rec(qint64 id, const QString& name, qint64 viewId = 0, bool visible = true, quint8 minZoom = 3,
                   quint8 maxZoom = 23) {
    OrmCategoryRec r;
    r.categoryId = id;
    r.name = name;
    r.viewId = viewId;
    r.visible = visible;
    r.minZoom = minZoom;
    r.maxZoom = maxZoom;
    return r;
}

TEST(OrmCategoryCache, FindByIdOverTheLinearAndTheBinaryBranch) {
    OrmCategoryCache cache(kUnlimCacheSize);
    // ids inserted out of order, 13 rows (> 10: binary search), one negative
    for (qint64 id : {9, 2, 11, 5, 13, 1, 7, 12, 3, 10, 6, 4, -8}) {
        cache.addOrUpdate(rec(id, QStringLiteral("c%1").arg(id)));
    }
    ASSERT_EQ(cache.count(), 13);
    for (qint64 id : {-8, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13}) {
        const OrmCategoryRow* row = cache.find(id);
        ASSERT_NE(row, nullptr) << id;
        EXPECT_EQ(row->id, id);
        EXPECT_EQ(row->name, QStringLiteral("c%1").arg(id));
    }
    EXPECT_EQ(cache.find(qint64{8}), nullptr);
    EXPECT_EQ(cache.find(qint64{0}), nullptr);
    // 3 rows: the linear branch (count <= 10)
    OrmCategoryCache small(kUnlimCacheSize);
    for (qint64 id : {30, 10, 20}) {
        small.addOrUpdate(rec(id, QStringLiteral("s%1").arg(id)));
    }
    ASSERT_NE(small.find(qint64{20}), nullptr);
    EXPECT_EQ(small.find(qint64{20})->name, QStringLiteral("s20"));
    EXPECT_EQ(small.find(qint64{25}), nullptr);
    EXPECT_EQ(cache.rows().front().id, -8);
    EXPECT_EQ(cache.rows().back().id, 13);
}

TEST(OrmCategoryCache, FindByNameIgnoresCaseAndTakesTheLowestId) {
    OrmCategoryCache cache(kUnlimCacheSize);
    cache.addOrUpdate(rec(7, QStringLiteral("Roads")));
    cache.addOrUpdate(rec(3, QStringLiteral("ROADS")));
    cache.addOrUpdate(rec(5, QString::fromUtf8("Город")));
    // AnsiSameText on the cache: "город" finds "Город" (SQL SYSTEMNOCASE would not, LC-12/LC-14)
    const OrmCategoryRow* city = cache.find(QString::fromUtf8("город"));
    ASSERT_NE(city, nullptr);
    EXPECT_EQ(city->id, 5);
    // two names equal up to case: the first in id order wins
    const OrmCategoryRow* roads = cache.find(QStringLiteral("rOaDs"));
    ASSERT_NE(roads, nullptr);
    EXPECT_EQ(roads->id, 3);
    EXPECT_EQ(cache.find(QStringLiteral("Road")), nullptr);
    EXPECT_EQ(cache.find(QStringLiteral("")), nullptr);
    // D row: legacy CompareStringW(NORM_IGNORECASE) equates Straße and STRASSE, simple case folding does not
    cache.addOrUpdate(rec(9, QString::fromUtf8("Straße")));
    EXPECT_EQ(cache.find(QStringLiteral("STRASSE")), nullptr);
}

// Size = InstanceSize 96 + ItemSize 16 * Count + DataSize (UTF-16 units * 2); literals computed from the
// Pascal (:131-162 for add/update, :164-183 AddPrepared adds no DataSize).
TEST(OrmCategoryCache, SizeFollowsTheLegacyFormulaAndAddPreparedSkipsDataSize) {
    OrmCategoryCache cache(kUnlimCacheSize);
    EXPECT_EQ(cache.size(), 96);
    cache.addOrUpdate(rec(1, QStringLiteral("abc")));
    EXPECT_EQ(cache.size(), 118);
    cache.addOrUpdate(rec(1, QStringLiteral("abcde")));
    EXPECT_EQ(cache.size(), 122);
    cache.addOrUpdate(rec(1, QStringLiteral("a")));
    EXPECT_EQ(cache.size(), 114);
    cache.addPrepared({{1, QStringLiteral("a")}, {2, QStringLiteral("bb")}, {3, QStringLiteral("ccc")}});
    EXPECT_EQ(cache.size(), 144); // 96 + 16 * 3, names not counted (L row)
    cache.addOrUpdate(rec(2, QStringLiteral("x")));
    EXPECT_EQ(cache.size(), 142); // 144 + (2 - 4)
}

TEST(OrmCategoryViewCache, SizeIs96Plus19PerRow) {
    OrmCategoryViewCache cache(kUnlimCacheSize);
    EXPECT_EQ(cache.size(), 96);
    cache.addOrUpdate(rec(1, QStringLiteral("a")));
    cache.addOrUpdate(rec(2, QStringLiteral("b")));
    EXPECT_EQ(cache.size(), 134);
}

TEST(OrmCategoryCache, AddOrUpdateReplacesTheNameOfAnExistingId) {
    OrmCategoryCache cache(kUnlimCacheSize);
    cache.addOrUpdate(rec(1, QStringLiteral("old")));
    cache.addOrUpdate(rec(1, QStringLiteral("new")));
    EXPECT_EQ(cache.count(), 1);
    EXPECT_EQ(cache.find(qint64{1})->name, QStringLiteral("new"));
    EXPECT_EQ(cache.find(QStringLiteral("old")), nullptr);
    cache.remove(1);
    EXPECT_EQ(cache.count(), 0);
    EXPECT_EQ(cache.find(qint64{1}), nullptr);
}

TEST(OrmCategoryCache, PreparedMeansWholeTableAndAddPreparedReplacesTheContent) {
    OrmCategoryCache cache(kUnlimCacheSize);
    EXPECT_FALSE(cache.isPrepared());
    cache.addOrUpdate(rec(99, QStringLiteral("stale")));
    EXPECT_FALSE(cache.isPrepared()); // a single add does not make it prepared

    cache.addPrepared({{4, QStringLiteral("d")}, {2, QStringLiteral("b")}, {3, QStringLiteral("c")}});
    EXPECT_TRUE(cache.isPrepared());
    ASSERT_EQ(cache.count(), 3);
    EXPECT_EQ(cache.find(qint64{99}), nullptr); // replaced
    EXPECT_EQ(cache.rows()[0].id, 2);
    EXPECT_EQ(cache.rows()[2].id, 4);

    cache.addPrepared({{5, QStringLiteral("e")}}); // one row
    EXPECT_EQ(cache.count(), 1);
    EXPECT_EQ(cache.find(qint64{5})->name, QStringLiteral("e"));

    cache.addPrepared({}); // an empty table is prepared too
    EXPECT_TRUE(cache.isPrepared());
    EXPECT_EQ(cache.count(), 0);

    cache.reset();
    EXPECT_FALSE(cache.isPrepared());
}

TEST(OrmCategoryViewCache, AddOrUpdateFindAndPrepared) {
    OrmCategoryViewCache cache(kUnlimCacheSize);
    EXPECT_EQ(cache.find(5), nullptr);
    cache.addOrUpdate(rec(5, QStringLiteral("x"), 50, false, 4, 18));
    cache.addOrUpdate(rec(2, QStringLiteral("y"), 0, true, 3, 23));
    ASSERT_EQ(cache.count(), 2);
    EXPECT_EQ(cache.rows()[0].id, 2);

    const OrmCategoryViewRow* row = cache.find(5);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->viewId, 50);
    EXPECT_FALSE(row->visible);
    EXPECT_EQ(row->minZoom, 4);
    EXPECT_EQ(row->maxZoom, 18);

    cache.addOrUpdate(rec(5, QStringLiteral("x"), 51, true, 6, 9)); // update in place
    EXPECT_EQ(cache.count(), 2);
    row = cache.find(5);
    EXPECT_EQ(row->viewId, 51);
    EXPECT_TRUE(row->visible);
    EXPECT_EQ(row->minZoom, 6);
    EXPECT_EQ(row->maxZoom, 9);

    EXPECT_FALSE(cache.isPrepared());
    cache.addPrepared({{8, 80, true, 3, 23}, {7, 70, false, 1, 2}});
    EXPECT_TRUE(cache.isPrepared());
    EXPECT_EQ(cache.count(), 2);
    EXPECT_EQ(cache.find(5), nullptr);
    EXPECT_EQ(cache.rows()[0].id, 7);
    EXPECT_EQ(cache.find(8)->viewId, 80);
}

TEST(OrmCategoryDbCache, BothCachesAreUnlimitedAndIndependent) {
    OrmCategoryDbCache db;
    for (qint64 id = 1; id <= 2000; ++id) {
        db.categoryCache.addOrUpdate(rec(id, QString(500, QLatin1Char('x'))));
    }
    EXPECT_EQ(db.categoryCache.count(), 2000); // never auto-reset
    EXPECT_EQ(db.categoryViewCache.count(), 0);
    EXPECT_FALSE(db.categoryViewCache.isPrepared());
}

} // namespace
} // namespace cg::marks
