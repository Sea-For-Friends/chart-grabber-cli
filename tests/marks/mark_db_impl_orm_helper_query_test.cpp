// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: nothing of its own (T-0111 has no feature id; contributes to the held-open SQLite mark database).
// Ports no legacy test (there is none): u_MarkDbImplORMHelper.pas :1753-1819 (_GetMarkIDArrayByRectSQL,
// S-41), :1968-2070 (GetMarkRecArrayByRect), :2072-2106 (_GetMarkRecArrayByTextSQLite3, S-43), :2159-2269
// (GetMarkRecArrayByText) and :2271-2279 (SetReadOnly), docs/specs/marks.md 2.9.9, 7.4. The data are written
// by the write half; every expectation is a literal or a plain-SQL read-back on a second connection.
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../corpus_skip.h"
#include "mark_db_impl_orm_helper_test_util.h"

using namespace cg::marks::testutil;
using cg::geo::DoublePoint;
using cg::geo::DoubleRect;
using cg::marks::MarkDbImplORMHelper;
using cg::marks::MarkDbImplORMHelperTestAccess;
using cg::marks::OrmGeoType;
using cg::marks::OrmMarkRec;

namespace {

using Access = MarkDbImplORMHelperTestAccess;

GeometryLonLatPtr lineOf(double lon1, double lat1, double lon2, double lat2) {
    const std::vector<LonLat> p{{lon1, lat1}, {lon2, lat2}};
    return kFactory.createLine(p, nullptr);
}

QString idsOf(const std::vector<OrmMarkRec>& recs, bool sorted) {
    std::vector<qint64> ids;
    for (const OrmMarkRec& r : recs) {
        ids.push_back(r.markId);
    }
    if (sorted) {
        std::sort(ids.begin(), ids.end());
    }
    QStringList text;
    for (const qint64 id : ids) {
        text.append(QString::number(id));
    }
    return text.join(QLatin1Char(','));
}

// Rectangle data set (id: bounding box lon / lat, category, visible):
//  1 point (10.5, 20.25)                      7
//  2 line  lon 10.5 .. 12.5, lat 20.25 .. 22.75   7   (extent 2 x 2.5)
//  3 poly  lon 1.5 .. 3.5, lat 2.5 .. 4.5     8   (extent 2 x 2)
//  4 line  lon 0 .. 10, lat 0 .. 10           9   (diagonal; extent 10 x 10)
//  5 point (-3.25, 47.5)                      7   hidden
//  6 line  lon -175 .. 175, lat 0 .. 5        10  (extent 350 x 5)
//  7 point (0.1, 0)                           7   (0.1 is not a 32-bit float)
//  8 line  lon 20 .. 25, lat 89.5 .. 90       11  (extent 5 x 0.5)
void seedRects(Env& env) {
    auto helper = env.helper();
    const auto add = [&](GeometryLonLatPtr geometry, OrmGeoType type, qint64 category, bool visible) {
        OrmMarkRec r = rec(QStringLiteral("m"), std::move(geometry), type, 1, category, visible);
        ASSERT_TRUE(helper.insertMarkSql(r, true));
    };
    add(kFactory.createPoint(LonLat{10.5, 20.25}), OrmGeoType::Point, 7, true);
    add(lineOf(10.5, 20.25, 12.5, 22.75), OrmGeoType::Line, 7, true);
    add(makePolygon(), OrmGeoType::Poly, 8, true);
    add(lineOf(0.0, 0.0, 10.0, 10.0), OrmGeoType::Line, 9, true);
    add(kFactory.createPoint(LonLat{-3.25, 47.5}), OrmGeoType::Point, 7, false);
    add(lineOf(-175.0, 0.0, 175.0, 5.0), OrmGeoType::Line, 10, true);
    add(kFactory.createPoint(LonLat{0.1, 0.0}), OrmGeoType::Point, 7, true);
    add(lineOf(20.0, 89.5, 25.0, 90.0), OrmGeoType::Line, 11, true);
}

struct Mode {
    const char* label;
    quint32 cacheMb;
    bool second; // the query is run once before, on the same helper
};
const Mode kModes[] = {{"cold", 100, false}, {"second run", 100, true}, {"cache size 0", 0, false}};

struct RectCase {
    const char* label;
    DoubleRect rect; // left, top, right, bottom
    DoublePoint size;
    std::vector<qint64> categories;
    bool hidden;
    const char* ids; // ascending: the order of the rows is not defined
};

} // namespace

