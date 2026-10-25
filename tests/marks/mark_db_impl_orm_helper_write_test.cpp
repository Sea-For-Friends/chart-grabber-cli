// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034 (held open). Ports no legacy test (there is none): the write half of
// u_MarkDbImplORMHelper.pas :230-774 against a copy of tests/fixtures/marks/Marks.db3 (docs/specs/marks.md
// §2.9.8 W-1, W-3, W-4; statements S-20 ... S-2D, S-40, S-42). Expected values are literals read back with
// plain SQL on a second connection, never through the helper's caches (except where a test is about a cache).
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"
#include "cg/marks/mormot/system_nocase.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <tuple>
#include <vector>

#include "../corpus_skip.h"
#include "fixture_path.h"
#include "mark_db_impl_orm_helper_test_util.h"

using namespace cg::marks::testutil;
using cg::marks::MarkDbImplORMHelper;
using cg::marks::MarkDbImplORMHelperTestAccess;
using cg::marks::MarkSystemImplConfigORM;
using cg::marks::MarkSystemImplORMClientProvider;
using cg::marks::MarkSystemORMError;
using cg::marks::OrmGeoType;
using cg::marks::OrmMarkRec;

// ---- insert (W-1, S-20 ... S-2D) ---------------------------------------------------------------------

namespace {
struct InsertCase {
    const char* label;
    GeometryLonLatPtr (*make)();
    OrmGeoType type;
    int geoCount;
    bool visible;
    QString name;
    QString desc;
    QString pic;
    const char* mark;  // Mark row after the leading "1|7|"
    const char* rtree; // MarkRTree row
    const char* view;  // MarkView rows, "" = none
    const char* meta;  // MarkMeta rows, "" = none
    const char* image; // MarkImage rows, "" = none
    const char* fts;
};
} // namespace

