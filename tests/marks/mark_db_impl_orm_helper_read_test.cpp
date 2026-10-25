// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: nothing of its own (T-0105 has no feature id; contributes to the held-open SQLite mark database).
// Ports no legacy test (there is none): the read half of u_MarkDbImplORMHelper.pas, ReadMarkSQL :1012-1174,
// the four _FillPrepare* functions :1349-1644 and GetMarkRecArray :1646-1751 (docs/specs/marks.md §2.9.8 R-1,
// R-2; statements S-30 ... S-36, S-3A ... S-3F), against a copy of tests/fixtures/marks/Marks.db3 filled by
// the write half. Expected values are literals (the table kMarks) or plain-SQL read-backs on a second
// connection, never read through the helper's own caches (the caches are inspected only to pin what a path
// fills).
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include "../corpus_skip.h"
#include "mark_db_impl_orm_helper_test_util.h"

using namespace cg::marks::testutil;
using cg::marks::MarkDbImplORMHelper;
using cg::marks::MarkDbImplORMHelperTestAccess;
using cg::marks::MarkSystemORMError;
using cg::marks::OrmGeoType;
using cg::marks::OrmMarkRec;

namespace {

using Access = MarkDbImplORMHelperTestAccess;

GeometryLonLatPtr makeWidePolygon() { // longitude extent 8, latitude extent 3
    const std::vector<LonLat> p{
        {0.0, 0.0}, {8.0, 0.0}, {8.0, 3.0}, {0.0, 0.0}}; // closed, as the importers pass it
    return kFactory.createPolygon(p);
}
GeometryLonLatPtr makeOtherPoint() {
    return kFactory.createPoint(LonLat{-3.25, 47.5});
}

// The data set. id / category / picture id / appearance id / view id / visible / type / count / name / desc /
// picture name (points only) / appearance (1: 4279312947,4294967295,12,-3; 2: 4282668390,4294967295,7,9).
struct M {
    int id;
    int category;
    int pic;
    int appearance;
    int view;
    bool visible;
    int type;
    int count;
    const char* name;
    const char* desc;
    const char* picName;
};
const M kMarks[] = {
    {1, 9, 1, 1, 1, false, 1, 1, "Город", "Описание", "Icon.PNG"},
    {2, 7, 0, 1, 0, true, 2, 1, "line two", "", ""},
    {3, 7, 0, 1, 2, false, 3, 1, "Poly", "", ""},
    {4, 9, 2, 1, 0, true, 2, 2, "multi", "", ""},
    {5, 7, 1, 2, 3, true, 1, 1, "pt5", "", "Icon.PNG"},
    {6, 11, 0, 1, 0, true, 2, 1, "Одна линия", "", ""},
};

GeometryLonLatPtr expectedGeometry(int id) {
    switch (id) {
    case 1:
        return makePoint();
    case 2:
        return makeLineWithElevation();
    case 3:
        return makeWidePolygon();
    case 4:
        return makeMultiLine();
    case 5:
        return makeOtherPoint();
    default:
        return makeLine();
    }
}

// Inserts the six marks through the write half (ids 1 ... 6 in this order, categories interleaved), then two
// view rows by plain SQL: user 1 shows mark 5 (view id 3), user 99 hides mark 2 (view id 4, must be ignored).
void seedSix(Env& env) {
    auto helper = env.helper();
    const auto add = [&](const char* name, GeometryLonLatPtr geometry, OrmGeoType type, int count,
                         int category, bool visible, const char* pic, const char* desc = "") {
        OrmMarkRec r = rec(QString::fromUtf8(name), std::move(geometry), type, count, category, visible,
                           QString::fromUtf8(pic));
        r.desc = QString::fromUtf8(desc);
        ASSERT_TRUE(helper.insertMarkSql(r, true));
    };
    add("Город", makePoint(), OrmGeoType::Point, 1, 9, false, "Icon.PNG", "Описание");
    add("line two", makeLineWithElevation(), OrmGeoType::Line, 1, 7, true, "");
    add("Poly", makeWidePolygon(), OrmGeoType::Poly, 1, 7, false, "");
    add("multi", makeMultiLine(), OrmGeoType::Line, 2, 9, true, "x.png");
    {
        OrmMarkRec r = rec(QStringLiteral("pt5"), makeOtherPoint(), OrmGeoType::Point, 1, 7, true,
                           QStringLiteral("icon.png"));
        r.color1 = 4282668390U;
        r.scale1 = 7;
        r.scale2 = 9;
        ASSERT_TRUE(helper.insertMarkSql(r, true));
    }
    add("Одна линия", makeLine(), OrmGeoType::Line, 1, 11, true, "");
    env.exec("INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (1,5,7,1)");
    env.exec("INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (99,2,7,0)");
}

QString joined(const QStringList& rows) {
    return rows.join(QLatin1Char(';'));
}

// One mark as text. `geometry`: "geo=ok" when the geometry has the content of the literal one of that id.
QString describe(const OrmMarkRec& r, bool geometry, bool appearance) {
    QString text = QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8/%9|%10")
                       .arg(r.markId)
                       .arg(r.categoryId)
                       .arg(r.picId)
                       .arg(r.appearanceId)
                       .arg(r.viewId)
                       .arg(r.visible ? 1 : 0)
                       .arg(static_cast<int>(r.geoType))
                       .arg(r.geoCount)
                       .arg(r.name, r.desc);
    if (appearance) {
        text += QStringLiteral("|%1|%2,%3,%4,%5")
                    .arg(r.picName)
                    .arg(r.color1)
                    .arg(r.color2)
                    .arg(r.scale1)
                    .arg(r.scale2);
    }
    if (geometry) {
        const bool same = r.geometry != nullptr &&
                          r.geometry->isSameGeometry(*expectedGeometry(static_cast<int>(r.markId)));
        text += same ? QStringLiteral("|geo=ok")
                     : (r.geometry ? QStringLiteral("|geo=BAD") : QStringLiteral("|geo=null"));
    }
    return text;
}

QString describeAll(const std::vector<OrmMarkRec>& recs, bool geometry, bool appearance) {
    QStringList parts;
    for (const OrmMarkRec& r : recs) {
        parts.append(describe(r, geometry, appearance));
    }
    return joined(parts);
}

// The same text from the literal table.
QString expectedText(const std::vector<int>& ids, bool geometry, bool appearance) {
    QStringList parts;
    for (const int id : ids) {
        const M& m = kMarks[id - 1];
        const bool second = m.appearance == 2;
        QString text = QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8/%9|%10")
                           .arg(m.id)
                           .arg(m.category)
                           .arg(m.pic)
                           .arg(m.appearance)
                           .arg(m.view)
                           .arg(m.visible ? 1 : 0)
                           .arg(m.type)
                           .arg(m.count)
                           .arg(QString::fromUtf8(m.name), QString::fromUtf8(m.desc));
        if (appearance) {
            text += QStringLiteral("|%1|%2,4294967295,%3,%4")
                        .arg(QString::fromUtf8(m.picName))
                        .arg(second ? QStringLiteral("4282668390") : QStringLiteral("4279312947"))
                        .arg(second ? 7 : 12)
                        .arg(second ? 9 : -3);
        }
        if (geometry) {
            text += QStringLiteral("|geo=ok");
        }
        parts.append(text);
    }
    return joined(parts);
}

std::vector<OrmMarkRec> listOf(MarkDbImplORMHelper& helper, qint64 category, bool hidden, bool geometry,
                               bool appearance) {
    std::vector<OrmMarkRec> out;
    const int n = helper.getMarkRecArray(category, hidden, geometry, appearance, out);
    EXPECT_EQ(n, static_cast<int>(out.size()));
    return out;
}

// readMarkSql as a list of zero or one marks.
std::vector<OrmMarkRec> readOf(MarkDbImplORMHelper& helper, qint64 id, qint64 category, const QString& name) {
    OrmMarkRec r;
    std::vector<OrmMarkRec> out;
    if (helper.readMarkSql(r, id, category, name)) {
        out.push_back(r);
    }
    return out;
}

const std::vector<qint64>* byCategory(MarkDbImplORMHelper& helper, qint64 category, int& count) {
    const std::vector<qint64>* ids = nullptr;
    count = 0;
    return Access::cache(helper).markIdByCategoryIndex->find(category, ids, count) ? ids : nullptr;
}

QString idsOf(const std::vector<OrmMarkRec>& recs) {
    QStringList ids;
    for (const OrmMarkRec& r : recs) {
        ids.append(QString::number(r.markId));
    }
    return ids.join(QLatin1Char(','));
}

} // namespace

