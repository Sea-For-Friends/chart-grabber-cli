// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034 (enabler, held open). Ports u_MarkSystemORMCacheBase.pas; docs/specs/marks.md §2.9.7.
// Proof of ticket T-0063: sorted order, size arithmetic, auto-reset at the limit, prepared-by-category.
#include "cg/marks/mormot/mark_system_orm_cache_base.h"

#include <QString>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <vector>

namespace cg::marks {
namespace {

// Legacy literals, Win64, derived by layout rules (see the header): InstanceSize of TOrmMarkCache and of
// TDynArrayByRecWithPointer. The test caches are given the TOrmMarkCache figure.
constexpr int kInstance = 152;
constexpr qint64 kRecInstance = 112;

// A descendant written as the legacy TOrmCategoryCache (Categories/u_MarkCategoryDbImplORMCache.pas):
// record {CategoryId, Name}, packed Delphi size 8 + 8, DataSize counts UTF-16 units * 2.
struct CatRow {
    qint64 id = 0;
    QString name;
};

class CatCache : public OrmCacheBase<CatRow> {
public:
    explicit CatCache(qint64 maxCacheSize) : OrmCacheBase<CatRow>(maxCacheSize, kInstance, 16) {}

    // TOrmCategoryCache.AddOrUpdate
    void addOrUpdate(qint64 id, const QString& name) {
        checkCacheSize();
        qint64 vsize = name.size() * 2;
        int i = 0;
        if (row_.fastLocateSorted(id, i)) {
            vsize -= rows()[static_cast<size_t>(i)].name.size() * 2;
            rows()[static_cast<size_t>(i)].name = name;
            dataSize_ += vsize;
        } else {
            row_.fastAddSorted(i, CatRow{id, name});
            dataSize_ += vsize;
        }
    }

    void addPrepared(const std::vector<CatRow>& arr) { // TOrmCategoryCache.AddPrepared
        reset();
        if (arr.size() == 1) {
            row_.fastAddSorted(0, arr[0]);
        } else if (arr.size() > 1) {
            row_.addArray(arr);
            row_.sort();
        }
        isPrepared_ = true;
    }

    [[nodiscard]] qint64 dataSize() const { return dataSize_; }
    [[nodiscard]] qint64 itemBytes() const { return 16; }
    [[nodiscard]] const DynArray<CatRow>& dyn() const { return row_; }
};

// A descendant with the per-category flag, as TOrmMarkCache.AddPrepared (:840-851 of its unit).
class PreparedCache : public OrmCacheBaseWithPreparedByCategory<CatRow> {
public:
    explicit PreparedCache(qint64 maxCacheSize, int kItem = 16)
        : OrmCacheBaseWithPreparedByCategory<CatRow>(maxCacheSize, kInstance, kItem) {}

    void addRow(qint64 id) {
        checkCacheSize();
        int i = 0;
        if (!row_.fastLocateSorted(id, i)) {
            row_.fastAddSorted(i, CatRow{id, {}});
        }
    }
    void markCategoryPrepared(qint64 categoryId) {
        int i = 0;
        if (!preparedCategories_.fastLocateSorted(categoryId, i)) {
            preparedCategories_.fastAddSorted(i, categoryId);
        }
    }
    void setPrepared() { isPrepared_ = true; }
    [[nodiscard]] int preparedCount() const { return preparedCategories_.count(); }
};

std::vector<qint64> idsOf(CatCache& c) {
    std::vector<qint64> r;
    for (int i = 0; i < c.count(); ++i) {
        r.push_back(c.rows()[static_cast<size_t>(i)].id);
    }
    return r;
}

// ------------------------------------------------------------------ sorted order

TEST(OrmCacheBaseTest, RowsAreKeptSortedBySignedId) {
    // ids as one table; 13 rows so that Find leaves the linear scan (> 10) for the binary search.
    const std::vector<qint64> in = {5, -3, 100, 7, 0, 3, 9, -1, 2, 4, 6, 8, 10};
    CatCache c(kUnlimCacheSize);
    for (const qint64 id : in) {
        c.addOrUpdate(id, QStringLiteral("n"));
    }
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{-3, -1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100}));
    for (const qint64 id : in) {
        EXPECT_GE(c.dyn().find(id), 0) << id;
    }
    EXPECT_EQ(c.dyn().find(11), -1);
    EXPECT_EQ(c.dyn().find(-4), -1);
}