TEST(MarkDbImplORMHelperWrite, InsertTable) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const InsertCase cases[] = {
        {"point, no picture, visible",
         makePoint,
         OrmGeoType::Point,
         1,
         true,
         QStringLiteral("Point A"),
         QStringLiteral("d"),
         {},
         "1|7|0|1|Point A|d|1|1|0|0|blob|010100000000000000000025400000000000403440",
         "1|10.5|10.5|20.25|20.25",
         "",
         "",
         "",
         "1|point a|d"},
        {"point, picture, hidden, non-ASCII", makePoint, OrmGeoType::Point, 1, false, kTown,
         QString::fromUtf8("Описание Ё"), QString::fromUtf8("Иконка.PNG"),
         "1|7|1|1|Город|Описание Ё|1|1|0|0|blob|010100000000000000000025400000000000403440",
         "1|10.5|10.5|20.25|20.25", "1|1|1|7|0", "", "1|Иконка.PNG", "1|город|описание ё"},
        {"line, visible",
         makeLine,
         OrmGeoType::Line,
         1,
         true,
         QStringLiteral("Line"),
         {},
         {},
         "1|7|0|1|Line||2|1|11930464|14913080|blob|"
         "0102000000020000000000000000002540000000000040344000000000000029400000000000c03640",
         "1|10.5|12.5|20.25|22.75",
         "",
         "",
         "",
         "1|line|"},
        {"line with elevation, hidden",
         makeLineWithElevation,
         OrmGeoType::Line,
         1,
         false,
         QStringLiteral("Hill"),
         QStringLiteral("D"),
         {},
         "1|7|0|1|Hill|D|2|1|11930464|14913080|blob|"
         "0102000000020000000000000000002540000000000040344000000000000029400000000000c03640",
         "1|10.5|12.5|20.25|22.75",
         "1|1|1|7|0",
         "1|1|JSON{\"v\":1,\"t\":1,\"g\":[{\"c\":2,\"m\":[{\"t\":1,\"n\":\"ele\",\"d\":"
         "\"AAAAAAAgWUAAAAAAAAhpQA==\"}]}]}",
         "",
         "1|hill|d"},
        {"line with elevation and picture, visible",
         makeLineWithElevation,
         OrmGeoType::Line,
         1,
         true,
         QStringLiteral("Both"),
         {},
         QStringLiteral("both.png"),
         "1|7|1|1|Both||2|1|11930464|14913080|blob|"
         "0102000000020000000000000000002540000000000040344000000000000029400000000000c03640",
         "1|10.5|12.5|20.25|22.75",
         "",
         "1|1|JSON{\"v\":1,\"t\":1,\"g\":[{\"c\":2,\"m\":[{\"t\":1,\"n\":\"ele\",\"d\":"
         "\"AAAAAAAgWUAAAAAAAAhpQA==\"}]}]}",
         "1|both.png",
         "1|both|"},
        {"polygon with picture, visible", makePolygon, OrmGeoType::Poly, 1, true, QStringLiteral("Poly"),
         QStringLiteral("x"), QStringLiteral("p.png"),
         "1|7|1|1|Poly|x|3|1|11930464|11930464|blob|"
         "01030000000100000004000000000000000000f83f00000000000004400000000000000c40000000000000044000000000"
         "00000c400000000000001240000000000000f83f0000000000000440",
         "1|1.5|3.5|2.5|4.5", "", "", "1|p.png", "1|poly|x"},
        {"multi-line of two, hidden",
         makeMultiLine,
         OrmGeoType::Line,
         2,
         false,
         QStringLiteral("Multi"),
         {},
         {},
         "1|7|0|1|Multi||2|2|29826160|35791392|blob|"
         "01050000000200000001020000000200000000000000000000000000000000000000000000000000f03f000000000000004"
         "00102000000020000000000000000000840000000000000104000000000000014400000000000001840",
         "1|0.0|5.0|0.0|6.0",
         "1|1|1|7|0",
         "",
         "",
         "1|multi|"},
    };
    for (const InsertCase& c : cases) {
        SCOPED_TRACE(c.label);
        Env env;
        auto helper = env.helper();
        OrmMarkRec r = rec(c.name, c.make(), c.type, c.geoCount, 7, c.visible, c.pic);
        r.desc = c.desc;
        r.markId = 55; // reset to 0 first, then set by the insert
        r.picId = 99;
        r.appearanceId = 99;
        ASSERT_TRUE(helper.insertMarkSql(r, true));
        EXPECT_EQ(r.markId, 1);
        EXPECT_EQ(r.picId, c.pic.isEmpty() ? 0 : 1);
        EXPECT_EQ(r.appearanceId, 1);
        EXPECT_EQ(env.marks(), QStringList{QString::fromUtf8(c.mark)});
        EXPECT_EQ(env.appearances(), QStringList{QStringLiteral("1|4279312947|4294967295|12|-3")});
        EXPECT_EQ(env.rtree(), QStringList{QString::fromUtf8(c.rtree)});
        EXPECT_EQ(env.fts(), QStringList{QString::fromUtf8(c.fts)});
        EXPECT_EQ(env.views(), QString(c.view).isEmpty() ? QStringList{} : QStringList{c.view});
        EXPECT_EQ(r.viewId, c.visible ? 0 : 1);
        EXPECT_EQ(env.metas(),
                  QString(c.meta).isEmpty() ? QStringList{} : QStringList{QString::fromUtf8(c.meta)});
        EXPECT_EQ(env.images(),
                  QString(c.image).isEmpty() ? QStringList{} : QStringList{QString::fromUtf8(c.image)});
    }
}

TEST(MarkDbImplORMHelperWrite, InsertWithoutTransactionRunsInTheCallersAndRollsBackWithIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    auto& transaction = env.provider().transaction();
    auto tr = transaction.start(false);
    OrmMarkRec r = rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point);
    ASSERT_TRUE(helper.insertMarkSql(r, false));
    // the caller's transaction is open: the plain connection sees nothing yet
    EXPECT_EQ(env.count("Mark"), 0);
    transaction.rollBack(tr);
    EXPECT_EQ(env.count("Mark"), 0);
    EXPECT_EQ(env.count("MarkFTS"), 0);
    EXPECT_EQ(env.count("MarkRTree"), 0);
    EXPECT_EQ(env.count("MarkImage"), 0);
    EXPECT_EQ(env.count("MarkAppearance"), 0);
}

TEST(MarkDbImplORMHelperWrite, InsertWithTransactionCommits) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec r = rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point);
    ASSERT_TRUE(helper.insertMarkSql(r, true));
    EXPECT_EQ(env.count("Mark"), 1);
}

TEST(MarkDbImplORMHelperWrite, ReadOnlyHelperWritesNothingAndLeavesTheRecord) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper(true);
    OrmMarkRec r = rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point);
    r.markId = 77;
    EXPECT_FALSE(helper.insertMarkSql(r, true));
    EXPECT_EQ(r.markId, 77);
    EXPECT_FALSE(helper.deleteMarkSql(5, true));
    EXPECT_FALSE(helper.deleteMarkSql(std::vector<qint64>{5}));
    EXPECT_TRUE(helper.isReadOnly());
    EXPECT_EQ(env.count("Mark"), 0);
    EXPECT_EQ(env.count("MarkAppearance"), 0);
}