TEST(MarkDbImplORMHelperQuery, RectangleQueryReturnsTheMarksWhoseBoundingBoxIntersectsTheRectangle) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedRects(env);
    // the R-tree as written (plain SQL): the box of mark 7 is rounded outward around 0.1 (a double that is
    // not a 32-bit float), the touching cases below depend on exact binary fractions
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree WHERE mLeft<0.1 AND mRight>0.1 AND RowID=7").size(), 1);
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree WHERE mLeft=10.5 AND mRight=12.5 AND mBottom=20.25 "
                       "AND mTop=22.75")
                  .size(),
              1);

    const RectCase cases[] = {
        {"contains two boxes", {10, 21, 11, 20}, {0, 0}, {}, false, "1,2"},
        {"crosses one box", {12, 30, 15, 22}, {0, 0}, {}, false, "2"},
        {"touches the corner of a box", {12.5, 30, 20, 22.75}, {0, 0}, {}, false, "2"},
        {"right edge touches the left edges", {5, 30, 10.5, 15}, {0, 0}, {}, false, "1,2"},
        {"left edge touches only the right edge", {12.5, 30, 20, 20}, {0, 0}, {}, false, "2"},
        {"top edge touches the bottom edges", {10, 20.25, 11, 15}, {0, 0}, {}, false, "1,2"},
        {"bottom edge touches only the top edge", {11, 30, 11.5, 22.75}, {0, 0}, {}, false, "2"},
        {"misses the corner by 1e-7", {12.5000001, 30, 20, 22.75}, {0, 0}, {}, false, ""},
        {"misses everything", {100, 60, 110, 50}, {0, 0}, {}, false, ""},
        {"inside a diagonal's box, away from the line", {8, 1, 9, 0}, {0, 0}, {}, false, "4,6"},
        {"the world", {-180, 90, 180, -90}, {0, 0}, {}, false, "1,2,3,4,6,7,8"},
        {"the world, hidden too", {-180, 90, 180, -90}, {0, 0}, {}, true, "1,2,3,4,5,6,7,8"},
        // no antimeridian handling: the rectangle is not split, only a box that spans it matches
        {"left > right (across the antimeridian)", {170, 10, -170, -10}, {0, 0}, {}, false, "6"},
        {"east of 174", {174, 10, 179, -10}, {0, 0}, {}, false, "6"},
        {"top edge at the pole touches a box", {-10, 90, 30, 90}, {0, 0}, {}, false, "8"},
        {"bottom edge at the pole touches a box", {-10, 95, 30, 90}, {0, 0}, {}, false, "8"},
        {"just below the pole box", {-10, 89.4, 30, 80}, {0, 0}, {}, false, ""},
        // 32-bit R-tree: the stored left bound of the point at 0.1 is rounded outward to a float below it,
        // which alone decides which right edges hit (no rounding of the compared value). How far below
        // depends on the SQLite version (T-0286: two float steps, 0.09999998658895493, on 3.53.4; one step,
        // 0.09999999403953552, on 3.54.0), so only edges outside both bounds are pinned: 0.099999997 hits,
        // 0.09999998 does not. (0.099999993 lies between the two bounds and was dropped.)
        {"right edge inside the rounded-out box of 0.1",
         {-1, 1, 0.099999997, -1},
         {0, 0},
         {},
         false,
         "4,6,7"},
        {"right edge a float step below the box of 0.1", {-1, 1, 0.09999998, -1}, {0, 0}, {}, false, "4,6"},
        // sizes: points always, a line or polygon when its lon OR lat size reaches the threshold
        {"size 3 x 3", {-180, 90, 180, -90}, {3, 3}, {}, false, "1,4,6,7,8"},
        {"size 3 x 2.2: lat of mark 2 (2.5)", {-180, 90, 180, -90}, {3, 2.2}, {}, false, "1,2,4,6,7,8"},
        {"size 1.9 x 9: lon of marks 2 and 3 (2)",
         {-180, 90, 180, -90},
         {1.9, 9},
         {},
         false,
         "1,2,3,4,6,7,8"},
        {"size 2 x 9: equal is enough", {-180, 90, 180, -90}, {2, 9}, {}, false, "1,2,3,4,6,7,8"},
        {"size 2.0000001 x 9: just over", {-180, 90, 180, -90}, {2.0000001, 9}, {}, false, "1,4,6,7,8"},
        // categories
        {"one category", {-180, 90, 180, -90}, {0, 0}, {7}, false, "1,2,7"},
        {"one category, hidden too", {-180, 90, 180, -90}, {0, 0}, {7}, true, "1,2,5,7"},
        {"category 0 is no filter", {-180, 90, 180, -90}, {0, 0}, {0}, false, "1,2,3,4,6,7,8"},
        {"negative category is no filter", {-180, 90, 180, -90}, {0, 0}, {-4}, false, "1,2,3,4,6,7,8"},
        {"no category given", {-180, 90, 180, -90}, {0, 0}, {}, false, "1,2,3,4,6,7,8"},
        {"two categories", {-180, 90, 180, -90}, {0, 0}, {9, 8}, false, "3,4"},
        {"four categories", {-180, 90, 180, -90}, {0, 0}, {11, 7, 9, 8}, false, "1,2,3,4,7,8"},
        {"category with no mark", {-180, 90, 180, -90}, {0, 0}, {55}, false, ""},
    };
    for (const RectCase& c : cases) {
        for (const Mode& mode : kModes) {
            SCOPED_TRACE(std::string(c.label) + " / " + mode.label);
            auto helper = env.helper(false, mode.cacheMb);
            std::vector<OrmMarkRec> out;
            std::vector<qint64> categories = c.categories;
            if (mode.second) {
                helper.getMarkRecArrayByRect(categories, c.rect, c.hidden, c.size, out);
            }
            categories = c.categories;
            const int count = helper.getMarkRecArrayByRect(categories, c.rect, c.hidden, c.size, out);
            EXPECT_EQ(idsOf(out, true), QString::fromLatin1(c.ids));
            EXPECT_EQ(count, static_cast<int>(out.size()));
        }
    }
}