TEST(OrmCacheBaseTest, UpdateKeepsPositionAndRemoveClosesTheGap) {
    CatCache c(kUnlimCacheSize);
    for (const qint64 id : {3, 1, 2}) {
        c.addOrUpdate(id, QStringLiteral("a"));
    }
    c.addOrUpdate(2, QStringLiteral("bb"));
    EXPECT_EQ(c.count(), 3);
    EXPECT_EQ(c.rows()[1].name, QStringLiteral("bb"));
    c.remove(1);
    c.remove(42); // absent: nothing
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{2, 3}));
    // the remembered count, not the storage, is what the readers iterate to
    EXPECT_EQ(c.rows().size(), static_cast<size_t>(c.count()));
}

TEST(OrmCacheBaseTest, AddPreparedSortsTheArrayAndSetsTheFlag) {
    CatCache c(kUnlimCacheSize);
    c.addOrUpdate(9, QStringLiteral("old"));
    c.addPrepared({{4, "d"}, {-2, "x"}, {7, "g"}});
    EXPECT_TRUE(c.isPrepared());
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{-2, 4, 7})); // AddPrepared starts with Reset
    c.addPrepared({{5, "e"}});
    EXPECT_EQ(idsOf(c), (std::vector<qint64>{5}));
    c.addPrepared({});
    EXPECT_EQ(c.count(), 0);
    EXPECT_TRUE(c.isPrepared());
}

TEST(IdDynArrayObjectTest, SortedUniqueIdsAndPartialAddArray) {
    IdDynArrayObject a;
    a.add(30);
    a.add(10);
    a.add(20);
    a.add(20); // FastLocateOrAddSorted: no duplicate
    EXPECT_EQ(a.idArray(), (std::vector<qint64>{10, 20, 30}));
    EXPECT_TRUE(a.find(20));
    EXPECT_FALSE(a.find(25));

    // :323-373 - start/count window; 20 and 30 exist and are skipped, 5 and 15 are added
    a.addArray({99, 5, 20, 15, 30, 77}, 1, 4);
    EXPECT_EQ(a.idArray(), (std::vector<qint64>{5, 10, 15, 20, 30}));
    EXPECT_EQ(a.count(), 5);
    a.remove(10);
    a.remove(1000);
    EXPECT_EQ(a.idArray(), (std::vector<qint64>{5, 15, 20, 30}));
    a.reset();
    EXPECT_EQ(a.count(), 0);
}

TEST(IdDynArrayObjectTest, AddArrayWindows) {
    struct Case {
        std::vector<qint64> existing;
        std::vector<qint64> source;
        int start;
        int count;
        std::vector<qint64> expected;
    };
    const std::vector<Case> cases = {
        {{}, {3, 1, 2}, 0, -1, {1, 2, 3}},        // empty target: whole array, sorted
        {{}, {3, 1, 2}, 1, -1, {1, 2}},           // count -1 = to the end
        {{}, {3, 1, 2}, 0, 2, {1, 3}},            // empty target, count window
        {{}, {3, 1, 2}, 3, -1, {}},               // start at the end: nothing
        {{}, {3, 1, 2}, 0, 0, {}},                // count 0: nothing
        {{7}, {3, 1, 2}, 0, -1, {1, 2, 3, 7}},    // all new: AddArray + Sort
        {{1, 2, 3}, {3, 1, 2}, 0, -1, {1, 2, 3}}, // all present: nothing
        {{2}, {5, 5, 2}, 0, -1, {2, 5, 5}},       // L-157: duplicates inside the source are both added
        {{}, {5, 5}, 0, -1, {5, 5}},              // ... also into an empty target
    };
    for (const Case& c : cases) {
        IdDynArrayObject a;
        for (const qint64 id : c.existing) {
            a.add(id);
        }
        a.addArray(c.source, c.start, c.count);
        EXPECT_EQ(a.idArray(), c.expected);
        EXPECT_EQ(a.count(), static_cast<int>(c.expected.size()));
    }
}