TEST(MarkDbImplORMHelperWrite, NullGeometryThrowsBeforeTouchingAnything) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec r = rec(QStringLiteral("a"), nullptr, OrmGeoType::Point);
    r.markId = 77;
    EXPECT_THROW(std::ignore = helper.insertMarkSql(r, true), MarkSystemORMError);
    EXPECT_EQ(r.markId, 0);
    EXPECT_EQ(env.count("Mark"), 0);
    EXPECT_EQ(env.count("MarkAppearance"), 0);
}

TEST(MarkDbImplORMHelperWrite, InsertFillsTheCachesAsLegacy) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec r = rec(kTown, makeLineWithElevation(), OrmGeoType::Line, 1, 7, false);
    r.desc = QStringLiteral("dd");
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    cache.markIdByCategoryIndex->addPrepared(7, {}, 0, 0); // category 7 prepared, 9 is not
    ASSERT_TRUE(helper.insertMarkSql(r, true));
    OrmMarkRec other = rec(QStringLiteral("o"), makePoint(), OrmGeoType::Point, 1, 9);
    ASSERT_TRUE(helper.insertMarkSql(other, true));
    const auto* mark = cache.markCache->find(1);
    ASSERT_NE(mark, nullptr);
    EXPECT_EQ(mark->name, kTown);
    EXPECT_EQ(mark->desc, QStringLiteral("dd"));
    EXPECT_EQ(mark->geoType, OrmGeoType::Line);
    const auto* index = cache.markIdIndex->find(1);
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(index->categoryId, 7);
    EXPECT_EQ(index->imageId, 0);
    EXPECT_EQ(index->appearanceId, 1);
    const auto* view = cache.markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewId, 1);
    EXPECT_FALSE(view->visible);
    GeometryLonLatPtr geometry;
    ASSERT_TRUE(cache.markGeometryCache->find(1, geometry));
    EXPECT_EQ(geometry, r.geometry);
    const std::vector<qint64>* ids = nullptr;
    int count = 0;
    ASSERT_TRUE(cache.markIdByCategoryIndex->find(7, ids, count));
    ASSERT_EQ(count, 1);
    EXPECT_EQ((*ids)[0], 1);
    // an unprepared category gets no array from an insert (TOrmMarkIdByCategoryIndex.Add, :729-745)
    EXPECT_FALSE(cache.markIdByCategoryIndex->find(9, ids, count));
}

TEST(MarkDbImplORMHelperWrite, FtsRowFailureThrowsAndRollsBackTechLeadRuling) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // Tech Lead ruling 2026-10-13 (user may overturn): a dangling MarkFTS row with the id of the next mark
    // makes S-24 fail. Legacy would NOT fail (forced-id Add seeds its result from the sent id,
    // mormot.orm.sqlite3.pas :1431, :1468-1469, :1805-1809) and would commit the mark without its FTS row;
    // the port throws and the helper (useTransactions) rolls everything back. Caches keep the entry (legacy
    // :764-771 does not undo them).
    Env env;
    env.exec("INSERT INTO MarkFTS (RowID,mName,mDesc) VALUES (1,'x','y')");
    auto helper = env.helper();
    OrmMarkRec r =
        rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point, 1, 7, true, QStringLiteral("i.png"));
    EXPECT_THROW(std::ignore = helper.insertMarkSql(r, true), MarkSystemORMError);
    EXPECT_EQ(env.count("Mark"), 0);
    EXPECT_EQ(env.count("MarkImage"), 0);
    EXPECT_EQ(env.count("MarkAppearance"), 0);
    EXPECT_EQ(env.count("MarkRTree"), 0);
    EXPECT_EQ(env.fts(), QStringList{QStringLiteral("1|x|y")});
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    EXPECT_NE(cache.markCache->find(1), nullptr);
    EXPECT_NE(cache.markIdIndex->find(1), nullptr);
    EXPECT_NE(cache.markImageCache->find(QStringLiteral("i.png")), nullptr);
    EXPECT_EQ(r.markId, 1);

    // the failed transaction was rolled back, not left open: the database is writable at once, and the next
    // insert (cached picture id and all) runs in a transaction of its own that commits
    env.exec("DELETE FROM MarkFTS");
    OrmMarkRec next = rec(QStringLiteral("b"), makePoint(), OrmGeoType::Point);
    ASSERT_TRUE(helper.insertMarkSql(next, true));
    EXPECT_EQ(next.markId, 1); // the rolled-back AUTOINCREMENT value is reused
    EXPECT_EQ(env.count("Mark"), 1);
}

