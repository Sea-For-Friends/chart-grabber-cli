// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkDbImplORMCache.pas :1-647 + the view
// cache :916-1047 (T-0076) and the second half :648-914, :1049-1155, :284-316 (T-0085); docs/specs/marks.md
// §2.9.7. T-0085 adds: by-category index, mark cache (instance 152, item 29), geometry cache (inner array
// 112 + 20 per row + bytes), the 30 % / 70 % split and the L-158 fix. Proof of ticket T-0076: per class add /
// find / remove / reset, addOrIgnore keeps the first, size arithmetic with literal numbers (Win64 legacy
// sizes: instance 96, view 152; item 16 image, 24 appearance, 25 view, 32 id index; strings 2 bytes per
// UTF-16 unit).
#include "cg/marks/mormot/marks/mark_db_impl_orm_cache.h"

#include <QString>

#include <gtest/gtest.h>

#include <vector>

#include "mark_test_util.h"

namespace cg::marks {
namespace {

template <class Cache> std::vector<qint64> idsOf(Cache& c) {
    std::vector<qint64> r;
    for (int i = 0; i < c.count(); ++i) {
        r.push_back(c.rows()[static_cast<size_t>(i)].id);
    }
    return r;
}

// ------------------------------------------------------------------ OrmMarkImageCache

TEST(OrmMarkImageCacheTest, AddFindRemoveResetAndSize) {
    OrmMarkImageCache c(kUnlimCacheSize);
    EXPECT_EQ(c.size(), 96);
    c.addOrIgnore(5, QStringLiteral("ab"));
    c.addOrIgnore(2, QStringLiteral("c"));
    c.addOrIgnore(9, QStringLiteral("defg"));
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2, 5, 9})); // given unsorted
    EXPECT_EQ(c.size(), 158);                            // 96 + 3 * 16 + (2 + 1 + 4) * 2

    ASSERT_NE(c.find(2), nullptr);
    EXPECT_EQ(c.find(2)->name, QStringLiteral("c"));
    EXPECT_EQ(c.find(7), nullptr);

    c.remove(5);
    c.remove(777); // absent: nothing
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2, 9}));
    EXPECT_EQ(c.size(), 138); // 96 + 2 * 16 + (1 + 4) * 2: the removed name leaves the data size

    c.reset();
    EXPECT_EQ(c.count(), 0);
    EXPECT_EQ(c.size(), 96);
    EXPECT_EQ(c.find(2), nullptr);
}

TEST(OrmMarkImageCacheTest, AddOrIgnoreKeepsTheFirstNameAndSize) {
    OrmMarkImageCache c(kUnlimCacheSize);
    c.addOrIgnore(5, QStringLiteral("ab"));
    c.addOrIgnore(5, QStringLiteral("a much longer name"));
    EXPECT_EQ(c.count(), 1);
    EXPECT_EQ(c.find(5)->name, QStringLiteral("ab"));
    EXPECT_EQ(c.size(), 96 + 16 + 4); // 116
    OrmMarkRec rec;
    rec.picId = 6;
    rec.picName = QStringLiteral("xyz");
    c.addOrIgnore(rec);
    EXPECT_EQ(c.find(6)->name, QStringLiteral("xyz"));
    EXPECT_EQ(c.size(), 116 + 16 + 6); // 138
}

TEST(OrmMarkImageCacheTest, FindByNameIsLinearAndIgnoresCase) {
    OrmMarkImageCache c(kUnlimCacheSize);
    c.addOrIgnore(9, QStringLiteral("Pin"));
    c.addOrIgnore(4, QStringLiteral("pIN")); // same name as 9 up to case, lower id
    c.addOrIgnore(1, QStringLiteral("Flag"));
    ASSERT_NE(c.find(QStringLiteral("PIN")), nullptr);
    EXPECT_EQ(c.find(QStringLiteral("PIN"))->id, 4); // first in id order
    EXPECT_EQ(c.find(QStringLiteral("flag"))->id, 1);
    EXPECT_EQ(c.find(QStringLiteral("Pins")), nullptr);
    c.addOrIgnore(2, QString::fromUtf8("\xD0\x93\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4"));           // Город
    ASSERT_NE(c.find(QString::fromUtf8("\xD0\xB3\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4")), nullptr); // город
    EXPECT_EQ(c.find(QString::fromUtf8("\xD0\xB3\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB4"))->id, 2);
}

TEST(OrmMarkImageCacheTest, LimitedCacheEmptiesItselfAtTheNextAdd) {
    OrmMarkImageCache c(120);
    c.addOrIgnore(1, QStringLiteral("ab")); // 96 + 16 + 4 = 116, not over 120
    c.addOrIgnore(2, QStringLiteral("cd")); // checked at 116 -> kept; now 136
    EXPECT_EQ(c.count(), 2);
    EXPECT_EQ(c.size(), 136);
    c.addOrIgnore(3, QStringLiteral("x")); // 136 > 120 -> emptied first, then 96 + 16 + 2
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{3}));
    EXPECT_EQ(c.size(), 114);
}