TEST(IdDynArrayObjectTest, AddArrayWindowPastTheArrayDoesNotReadOutOfRange) { // D-66
    struct Case {
        qint64 first;
        int start;
        int count;
        std::vector<qint64> expected;
    };
    const std::vector<Case> cases = {
        {1, 1, 10, {1, 8, 9}}, // count runs over the end of the source (legacy: out-of-range read)
        {1, -1, 2, {1}},       // negative start (legacy: read before the array)
        {1, 5, 1, {1}},        // start past the end
    };
    for (const Case& c : cases) {
        IdDynArrayObject a;
        a.add(c.first);
        a.addArray({7, 8, 9}, c.start, c.count);
        EXPECT_EQ(a.idArray(), c.expected);
        IdDynArrayObject empty; // the empty-target branch: legacy copies from before the array
        empty.addArray({7, 8, 9}, std::numeric_limits<int>::min(), -1);
        EXPECT_EQ(empty.count(), 0);
        empty.addArray({7, 8, 9}, -1, -1);
        EXPECT_EQ(empty.count(), 0);
        EXPECT_EQ(a.idArray(), c.expected);
    }
}

TEST(DynArrayByRecWithPointerTest, SortedRowsAndFind) {
    DynArrayByRecWithPointer<int> d(kUnlimCacheSize);
    for (const qint64 id : {5, -1, 12, 3}) {
        d.add(id, std::make_shared<int>(static_cast<int>(id)), 4, true);
    }
    ASSERT_EQ(d.count(), 4);
    std::vector<qint64> ids;
    for (const auto& r : d.rows()) {
        ids.push_back(r.id);
    }
    EXPECT_EQ(ids, (std::vector<qint64>{-1, 3, 5, 12}));
    DynArrayByRecWithPointer<int>::Rec* rec = nullptr;
    ASSERT_TRUE(d.find(5, rec));
    EXPECT_EQ(*rec->data, 5);
    EXPECT_FALSE(d.find(6, rec));

    // AddArray: 3 and 5 exist (skipped), 4 and -7 are new
    using Rec = DynArrayByRecWithPointer<int>::Rec;
    d.addArray({Rec{3, 1, std::make_shared<int>(0)}, Rec{4, 1, std::make_shared<int>(4)}, Rec{5, 1, nullptr},
                Rec{-7, 1, std::make_shared<int>(-7)}});
    ids.clear();
    for (const auto& r : d.rows()) {
        ids.push_back(r.id);
    }
    EXPECT_EQ(ids, (std::vector<qint64>{-7, -1, 3, 4, 5, 12}));
    ASSERT_TRUE(d.find(3, rec));
    EXPECT_EQ(*rec->data, 3); // the existing row was not replaced
}

// ------------------------------------------------------------------ size arithmetic

TEST(OrmCacheBaseTest, SizeIsInstancePlusItemSizeTimesCountPlusDataSize) {
    CatCache c(kUnlimCacheSize);
    const qint64 instance = kInstance;
    EXPECT_EQ(c.size(), instance);
    struct Step {
        qint64 id;
        QString name;
        int expectedCount;
        qint64 expectedData; // UTF-16 units * 2, text of an updated row replaced
    };
    const std::vector<Step> steps = {
        {1, QStringLiteral("abc"), 1, 6},
        {2, QString(), 2, 6},
        {3, QStringLiteral("0123456789"), 3, 26},
        {3, QStringLiteral("01"), 3, 10},                 // update: DataSize -= old, += new
        {1, QString::fromUtf8("\xD0\x90\xD0\x91"), 3, 8}, // Cyrillic: 2 units, not 4 bytes
    };
    for (const Step& s : steps) {
        c.addOrUpdate(s.id, s.name);
        EXPECT_EQ(c.count(), s.expectedCount);
        EXPECT_EQ(c.dataSize(), s.expectedData);
        EXPECT_EQ(c.size(), instance + 16 * s.expectedCount + s.expectedData);
    }
    c.reset();
    EXPECT_EQ(c.size(), instance);
}