TEST(MarkDbImplORMHelperWrite, FailedMetaInsertRollsBackAndTheStaleCachesGiveTheNextMarkDanglingIds) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // S-2D is checked by legacy (CheckID, :756): a MarkMeta row that already exists for the id (unique mMark)
    // makes it return 0. The helper rolls back because useTransactions is true; with false the caller does
    // (u_MarkDbImplORM.pas:717-719). The caches written before the failure stay (L2, spec candidate LC-17).
    Env env;
    env.exec("INSERT INTO MarkMeta (mMark,mMeta) VALUES (1,x'00')");
    auto helper = env.helper();
    OrmMarkRec r = rec(QStringLiteral("a"), makeLineWithElevation(), OrmGeoType::Line, 1, 7, false,
                       QStringLiteral("i.png"));
    try {
        std::ignore = helper.insertMarkSql(r, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
    EXPECT_EQ(env.count("Mark"), 0);
    EXPECT_EQ(env.count("MarkFTS"), 0);
    EXPECT_EQ(env.count("MarkRTree"), 0);
    EXPECT_EQ(env.count("MarkView"), 0);
    EXPECT_EQ(env.count("MarkImage"), 0);
    EXPECT_EQ(env.count("MarkAppearance"), 0);
    EXPECT_EQ(env.count("MarkMeta"), 1); // only the seeded row
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    EXPECT_NE(cache.markCache->find(1), nullptr);
    EXPECT_NE(cache.markViewCache->find(1), nullptr);
    EXPECT_NE(cache.markImageCache->find(QStringLiteral("i.png")), nullptr);

    // consequence: the next mark with the same picture and look reuses the rolled-back ids, whose rows are
    // gone
    env.exec("DELETE FROM MarkMeta");
    OrmMarkRec next =
        rec(QStringLiteral("b"), makePoint(), OrmGeoType::Point, 1, 7, true, QStringLiteral("I.PNG"));
    ASSERT_TRUE(helper.insertMarkSql(next, true));
    EXPECT_EQ(env.rows("SELECT ID,mImage,mAppearance FROM Mark"), QStringList{QStringLiteral("1|1|1")});
    EXPECT_EQ(env.count("MarkImage"), 0);
    EXPECT_EQ(env.count("MarkAppearance"), 0);
}

// ---- picture and appearance de-duplication (S-20, S-21, S-28, S-29) -------------------------------

TEST(MarkDbImplORMHelperWrite, ImagesAndAppearancesAreSharedAndLookedUpNoCase) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec a =
        rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point, 1, 7, true, QStringLiteral("icon.png"));
    OrmMarkRec b =
        rec(QStringLiteral("b"), makePoint(), OrmGeoType::Point, 1, 7, true, QStringLiteral("other.png"));
    OrmMarkRec c =
        rec(QStringLiteral("c"), makePoint(), OrmGeoType::Point, 1, 7, true, QStringLiteral("icon.png"));
    c.scale1 = 13; // another appearance
    ASSERT_TRUE(helper.insertMarkSql(a, true));
    ASSERT_TRUE(helper.insertMarkSql(b, true));
    ASSERT_TRUE(helper.insertMarkSql(c, true));
    EXPECT_EQ(env.images(), (QStringList{"1|icon.png", "2|other.png"}));
    EXPECT_EQ(env.appearances(),
              (QStringList{"1|4279312947|4294967295|12|-3", "2|4279312947|4294967295|13|-3"}));
    EXPECT_EQ(env.rows("SELECT ID,mImage,mAppearance FROM Mark ORDER BY ID"),
              (QStringList{"1|1|1", "2|2|1", "3|1|2"}));
}