// ---- the data set ----------------------------------------------------------------------------------------

TEST(MarkDbImplORMHelperRead, SeededDatabaseHasTheDocumentedContent) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    EXPECT_EQ(joined(env.rows("SELECT ID,mCategory,mImage,mAppearance,mName,mGeoType,mGeoCount FROM Mark")),
              QString::fromUtf8("1|9|1|1|Город|1|1;2|7|0|1|line two|2|1;3|7|0|1|Poly|3|1;4|9|2|1|multi|2|2;"
                                "5|7|1|2|pt5|1|1;6|11|0|1|Одна линия|2|1"));
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|9|0;2|1|3|7|0;3|1|5|7|1;4|99|2|7|0"));
    EXPECT_EQ(joined(env.images()), QStringLiteral("1|Icon.PNG;2|x.png"));
    EXPECT_EQ(joined(env.rows("SELECT mMark FROM MarkMeta")), QStringLiteral("2"));
}

// ---- cache-off vs cache-on
// -----------------------------------------------------------------------------------

namespace {

struct QueryCase {
    const char* label;
    std::function<std::vector<OrmMarkRec>(MarkDbImplORMHelper&)> run;
    bool geometry;
    bool appearance;
    std::vector<int> ids; // the literal answer
};

struct ModeCase {
    const char* label;
    bool readOnly;
    quint32 cacheMb;
    int prelude; // 0 none; 1 the query once before (every cache it needs is prepared); 2 readMarkSql of mark
                 // 5; 3 the whole table without geometry and appearance
};

} // namespace