// ------------------------------------------------------------------ OrmMarkAppearanceCache

TEST(OrmMarkAppearanceCacheTest, AddFindRemoveResetAndSize) {
    OrmMarkAppearanceCache c(kUnlimCacheSize);
    c.addOrIgnore(7, 0xFF112233U, 0x80000001U, 10, 20);
    c.addOrIgnore(3, 0x00000001U, 0x00000002U, -5, 6);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{3, 7}));
    EXPECT_EQ(c.size(), 144);

    ASSERT_NE(c.find(7), nullptr);
    EXPECT_EQ(c.find(7)->color1, 0xFF112233U);
    EXPECT_EQ(c.find(7)->color2, 0x80000001U);
    EXPECT_EQ(c.find(7)->scale1, 10);
    EXPECT_EQ(c.find(7)->scale2, 20);
    EXPECT_EQ(c.find(4), nullptr);

    c.remove(3);
    c.remove(3);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{7}));
    EXPECT_EQ(c.size(), 120);

    c.reset();
    EXPECT_EQ(c.count(), 0);
    EXPECT_EQ(c.size(), 96);
}

TEST(OrmMarkAppearanceCacheTest, AddOrIgnoreKeepsTheFirst) {
    OrmMarkAppearanceCache c(kUnlimCacheSize);
    c.addOrIgnore(7, 1, 2, 3, 4);
    c.addOrIgnore(7, 9, 9, 9, 9);
    EXPECT_EQ(c.count(), 1);
    EXPECT_EQ(c.find(7)->color1, 1U);
    EXPECT_EQ(c.find(7)->scale2, 4);
    OrmMarkRec rec;
    rec.appearanceId = 8;
    rec.color1 = 11;
    rec.color2 = 12;
    rec.scale1 = 13;
    rec.scale2 = -14;
    c.addOrIgnore(rec);
    EXPECT_EQ(c.find(8)->scale2, -14);
    EXPECT_EQ(c.size(), 96 + 2 * 24);
}

TEST(OrmMarkAppearanceCacheTest, FindByTheFourValuesNeedsAllFourEqual) {
    OrmMarkAppearanceCache c(kUnlimCacheSize);
    c.addOrIgnore(9, 1, 2, 3, 4);
    c.addOrIgnore(5, 1, 2, 3, 4); // same values, lower id: found first
    c.addOrIgnore(6, 1, 2, 3, 5);
    ASSERT_NE(c.find(1, 2, 3, 4), nullptr);
    EXPECT_EQ(c.find(1, 2, 3, 4)->id, 5);
    EXPECT_EQ(c.find(1, 2, 3, 5)->id, 6);
    EXPECT_EQ(c.find(0, 2, 3, 4), nullptr);
    EXPECT_EQ(c.find(1, 0, 3, 4), nullptr);
    EXPECT_EQ(c.find(1, 2, 0, 4), nullptr);
    EXPECT_EQ(c.find(1, 2, 3, 0), nullptr);
}

// ------------------------------------------------------------------ OrmMarkViewCache

TEST(OrmMarkViewCacheTest, AddUpdateFindRemoveResetAndSize) {
    OrmMarkViewCache c(kUnlimCacheSize);
    EXPECT_EQ(c.size(), 152);
    c.addOrUpdate(30, 300, 3, true);
    c.addOrUpdate(10, 100, 1, false);
    c.addOrUpdate(20, 200, 2, true);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{10, 20, 30}));
    EXPECT_EQ(c.size(), 227);

    c.addOrUpdate(20, 222, 5, false); // update: same place, same count
    EXPECT_EQ(c.count(), 3);
    EXPECT_EQ(c.find(20)->viewId, 222);
    EXPECT_EQ(c.find(20)->categoryId, 5);
    EXPECT_FALSE(c.find(20)->visible);
    EXPECT_EQ(c.find(40), nullptr);

    OrmMarkRec rec;
    rec.markId = 5;
    rec.viewId = 55;
    rec.categoryId = 4;
    rec.visible = true;
    c.addOrUpdate(rec);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{5, 10, 20, 30}));
    EXPECT_EQ(c.find(5)->viewId, 55);
    EXPECT_TRUE(c.find(5)->visible);

    c.remove(10);
    c.remove(10);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{5, 20, 30}));
    EXPECT_EQ(c.size(), 227);

    c.reset();
    EXPECT_EQ(c.count(), 0);
    EXPECT_EQ(c.size(), 152);
}