TEST(MarkDbImplORMHelperWrite, ColdCacheFindsTheStoredRowsBySql) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // a fresh helper has empty caches: S-20 and S-28 answer, the lookup is SYSTEMNOCASE
    Env env;
    {
        auto first = env.helper();
        OrmMarkRec a = rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point, 1, 7, true,
                           QString::fromUtf8("Café.png"));
        ASSERT_TRUE(first.insertMarkSql(a, true));
    }
    auto second = env.helper();
    OrmMarkRec b =
        rec(QStringLiteral("b"), makePoint(), OrmGeoType::Point, 1, 7, true, QString::fromUtf8("CAFÉ.PNG"));
    ASSERT_TRUE(second.insertMarkSql(b, true));
    EXPECT_EQ(b.picId, 1);
    EXPECT_EQ(b.appearanceId, 1);
    EXPECT_EQ(env.images(), QStringList{QString::fromUtf8("1|Café.png")}); // the stored spelling stays
    EXPECT_EQ(env.count("MarkAppearance"), 1);
    // and the second helper cached what S-20 answered, under the requested spelling
    EXPECT_NE(
        MarkDbImplORMHelperTestAccess::cache(second).markImageCache->find(QString::fromUtf8("CAFÉ.PNG")),
        nullptr);
}

TEST(MarkDbImplORMHelperWrite, ReadImageAndAppearanceColdThenFromTheCache) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    qint64 picId = 0;
    qint64 appearanceId = 0;
    {
        auto writer = env.helper();
        picId = MarkDbImplORMHelperTestAccess::addMarkImage(writer, QString::fromUtf8("Иконка.png"));
        appearanceId = MarkDbImplORMHelperTestAccess::addMarkAppearance(writer, 5, 4294967295U, -7, 9);
    }
    EXPECT_EQ(picId, 1);
    EXPECT_EQ(appearanceId, 1);

    auto helper = env.helper(); // cold
    OrmMarkRec r;
    r.geoType = OrmGeoType::Point;
    r.picId = picId;
    r.appearanceId = appearanceId;
    r.picName = QStringLiteral("stale");
    MarkDbImplORMHelperTestAccess::readMarkImage(helper, r);
    MarkDbImplORMHelperTestAccess::readMarkAppearance(helper, r);
    EXPECT_EQ(r.picName, QString::fromUtf8("Иконка.png"));
    EXPECT_EQ(r.color1, 5U);
    EXPECT_EQ(r.color2, 4294967295U);
    EXPECT_EQ(r.scale1, -7);
    EXPECT_EQ(r.scale2, 9);

    // now cached: a change behind the helper's back is not seen
    env.exec("UPDATE MarkImage SET miName='changed' WHERE ID=1");
    env.exec("UPDATE MarkAppearance SET maScale1=100 WHERE ID=1");
    OrmMarkRec again = r;
    again.picName.clear();
    again.scale1 = 0;
    MarkDbImplORMHelperTestAccess::readMarkImage(helper, again);
    MarkDbImplORMHelperTestAccess::readMarkAppearance(helper, again);
    EXPECT_EQ(again.picName, QString::fromUtf8("Иконка.png"));
    EXPECT_EQ(again.scale1, -7);
    // a cold helper sees the change
    auto fresh = env.helper();
    OrmMarkRec cold = r;
    MarkDbImplORMHelperTestAccess::readMarkImage(fresh, cold);
    MarkDbImplORMHelperTestAccess::readMarkAppearance(fresh, cold);
    EXPECT_EQ(cold.picName, QStringLiteral("changed"));
    EXPECT_EQ(cold.scale1, 100);
}

TEST(MarkDbImplORMHelperWrite, ReadImageOnlyForPointsAndEmptyForNoPicture) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec line;
    line.geoType = OrmGeoType::Line;
    line.picId = 5;
    line.picName = QStringLiteral("keep");
    MarkDbImplORMHelperTestAccess::readMarkImage(helper, line); // not a point: untouched, no SQL, no throw
    EXPECT_EQ(line.picName, QStringLiteral("keep"));
    OrmMarkRec point;
    point.geoType = OrmGeoType::Point;
    point.picId = 0;
    point.picName = QStringLiteral("gone");
    MarkDbImplORMHelperTestAccess::readMarkImage(helper, point);
    EXPECT_EQ(point.picName, QString());
}