TEST(MarkDbImplORMHelperRead, EveryQueryGivesTheSameAnswerFromAColdDatabaseAndFromPreparedCaches) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    // the statement itself: the collation of mName compares the Cyrillic letters as they are
    EXPECT_EQ(env.rows("SELECT ID FROM Mark WHERE mName='город'").size(), 0);
    EXPECT_EQ(env.rows("SELECT ID FROM Mark WHERE mName='POLY'").size(), 1);
    const std::vector<QueryCase> queries = {
        {"category 7 visible",
         [](auto& h) { return listOf(h, 7, false, false, false); },
         false,
         false,
         {2, 5}},
        {"category 7 all, geometry, appearance",
         [](auto& h) { return listOf(h, 7, true, true, true); },
         true,
         true,
         {2, 3, 5}},
        {"category 9 all, geometry, appearance",
         [](auto& h) { return listOf(h, 9, true, true, true); },
         true,
         true,
         {1, 4}},
        {"category 9 visible, appearance",
         [](auto& h) { return listOf(h, 9, false, false, true); },
         false,
         true,
         {4}},
        {"category 11, geometry", [](auto& h) { return listOf(h, 11, true, true, false); }, true, false, {6}},
        {"category without marks", [](auto& h) { return listOf(h, 99, true, true, true); }, true, true, {}},
        {"whole table visible, geometry",
         [](auto& h) { return listOf(h, 0, false, true, false); },
         true,
         false,
         {2, 4, 5, 6}},
        {"whole table all, geometry, appearance",
         [](auto& h) { return listOf(h, 0, true, true, true); },
         true,
         true,
         {1, 2, 3, 4, 5, 6}},
        {"whole table all",
         [](auto& h) { return listOf(h, 0, true, false, false); },
         false,
         false,
         {1, 2, 3, 4, 5, 6}},
        {"read hidden mark by id", [](auto& h) { return readOf(h, 1, 0, {}); }, true, true, {1}},
        {"read mark with meta by id", [](auto& h) { return readOf(h, 2, 0, {}); }, true, true, {2}},
        {"read mark with a view row of another user",
         [](auto& h) { return readOf(h, 5, 0, {}); },
         true,
         true,
         {5}},
        {"read by name, any category, non-ASCII",
         [](auto& h) { return readOf(h, 0, 0, QString::fromUtf8("Город")); },
         true,
         true,
         {1}},
        {"read by name, Cyrillic in the other case (SYSTEMNOCASE folds the WinAnsi range only)",
         [](auto& h) { return readOf(h, 0, 0, QString::fromUtf8("город")); },
         true,
         true,
         {}},
        {"read by name and category",
         [](auto& h) { return readOf(h, 0, 7, QStringLiteral("poly")); },
         true,
         true,
         {3}},
        {"read by name, wrong category",
         [](auto& h) { return readOf(h, 0, 9, QStringLiteral("Poly")); },
         true,
         true,
         {}},
        {"read missing id", [](auto& h) { return readOf(h, 77, 0, {}); }, true, true, {}},
        {"read with neither id nor name", [](auto& h) { return readOf(h, 0, 7, {}); }, true, true, {}},
        {"read negative id with name",
         [](auto& h) { return readOf(h, -4, 0, QStringLiteral("multi")); },
         true,
         true,
         {4}},
    };
    const ModeCase modes[] = {
        {"cold", false, 100, 0},
        {"second run on the same helper", false, 100, 1},
        {"after a single read", false, 100, 2},
        {"after the whole table", false, 100, 3},
        {"read-only, cold", true, 100, 0},
        {"cache size 0, cold", false, 0, 0},
        {"cache size 0, second run", false, 0, 1},
    };
    for (const QueryCase& query : queries) {
        for (const ModeCase& mode : modes) {
            SCOPED_TRACE(std::string(query.label) + " / " + mode.label);
            auto helper = env.helper(mode.readOnly, mode.cacheMb);
            switch (mode.prelude) {
            case 1:
                std::ignore = query.run(helper);
                break;
            case 2:
                std::ignore = readOf(helper, 5, 0, {});
                break;
            case 3:
                std::ignore = listOf(helper, 0, true, false, false);
                break;
            default:
                break;
            }
            EXPECT_EQ(describeAll(query.run(helper), query.geometry, query.appearance),
                      expectedText(query.ids, query.geometry, query.appearance));
        }
    }
}

TEST(MarkDbImplORMHelperRead, PreparedCachesAnswerWithoutTheDatabaseAndAColdHelperSeesTheChange) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto warm = env.helper();
    const std::vector<OrmMarkRec> before = listOf(warm, 7, true, true, true);
    ASSERT_EQ(describeAll(before, true, true), expectedText({2, 3, 5}, true, true));
    OrmMarkRec single;
    ASSERT_TRUE(warm.readMarkSql(single, 2, 0, {}));

    // the database changes under the helper: name, category, geometry, view
    env.exec("UPDATE Mark SET mName='changed', mCategory=11, mGeoWKB=X'FF' WHERE ID=2");
    env.exec("UPDATE MarkView SET mvVisible=1 WHERE mvMark=3");
    EXPECT_EQ(describeAll(listOf(warm, 7, true, true, true), true, true),
              expectedText({2, 3, 5}, true, true));
    OrmMarkRec again;
    ASSERT_TRUE(warm.readMarkSql(again, 2, 0, {}));
    EXPECT_EQ(describe(again, true, true), expectedText({2}, true, true));

    // a fresh helper reads the new state: category 7 no longer holds mark 2, mark 3 is visible now
    auto cold = env.helper();
    EXPECT_EQ(describeAll(listOf(cold, 7, false, false, false), false, false),
              QString::fromUtf8("3/7/0/1/2/1/3/1/Poly|;5/7/1/2/3/1/1/1/pt5|"));
}