TEST(OrmMarkViewCacheTest, PreparedCategoryAddsOnlyTheMissingRows) {
    OrmMarkViewCache c(kUnlimCacheSize);
    c.addOrUpdate(20, 200, 3, true);
    c.addPrepared(3, {{30, 301, 3, true}, {10, 101, 3, false}, {20, 999, 9, false}});
    EXPECT_TRUE(c.isCategoryPrepared(3));
    EXPECT_FALSE(c.isCategoryPrepared(4));
    EXPECT_FALSE(c.isPrepared());
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{10, 20, 30}));
    EXPECT_EQ(c.find(20)->viewId, 200); // the cached row is kept
    EXPECT_EQ(c.find(30)->viewId, 301);
    EXPECT_EQ(c.size(), 152 + 3 * 25);

    // all rows new: whole array added; none new: nothing changes
    c.addPrepared(1, {{60, 1, 1, true}, {40, 1, 1, true}});
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{10, 20, 30, 40, 60}));
    c.addPrepared(1, {{60, 7, 7, false}});
    EXPECT_EQ(c.count(), 5);
    EXPECT_EQ(c.find(60)->viewId, 1);

    // an empty category is prepared too
    c.addPrepared(8, {});
    EXPECT_TRUE(c.isCategoryPrepared(8));
    EXPECT_EQ(c.count(), 5);
}

TEST(OrmMarkViewCacheTest, CategoryZeroPreparesTheWholeTableAndReplacesTheContent) {
    OrmMarkViewCache c(kUnlimCacheSize);
    c.addPrepared(3, {{1, 1, 3, true}});
    ASSERT_TRUE(c.isCategoryPrepared(3));
    c.addPrepared(0, {{9, 1, 1, true}, {4, 2, 2, false}, {6, 3, 3, true}});
    EXPECT_TRUE(c.isPrepared());
    EXPECT_FALSE(c.isCategoryPrepared(3)); // Reset cleared the list
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{4, 6, 9}));
    EXPECT_EQ(c.size(), 152 + 3 * 25);

    c.addPrepared(0, {{2, 1, 1, true}}); // one row
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2}));
    c.addPrepared(-1, {{8, 1, 1, true}, {5, 1, 1, true}}); // not > 0: the whole-table branch too
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{5, 8}));
    c.addPrepared(0, {});
    EXPECT_EQ(c.count(), 0);
    EXPECT_TRUE(c.isPrepared());

    c.reset();
    EXPECT_FALSE(c.isPrepared());
}

// Reproduced legacy behaviour (L-157): the rows of one source are not de-duplicated among themselves.
TEST(OrmMarkViewCacheTest, SourceIdsAreNotDeduplicatedAmongThemselves) {
    OrmMarkViewCache c(kUnlimCacheSize);
    c.addPrepared(2, {{5, 1, 2, true}, {5, 2, 2, false}});
    EXPECT_EQ(c.count(), 2);
    EXPECT_EQ(c.size(), 202); // 152 + 2 * 25
    EXPECT_EQ(c.rows()[0].viewId, 1);
    EXPECT_EQ(c.rows()[1].viewId, 2);
}

// ------------------------------------------------------------------ OrmMarkIdIndex

TEST(OrmMarkIdIndexTest, AddUpdateFindRemoveResetAndSize) {
    OrmMarkIdIndex c(kUnlimCacheSize);
    OrmMarkRec a;
    a.markId = 8;
    a.categoryId = 1;
    a.picId = 2;
    a.appearanceId = 3;
    OrmMarkRec b = a;
    b.markId = 4;
    c.addOrUpdate(a);
    c.addOrUpdate(b);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{4, 8}));
    EXPECT_EQ(c.size(), 160);

    a.categoryId = 11;
    a.picId = 12;
    a.appearanceId = 13;
    c.addOrUpdate(a); // update in place
    EXPECT_EQ(c.count(), 2);
    ASSERT_NE(c.find(8), nullptr);
    EXPECT_EQ(c.find(8)->categoryId, 11);
    EXPECT_EQ(c.find(8)->imageId, 12);
    EXPECT_EQ(c.find(8)->appearanceId, 13);
    EXPECT_EQ(c.find(4)->categoryId, 1);
    EXPECT_EQ(c.find(5), nullptr);

    c.remove(4);
    c.remove(4);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{8}));
    EXPECT_EQ(c.size(), 128);

    c.reset();
    EXPECT_EQ(c.count(), 0);
    EXPECT_EQ(c.size(), 96);
}

TEST(OrmMarkIdIndexTest, AddPreparedReplacesTheContentAndSetsTheFlag) {
    OrmMarkIdIndex c(kUnlimCacheSize);
    OrmMarkRec rec;
    rec.markId = 99;
    c.addOrUpdate(rec);
    EXPECT_FALSE(c.isPrepared());
    c.addPrepared({{7, 1, 1, 1}, {3, 2, 2, 2}, {5, 3, 3, 3}});
    EXPECT_TRUE(c.isPrepared());
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{3, 5, 7}));
    EXPECT_EQ(c.find(5)->categoryId, 3);
    c.addPrepared({{2, 0, 0, 0}});
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2}));
    c.addPrepared({});
    EXPECT_EQ(c.count(), 0);
    EXPECT_TRUE(c.isPrepared());
    c.reset();
    EXPECT_FALSE(c.isPrepared());
}