TEST(MarkDbImplORMHelperWrite, ReadOfAMissingImageOrAppearanceThrowsIdIsEmpty) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec r;
    r.geoType = OrmGeoType::Point;
    r.picId = 42;
    r.appearanceId = 43;
    try {
        MarkDbImplORMHelperTestAccess::readMarkImage(helper, r);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
    try {
        MarkDbImplORMHelperTestAccess::readMarkAppearance(helper, r);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
}

// ---- delete one (W-3, S-40) ----------------------------------------------------------------------

namespace {
// Three marks: 1 (category 7, hidden, with meta), 2 (category 8), 3 (category 7).
void insertThree(MarkDbImplORMHelper& helper) {
    OrmMarkRec a = rec(QStringLiteral("a"), makeLineWithElevation(), OrmGeoType::Line, 1, 7, false);
    OrmMarkRec b =
        rec(QStringLiteral("b"), makePoint(), OrmGeoType::Point, 1, 8, true, QStringLiteral("i.png"));
    OrmMarkRec c = rec(QStringLiteral("c"), makePolygon(), OrmGeoType::Poly, 1, 7, false);
    ASSERT_TRUE(helper.insertMarkSql(a, true));
    ASSERT_TRUE(helper.insertMarkSql(b, true));
    ASSERT_TRUE(helper.insertMarkSql(c, true));
}
} // namespace

TEST(MarkDbImplORMHelperWrite, DeleteOneRemovesAllFiveTablesAndKeepsPicturesAndAppearances) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    ASSERT_EQ(env.count("MarkView"), 2);
    ASSERT_EQ(env.count("MarkMeta"), 1);
    EXPECT_TRUE(helper.deleteMarkSql(1, true));
    EXPECT_EQ(env.rows("SELECT ID FROM Mark ORDER BY ID"), (QStringList{"2", "3"}));
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkFTS ORDER BY RowID"), (QStringList{"2", "3"}));
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree ORDER BY RowID"), (QStringList{"2", "3"}));
    EXPECT_EQ(env.rows("SELECT mvMark FROM MarkView"), QStringList{"3"});
    EXPECT_EQ(env.count("MarkMeta"), 0);
    EXPECT_EQ(env.images(), QStringList{"1|i.png"});
    EXPECT_EQ(env.count("MarkAppearance"), 1);
}

TEST(MarkDbImplORMHelperWrite, DeleteOneRemovesTheViewRowsOfEveryUser) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    env.exec("INSERT INTO User (ID,uName) VALUES (2,'other')");
    env.exec("INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (2,1,7,1)");
    EXPECT_TRUE(helper.deleteMarkSql(1, true));
    EXPECT_EQ(env.rows("SELECT mvUser,mvMark FROM MarkView ORDER BY ID"), QStringList{"1|3"});
}

TEST(MarkDbImplORMHelperWrite, DeleteOneOfAnUnknownIdSucceedsAndBadIdsAreRefused) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    EXPECT_TRUE(helper.deleteMarkSql(999, true));
    EXPECT_EQ(env.count("Mark"), 3);
    EXPECT_FALSE(helper.deleteMarkSql(0, true));
    EXPECT_FALSE(helper.deleteMarkSql(-4, true));
    EXPECT_EQ(env.count("Mark"), 3);
}

TEST(MarkDbImplORMHelperWrite, DeleteOneWithoutTransactionBelongsToTheCallersTransaction) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    auto& transaction = env.provider().transaction();
    auto tr = transaction.start(false);
    EXPECT_TRUE(helper.deleteMarkSql(2, false));
    transaction.rollBack(tr);
    EXPECT_EQ(env.count("Mark"), 3);
    EXPECT_EQ(env.count("MarkFTS"), 3);
    EXPECT_EQ(env.count("MarkRTree"), 3);
}

TEST(MarkDbImplORMHelperWrite, DeleteOneDropsTheMarkFromTheCaches) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    ASSERT_NE(cache.markCache->find(3), nullptr);
    EXPECT_TRUE(helper.deleteMarkSql(3, true));
    EXPECT_EQ(cache.markCache->find(3), nullptr);
    EXPECT_EQ(cache.markViewCache->find(3), nullptr);
    EXPECT_EQ(cache.markIdIndex->find(3), nullptr);
    GeometryLonLatPtr geometry;
    EXPECT_FALSE(cache.markGeometryCache->find(3, geometry));
    EXPECT_NE(cache.markCache->find(1), nullptr);
    EXPECT_NE(cache.markCache->find(2), nullptr);
}