TEST(MarkDbImplORMHelperQuery, ARectangleResultIsFilledLikeReadMarkSql) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedRects(env);
    auto helper = env.helper();
    std::vector<qint64> categories;
    std::vector<OrmMarkRec> out;
    ASSERT_EQ(helper.getMarkRecArrayByRect(categories, {10, 21, 11, 20}, false, {0, 0}, out), 2);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.markId < b.markId; });
    EXPECT_EQ(out[0].categoryId, 7);
    EXPECT_EQ(out[0].geoType, OrmGeoType::Point);
    EXPECT_EQ(out[0].name, QStringLiteral("m"));
    EXPECT_EQ(out[1].geoType, OrmGeoType::Line);
    EXPECT_EQ(out[1].categoryId, 7);
}

TEST(MarkDbImplORMHelperQuery, ManyCategoriesAreFilteredInMemoryAndTheListIsSortedInPlace) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedRects(env);
    const DoubleRect world{-180, 90, 180, -90};
    // 127 ids: an IN (...) clause, the caller's list is left as it is
    std::vector<qint64> few{11, 8};
    for (qint64 junk = 1124; junk >= 1000; --junk) {
        few.push_back(junk);
    }
    ASSERT_EQ(few.size(), 127U);
    const std::vector<qint64> fewBefore = few;
    // 128 ids: no clause, the rows are filtered against the sorted list
    std::vector<qint64> many{11, 8};
    for (qint64 junk = 1125; junk >= 1000; --junk) {
        many.push_back(junk);
    }
    ASSERT_EQ(many.size(), 128U);
    for (const Mode& mode : kModes) {
        SCOPED_TRACE(mode.label);
        auto helper = env.helper(false, mode.cacheMb);
        std::vector<OrmMarkRec> out;
        std::vector<qint64> list = few;
        helper.getMarkRecArrayByRect(list, world, false, {0, 0}, out);
        EXPECT_EQ(idsOf(out, true), QStringLiteral("3,8"));
        EXPECT_EQ(list, fewBefore);

        list = many;
        helper.getMarkRecArrayByRect(list, world, false, {0, 0}, out);
        EXPECT_EQ(idsOf(out, true), QStringLiteral("3,8"));
        EXPECT_TRUE(std::is_sorted(list.begin(), list.end()));
        EXPECT_EQ(list.front(), 8);
        EXPECT_EQ(list.back(), 1125);
    }
    // the filter really is the list: with 128 ids that name category 7 only, the other categories are out
    std::vector<qint64> seven{7};
    for (qint64 junk = 2000; junk < 2127; ++junk) {
        seven.push_back(junk);
    }
    ASSERT_EQ(seven.size(), 128U);
    auto helper = env.helper();
    std::vector<OrmMarkRec> out;
    helper.getMarkRecArrayByRect(seven, world, true, {0, 0}, out);
    EXPECT_EQ(idsOf(out, true), QStringLiteral("1,2,5,7"));
}