TEST(OrmMarkIdIndexTest, AddArrayTakesOnlyIdsNotCachedYet) {
    OrmMarkIdIndex c(kUnlimCacheSize);
    c.addArray({{9, 1, 1, 1}, {2, 1, 1, 1}, {5, 1, 1, 1}}); // empty target
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2, 5, 9}));
    EXPECT_FALSE(c.isPrepared());
    c.addArray({{5, 7, 7, 7}, {1, 2, 2, 2}, {9, 7, 7, 7}, {6, 3, 3, 3}}); // two new
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{1, 2, 5, 6, 9}));
    EXPECT_EQ(c.find(5)->categoryId, 1); // the cached row is kept
    EXPECT_EQ(c.find(6)->categoryId, 3);
    c.addArray({{4, 4, 4, 4}, {3, 4, 4, 4}}); // all new
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{1, 2, 3, 4, 5, 6, 9}));
    c.addArray({{4, 0, 0, 0}, {1, 0, 0, 0}}); // none new
    EXPECT_EQ(c.count(), 7);
    EXPECT_EQ(c.size(), 96 + 7 * 32);
}

// Reproduced legacy behaviour (L-157): the rows of one source are not de-duplicated among themselves.
TEST(OrmMarkIdIndexTest, AddArraySourceIdsAreNotDeduplicatedAmongThemselves) {
    OrmMarkIdIndex c(kUnlimCacheSize);
    OrmMarkRec rec;
    rec.markId = 9;
    c.addOrUpdate(rec);
    c.addArray({{5, 1, 1, 1}, {5, 2, 2, 2}});
    EXPECT_EQ(c.count(), 3);
    EXPECT_EQ(c.size(), 192); // 96 + 3 * 32
    EXPECT_EQ(c.rows()[0].categoryId, 1);
    EXPECT_EQ(c.rows()[1].categoryId, 2);
}

TEST(OrmMarkIdIndexTest, AddArrayWindow) {
    const std::vector<OrmMarkIdIndexRec> src = {{50, 0, 0, 0}, {40, 0, 0, 0}, {30, 0, 0, 0}, {20, 0, 0, 0}};
    OrmMarkIdIndex c(kUnlimCacheSize);
    c.addArray(src, 1, 2); // 40, 30
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{30, 40}));
    c.addArray(src, 2); // count -1: to the end; 30 cached, 20 new
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{20, 30, 40}));
    c.addArray(src, 0, 2); // 50 new, 40 cached
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{20, 30, 40, 50}));
}

// Regression for D-66: legacy reads the source out of range for these windows.
TEST(OrmMarkIdIndexTest, AddArrayWindowPastTheArrayDoesNotReadOutOfRange) {
    const std::vector<OrmMarkIdIndexRec> src = {{50, 0, 0, 0}, {40, 0, 0, 0}, {30, 0, 0, 0}};
    OrmMarkIdIndex c(kUnlimCacheSize);
    OrmMarkRec rec;
    rec.markId = 60; // non-empty target: the loop branch that indexes the source
    c.addOrUpdate(rec);
    c.addArray(src, 1, 10); // runs over the end: limited to 40, 30
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{30, 40, 60}));
    c.addArray(src, 3);     // start at the end: nothing
    c.addArray(src, 7, 2);  // start past the end: nothing
    c.addArray(src, -1, 2); // negative start: nothing
    c.addArray(src, -2);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{30, 40, 60}));
    c.addArray(src, 0, 99);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{30, 40, 50, 60}));
    OrmMarkIdIndex e(kUnlimCacheSize); // empty target
    e.addArray(src, 2, 50);
    EXPECT_EQ(idsOf(e), (std::vector<qint64>{30}));
}

TEST(OrmMarkIdIndexTest, MarkRecToIndexRecCopiesTheFourIds) {
    OrmMarkRec rec;
    rec.markId = 1;
    rec.categoryId = 2;
    rec.picId = 3;
    rec.appearanceId = 4;
    const OrmMarkIdIndexRec r = OrmMarkIdIndex::markRecToIndexRec(rec);
    EXPECT_EQ(r.id, 1);
    EXPECT_EQ(r.categoryId, 2);
    EXPECT_EQ(r.imageId, 3);
    EXPECT_EQ(r.appearanceId, 4);
}

// ------------------------------------------------------------------ OrmMarkIdByCategoryIndex (T-0085)

const std::vector<qint64>* noIds = nullptr;