TEST(MarkDbImplORMHelperWrite, DeleteOneCleansTheByCategoryIndexOfTheNextMarkNotOfItsOwn) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // Legacy defect reproduced (:479-482): VIndex is read after FMarkIdIndex.Delete, so it names the row
    // that followed. Marks 1 (cat 7), 2 (cat 8), 3 (cat 7).
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    // the by-category index only grows arrays that exist (a prepared category): prepare both
    cache.markIdByCategoryIndex->addPrepared(7, {1, 3}, 0, 2);
    cache.markIdByCategoryIndex->addPrepared(8, {2}, 0, 1);
    auto idsOf = [&](qint64 category) {
        const std::vector<qint64>* ids = nullptr;
        int count = 0;
        std::vector<qint64> out;
        if (cache.markIdByCategoryIndex->find(category, ids, count)) {
            out.assign(ids->begin(), ids->begin() + count);
        }
        return out;
    };
    ASSERT_EQ(idsOf(7), (std::vector<qint64>{1, 3}));
    ASSERT_EQ(idsOf(8), (std::vector<qint64>{2}));

    // delete 1: the next row is mark 2 (category 8): 1 is looked up in category 8 and stays in 7
    EXPECT_TRUE(helper.deleteMarkSql(1, true));
    EXPECT_EQ(idsOf(7), (std::vector<qint64>{1, 3}));
    EXPECT_EQ(idsOf(8), (std::vector<qint64>{2}));

    // delete 3, now the last row: the zero-filled slot names category 0, so 3 stays in 7 as well
    EXPECT_TRUE(helper.deleteMarkSql(3, true));
    EXPECT_EQ(idsOf(7), (std::vector<qint64>{1, 3}));

    // delete 2 (now the only row): category 0 again
    EXPECT_TRUE(helper.deleteMarkSql(2, true));
    EXPECT_EQ(idsOf(8), (std::vector<qint64>{2}));
}

TEST(MarkDbImplORMHelperWrite, DeleteOneInTheSameCategoryAsTheNextMarkCleansItsOwn) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // the same pointer slip is invisible when the next mark shares the category
    Env env;
    auto helper = env.helper();
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    cache.markIdByCategoryIndex->addPrepared(7, {}, 0, 0); // a prepared (empty) category 7
    OrmMarkRec a = rec(QStringLiteral("a"), makePoint(), OrmGeoType::Point, 1, 7);
    OrmMarkRec b = rec(QStringLiteral("b"), makePoint(), OrmGeoType::Point, 1, 7);
    ASSERT_TRUE(helper.insertMarkSql(a, true));
    ASSERT_TRUE(helper.insertMarkSql(b, true));
    EXPECT_TRUE(helper.deleteMarkSql(1, true));
    const std::vector<qint64>* ids = nullptr;
    int count = 0;
    ASSERT_TRUE(cache.markIdByCategoryIndex->find(7, ids, count));
    ASSERT_EQ(count, 1);
    EXPECT_EQ((*ids)[0], 2);
}

TEST(MarkDbImplORMHelperWrite, DeleteOneWorksOnAColdCache) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    {
        auto writer = env.helper();
        insertThree(writer);
    }
    auto helper = env.helper(); // nothing cached: no index row, the caches are not touched
    EXPECT_TRUE(helper.deleteMarkSql(2, true));
    EXPECT_EQ(env.rows("SELECT ID FROM Mark ORDER BY ID"), (QStringList{"1", "3"}));
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree ORDER BY RowID"), (QStringList{"1", "3"}));
}

// ---- delete many (W-4, S-42) -----------------------------------------------------------------------

TEST(MarkDbImplORMHelperWrite, DeleteManyOfAnEmptyListIsTrueAndTouchesNothing) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    EXPECT_TRUE(helper.deleteMarkSql(std::vector<qint64>{}));
    EXPECT_EQ(env.count("Mark"), 3);
    EXPECT_NE(MarkDbImplORMHelperTestAccess::cache(helper).markCache->find(1), nullptr);
}

TEST(MarkDbImplORMHelperWrite, DeleteManyRemovesTheListedMarksAndResetsEveryCache) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    EXPECT_TRUE(helper.deleteMarkSql(std::vector<qint64>{3, 1, 999}));
    EXPECT_EQ(env.rows("SELECT ID FROM Mark"), QStringList{"2"});
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkFTS"), QStringList{"2"});
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree"), QStringList{"2"});
    EXPECT_EQ(env.count("MarkView"), 0);
    EXPECT_EQ(env.count("MarkMeta"), 0);
    EXPECT_EQ(env.count("MarkImage"), 1);
    EXPECT_EQ(env.count("MarkAppearance"), 1);
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    EXPECT_EQ(cache.markCache->find(2), nullptr); // 2 survives in the database but the cache was reset
    EXPECT_EQ(cache.markIdIndex->find(2), nullptr);
    const std::vector<qint64>* ids = nullptr;
    int count = 0;
    EXPECT_FALSE(cache.markIdByCategoryIndex->find(8, ids, count));
}