// ---- R-1: reading one mark
// ---------------------------------------------------------------------------------

TEST(MarkDbImplORMHelperRead, ColdReadByIdFillsTheIndexMarkGeometryViewImageAndAppearanceCaches) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    OrmMarkRec r;
    ASSERT_TRUE(helper.readMarkSql(r, 3, 0, {}));
    EXPECT_EQ(describe(r, true, true), expectedText({3}, true, true));
    const auto* index = cache.markIdIndex->find(3);
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(index->categoryId, 7);
    EXPECT_EQ(index->imageId, 0);
    EXPECT_EQ(index->appearanceId, 1);
    ASSERT_NE(cache.markCache->find(3), nullptr);
    EXPECT_EQ(cache.markCache->find(3)->name, QStringLiteral("Poly"));
    GeometryLonLatPtr geometry;
    ASSERT_TRUE(cache.markGeometryCache->find(3, geometry));
    EXPECT_TRUE(geometry->isSameGeometry(*makeWidePolygon()));
    ASSERT_NE(cache.markViewCache->find(3), nullptr);
    EXPECT_EQ(cache.markViewCache->find(3)->viewId, 2);
    EXPECT_FALSE(cache.markViewCache->find(3)->visible);
    EXPECT_NE(cache.markAppearanceCache->find(1), nullptr);
    // the by-category index only takes marks of a category that is prepared (Add of the cache): not 7 here
    int count = 0;
    EXPECT_EQ(byCategory(helper, 7, count), nullptr);
}

TEST(MarkDbImplORMHelperRead, ReadOfAPointResolvesThePictureAndOtherTypesKeepOnlyTheId) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    OrmMarkRec point;
    ASSERT_TRUE(helper.readMarkSql(point, 5, 0, {}));
    EXPECT_EQ(point.picId, 1);
    EXPECT_EQ(point.picName, QStringLiteral("Icon.PNG")); // the stored spelling of "icon.png"
    OrmMarkRec multi;
    ASSERT_TRUE(helper.readMarkSql(multi, 4, 0, {}));
    EXPECT_EQ(multi.picId, 2);
    EXPECT_EQ(multi.picName, QString());
    EXPECT_EQ(multi.geoCount, 2);
}

TEST(MarkDbImplORMHelperRead, IndexHitWithoutMarkCacheRowReadsTheMainFieldsBySql) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    ASSERT_EQ(Access::fillPrepareMarkIdIndex(helper, 9), 2); // the id index only
    ASSERT_EQ(cache.markCache->find(1), nullptr);
    OrmMarkRec r;
    ASSERT_TRUE(helper.readMarkSql(r, 1, 0, {}));
    EXPECT_EQ(describe(r, true, true), expectedText({1}, true, true));
    ASSERT_NE(cache.markCache->find(1), nullptr);
    EXPECT_EQ(cache.markCache->find(1)->desc, QString::fromUtf8("Описание"));
    EXPECT_EQ(cache.markCache->find(1)->geoType, OrmGeoType::Point);
    EXPECT_EQ(cache.markCache->find(1)->geoCount, 1);
}

TEST(MarkDbImplORMHelperRead, DanglingIndexEntryIsPurgedAndTheSideTablesOfTheMarkGoToo) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    ASSERT_EQ(Access::fillPrepareMarkIdIndex(helper, 7), 3);
    ASSERT_NE(cache.markIdIndex->find(3), nullptr);
    env.exec("DELETE FROM Mark WHERE ID=3"); // the Mark row only: FTS, RTree and MarkView rows stay
    ASSERT_EQ(env.rows("SELECT RowID FROM MarkFTS WHERE RowID=3").size(), 1);
    ASSERT_EQ(env.rows("SELECT RowID FROM MarkRTree WHERE RowID=3").size(), 1);

    OrmMarkRec r;
    EXPECT_FALSE(helper.readMarkSql(r, 3, 0, {}));
    EXPECT_EQ(cache.markIdIndex->find(3), nullptr);
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkFTS WHERE RowID=3").size(), 0);
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree WHERE RowID=3").size(), 0);
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|9|0;3|1|5|7|1;4|99|2|7|0")); // mark 3's row is gone
    // L-212: the by-category list is cleaned for the category of the NEXT index row (mark 5, category 7)
    int count = 0;
    const auto* ids = byCategory(helper, 7, count);
    ASSERT_NE(ids, nullptr);
    ASSERT_EQ(count, 2);
    EXPECT_EQ((*ids)[0], 2);
    EXPECT_EQ((*ids)[1], 5);
    EXPECT_EQ(describeAll(listOf(helper, 7, true, false, false), false, false),
              expectedText({2, 5}, false, false));
}