TEST(OrmMarkIdByCategoryIndexTest, AddPreparedFindAddRemoveReset) {
    OrmMarkIdByCategoryIndex x(kUnlimCacheSize);
    const std::vector<qint64>* ids = noIds;
    int n = -5;
    EXPECT_FALSE(x.find(7, ids, n));
    x.add(7, 11); // category not prepared: nothing
    EXPECT_FALSE(x.find(7, ids, n));

    x.addPrepared(7, {30, 10, 20, 99}, 0, 3); // unsorted; the window takes 30, 10, 20
    ASSERT_TRUE(x.find(7, ids, n));
    EXPECT_EQ(n, 3);
    EXPECT_EQ((std::vector<qint64>(ids->begin(), ids->begin() + n)), (std::vector<qint64>{10, 20, 30}));
    EXPECT_FALSE(x.find(8, ids, n));

    x.add(7, 25);
    x.add(7, 20); // already there
    x.add(8, 1);  // other category, not prepared: nothing
    ASSERT_TRUE(x.find(7, ids, n));
    EXPECT_EQ((std::vector<qint64>(ids->begin(), ids->begin() + n)), (std::vector<qint64>{10, 20, 25, 30}));
    EXPECT_FALSE(x.find(8, ids, n));

    x.remove(7, 20);
    x.remove(7, 777); // absent
    x.remove(8, 10);  // other category
    ASSERT_TRUE(x.find(7, ids, n));
    EXPECT_EQ((std::vector<qint64>(ids->begin(), ids->begin() + n)), (std::vector<qint64>{10, 25, 30}));

    x.reset();
    EXPECT_FALSE(x.find(7, ids, n));
}

TEST(OrmMarkIdByCategoryIndexTest, AddPreparedAgainReplacesTheArray) {
    OrmMarkIdByCategoryIndex x(kUnlimCacheSize);
    x.addPrepared(7, {5, 4, 3, 2}, 1, 2); // window: 4, 3
    x.addPrepared(2, {9, 8}, 0, -1);
    const std::vector<qint64>* ids = noIds;
    int n = 0;
    ASSERT_TRUE(x.find(7, ids, n));
    EXPECT_EQ((std::vector<qint64>(ids->begin(), ids->begin() + n)), (std::vector<qint64>{3, 4}));
    x.addPrepared(7, {60, 50}, 0, 2); // old content dropped
    ASSERT_TRUE(x.find(7, ids, n));
    EXPECT_EQ((std::vector<qint64>(ids->begin(), ids->begin() + n)), (std::vector<qint64>{50, 60}));
    ASSERT_TRUE(x.find(2, ids, n));
    EXPECT_EQ(n, 2);
    x.addPrepared(7, {1}, 0, 0); // empty window: prepared and empty
    ASSERT_TRUE(x.find(7, ids, n));
    EXPECT_EQ(n, 0);
}

// ------------------------------------------------------------------ OrmMarkCache (T-0085)

OrmMarkRec markRec(qint64 id, const QString& name, const QString& desc) {
    OrmMarkRec r;
    r.markId = id;
    r.name = name;
    r.desc = desc;
    r.geoType = OrmGeoType::Line;
    r.geoCount = 3;
    return r;
}

OrmMarkRow markRow(qint64 id, const QString& name, const QString& desc) {
    OrmMarkRow r;
    r.id = id;
    r.name = name;
    r.desc = desc;
    return r;
}

TEST(OrmMarkCacheTest, AddUpdateFindRemoveResetAndSize) {
    OrmMarkCache c(kUnlimCacheSize);
    EXPECT_EQ(c.size(), 152);
    c.addOrUpdate(markRec(5, QStringLiteral("ab"), QStringLiteral("c")));
    c.addOrUpdate(markRec(2, QStringLiteral("Город"), QString()));
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2, 5})); // given unsorted
    EXPECT_EQ(c.size(), 226);                         // 152 + 2 * 29 + (2 + 1 + 5 + 0) * 2
    ASSERT_NE(c.find(5), nullptr);
    EXPECT_EQ(c.find(5)->desc, QStringLiteral("c"));
    EXPECT_EQ(c.find(5)->geoType, OrmGeoType::Line);
    EXPECT_EQ(c.find(5)->geoCount, 3);
    EXPECT_EQ(c.find(4), nullptr);

    // update: the size changes by the difference of the texts (4 units - 3 units) * 2
    c.addOrUpdate(markRec(5, QStringLiteral("abcd"), QString()));
    EXPECT_EQ(c.count(), 2);
    EXPECT_EQ(c.find(5)->name, QStringLiteral("abcd"));
    EXPECT_EQ(c.size(), 228);

    // Delete (cache base :253-262) does not give the text bytes back
    c.remove(2);
    c.remove(99);
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{5}));
    EXPECT_EQ(c.size(), 199); // 152 + 29 + 18

    c.reset();
    EXPECT_EQ(c.count(), 0);
    EXPECT_EQ(c.size(), 152);
}