TEST(DynArrayByRecWithPointerTest, SizeCountsDataOnlyForNonNilPointers) {
    DynArrayByRecWithPointer<int> d(kUnlimCacheSize);
    const qint64 instance = kRecInstance;
    auto item = [] { return std::make_shared<int>(1); };
    EXPECT_EQ(d.size(), instance);
    d.add(1, item(), 100, true);
    EXPECT_EQ(d.size(), instance + 20 + 100); // packed TRecWithPointer = 8 + 4 + 8
    d.add(2, nullptr, 50, true);              // size stored, but no data -> not counted
    EXPECT_EQ(d.size(), instance + 40 + 100);
    d.add(1, item(), 30, true); // update: -100 +30
    EXPECT_EQ(d.size(), instance + 40 + 30);
    d.add(1, item(), 999, false); // exists, no update: unchanged
    EXPECT_EQ(d.size(), instance + 40 + 30);
    d.add(2, item(), 7, true); // old nil: nothing subtracted
    EXPECT_EQ(d.size(), instance + 40 + 37);
    d.remove(2);
    EXPECT_EQ(d.size(), instance + 20 + 30);
    d.remove(2);
    d.reset();
    EXPECT_EQ(d.size(), instance);
}

// ------------------------------------------------------------------ auto-reset at the limit

TEST(OrmCacheBaseTest, AutoResetEmptiesTheWholeCacheAtTheNextAdd) {
    const qint64 instance = kInstance;
    struct Case {
        const char* what;
        qint64 limit;
        int addsBefore;    // adds of a 4-unit name (8 data bytes + 16 item bytes = 24 each)
        int expectedCount; // after one more add
    };
    const std::vector<Case> cases = {
        {"exactly at the limit: size() > max is false", instance + 24 * 3, 3, 4},
        {"one byte over: the next add resets first", instance + 24 * 3 - 1, 3, 1},
        {"zero limit (CacheSizeMb = 0): every add resets", 0, 5, 1},
        {"unlimited never resets", kUnlimCacheSize, 50, 51},
    };
    for (const Case& c : cases) {
        CatCache cache(c.limit);
        for (int i = 1; i <= c.addsBefore; ++i) {
            cache.addOrUpdate(i, QStringLiteral("abcd"));
        }
        cache.addOrUpdate(1000, QStringLiteral("abcd"));
        EXPECT_EQ(cache.count(), c.expectedCount) << c.what;
        if (c.expectedCount == 1) {
            EXPECT_EQ(cache.dataSize(), 8) << c.what; // only the new row is accounted
            EXPECT_EQ(cache.rows()[0].id, 1000) << c.what;
        }
    }
}

TEST(OrmCacheBaseTest, AutoResetClearsPreparedFlagAndCategories) {
    const qint64 instance = kInstance;
    PreparedCache c(instance + 16 * 2); // two rows fit, three do not
    c.addRow(1);
    c.addRow(2);
    c.markCategoryPrepared(7);
    c.setPrepared();
    ASSERT_TRUE(c.isCategoryPrepared(7));
    ASSERT_TRUE(c.isPrepared());
    c.addRow(3); // size == max: no reset yet
    EXPECT_EQ(c.count(), 3);
    EXPECT_TRUE(c.isCategoryPrepared(7));
    c.addRow(4); // size > max: everything goes, the virtual reset() clears the categories too
    EXPECT_EQ(c.count(), 1);
    EXPECT_FALSE(c.isPrepared());
    EXPECT_FALSE(c.isCategoryPrepared(7));
    EXPECT_EQ(c.preparedCount(), 0);
}