TEST(MarkDbImplORMHelperQuery, ARectangleQueryOnAnEmptyDatabaseOrWithAMarkRowGoneIsEmpty) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    std::vector<qint64> categories;
    std::vector<OrmMarkRec> out(3);
    EXPECT_EQ(helper.getMarkRecArrayByRect(categories, {-180, 90, 180, -90}, true, {0, 0}, out), 0);
    EXPECT_TRUE(out.empty());
    // an R-tree row without its mark row is not in the join
    env.exec("INSERT INTO MarkRTree (RowID,mLeft,mRight,mBottom,mTop) VALUES (9,1,2,1,2)");
    EXPECT_EQ(helper.getMarkRecArrayByRect(categories, {-180, 90, 180, -90}, true, {0, 0}, out), 0);
}

namespace {

// Text data set (id: name / description, all in category 7; everything is stored lower-cased by the write
// half):
//  1 "Город" / "Центр"        2 "Москва Город" / "Capital City"   3 "O'Brien" / "Irish Pub"
//  4 "Alpha Beta" / "Gamma Delta"   5 "Alphabet" / "ГОРОД" (hidden)   6 "Delta" / "Alpha"   7 "«Город»" / ""
//  8 "Saint-Petersburg" / ""
void seedTexts(Env& env) {
    auto helper = env.helper();
    const struct {
        const char* name;
        const char* desc;
        bool visible;
    } marks[] = {{"Город", "Центр", true},       {"Москва Город", "Capital City", true},
                 {"O'Brien", "Irish Pub", true}, {"Alpha Beta", "Gamma Delta", true},
                 {"Alphabet", "ГОРОД", false},   {"Delta", "Alpha", true},
                 {"«Город»", "", true},          {"Saint-Petersburg", "", true}};
    for (const auto& m : marks) {
        OrmMarkRec r = rec(QString::fromUtf8(m.name), makePoint(), OrmGeoType::Point, 1, 7, m.visible);
        r.desc = QString::fromUtf8(m.desc);
        ASSERT_TRUE(helper.insertMarkSql(r, true));
    }
}

struct TextCase {
    const char* label;
    const char* text;
    int maxCount;
    bool hidden;
    bool inDesc;
    const char* ids; // in the order the helper returns them
};

// OWNER ruling 2026-10-22: the enhanced full-text syntax (parentheses, DV-9) is not needed on Windows, where
// the MSYS2 SQLite is built without SQLITE_ENABLE_FTS3_PARENTHESIS. Checked at run time, not by operating
// system.
bool ftsParenthesisAvailable(Env& env) {
    return env.rows("SELECT sqlite_compileoption_used('ENABLE_FTS3_PARENTHESIS')").value(0) ==
           QStringLiteral("1");
}

} // namespace