TEST(OrmMarkCacheTest, AddPreparedForACategoryAddsOnlyTheMissingRows) {
    OrmMarkCache c(kUnlimCacheSize);
    c.addPrepared(3, {markRow(9, QStringLiteral("nn"), QStringLiteral("d")),
                      markRow(4, QStringLiteral("Ом"), QString())});
    EXPECT_TRUE(c.isCategoryPrepared(3));
    EXPECT_FALSE(c.isCategoryPrepared(4));
    EXPECT_FALSE(c.isPrepared());
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{4, 9}));
    EXPECT_EQ(c.size(), 220); // 152 + 2 * 29 + (2 + 1 + 2 + 0) * 2
    // 4 is cached already and keeps its row; 6 is new and 1 more
    c.addPrepared(
        5, {markRow(6, QStringLiteral("xyz"), QString()), markRow(4, QStringLiteral("other"), QString())});
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{4, 6, 9}));
    EXPECT_EQ(c.find(4)->name, QStringLiteral("Ом"));
    EXPECT_EQ(c.size(), 255); // 152 + 3 * 29 + (5 + 3) * 2
    // an empty answer still marks the category prepared
    c.addPrepared(8, {});
    EXPECT_TRUE(c.isCategoryPrepared(8));
    EXPECT_EQ(c.count(), 3);
}

TEST(OrmMarkCacheTest, AddPreparedForTheWholeTableReplacesTheContent) {
    OrmMarkCache c(kUnlimCacheSize);
    c.addPrepared(3, {markRow(1, QStringLiteral("old"), QString())});
    c.addPrepared(0, {markRow(8, QStringLiteral("ab"), QString()),
                      markRow(2, QStringLiteral("c"), QStringLiteral("de"))});
    EXPECT_TRUE(c.isPrepared());
    EXPECT_FALSE(c.isCategoryPrepared(3)); // Reset dropped the category list
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2, 8}));
    EXPECT_EQ(c.size(), 152 + 2 * 29 + 10); // 220
    c.addPrepared(-4,
                  {markRow(5, QStringLiteral("q"), QString())}); // any non-positive id: whole table, one row
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{5}));
    EXPECT_EQ(c.size(), 152 + 29 + 2);
    c.addPrepared(0, {});
    EXPECT_EQ(c.count(), 0);
    EXPECT_TRUE(c.isPrepared());
    EXPECT_EQ(c.size(), 152);
}

TEST(OrmMarkCacheTest, OverTheLimitTheNextAddEmptiesTheCache) {
    OrmMarkCache c(200);
    c.addOrUpdate(markRec(1, QStringLiteral("ab"), QString())); // 152 <= 200 before; 185 after
    c.addOrUpdate(markRec(2, QStringLiteral("ab"), QString())); // 185 <= 200 before; 218 after
    EXPECT_EQ(c.count(), 2);
    EXPECT_EQ(c.size(), 218);
    c.addPrepared(4, {markRow(7, QString(), QString())}); // 218 > 200: reset first, category joins afterwards
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{7}));
    EXPECT_TRUE(c.isCategoryPrepared(4));
    c.addOrUpdate(markRec(7, QStringLiteral("zz"), QString())); // 181 <= 200: update in place
    EXPECT_EQ(c.count(), 1);
    EXPECT_TRUE(c.isCategoryPrepared(4));
    EXPECT_EQ(c.size(), 185);
}

// ------------------------------------------------------------------ OrmMarkGeometryCache (T-0085)
// The legacy class has no Size / Count: the inner array's size is 112 + 20 per row + the bytes of the rows
// that hold a geometry (TDynArrayByRecWithPointer.Size), seen here through the moment of the auto-reset.

TEST(OrmMarkGeometryCacheTest, AddOrUpdateFindRemoveReset) {
    OrmMarkGeometryCache c(kUnlimCacheSize);
    const geo::GeometryLonLatPtr a = testutil::point();
    const geo::GeometryLonLatPtr b = testutil::point();
    geo::GeometryLonLatPtr got;
    EXPECT_FALSE(c.find(1, got));
    c.addOrUpdate(7, 10, a);
    c.addOrUpdate(3, 20, b);
    ASSERT_TRUE(c.find(7, got));
    EXPECT_EQ(got, a);
    ASSERT_TRUE(c.find(3, got));
    EXPECT_EQ(got, b);
    c.addOrUpdate(7, 5, b); // replaced
    ASSERT_TRUE(c.find(7, got));
    EXPECT_EQ(got, b);
    c.remove(3);
    c.remove(99);
    EXPECT_FALSE(c.find(3, got));
    EXPECT_TRUE(c.find(7, got));
    c.reset();
    EXPECT_FALSE(c.find(7, got));
}