TEST(DynArrayByRecWithPointerTest, AutoResetReleasesTheDataAndHappensEvenForAnUpdate) {
    const qint64 instance = kRecInstance;
    DynArrayByRecWithPointer<int> d(instance + 20 + 10); // one row of 10 data bytes fits
    auto first = std::make_shared<int>(1);
    const std::weak_ptr<int> watch = first;
    d.add(1, first, 10, true);
    first.reset();
    ASSERT_FALSE(watch.expired());
    d.add(2, std::make_shared<int>(2), 10, true); // size == max before this add: kept, now 2 rows
    EXPECT_EQ(d.count(), 2);
    EXPECT_FALSE(watch.expired());
    // size is now over the limit: even an update of an existing id empties the cache first (:430-486)
    d.add(1, std::make_shared<int>(11), 10, true);
    EXPECT_TRUE(watch.expired());
    EXPECT_EQ(d.count(), 1);
    DynArrayByRecWithPointer<int>::Rec* rec = nullptr;
    EXPECT_FALSE(d.find(2, rec));
    ASSERT_TRUE(d.find(1, rec));
    EXPECT_EQ(*rec->data, 11);
    EXPECT_EQ(d.size(), instance + 20 + 10);

    DynArrayByRecWithPointer<int> zero(0); // addArray checks the size first as well
    zero.add(1, std::make_shared<int>(1), 5, true);
    zero.addArray({{2, 5, std::make_shared<int>(2)}});
    EXPECT_EQ(zero.count(), 1);
    EXPECT_FALSE(zero.find(1, rec));
}

// ------------------------------------------------------------------ prepared by category

TEST(OrmCacheBaseWithPreparedByCategoryTest, CategoriesAreSortedAndSurviveOnlyUntilReset) {
    PreparedCache c(kUnlimCacheSize);
    EXPECT_FALSE(c.isCategoryPrepared(5));
    // 13 categories (> 10: binary search), negative ids sort first, a repeated one is not added twice
    for (const qint64 cat : {50, 3, -9, 8, 12, 1, 77, 5, 6, 9, 10, 11, 4, 8}) {
        c.markCategoryPrepared(cat);
    }
    EXPECT_EQ(c.preparedCount(), 13);
    for (const qint64 cat : {50, 3, -9, 8, 12, 1, 77, 5, 6, 9, 10, 11, 4}) {
        EXPECT_TRUE(c.isCategoryPrepared(cat)) << cat;
    }
    EXPECT_FALSE(c.isCategoryPrepared(2));
    EXPECT_FALSE(c.isCategoryPrepared(0));
    EXPECT_FALSE(c.isPrepared()); // the whole-table flag is independent of the categories
    c.addRow(1);
    c.setPrepared();
    c.reset();
    EXPECT_EQ(c.preparedCount(), 0);
    EXPECT_FALSE(c.isCategoryPrepared(50));
    EXPECT_FALSE(c.isPrepared());
    EXPECT_EQ(c.count(), 0);
}

// The default limits (CacheSizeMb = 100, u_MarkSystemImplConfigORM.pas:52; Init: Round(0.3 * max) and
// Round(0.7 * max) of 100 MiB, Marks/u_MarkDbImplORMHelper.pas:243, Marks/u_MarkDbImplORMCache.pas:291-300):
// 31,457,280 and 73,400,320 bytes. The row counts below are for rows without text / data.
TEST(OrmCacheBaseTest, MarkCacheResetsAtItsDefaultLimitAfter1084729Rows) {
    // TOrmMarkRow packed: 8 + 8 + 8 + 1 (enum) + 4 = 29; size = 152 + 29 * n; 152 + 29 * 1084728 = 31457264
    // (<= limit), 152 + 29 * 1084729 = 31457293 (> limit).
    PreparedCache c(31457280, 29);
    for (qint64 id = 1; id <= 1084729; ++id) {
        c.addRow(id);
    }
    ASSERT_EQ(c.count(), 1084729);
    c.addRow(1084730); // checkCacheSize sees 31457293 > 31457280
    EXPECT_EQ(c.count(), 1);
}

TEST(DynArrayByRecWithPointerTest, GeometryCacheResetsAtItsDefaultLimitAfter3670011Rows) {
    // size = 112 + 20 * n; 112 + 20 * 3670010 = 73400312 (<= limit), 112 + 20 * 3670011 = 73400332 (> limit).
    DynArrayByRecWithPointer<int> d(73400320);
    for (qint64 id = 1; id <= 3670011; ++id) {
        d.add(id, nullptr, 0, true);
    }
    ASSERT_EQ(d.count(), 3670011);
    d.add(3670012, nullptr, 0, true);
    EXPECT_EQ(d.count(), 1);
}

} // namespace
} // namespace cg::marks