TEST(MarkDbImplORMHelperRead, PurgeOfTheLastIndexRowLeavesTheStaleIdInItsCategoryAndTheListSkipsIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    ASSERT_EQ(Access::fillPrepareMarkIdIndex(helper, 7), 3);
    env.exec("DELETE FROM Mark WHERE ID=5"); // the last row of the id index (L-212: category 0 is cleaned)
    OrmMarkRec r;
    EXPECT_FALSE(helper.readMarkSql(r, 5, 0, {}));
    int count = 0;
    const auto* ids = byCategory(helper, 7, count);
    ASSERT_NE(ids, nullptr);
    EXPECT_EQ(count, 3);                                                            // 2, 3 and the stale 5
    EXPECT_EQ(idsOf(listOf(helper, 7, true, false, false)), QStringLiteral("2,3")); // no index row: skipped
}

TEST(MarkDbImplORMHelperRead, ReadOnlyHelperDoesNotPurgeADanglingIndexEntry) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper(true);
    ASSERT_EQ(Access::fillPrepareMarkIdIndex(helper, 7), 3);
    env.exec("DELETE FROM Mark WHERE ID=3");
    OrmMarkRec r;
    EXPECT_FALSE(helper.readMarkSql(r, 3, 0, {}));
    EXPECT_NE(Access::cache(helper).markIdIndex->find(3), nullptr);
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkFTS WHERE RowID=3").size(), 1);
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree WHERE RowID=3").size(), 1);
}

TEST(MarkDbImplORMHelperRead, ViewRowIsReadBySqlForTheCurrentUserOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    ASSERT_FALSE(cache.markViewCache->isPrepared());
    OrmMarkRec hidden;
    ASSERT_TRUE(helper.readMarkSql(hidden, 1, 0, {}));
    EXPECT_EQ(hidden.viewId, 1);
    EXPECT_FALSE(hidden.visible);
    OrmMarkRec shown;
    ASSERT_TRUE(helper.readMarkSql(shown, 5, 0, {}));
    EXPECT_EQ(shown.viewId, 3);
    EXPECT_TRUE(shown.visible);
    // user 99 hides mark 2: not this user's row; no row of user 1: visible by default, view id 0
    OrmMarkRec none;
    ASSERT_TRUE(helper.readMarkSql(none, 2, 0, {}));
    EXPECT_EQ(none.viewId, 0);
    EXPECT_TRUE(none.visible);
    ASSERT_NE(cache.markViewCache->find(2), nullptr); // cached with view id 0
    EXPECT_EQ(cache.markViewCache->find(2)->viewId, 0);
    EXPECT_EQ(cache.markViewCache->find(2)->categoryId, 7);
}

TEST(MarkDbImplORMHelperRead, WholeTablePreparedViewCacheSkipsTheSqlLookupOfAMarkWithoutARow) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    ASSERT_EQ(listOf(helper, 0, true, false, false).size(), 6U);
    ASSERT_TRUE(Access::cache(helper).markViewCache->isPrepared());
    env.exec("INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (1,4,9,0)"); // hides mark 4
    OrmMarkRec stale;
    ASSERT_TRUE(helper.readMarkSql(stale, 4, 0, {}));
    EXPECT_TRUE(stale.visible); // legacy: IsPrepared and no row -> no S-36, visible by default
    EXPECT_EQ(stale.viewId, 0);
    auto cold = env.helper();
    OrmMarkRec fresh;
    ASSERT_TRUE(cold.readMarkSql(fresh, 4, 0, {}));
    EXPECT_FALSE(fresh.visible);
    EXPECT_EQ(fresh.viewId, 5);
}

TEST(MarkDbImplORMHelperRead, ReadOfAMarkWithZeroCategoryOrZeroAppearanceThrowsIdIsEmpty) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    env.exec("UPDATE Mark SET mCategory=0 WHERE ID=2");
    env.exec("UPDATE Mark SET mAppearance=0 WHERE ID=3");
    for (const int id : {2, 3}) {
        SCOPED_TRACE(id);
        auto helper = env.helper();
        OrmMarkRec r;
        try {
            std::ignore = helper.readMarkSql(r, id, 0, {});
            ADD_FAILURE() << "no throw";
        } catch (const MarkSystemORMError& e) {
            EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
        }
        EXPECT_EQ(Access::cache(helper).markIdIndex->find(id), nullptr); // nothing cached before the check
    }
}

TEST(MarkDbImplORMHelperRead, CorruptGeometryBlobThrowsInReadAndInAListWithGeometryOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    env.exec("UPDATE Mark SET mGeoWKB=X'FF' WHERE ID=2");
    auto helper = env.helper();
    OrmMarkRec r;
    EXPECT_THROW(std::ignore = helper.readMarkSql(r, 2, 0, {}), MarkSystemORMError);
    EXPECT_THROW(std::ignore = listOf(helper, 7, true, true, false), MarkSystemORMError);
    EXPECT_EQ(describeAll(listOf(helper, 7, true, false, false), false, false),
              expectedText({2, 3, 5}, false, false));
}