TEST(OrmMarkGeometryCacheTest, OverTheLimitTheNextAddEmptiesTheArray) {
    OrmMarkGeometryCache c(200);
    geo::GeometryLonLatPtr got;
    c.addOrUpdate(1, 10, testutil::point());  // 112 before; 142 after (112 + 20 + 10)
    c.addOrUpdate(2, 100, testutil::point()); // 142 <= 200 before; 262 after (112 + 40 + 110)
    EXPECT_TRUE(c.find(1, got));
    EXPECT_TRUE(c.find(2, got));
    c.addOrUpdate(3, 5, testutil::point()); // 262 > 200: emptied first
    EXPECT_FALSE(c.find(1, got));
    EXPECT_FALSE(c.find(2, got));
    EXPECT_TRUE(c.find(3, got));
    // a row without a geometry adds no bytes: 112 + 20 = 132 stays under 200 however big its size
    OrmMarkGeometryCache d(200);
    d.addOrUpdate(1, 100000, nullptr);
    d.addOrUpdate(2, 100000, nullptr);
    d.addOrUpdate(3, 1, testutil::point());
    EXPECT_TRUE(d.find(1, got));
    EXPECT_EQ(got, nullptr);
}

TEST(OrmMarkGeometryCacheTest, AddPreparedKeepsTheCachedRowsAndMarksTheCategory) {
    OrmMarkGeometryCache c(kUnlimCacheSize);
    const geo::GeometryLonLatPtr old = testutil::point();
    const geo::GeometryLonLatPtr g2 = testutil::point();
    const geo::GeometryLonLatPtr g5 = testutil::point();
    const geo::GeometryLonLatPtr g9 = testutil::point();
    c.addOrUpdate(5, 1, old);
    EXPECT_FALSE(c.isCategoryPrepared(4));
    c.addPrepared(4,
                  {OrmMarkGeometryRec{9, 3, g9}, OrmMarkGeometryRec{5, 3, g5}, OrmMarkGeometryRec{2, 3, g2}});
    EXPECT_TRUE(c.isCategoryPrepared(4));
    EXPECT_FALSE(c.isCategoryPrepared(6));
    EXPECT_FALSE(c.isPrepared());
    geo::GeometryLonLatPtr got;
    ASSERT_TRUE(c.find(5, got));
    EXPECT_EQ(got, old); // ids already cached keep their data
    ASSERT_TRUE(c.find(2, got));
    EXPECT_EQ(got, g2);
    ASSERT_TRUE(c.find(9, got));
    EXPECT_EQ(got, g9);
    c.addPrepared(0, {}); // the whole table
    EXPECT_TRUE(c.isPrepared());
    EXPECT_TRUE(c.isCategoryPrepared(6));
    c.addPrepared(-1, {}); // every non-positive id is the whole table
    EXPECT_TRUE(c.isPrepared());
}

// L-158 fixed (deviation D-88). Legacy keeps the prepared state when the inner array
// empties itself, so these expectations are the opposite of legacy (isCategoryPrepared / isPrepared stay
// true).
TEST(OrmMarkGeometryCacheTest, AutoResetAlsoClearsThePreparedState) {
    OrmMarkGeometryCache c(200);
    geo::GeometryLonLatPtr got;
    const geo::GeometryLonLatPtr g1 = testutil::point();
    c.addPrepared(7, {OrmMarkGeometryRec{1, 150, g1}}); // 112 before; 282 after
    EXPECT_TRUE(c.isCategoryPrepared(7));
    c.addPrepared(9,
                  {OrmMarkGeometryRec{2, 1, testutil::point()}}); // 282 > 200: array emptied, then refilled
    EXPECT_FALSE(c.find(1, got));
    EXPECT_TRUE(c.find(2, got));
    EXPECT_FALSE(c.isCategoryPrepared(7)); // legacy: still true, mark 1 silently missing
    EXPECT_TRUE(c.isCategoryPrepared(9));  // prepared with the data that was just put in
    EXPECT_FALSE(c.isPrepared());

    OrmMarkGeometryCache d(200);
    d.addPrepared(0, {OrmMarkGeometryRec{1, 150, g1}}); // 282 after
    EXPECT_TRUE(d.isPrepared());
    d.addOrUpdate(2, 1, testutil::point()); // 282 > 200: emptied; legacy keeps IsPrepared
    EXPECT_FALSE(d.find(1, got));
    EXPECT_FALSE(d.isPrepared());
    EXPECT_FALSE(d.isCategoryPrepared(7));
    d.addPrepared(8, {}); // 132 <= 200: no reset, the category state is the fresh one
    EXPECT_TRUE(d.isCategoryPrepared(8));
    EXPECT_TRUE(d.find(2, got));
}

TEST(OrmMarkGeometryCacheTest, ResetAlsoClearsTheWholeTableFlag) {
    OrmMarkGeometryCache c(kUnlimCacheSize);
    c.addPrepared(0, {OrmMarkGeometryRec{1, 1, testutil::point()}});
    c.addPrepared(5, {});
    EXPECT_TRUE(c.isPrepared());
    c.reset();
    EXPECT_FALSE(c.isPrepared()); // legacy Reset (:1146-1155) leaves FIsPrepared set
    EXPECT_FALSE(c.isCategoryPrepared(5));
    geo::GeometryLonLatPtr got;
    EXPECT_FALSE(c.find(1, got));
}