TEST(MarkDbImplORMHelperQuery, TextQuerySearchesNamesAndDescriptionsAsAnFtsQueryOfTheLoweredText) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    if (!ftsParenthesisAvailable(env)) {
        GTEST_SKIP()
            << "owner ruling 2026-10-22: the enhanced full-text syntax is not needed on Windows; this "
               "SQLite is built without ENABLE_FTS3_PARENTHESIS";
    }
    seedTexts(env);
    // the statement's input: the write half stored lower-cased text, also non-ASCII
    EXPECT_EQ(env.rows("SELECT mName FROM MarkFTS WHERE RowID=1").value(0), QString::fromUtf8("город"));
    EXPECT_EQ(env.rows("SELECT mDesc FROM MarkFTS WHERE RowID=5").value(0), QString::fromUtf8("город"));

    const TextCase cases[] = {
        {"name only, Cyrillic", "город", 0, false, false, "1,2"},
        {"name only, upper case Cyrillic", "ГОРОД", 0, false, false, "1,2"},
        {"mixed case Cyrillic", "ГоРоД", 0, false, false, "1,2"},
        {"name + description: merged, sorted, hidden dropped", "ГОРОД", 0, false, true, "1,2"},
        {"name + description, hidden too", "ГОРОД", 0, true, true, "1,2,5"},
        {"mixed case Latin, name + description", "ALPHA", 0, false, true, "4,6"},
        {"a word is not a prefix", "alpha", 0, true, false, "4"},
        {"prefix", "alpha*", 0, true, false, "4,5"},
        {"prefix, name + description, hidden dropped", "alpha*", 0, false, true, "4,6"},
        {"prefix, name + description, hidden too", "alpha*", 0, true, true, "4,5,6"},
        {"limit 1: the name hits fill it, no description search", "alpha*", 1, true, true, "4"},
        {"limit 2: two name hits, no description search", "alpha*", 2, true, true, "4,5"},
        {"limit 3: description search with limit 3", "alpha*", 3, true, true, "4,5,6"},
        {"limit 2, one name hit: description search follows", "delta", 2, true, true, "4,6"},
        {"limit 1, one name hit: it is full", "delta", 1, true, true, "6"},
        {"negative limit is no limit", "alpha*", -5, true, true, "4,5,6"},
        {"description only", "capital city", 0, false, true, "2"},
        {"description is not searched when not asked", "capital city", 0, false, false, ""},
        {"punctuation is dropped by the tokenizer", "city,", 0, false, true, "2"},
        {"a phrase", "\"alpha beta\"", 0, false, false, "4"},
        {"a column filter inside the query", "mdesc:alpha", 0, false, false, "6"},
        // the apostrophe: legacy concatenated it into the statement (syntax error, no result)
        {"apostrophe", "O'Brien", 0, false, false, "3"},
        {"crafted quote text", "x' OR mDesc MATCH 'y", 0, false, true, ""},
        {"crafted quote text 2", "alpha' OR 1=1 OR 'a", 0, false, true, ""},
        // operators: the text is lower-cased before it is sent, AND / OR / NOT / NEAR are words afterwards
        {"AND is a word", "alpha AND beta", 0, false, false, ""},
        {"OR is a word", "alpha OR delta", 0, false, false, ""},
        {"NOT is a word", "alpha NOT beta", 0, false, false, ""},
        // DV-9: the system SQLite has the enhanced syntax, "-beta" is the term beta, not an exclusion
        {"minus", "alpha -beta", 0, false, false, "4"},
        // DV-9, user decision pending: the port finds a hyphenated name by its own text; legacy's standard
        // syntax reads "saint -petersburg" as "saint and not petersburg" (not run on Windows)
        {"hyphenated name", "saint-petersburg", 0, false, false, "8"},
        // DV-9, user decision pending: port = syntax error, no result; legacy reads "a AND b"
        {"unbalanced parenthesis", "(alpha beta", 0, false, false, ""},
        {"parentheses", "(alpha beta)", 0, false, false, "4"},
        // the tokenizer: bytes >= 0x80 are token characters, so the guillemets belong to the token
        {"guillemets are part of the token", "«город»", 0, false, false, "7"},
        // queries that give nothing
        {"empty", "", 0, true, true, ""},
        {"blanks", "   ", 0, true, true, ""},
        {"punctuation only", "!!!", 0, true, true, ""},
        {"star only", "*", 0, true, true, ""},
        {"unbalanced quote: SQLite rejects the expression", "\"alpha", 0, true, true, ""},
        {"unbalanced quote after a good word", "alpha \"beta", 0, true, true, ""},
    };
    for (const TextCase& c : cases) {
        for (const Mode& mode : kModes) {
            SCOPED_TRACE(std::string(c.label) + " / " + mode.label);
            auto helper = env.helper(false, mode.cacheMb);
            std::vector<OrmMarkRec> out;
            if (mode.second) {
                helper.getMarkRecArrayByText(QString::fromUtf8(c.text), c.maxCount, c.hidden, c.inDesc, out);
            }
            const int count =
                helper.getMarkRecArrayByText(QString::fromUtf8(c.text), c.maxCount, c.hidden, c.inDesc, out);
            EXPECT_EQ(idsOf(out, false), QString::fromLatin1(c.ids));
            EXPECT_EQ(count, static_cast<int>(out.size()));
        }
    }
}