TEST(MarkDbImplORMHelperRead, DanglingPictureIdMakesAListWithAppearanceAndAReadThrow) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    env.exec("UPDATE Mark SET mImage=99 WHERE ID=1"); // L-213: an id of a row that is not there
    auto helper = env.helper();
    const std::vector<OrmMarkRec> plain = listOf(helper, 9, true, false, false);
    ASSERT_EQ(plain.size(), 2U);
    EXPECT_EQ(plain[0].picId, 99);
    EXPECT_EQ(plain[0].picName, QString());
    try {
        std::ignore = listOf(helper, 9, true, false, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
    OrmMarkRec r;
    EXPECT_THROW(std::ignore = helper.readMarkSql(r, 1, 0, {}), MarkSystemORMError);
}

// ---- R-2: the fill-prepare functions
// ------------------------------------------------------------------------

TEST(MarkDbImplORMHelperRead, IdIndexFillByCategoryCountsAndCachesAndIsNotAskedTwice) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 99), 0);
    int count = -1;
    ASSERT_NE(byCategory(helper, 99, count), nullptr); // an empty category counts as prepared
    EXPECT_EQ(count, 0);
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 7), 3);
    ASSERT_NE(cache.markIdIndex->find(5), nullptr);
    EXPECT_EQ(cache.markIdIndex->find(5)->categoryId, 7);
    EXPECT_EQ(cache.markIdIndex->find(5)->imageId, 1);
    EXPECT_EQ(cache.markIdIndex->find(5)->appearanceId, 2);
    env.exec("UPDATE Mark SET mCategory=99 WHERE ID=6");
    env.exec("DELETE FROM Mark WHERE ID=2");
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 99), 0); // answered by the by-category index
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 7), 3);
    EXPECT_FALSE(cache.markIdIndex->isPrepared());
    auto fresh = env.helper();
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(fresh, 99), 1); // a fresh helper sees the update
}

TEST(MarkDbImplORMHelperRead, IdIndexWholeTableFillRegistersAllButTheLastCategoryGroup) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 0), 6);
    EXPECT_TRUE(cache.markIdIndex->isPrepared());
    ASSERT_EQ(cache.markIdIndex->count(), 6);
    EXPECT_EQ(cache.markIdIndex->find(6)->categoryId, 11);
    int count = 0;
    const auto* seven = byCategory(helper, 7, count);
    ASSERT_NE(seven, nullptr);
    ASSERT_EQ(count, 3);
    EXPECT_EQ((*seven)[0], 2);
    EXPECT_EQ((*seven)[1], 3);
    EXPECT_EQ((*seven)[2], 5);
    const auto* nine = byCategory(helper, 9, count);
    ASSERT_NE(nine, nullptr);
    ASSERT_EQ(count, 2);
    EXPECT_EQ((*nine)[0], 1);
    EXPECT_EQ((*nine)[1], 4);
    // legacy never flushes the last group after its loop: category 11 (the highest) is not registered
    EXPECT_EQ(byCategory(helper, 11, count), nullptr);
    // ... so asking for it goes to SQL (S-3A) and still gets the mark
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 11), 1);
    EXPECT_EQ(idsOf(listOf(helper, 11, true, false, false)), QStringLiteral("6"));
    // the whole table is not asked twice
    env.exec("DELETE FROM Mark WHERE ID=6");
    EXPECT_EQ(Access::fillPrepareMarkIdIndex(helper, 0), 6);
}

TEST(MarkDbImplORMHelperRead, MarkCacheFillByCategoryAndWholeTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    Access::fillPrepareMarkIdCache(helper, 9);
    EXPECT_TRUE(cache.markCache->isCategoryPrepared(9));
    EXPECT_FALSE(cache.markCache->isCategoryPrepared(7));
    ASSERT_NE(cache.markCache->find(1), nullptr);
    EXPECT_EQ(cache.markCache->find(1)->name, QString::fromUtf8("Город"));
    EXPECT_EQ(cache.markCache->find(1)->desc, QString::fromUtf8("Описание"));
    EXPECT_EQ(cache.markCache->find(1)->geoType, OrmGeoType::Point);
    ASSERT_NE(cache.markCache->find(4), nullptr);
    EXPECT_EQ(cache.markCache->find(4)->geoType, OrmGeoType::Line);
    EXPECT_EQ(cache.markCache->find(4)->geoCount, 2);
    EXPECT_EQ(cache.markCache->find(2), nullptr);
    Access::fillPrepareMarkIdCache(helper, 0);
    EXPECT_TRUE(cache.markCache->isPrepared());
    ASSERT_NE(cache.markCache->find(3), nullptr);
    EXPECT_EQ(cache.markCache->find(3)->geoType, OrmGeoType::Poly);
    EXPECT_EQ(cache.markCache->find(6)->name, QString::fromUtf8("Одна линия"));
    env.exec("UPDATE Mark SET mName='x'");
    Access::fillPrepareMarkIdCache(helper, 0); // prepared: no second query
    EXPECT_EQ(cache.markCache->find(6)->name, QString::fromUtf8("Одна линия"));
}