// ------------------------------------------------------------------ OrmMarkDbCache (T-0085)

TEST(OrmMarkDbCacheTest, InitSplitsThirtyAndSeventyAndDoneFreesAll) {
    OrmMarkDbCache db;
    EXPECT_EQ(db.markCache, nullptr);
    db.init(700); // mark cache 210, geometry cache 490
    ASSERT_NE(db.markCache, nullptr);
    ASSERT_NE(db.markGeometryCache, nullptr);
    ASSERT_NE(db.markViewCache, nullptr);
    ASSERT_NE(db.markImageCache, nullptr);
    ASSERT_NE(db.markAppearanceCache, nullptr);
    ASSERT_NE(db.markIdIndex, nullptr);
    ASSERT_NE(db.markIdByCategoryIndex, nullptr);

    // mark cache: 152 + 29 n; the add that finds more than 210 empties it. 3 rows = 239, the 4th add empties.
    for (qint64 id = 1; id <= 3; ++id) {
        db.markCache->addOrUpdate(markRec(id, QString(), QString()));
    }
    EXPECT_EQ(db.markCache->count(), 3);
    db.markCache->addOrUpdate(markRec(4, QString(), QString()));
    EXPECT_EQ(db.markCache->count(), 1);

    // geometry cache: 112 + 20 n; 19 rows = 492 > 490, so the 20th add empties it (the 19th does not:
    // 472 <= 490). 30 % of 700 would be 210 and trip it already at the 6th row.
    geo::GeometryLonLatPtr got;
    for (qint64 id = 1; id <= 19; ++id) {
        db.markGeometryCache->addOrUpdate(id, 0, testutil::point());
    }
    EXPECT_TRUE(db.markGeometryCache->find(1, got));
    db.markGeometryCache->addOrUpdate(20, 0, testutil::point());
    EXPECT_FALSE(db.markGeometryCache->find(1, got));
    EXPECT_TRUE(db.markGeometryCache->find(20, got));

    // the unlimited ones never empty themselves
    for (qint64 id = 1; id <= 50; ++id) {
        db.markImageCache->addOrIgnore(id, QString(100, QLatin1Char('x')));
    }
    EXPECT_EQ(db.markImageCache->count(), 50);

    db.done();
    EXPECT_EQ(db.markCache, nullptr);
    EXPECT_EQ(db.markGeometryCache, nullptr);
    EXPECT_EQ(db.markViewCache, nullptr);
    EXPECT_EQ(db.markImageCache, nullptr);
    EXPECT_EQ(db.markAppearanceCache, nullptr);
    EXPECT_EQ(db.markIdIndex, nullptr);
    EXPECT_EQ(db.markIdByCategoryIndex, nullptr);
}

// 30 % of 104,857,600 = 31,457,280. One row with a name of N UTF-16 units: size 152 + 29 + 2 N.
// N = 15,728,549 -> 31,457,279 (not over), N = 15,728,550 -> 31,457,281 (over).
TEST(OrmMarkDbCacheTest, MarkCacheLimitAtTheDefaultSizeIsPinnedToOneByte) {
    OrmMarkDbCache db;
    db.init(104857600);
    db.markCache->addOrUpdate(markRec(1, QString(15728549, QLatin1Char('x')), QString()));
    db.markCache->addOrUpdate(markRec(2, QString(), QString())); // 31,457,279 <= limit: kept
    EXPECT_EQ(db.markCache->count(), 2);
    db.done();

    db.init(104857600);
    db.markCache->addOrUpdate(markRec(1, QString(15728550, QLatin1Char('x')), QString()));
    db.markCache->addOrUpdate(markRec(2, QString(), QString())); // 31,457,281 > limit: emptied first
    EXPECT_EQ(db.markCache->count(), 1);
    EXPECT_EQ(db.markCache->find(1), nullptr);
    db.done();
}

TEST(OrmMarkDbCacheTest, DefaultCacheSizeGivesTheSpecLimits) {
    // CacheSizeMb = 100 -> 104,857,600 bytes: 31,457,280 for the marks, 73,400,320 for the geometries.
    // Observed by the geometry array: one row of 73,400,320 - 132 bytes keeps 112 + 20 + size = limit.
    OrmMarkDbCache db;
    db.init(104857600);
    geo::GeometryLonLatPtr got;
    db.markGeometryCache->addOrUpdate(1, 73400320 - 132, testutil::point()); // size == limit: not over
    db.markGeometryCache->addOrUpdate(2, 1, testutil::point());
    EXPECT_TRUE(db.markGeometryCache->find(1, got)); // 112 + 20 + 73400188 = 73400320 is not > limit
    db.markGeometryCache->addOrUpdate(3, 1, testutil::point());
    EXPECT_FALSE(db.markGeometryCache->find(1, got)); // now 73400320 + 20 + 1 > limit
    db.done();
}

} // namespace
} // namespace cg::marks