TEST(MarkDbImplORMHelperWrite, DeleteFiveHundredAndOneMarksTakesTwoChunks) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    auto& transaction = env.provider().transaction();
    auto tr = transaction.start(false);
    std::vector<qint64> ids;
    for (int i = 0; i < 501; ++i) {
        OrmMarkRec r =
            rec(QStringLiteral("m%1").arg(i), makeLineWithElevation(), OrmGeoType::Line, 1, 7, i % 2 == 0);
        ASSERT_TRUE(helper.insertMarkSql(r, false));
        ids.push_back(r.markId);
    }
    transaction.commit(tr);
    ASSERT_EQ(ids.front(), 1);
    ASSERT_EQ(ids.back(), 501);
    ASSERT_EQ(env.count("Mark"), 501);
    ASSERT_EQ(env.count("MarkView"), 250); // the odd i are hidden
    ASSERT_EQ(env.count("MarkMeta"), 501);

    // 500 ids first: the 501st mark must survive; then the whole list: every table is empty
    const std::vector<qint64> first500(ids.begin(), ids.begin() + 500);
    EXPECT_TRUE(helper.deleteMarkSql(first500));
    EXPECT_EQ(env.rows("SELECT ID FROM Mark"), QStringList{"501"});
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkFTS"), QStringList{"501"});
    EXPECT_EQ(env.rows("SELECT RowID FROM MarkRTree"), QStringList{"501"});
    EXPECT_EQ(env.rows("SELECT mMark FROM MarkMeta"), QStringList{"501"});

    // 501 ids in one call (two chunks: 500 + 1), the last id is the only one of the second chunk
    OrmMarkRec again = rec(QStringLiteral("again"), makeLine(), OrmGeoType::Line, 1, 7, false);
    ASSERT_TRUE(helper.insertMarkSql(again, true));
    std::vector<qint64> all(ids.begin() + 1, ids.end()); // 500 ids 2..501
    all.push_back(again.markId);                         // 502: the 501st id
    ASSERT_EQ(all.size(), 501U);
    EXPECT_TRUE(helper.deleteMarkSql(all));
    EXPECT_EQ(env.count("Mark"), 0);
    EXPECT_EQ(env.count("MarkFTS"), 0);
    EXPECT_EQ(env.count("MarkRTree"), 0);
    EXPECT_EQ(env.count("MarkView"), 0);
    EXPECT_EQ(env.count("MarkMeta"), 0);
}

TEST(MarkDbImplORMHelperWrite, DeleteManyRollsBackOnFailureAndStillClearsTheCaches) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    // A trigger makes the Mark delete fail after the side tables were deleted in the chunk: the whole
    // transaction is rolled back (S-42 order: RTree, FTS, Meta, View, Mark).
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    env.exec("CREATE TRIGGER refuse BEFORE DELETE ON Mark BEGIN SELECT RAISE(ABORT,'no'); END");
    EXPECT_THROW(std::ignore = helper.deleteMarkSql(std::vector<qint64>{1, 2}), MarkSystemORMError);
    EXPECT_EQ(env.count("Mark"), 3);
    EXPECT_EQ(env.count("MarkRTree"), 3);
    EXPECT_EQ(env.count("MarkFTS"), 3);
    EXPECT_EQ(env.count("MarkMeta"), 1);
    EXPECT_EQ(env.count("MarkView"), 2);
    EXPECT_EQ(MarkDbImplORMHelperTestAccess::cache(helper).markCache->find(1), nullptr);
}

TEST(MarkDbImplORMHelperWrite, DeleteOneRollsBackOnFailure) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    insertThree(helper);
    env.exec("CREATE TRIGGER refuse BEFORE DELETE ON Mark BEGIN SELECT RAISE(ABORT,'no'); END");
    try {
        std::ignore = helper.deleteMarkSql(1, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Delete operation is failed!");
    }
    EXPECT_EQ(env.count("Mark"), 3);
    EXPECT_EQ(env.count("MarkRTree"), 3); // deleted before the failing statement, rolled back
    EXPECT_EQ(env.count("MarkFTS"), 3);
    EXPECT_EQ(env.count("MarkMeta"), 1);
    EXPECT_EQ(env.count("MarkView"), 2);
    // caches were cleared first and stay cleared
    EXPECT_EQ(MarkDbImplORMHelperTestAccess::cache(helper).markCache->find(1), nullptr);
}