TEST(MarkDbImplORMHelperRead, GeometryCacheFillTakesTheMetaOfTheMarksOfTheScope) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    Access::fillPrepareMarkGeometryCache(helper, 7);
    EXPECT_TRUE(cache.markGeometryCache->isCategoryPrepared(7));
    EXPECT_FALSE(cache.markGeometryCache->isCategoryPrepared(9));
    GeometryLonLatPtr g;
    ASSERT_TRUE(cache.markGeometryCache->find(2, g));
    EXPECT_TRUE(g->isSameGeometry(*makeLineWithElevation())); // the meta of mark 2 is applied
    EXPECT_FALSE(g->isSameGeometry(*makeLine()));
    ASSERT_TRUE(cache.markGeometryCache->find(3, g));
    EXPECT_TRUE(g->isSameGeometry(*makeWidePolygon()));
    ASSERT_TRUE(cache.markGeometryCache->find(5, g));
    EXPECT_TRUE(g->isSameGeometry(*makeOtherPoint()));
    EXPECT_FALSE(cache.markGeometryCache->find(1, g));
    EXPECT_FALSE(cache.markGeometryCache->find(6, g));

    Access::fillPrepareMarkGeometryCache(helper, 0);
    EXPECT_TRUE(cache.markGeometryCache->isPrepared());
    ASSERT_TRUE(cache.markGeometryCache->find(4, g));
    EXPECT_TRUE(g->isSameGeometry(*makeMultiLine()));
    ASSERT_TRUE(cache.markGeometryCache->find(6, g));
    EXPECT_TRUE(g->isSameGeometry(*makeLine()));
    ASSERT_TRUE(cache.markGeometryCache->find(2, g));
    EXPECT_TRUE(g->isSameGeometry(*makeLineWithElevation()));
}

TEST(MarkDbImplORMHelperRead, ViewCacheFillTakesTheRowsOfTheCurrentUserOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& cache = Access::cache(helper);
    Access::fillPrepareMarkViewCache(helper, 7);
    EXPECT_TRUE(cache.markViewCache->isCategoryPrepared(7));
    EXPECT_EQ(cache.markViewCache->find(1), nullptr); // category 9
    EXPECT_EQ(cache.markViewCache->find(2), nullptr); // only user 99 has a row
    ASSERT_NE(cache.markViewCache->find(3), nullptr);
    EXPECT_EQ(cache.markViewCache->find(3)->viewId, 2);
    EXPECT_EQ(cache.markViewCache->find(3)->categoryId, 7);
    EXPECT_FALSE(cache.markViewCache->find(3)->visible);
    ASSERT_NE(cache.markViewCache->find(5), nullptr);
    EXPECT_EQ(cache.markViewCache->find(5)->viewId, 3);
    EXPECT_TRUE(cache.markViewCache->find(5)->visible);

    Access::fillPrepareMarkViewCache(helper, 0);
    EXPECT_TRUE(cache.markViewCache->isPrepared());
    ASSERT_NE(cache.markViewCache->find(1), nullptr);
    EXPECT_EQ(cache.markViewCache->find(1)->viewId, 1);
    EXPECT_EQ(cache.markViewCache->find(1)->categoryId, 9); // from the column
    EXPECT_FALSE(cache.markViewCache->find(1)->visible);
    EXPECT_EQ(cache.markViewCache->find(2), nullptr);
}

// ---- L-158 / D-88 at helper level
// ----------------------------------------------------------------------------

TEST(MarkDbImplORMHelperRead, ListWithGeometryAfterTheGeometryCacheResetItselfStillReturnsEveryMark) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper(false, 0); // limit 0: the geometry array is over its limit at every add
    ASSERT_EQ(describeAll(listOf(helper, 7, true, true, true), true, true),
              expectedText({2, 3, 5}, true, true));
    ASSERT_TRUE(Access::cache(helper).markGeometryCache->isCategoryPrepared(7));
    // category 9 is prepared next: its add empties the array (category 7's geometries go with it)
    ASSERT_EQ(describeAll(listOf(helper, 9, true, true, true), true, true), expectedText({1, 4}, true, true));
    GeometryLonLatPtr g;
    ASSERT_FALSE(Access::cache(helper).markGeometryCache->find(2, g));
    // legacy: category 7 still counts as prepared, nothing refills it and the list is empty; fixed (D-88)
    EXPECT_FALSE(Access::cache(helper).markGeometryCache->isCategoryPrepared(7));
    EXPECT_EQ(describeAll(listOf(helper, 7, true, true, true), true, true),
              expectedText({2, 3, 5}, true, true));
    EXPECT_EQ(describeAll(listOf(helper, 7, false, true, false), true, false),
              expectedText({2, 5}, true, false));
}

TEST(MarkDbImplORMHelperRead, ListOfTheWholeTableAfterAForcedGeometryCacheResetStillReturnsEveryMark) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    auto& geometryCache = *Access::cache(helper).markGeometryCache;
    ASSERT_EQ(describeAll(listOf(helper, 0, true, true, false), true, false),
              expectedText({1, 2, 3, 4, 5, 6}, true, false));
    ASSERT_TRUE(geometryCache.isPrepared());
    geometryCache.reset(); // the forced reset
    GeometryLonLatPtr g;
    ASSERT_FALSE(geometryCache.find(1, g));
    EXPECT_EQ(describeAll(listOf(helper, 0, true, true, false), true, false),
              expectedText({1, 2, 3, 4, 5, 6}, true, false));
    EXPECT_EQ(describeAll(listOf(helper, 9, true, true, false), true, false),
              expectedText({1, 4}, true, false));
}