TEST(MarkDbImplORMHelperQuery, TheFtsTableOfTheDdlIsFts4WithTheSimpleTokenizer) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // FINDING (T-0111): the tokenizer the DDL names is the system SQLite's own "simple" tokenizer; it folds
    // ASCII letters only and treats every byte >= 0x80 as a letter. Pinned here as the port's behaviour.
    Env env;
    if (!ftsParenthesisAvailable(env)) {
        GTEST_SKIP()
            << "owner ruling 2026-10-22: the enhanced full-text syntax is not needed on Windows; this "
               "SQLite is built without ENABLE_FTS3_PARENTHESIS";
    }
    seedTexts(env);
    // the stored text, not lower-cased (plain SQL, past the write half)
    env.exec("UPDATE MarkFTS SET mName='Upper Case' WHERE RowID=6");
    env.exec("UPDATE MarkFTS SET mName='ГОРОД Alpha' WHERE RowID=4");
    auto helper = env.helper();
    std::vector<OrmMarkRec> out;
    // ASCII upper case in the index is found by a lower-cased query
    helper.getMarkRecArrayByText(QStringLiteral("UPPER"), 0, true, false, out);
    EXPECT_EQ(idsOf(out, false), QStringLiteral("6"));
    // Cyrillic upper case in the index is not found: the tokenizer does not fold it
    helper.getMarkRecArrayByText(QString::fromUtf8("город"), 0, true, false, out);
    EXPECT_EQ(idsOf(out, false), QStringLiteral("1,2"));
    // ... and the upper case token is only found by the same upper case text, which the helper never sends
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkFTS WHERE mName MATCH 'ГОРОД'").join(QLatin1Char(',')),
              QStringLiteral("4"));
    helper.getMarkRecArrayByText(QString::fromUtf8("ГОРОД"), 0, true, false, out);
    EXPECT_EQ(idsOf(out, false), QStringLiteral("1,2"));
    EXPECT_EQ(env.rows("SELECT sqlite_compileoption_used('ENABLE_FTS3_PARENTHESIS')").value(0),
              QStringLiteral("1"));
}

TEST(MarkDbImplORMHelperQuery, TextQueryWithAMarkRowGoneFromTheMarkTableSkipsIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedTexts(env);
    env.exec("DELETE FROM Mark WHERE ID=1");
    auto helper = env.helper();
    std::vector<OrmMarkRec> out;
    EXPECT_EQ(helper.getMarkRecArrayByText(QString::fromUtf8("город"), 0, true, false, out), 1);
    EXPECT_EQ(idsOf(out, false), QStringLiteral("2"));
}

TEST(MarkDbImplORMHelperQuery, SetReadOnlyDropsTheViewCacheOnlyWhenLeavingReadOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedTexts(env); // all in category 7; mark 5 is hidden (view row)
    const auto visibleIds = [](MarkDbImplORMHelper& h) {
        std::vector<OrmMarkRec> out;
        h.getMarkRecArray(7, false, false, false, out);
        return idsOf(out, false);
    };
    auto helper = env.helper(true);
    EXPECT_EQ(visibleIds(helper), QStringLiteral("1,2,3,4,6,7,8"));
    ASSERT_TRUE(helper.setMarksInCategoryVisibleSql(7, false)); // memory only: read-only
    EXPECT_EQ(visibleIds(helper), QStringLiteral(""));
    ASSERT_NE(Access::cache(helper).markViewCache->find(1), nullptr);
    EXPECT_EQ(env.rows("SELECT COUNT(*) FROM MarkView WHERE mvVisible=0").value(0), QStringLiteral("1"));

    helper.setReadOnly(true); // same value: nothing happens
    EXPECT_TRUE(helper.isReadOnly());
    EXPECT_NE(Access::cache(helper).markViewCache->find(1), nullptr);
    EXPECT_EQ(visibleIds(helper), QStringLiteral(""));

    helper.setReadOnly(false); // leaving read-only: the fake view rows are gone
    EXPECT_FALSE(helper.isReadOnly());
    EXPECT_EQ(Access::cache(helper).markViewCache->find(1), nullptr);
    EXPECT_FALSE(Access::cache(helper).markViewCache->isPrepared());
    EXPECT_EQ(visibleIds(helper), QStringLiteral("1,2,3,4,6,7,8")); // what the database says

    // writable -> writable and writable -> read-only keep the cache (mark 5 has a view row of its own)
    ASSERT_NE(Access::cache(helper).markViewCache->find(5), nullptr);
    helper.setReadOnly(false);
    EXPECT_NE(Access::cache(helper).markViewCache->find(5), nullptr);
    helper.setReadOnly(true);
    EXPECT_TRUE(helper.isReadOnly());
    EXPECT_NE(Access::cache(helper).markViewCache->find(5), nullptr);
}