// ---- ordering
// ------------------------------------------------------------------------------------------------

TEST(MarkDbImplORMHelperRead, ListsComeInAscendingMarkIdWhateverTheInsertionOrderOfTheCategories) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    // The marks were inserted 9, 7, 7, 9, 7, 11. A category list is the by-category index in ascending id,
    // the whole table is the id index in ascending id; hidden marks only with includeHidden.
    auto helper = env.helper();
    EXPECT_EQ(idsOf(listOf(helper, 7, true, false, false)), QStringLiteral("2,3,5"));
    EXPECT_EQ(idsOf(listOf(helper, 9, true, false, false)), QStringLiteral("1,4"));
    EXPECT_EQ(idsOf(listOf(helper, 0, true, false, false)), QStringLiteral("1,2,3,4,5,6"));
    EXPECT_EQ(idsOf(listOf(helper, 0, false, false, false)), QStringLiteral("2,4,5,6"));
    // ids of marks added later (a mark inserted through the helper joins the prepared category in order)
    OrmMarkRec added = rec(QStringLiteral("late"), makePoint(), OrmGeoType::Point, 1, 7, true);
    ASSERT_TRUE(helper.insertMarkSql(added, true));
    EXPECT_EQ(idsOf(listOf(helper, 7, true, false, false)), QStringLiteral("2,3,5,7"));
    EXPECT_EQ(idsOf(listOf(helper, 0, true, false, false)), QStringLiteral("1,2,3,4,5,6,7"));
}

// ---- review round: malformed meta, category 0 walk, S-35 without a row, NULL visibility
// ----------------------

TEST(MarkDbImplORMHelperRead, MalformedMetaRowLoadsTheMarkWithoutMetaInReadAndInAListWithGeometry) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // legacy Parse returns nil for invalid JSON and for a missing geometry array
    // (u_GeometryMetaFromStreamJson.pas :97-100, :115-118): the mark loads without meta
    const char* const blobs[] = {"CAST('JSON{' AS BLOB)", "CAST('JSON{\"v\":1,\"t\":1}' AS BLOB)"};
    for (const char* blob : blobs) {
        SCOPED_TRACE(blob);
        Env env;
        seedSix(env);
        env.exec((QByteArray("INSERT INTO MarkMeta (mMark,mMeta) VALUES (6,") + blob + ")").constData());
        auto single = env.helper();
        EXPECT_EQ(describeAll(readOf(single, 6, 0, {}), true, true), expectedText({6}, true, true));
        auto list = env.helper();
        EXPECT_EQ(describeAll(listOf(list, 0, true, true, true), true, true),
                  expectedText({1, 2, 3, 4, 5, 6}, true, true));
        auto byCategory = env.helper();
        EXPECT_EQ(describeAll(listOf(byCategory, 11, true, true, false), true, false),
                  expectedText({6}, true, false));
    }
}

TEST(MarkDbImplORMHelperRead, WholeTableWalkLumpsMarksOfCategoryZeroIntoTheNextCategory) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    env.exec("UPDATE Mark SET mCategory=0 WHERE ID=1");
    auto helper = env.helper();
    ASSERT_EQ(listOf(helper, 0, true, false, false).size(), 6U);
    int count = 0;
    const auto* ids = byCategory(helper, 7, count);
    ASSERT_NE(ids, nullptr);
    ASSERT_EQ(count, 4);
    EXPECT_EQ((*ids)[0], 1); // the foreign mark: its category is 0
    // the list of category 7 then returns it (whole-table caches are prepared)
    const std::vector<OrmMarkRec> seven = listOf(helper, 7, true, false, false);
    EXPECT_EQ(idsOf(seven), QStringLiteral("1,2,3,5"));
    EXPECT_EQ(seven[0].categoryId, 0);
}

TEST(MarkDbImplORMHelperRead, GeometryRowDeletedOutsideAfterIndexAndMarkCacheHitThrowsNothingToRetrieve) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    auto helper = env.helper();
    ASSERT_EQ(listOf(helper, 7, true, false, false).size(), 3U); // index, mark and view caches; geometry cold
    env.exec("DELETE FROM Mark WHERE ID=2");
    OrmMarkRec r;
    try {
        std::ignore = helper.readMarkSql(r, 2, 0, {});
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Nothing to Retrieve!");
    }
}

TEST(MarkDbImplORMHelperRead, NullViewVisibilityIsHiddenInTheViewFill) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedSix(env);
    env.exec("INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (1,2,7,NULL)");
    auto helper = env.helper();
    const std::vector<OrmMarkRec> all = listOf(helper, 7, true, false, false);
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].markId, 2);
    EXPECT_EQ(all[0].viewId, 5);
    EXPECT_FALSE(all[0].visible);
    EXPECT_EQ(idsOf(listOf(helper, 7, false, false, false)), QStringLiteral("5"));
}
