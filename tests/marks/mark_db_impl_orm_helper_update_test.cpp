// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034 (held open). Ports no legacy test (there is none): the update half of
// u_MarkDbImplORMHelper.pas, UpdateMarkSQL :776-1010, UpdateMarkView :1176-1264, SetMarksInCategoryVisibleSQL
// :1266-1347 and the geometry decoders :267-299 (docs/specs/marks.md §2.9.8 W-2, W-5, W-6; statements S-23
// ... S-27, S-2E, S-2F, S-36 ... S-39), against a copy of tests/fixtures/marks/Marks.db3. Expected values are
// literals read back with plain SQL on a second connection. Which columns of which table a statement sets is
// read from triggers (AFTER UPDATE OF <column>) that write a log table; virtual tables (FTS, RTree) are
// checked by content.
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"

#include <gtest/gtest.h>

#include <algorithm>
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

const char* const kHexLine1 = "0102000000020000000000000000002540000000000040344000000000000029400000000000c0"
                              "3640"; // (10.5,20.25)-(12.5,22.75)
QString hexLine2() {
    // (1.5,2.5)-(3.5,4.5): 01 | 02000000 | 02000000 | 1.5 2.5 3.5 4.5 as little-endian doubles
    return QStringLiteral("0102000000") + QStringLiteral("02000000") + QStringLiteral("000000000000f83f") +
           QStringLiteral("0000000000000440") + QStringLiteral("0000000000000c40") +
           QStringLiteral("0000000000001240");
}

constexpr const char* kMetaElevation100 = "JSON{\"v\":1,\"t\":1,\"g\":[{\"c\":2,\"m\":[{\"t\":1,\"n\":"
                                          "\"ele\",\"d\":\"AAAAAAAgWUAAAAAAAAhpQA==\"}]}]}";
constexpr const char* kMetaElevation5 = "JSON{\"v\":1,\"t\":1,\"g\":[{\"c\":2,\"m\":[{\"t\":1,\"n\":\"ele\","
                                        "\"d\":\"AAAAAAAAFEAAAAAAAAAYQA==\"}]}]}";

GeometryLonLatPtr makeLine2() {
    const std::vector<LonLat> p{{1.5, 2.5}, {3.5, 4.5}};
    return kFactory.createLine(p, nullptr);
}
GeometryLonLatPtr makeLine2WithElevation() {
    const std::vector<LonLat> p{{1.5, 2.5}, {3.5, 4.5}};
    PointsMeta meta;
    meta.elevation = {5.0, 6.0};
    return kFactory.createLine(p, &meta);
}

// Logs every statement that sets a column of Mark, MarkView, MarkMeta (or inserts into the small tables).
void installLog(Env& env) {
    env.exec("CREATE TABLE T100Log (n INTEGER PRIMARY KEY AUTOINCREMENT, s TEXT)");
    const auto trigger = [&](const QByteArray& table, const QByteArray& name, const QByteArray& when) {
        const QByteArray sql = "CREATE TRIGGER T100_" + table + "_" + name + " AFTER " + when + " ON " +
                               table + " BEGIN INSERT INTO T100Log(s) VALUES('" + table + "." + name +
                               "'); END";
        env.exec(sql.constData());
    };
    for (const char* column : {"mCategory", "mImage", "mAppearance", "mName", "mDesc", "mGeoType",
                               "mGeoCount", "mGeoLonSize", "mGeoLatSize", "mGeoWKB"}) {
        trigger("Mark", column, QByteArray("UPDATE OF ") + column);
    }
    for (const char* column : {"mvCategory", "mvVisible"}) {
        trigger("MarkView", column, QByteArray("UPDATE OF ") + column);
    }
    trigger("MarkView", "ins", "INSERT");
    trigger("MarkMeta", "ins", "INSERT");
    trigger("MarkMeta", "mMeta", "UPDATE OF mMeta");
    trigger("MarkMeta", "del", "DELETE");
    trigger("MarkImage", "ins", "INSERT");
    trigger("MarkAppearance", "ins", "INSERT");
}

// The log in statement order: consecutive entries of the same table are one group, sorted inside (the firing
// order of the column triggers of one statement is not specified); groups are joined with '|', entries with
// ','.
QString logOf(Env& env) {
    const QStringList entries = env.rows("SELECT s FROM T100Log ORDER BY n");
    QStringList groups;
    QStringList group;
    QString table;
    const auto flush = [&] {
        if (!group.isEmpty()) {
            std::sort(group.begin(), group.end());
            groups.append(group.join(QLatin1Char(',')));
            group.clear();
        }
    };
    for (const QString& entry : entries) {
        const QString current = entry.section(QLatin1Char('.'), 0, 0);
        if (current != table) {
            flush();
            table = current;
        }
        group.append(entry);
    }
    flush();
    return groups.join(QLatin1Char('|'));
}

QString joined(const QStringList& rows) {
    return rows.join(QLatin1Char(';'));
}

// The mark inserted by the seed of most tests.
OrmMarkRec seed(MarkDbImplORMHelper& helper, bool withMeta = true) {
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    cache.markIdByCategoryIndex->addPrepared(7, {}, 0,
                                             0); // the by-category index keeps ids of prepared categories
    cache.markIdByCategoryIndex->addPrepared(9, {}, 0, 0);
    OrmMarkRec r = rec(kTown, withMeta ? makeLineWithElevation() : makeLine(), OrmGeoType::Line, 1, 7, true,
                       QStringLiteral("i.png"));
    r.desc = QString::fromUtf8("Описание");
    EXPECT_TRUE(helper.insertMarkSql(r, true));
    return r;
}

// What the caches hold for mark 1, as one string.
QString cacheState(MarkDbImplORMHelper& helper, const GeometryLonLatPtr& oldGeometry,
                   const GeometryLonLatPtr& newGeometry) {
    auto& cache = MarkDbImplORMHelperTestAccess::cache(helper);
    QStringList parts;
    if (const auto* m = cache.markCache->find(1)) {
        parts << QStringLiteral("mark=%1/%2/%3/%4")
                     .arg(m->name, m->desc)
                     .arg(static_cast<int>(m->geoType))
                     .arg(m->geoCount);
    } else {
        parts << QStringLiteral("mark=-");
    }
    if (const auto* x = cache.markIdIndex->find(1)) {
        parts << QStringLiteral("idx=%1/%2/%3").arg(x->categoryId).arg(x->imageId).arg(x->appearanceId);
    } else {
        parts << QStringLiteral("idx=-");
    }
    if (const auto* v = cache.markViewCache->find(1)) {
        parts << QStringLiteral("view=%1/%2/%3").arg(v->viewId).arg(v->categoryId).arg(v->visible ? 1 : 0);
    } else {
        parts << QStringLiteral("view=-");
    }
    GeometryLonLatPtr geometry;
    if (!cache.markGeometryCache->find(1, geometry)) {
        parts << QStringLiteral("geo=-");
    } else if (geometry == oldGeometry) {
        parts << QStringLiteral("geo=old");
    } else if (geometry == newGeometry) {
        parts << QStringLiteral("geo=new");
    } else {
        parts << QStringLiteral("geo=other");
    }
    for (const qint64 category : {7, 9}) {
        const std::vector<qint64>* ids = nullptr;
        int count = 0;
        QString text = QStringLiteral("c%1=").arg(category);
        if (cache.markIdByCategoryIndex->find(category, ids, count)) {
            for (int i = 0; i < count; ++i) {
                text += QString::number((*ids)[static_cast<std::size_t>(i)]);
            }
        } else {
            text += QLatin1Char('-');
        }
        parts << text;
    }
    return parts.join(QLatin1Char(' '));
}

struct UpdateCase {
    const char* label;
    void (*change)(OrmMarkRec&);
    bool result;
    const char* ids; // markId/picId/appearanceId of the new record afterwards
    const char* log;
    QString mark;
    QString fts;
    const char* rtree;
    const char* views;
    QString metas;
    QString images;
    const char* appearances;
    QString cache;
};

const QString kBaseFts = QString::fromUtf8("1|город|описание");
const QString kBaseMeta = QStringLiteral("1|1|") + QString::fromUtf8(kMetaElevation100);
const QString kBaseMarkTail = QStringLiteral("|2|1|11930464|14913080|blob|") + QString::fromUtf8(kHexLine1);
const char* const kBaseRtree = "1|10.5|12.5|20.25|22.75";
const char* const kBaseAppearance = "1|4279312947|4294967295|12|-3";

QString markRow(const QString& category, const QString& image, const QString& appearance, const QString& name,
                const QString& desc, const QString& tail = kBaseMarkTail) {
    return QStringLiteral("1|%1|%2|%3|%4|%5").arg(category, image, appearance, name, desc) + tail;
}

} // namespace

// W-2: one test row per changed-field flag; each field alone writes exactly its columns, its index rows, its
// cache entries and nothing else.
TEST(MarkDbImplORMHelperUpdate, EachChangedFieldAloneWritesExactlyItsStatementsAndCacheEntries) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    const QString yolka = QString::fromUtf8("Ёлка");
    const QString desc2 = QString::fromUtf8("Другое Описание");
    const QString desc1 = QString::fromUtf8("Описание");
    const QString baseCache =
        QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/1 view=0/7/1 geo=old c7=1 c9=");
    const QString newGeoCache =
        QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/1 view=0/7/1 geo=new c7=1 c9=");
    const UpdateCase cases[] = {
        {"nothing changed", [](OrmMarkRec&) {}, false, "1/1/1", "",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("1"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"), kBaseAppearance, baseCache},
        {"name (non-ASCII, FTS lower-cased)", [](OrmMarkRec& r) { r.name = QString::fromUtf8("Ёлка"); }, true,
         "1/1/1", "Mark.mName",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("1"), yolka, desc1),
         QString::fromUtf8("1|ёлка|описание"), kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"),
         kBaseAppearance, QString::fromUtf8("mark=Ёлка/Описание/2/1 idx=7/1/1 view=0/7/1 geo=old c7=1 c9=")},
        {"description (FTS lower-cased)",
         [](OrmMarkRec& r) { r.desc = QString::fromUtf8("Другое Описание"); }, true, "1/1/1", "Mark.mDesc",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("1"), kTown, desc2),
         QString::fromUtf8("1|город|другое описание"), kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"),
         kBaseAppearance,
         QString::fromUtf8("mark=Город/Другое Описание/2/1 idx=7/1/1 view=0/7/1 geo=old c7=1 c9=")},
        {"picture name", [](OrmMarkRec& r) { r.picName = QStringLiteral("j.png"); }, true, "1/2/1",
         "MarkImage.ins|Mark.mImage",
         markRow(QStringLiteral("7"), QStringLiteral("2"), QStringLiteral("1"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png;2|j.png"), kBaseAppearance,
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/2/1 view=0/7/1 geo=old c7=1 c9=")},
        {"picture removed", [](OrmMarkRec& r) { r.picName.clear(); }, true, "1/0/1", "Mark.mImage",
         markRow(QStringLiteral("7"), QStringLiteral("0"), QStringLiteral("1"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"), kBaseAppearance,
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/0/1 view=0/7/1 geo=old c7=1 c9=")},
        {"category (visible mark: the view cache follows, no row)", [](OrmMarkRec& r) { r.categoryId = 9; },
         true, "1/1/1", "Mark.mCategory",
         markRow(QStringLiteral("9"), QStringLiteral("1"), QStringLiteral("1"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"), kBaseAppearance,
         QString::fromUtf8("mark=Город/Описание/2/1 idx=9/1/1 view=0/9/1 geo=old c7= c9=1")},
        {"color1", [](OrmMarkRec& r) { r.color1 = 4279312948U; }, true, "1/1/2",
         "MarkAppearance.ins|Mark.mAppearance",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("2"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"),
         "1|4279312947|4294967295|12|-3;2|4279312948|4294967295|12|-3",
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/2 view=0/7/1 geo=old c7=1 c9=")},
        {"color2", [](OrmMarkRec& r) { r.color2 = 4294967294U; }, true, "1/1/2",
         "MarkAppearance.ins|Mark.mAppearance",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("2"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"),
         "1|4279312947|4294967295|12|-3;2|4279312947|4294967294|12|-3",
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/2 view=0/7/1 geo=old c7=1 c9=")},
        {"scale1", [](OrmMarkRec& r) { r.scale1 = 13; }, true, "1/1/2", "MarkAppearance.ins|Mark.mAppearance",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("2"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"),
         "1|4279312947|4294967295|12|-3;2|4279312947|4294967295|13|-3",
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/2 view=0/7/1 geo=old c7=1 c9=")},
        {"scale2", [](OrmMarkRec& r) { r.scale2 = -4; }, true, "1/1/2", "MarkAppearance.ins|Mark.mAppearance",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("2"), kTown, desc1), kBaseFts,
         kBaseRtree, "", kBaseMeta, QStringLiteral("1|i.png"),
         "1|4279312947|4294967295|12|-3;2|4279312947|4294967295|12|-4",
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/2 view=0/7/1 geo=old c7=1 c9=")},
        {"geometry, new one has no meta (meta row deleted)", [](OrmMarkRec& r) { r.geometry = makeLine2(); },
         true, "1/1/1",
         "Mark.mGeoCount,Mark.mGeoLatSize,Mark.mGeoLonSize,Mark.mGeoType,Mark.mGeoWKB|MarkMeta.del",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("1"), kTown, desc1,
                 QStringLiteral("|2|1|11930464|11930464|blob|") + hexLine2()),
         kBaseFts, "1|1.5|3.5|2.5|4.5", "", QString(), QStringLiteral("1|i.png"), kBaseAppearance,
         newGeoCache},
        {"geometry, new one has other meta (meta row updated in place)",
         [](OrmMarkRec& r) { r.geometry = makeLine2WithElevation(); }, true, "1/1/1",
         "Mark.mGeoCount,Mark.mGeoLatSize,Mark.mGeoLonSize,Mark.mGeoType,Mark.mGeoWKB|MarkMeta.mMeta",
         markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("1"), kTown, desc1,
                 QStringLiteral("|2|1|11930464|11930464|blob|") + hexLine2()),
         kBaseFts, "1|1.5|3.5|2.5|4.5", "", QStringLiteral("1|1|") + QString::fromUtf8(kMetaElevation5),
         QStringLiteral("1|i.png"), kBaseAppearance, newGeoCache},
        {"visibility (hide: a view row is added)", [](OrmMarkRec& r) { r.visible = false; }, true, "1/1/1",
         "MarkView.ins", markRow(QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("1"), kTown, desc1),
         kBaseFts, kBaseRtree, "1|1|1|7|0", kBaseMeta, QStringLiteral("1|i.png"), kBaseAppearance,
         QString::fromUtf8("mark=Город/Описание/2/1 idx=7/1/1 view=1/7/0 geo=old c7=1 c9=")},
    };
    for (const UpdateCase& c : cases) {
        SCOPED_TRACE(c.label);
        Env env;
        auto helper = env.helper();
        const OrmMarkRec seeded = seed(helper);
        installLog(env);
        OrmMarkRec updated = seeded;
        c.change(updated);
        EXPECT_EQ(helper.updateMarkSql(seeded, updated), c.result);
        EXPECT_EQ(QStringLiteral("%1/%2/%3").arg(updated.markId).arg(updated.picId).arg(updated.appearanceId),
                  QString::fromUtf8(c.ids));
        EXPECT_EQ(logOf(env), QString::fromUtf8(c.log));
        EXPECT_EQ(joined(env.rows("SELECT ID,mCategory,mImage,mAppearance,mName,mDesc,mGeoType,mGeoCount,"
                                  "mGeoLonSize,mGeoLatSize,typeof(mGeoWKB),lower(hex(mGeoWKB)) FROM Mark")),
                  c.mark);
        EXPECT_EQ(joined(env.fts()), c.fts);
        EXPECT_EQ(joined(env.rtree()), QString::fromUtf8(c.rtree));
        EXPECT_EQ(joined(env.views()), QString::fromUtf8(c.views));
        EXPECT_EQ(joined(env.metas()), c.metas);
        EXPECT_EQ(joined(env.images()), c.images);
        EXPECT_EQ(joined(env.appearances()), QString::fromUtf8(c.appearances));
        EXPECT_EQ(cacheState(helper, seeded.geometry, updated.geometry), c.cache);
    }
}

TEST(MarkDbImplORMHelperUpdate, NewGeometryWithMetaAddsTheMetaRow) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper, false);
    EXPECT_EQ(env.count("MarkMeta"), 0);
    installLog(env);
    OrmMarkRec updated = seeded;
    updated.geometry = makeLine2WithElevation();
    EXPECT_TRUE(helper.updateMarkSql(seeded, updated));
    EXPECT_EQ(logOf(env),
              QStringLiteral("Mark.mGeoCount,Mark.mGeoLatSize,Mark.mGeoLonSize,Mark.mGeoType,Mark.mGeoWKB|"
                             "MarkMeta.ins,MarkMeta.mMeta"));
    EXPECT_EQ(joined(env.metas()), QStringLiteral("1|1|") + QString::fromUtf8(kMetaElevation5));
}

TEST(MarkDbImplORMHelperUpdate, NewGeometryWithoutMetaAndNoMetaRowTouchesNoMetaRow) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper, false);
    installLog(env);
    OrmMarkRec updated = seeded;
    updated.geometry = makeLine2();
    EXPECT_TRUE(helper.updateMarkSql(seeded, updated));
    EXPECT_EQ(logOf(env),
              QStringLiteral("Mark.mGeoCount,Mark.mGeoLatSize,Mark.mGeoLonSize,Mark.mGeoType,Mark.mGeoWKB"));
    EXPECT_EQ(env.count("MarkMeta"), 0);
}

// W-5: a MarkView row, once created, is never deleted by un-hiding.
TEST(MarkDbImplORMHelperUpdate, ViewRowStaysAfterUnHideAndIsUpdatedWhenHiddenAgain) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec hidden = rec(kTown, makePoint(), OrmGeoType::Point, 1, 7, false);
    ASSERT_TRUE(helper.insertMarkSql(hidden, true));
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|0"));
    installLog(env);

    OrmMarkRec shown = hidden;
    shown.visible = true;
    EXPECT_TRUE(helper.updateMarkSql(hidden, shown));
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|1"));
    EXPECT_EQ(logOf(env), QStringLiteral("MarkView.mvVisible"));
    const auto* view = MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewId, 1);
    EXPECT_TRUE(view->visible);

    OrmMarkRec again = shown;
    again.visible = false;
    EXPECT_TRUE(helper.updateMarkSql(shown, again));
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|0")); // the same row, not a new one
    EXPECT_EQ(logOf(env), QStringLiteral("MarkView.mvVisible,MarkView.mvVisible")); // one UPDATE per call
}

TEST(MarkDbImplORMHelperUpdate, HiddenMarkChangingCategoryUpdatesItsViewRowCategory) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec hidden = rec(kTown, makePoint(), OrmGeoType::Point, 1, 7, false);
    ASSERT_TRUE(helper.insertMarkSql(hidden, true));
    installLog(env);
    OrmMarkRec moved = hidden;
    moved.categoryId = 9;
    EXPECT_TRUE(helper.updateMarkSql(hidden, moved));
    EXPECT_EQ(logOf(env), QStringLiteral("Mark.mCategory|MarkView.mvCategory"));
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|9|0"));
    EXPECT_EQ(joined(env.rows("SELECT ID,mCategory FROM Mark")), QStringLiteral("1|9"));
}

// A cold cache: no mark / view cache entry, so the view row is read by S-36.
TEST(MarkDbImplORMHelperUpdate, ColdCacheReadsTheViewRowBySqlAndUpdatesIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    OrmMarkRec hidden = rec(kTown, makePoint(), OrmGeoType::Point, 1, 7, false);
    {
        auto first = env.helper();
        ASSERT_TRUE(first.insertMarkSql(hidden, true));
    }
    installLog(env);
    auto cold = env.helper();
    OrmMarkRec moved = hidden;
    moved.categoryId = 9;
    moved.visible = true;
    moved.name = QString::fromUtf8("Новое");
    EXPECT_TRUE(cold.updateMarkSql(hidden, moved));
    EXPECT_EQ(logOf(env), QStringLiteral("Mark.mCategory,Mark.mName|MarkView.mvCategory,MarkView.mvVisible"));
    const auto& coldCache = MarkDbImplORMHelperTestAccess::cache(cold);
    const auto* markRow = coldCache.markCache->find(1); // written because the name changed
    ASSERT_NE(markRow, nullptr);
    EXPECT_EQ(markRow->name, QString::fromUtf8("Новое"));
    EXPECT_EQ(markRow->geoType, OrmGeoType::Point);
    const auto* indexRow = coldCache.markIdIndex->find(1);
    ASSERT_NE(indexRow, nullptr);
    EXPECT_EQ(indexRow->categoryId, 9);
    EXPECT_EQ(indexRow->imageId, 0);
    EXPECT_EQ(indexRow->appearanceId, 1);
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|9|1"));
    const auto* view = MarkDbImplORMHelperTestAccess::cache(cold).markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewId, 1);
    EXPECT_EQ(view->categoryId, 9);
}

// The facade re-reads the old mark from the database (u_MarkDbImplORM.pas:607-619): equal content in ANOTHER
// object is the normal "geometry not changed" case; IsSameGeometry compares content, not pointers.
TEST(MarkDbImplORMHelperUpdate, EqualGeometryInAnotherObjectIsNotRewrittenButOtherMetaIs) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    installLog(env);
    OrmMarkRec renamed = seeded;
    renamed.geometry = makeLineWithElevation(); // same content, new object
    ASSERT_NE(renamed.geometry, seeded.geometry);
    renamed.name = QStringLiteral("x");
    EXPECT_TRUE(helper.updateMarkSql(seeded, renamed));
    EXPECT_EQ(logOf(env), QStringLiteral("Mark.mName"));
    GeometryLonLatPtr cached;
    ASSERT_TRUE(MarkDbImplORMHelperTestAccess::cache(helper).markGeometryCache->find(1, cached));
    EXPECT_EQ(cached, seeded.geometry); // the geometry cache was not touched

    // same points, other elevations: a different geometry
    const std::vector<LonLat> points{{10.5, 20.25}, {12.5, 22.75}};
    PointsMeta meta;
    meta.elevation = {5.0, 6.0};
    OrmMarkRec remeta = seeded;
    remeta.geometry = kFactory.createLine(points, &meta);
    env.exec("DELETE FROM T100Log");
    EXPECT_TRUE(helper.updateMarkSql(seeded, remeta));
    EXPECT_EQ(logOf(env),
              QStringLiteral("Mark.mGeoCount,Mark.mGeoLatSize,Mark.mGeoLonSize,Mark.mGeoType,Mark.mGeoWKB|"
                             "MarkMeta.mMeta"));
}

// Type, count and unequal extents all change: each column carries its own new value.
TEST(MarkDbImplORMHelperUpdate, GeometryChangeWritesTheNewTypeCountAndBothSizes) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    const std::vector<LonLat> ring{{0.0, 0.0}, {4.0, 0.0}, {4.0, 1.0}};
    OrmMarkRec polygon = seeded;
    polygon.geometry = kFactory.createPolygon(ring);
    polygon.geoType = OrmGeoType::Poly;
    polygon.geoCount = 7;
    EXPECT_TRUE(helper.updateMarkSql(seeded, polygon));
    const QString hex = QStringLiteral("0103000000") + QStringLiteral("01000000") +
                        QStringLiteral("04000000") + QStringLiteral("00000000000000000000000000000000") +
                        QStringLiteral("00000000000010400000000000000000") +
                        QStringLiteral("0000000000001040000000000000f03f") +
                        QStringLiteral("00000000000000000000000000000000");
    EXPECT_EQ(
        joined(env.rows("SELECT mGeoType,mGeoCount,mGeoLonSize,mGeoLatSize,lower(hex(mGeoWKB)) FROM Mark")),
        QStringLiteral("3|7|23860928|5965232|") + hex);
    EXPECT_EQ(joined(env.rtree()), QStringLiteral("1|0.0|4.0|0.0|1.0"));
    EXPECT_EQ(env.count("MarkMeta"), 0);
    const auto* cached = MarkDbImplORMHelperTestAccess::cache(helper).markCache->find(1);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached->geoType, OrmGeoType::Poly);
    EXPECT_EQ(cached->geoCount, 7);
}

// UpdateMarkList runs the helper inside the caller's transaction (u_MarkDbImplORM.pas:672-719): the helper's
// Start joins it (u_MarkSystemORMTransaction.pas:73-82), its Commit is a no-op, the caller's rollback undoes
// the update.
TEST(MarkDbImplORMHelperUpdate, UpdateInTheCallersTransactionRollsBackWithIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    auto& transaction = env.provider().transaction();
    auto tr = transaction.start(false);
    OrmMarkRec renamed = seeded;
    renamed.name = QStringLiteral("x");
    EXPECT_TRUE(helper.updateMarkSql(seeded, renamed));
    transaction.rollBack(tr);
    EXPECT_EQ(joined(env.rows("SELECT mName FROM Mark")), QString::fromUtf8("Город"));
    EXPECT_EQ(joined(env.fts()), kBaseFts);
    const auto* cached = MarkDbImplORMHelperTestAccess::cache(helper).markCache->find(1);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached->name, QStringLiteral("x")); // the caches are not undone
}

TEST(MarkDbImplORMHelperUpdate, ReadOnlyWritesNothingAndOnlyFollowsTheVisibility) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    OrmMarkRec seeded;
    {
        auto writer = env.helper();
        seeded = seed(writer);
    }
    installLog(env);
    const QString before = joined(env.marks());
    auto readOnly = env.helper(true);

    OrmMarkRec renamed = seeded;
    renamed.name = QStringLiteral("other");
    EXPECT_FALSE(readOnly.updateMarkSql(seeded, renamed)); // visibility equal: nothing, false
    EXPECT_EQ(renamed.markId, 1);

    OrmMarkRec hiddenAndRenamed = seeded;
    hiddenAndRenamed.name = QStringLiteral("other");
    hiddenAndRenamed.categoryId = 9;
    hiddenAndRenamed.visible = false;
    EXPECT_TRUE(readOnly.updateMarkSql(seeded, hiddenAndRenamed)); // only the view cache moves
    EXPECT_EQ(joined(env.marks()), before);
    EXPECT_EQ(env.count("MarkView"), 0);
    EXPECT_EQ(logOf(env), QString());
    const auto* view = MarkDbImplORMHelperTestAccess::cache(readOnly).markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewId, 0);
    EXPECT_EQ(view->categoryId, 9);
    EXPECT_FALSE(view->visible);

    // a read-only update with a null new geometry does not reach IsSameGeometry
    OrmMarkRec noGeometry = seeded;
    noGeometry.geometry.reset();
    noGeometry.visible = true;
    EXPECT_TRUE(readOnly.updateMarkSql(hiddenAndRenamed, noGeometry));
}

TEST(MarkDbImplORMHelperUpdate, EmptyMarkIdAndNullNewGeometryThrowBeforeAnyWrite) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    installLog(env);
    const auto expectThrow = [&](const OrmMarkRec& oldRec, OrmMarkRec& newRec, const char* what) {
        try {
            std::ignore = helper.updateMarkSql(oldRec, newRec);
            ADD_FAILURE() << "no throw";
        } catch (const MarkSystemORMError& e) {
            EXPECT_STREQ(e.what(), what);
        }
    };
    OrmMarkRec noId = seeded;
    noId.markId = 0;
    OrmMarkRec renamed = seeded;
    renamed.name = QStringLiteral("x");
    expectThrow(noId, renamed, "MarkSystemORM: ID is empty!");

    OrmMarkRec noGeometry = seeded;
    noGeometry.geometry.reset();
    noGeometry.name = QStringLiteral("x");
    expectThrow(seeded, noGeometry, "MarkSystemORM: Geometry is empty!");
    EXPECT_EQ(noGeometry.markId, 1); // copied before the throw, as in legacy
    EXPECT_EQ(logOf(env), QString());
    EXPECT_EQ(joined(env.fts()), kBaseFts);
}

TEST(MarkDbImplORMHelperUpdate, NullOldGeometryCountsAsChanged) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    OrmMarkRec oldNoGeometry = seeded;
    oldNoGeometry.geometry.reset();
    OrmMarkRec same = seeded;
    EXPECT_TRUE(helper.updateMarkSql(oldNoGeometry, same)); // IsSameGeometry(nil) = False: geometry rewritten
    EXPECT_EQ(joined(env.rtree()), QString::fromUtf8(kBaseRtree));
}

// Failures: the helper's own transaction rolls back, the caches keep what was written before (not undone).
TEST(MarkDbImplORMHelperUpdate, FailedMarkUpdateRollsBackThePictureRowAndTheCachesKeepIt) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    env.exec("CREATE TRIGGER T100_fail BEFORE UPDATE OF mImage ON Mark BEGIN SELECT RAISE(ABORT,'no'); END");
    OrmMarkRec updated = seeded;
    updated.picName = QStringLiteral("j.png");
    try {
        std::ignore = helper.updateMarkSql(seeded, updated);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Update operation is failed!");
    }
    EXPECT_EQ(joined(env.images()), QStringLiteral("1|i.png")); // S-21 rolled back
    const auto* image =
        MarkDbImplORMHelperTestAccess::cache(helper).markImageCache->find(QStringLiteral("j.png"));
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->id, 2); // stale: the cache keeps the id of the rolled-back row
    // the transaction is closed: the next write commits
    env.exec("DROP TRIGGER T100_fail");
    OrmMarkRec renamed = seeded;
    renamed.name = QStringLiteral("n");
    EXPECT_TRUE(helper.updateMarkSql(seeded, renamed));
    EXPECT_EQ(joined(env.rows("SELECT mName FROM Mark")), QStringLiteral("n"));
}

// Legacy detects a failed FTS / RTree UPDATE (Update returns the result of MainEngineUpdate): the port throws
// and rolls back as well, so there is no deviation here (unlike the forced-id INSERTs of the write half).
TEST(MarkDbImplORMHelperUpdate, FtsUpdateFailureThrowsAndRollsBack) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    env.exec("DROP TABLE MarkFTS");
    OrmMarkRec updated = seeded;
    updated.name = QStringLiteral("x");
    try {
        std::ignore = helper.updateMarkSql(seeded, updated);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Update operation is failed!");
    }
    EXPECT_EQ(joined(env.rows("SELECT mName FROM Mark")), QString::fromUtf8("Город")); // S-2E rolled back
    const auto* cached = MarkDbImplORMHelperTestAccess::cache(helper).markCache->find(1);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached->name, QStringLiteral("x")); // not undone
}

TEST(MarkDbImplORMHelperUpdate, RTreeUpdateFailureThrowsAndRollsBack) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const OrmMarkRec seeded = seed(helper);
    env.exec("DROP TABLE MarkRTree");
    OrmMarkRec updated = seeded;
    updated.geometry = makeLine2();
    try {
        std::ignore = helper.updateMarkSql(seeded, updated);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Update operation is failed!");
    }
    EXPECT_EQ(joined(env.rows("SELECT lower(hex(mGeoWKB)) FROM Mark")), QString::fromUtf8(kHexLine1));
    GeometryLonLatPtr cached;
    ASSERT_TRUE(MarkDbImplORMHelperTestAccess::cache(helper).markGeometryCache->find(1, cached));
    EXPECT_EQ(cached, updated.geometry); // not undone
}

// ---- updateMarkView (W-5) ------------------------------------------------------------------------------

namespace {
// A hidden mark 1 (category 7, view row 1) written by another helper, so the helper under test starts cold.
void seedHiddenMark(Env& env) {
    auto writer = env.helper();
    OrmMarkRec r = rec(kTown, makePoint(), OrmGeoType::Point, 1, 7, false);
    ASSERT_TRUE(writer.insertMarkSql(r, true));
}
} // namespace

TEST(MarkDbImplORMHelperUpdate, ViewColdRowIsReadAndUpdatedAndCategoryZeroKeepsTheStoredOne) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedHiddenMark(env);
    installLog(env);
    auto helper = env.helper();
    EXPECT_TRUE(helper.updateMarkView(1, 0, true, true)); // category 0: mvCategory is not touched
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|1"));
    EXPECT_EQ(logOf(env), QStringLiteral("MarkView.mvVisible"));
    const auto* view = MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->categoryId, 0); // the cache takes the argument as legacy does
    EXPECT_EQ(view->viewId, 1);
    // the cached row now says category 0: legacy CheckID(VCategory) throws before it compares anything
    try {
        std::ignore = helper.updateMarkView(1, 0, true, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
}

TEST(MarkDbImplORMHelperUpdate, ViewWithoutRowAddsOneOnlyWhenHidingAndAnUpToDateRowIsNotRewritten) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    OrmMarkRec visible = rec(kTown, makePoint(), OrmGeoType::Point, 1, 7, true);
    ASSERT_TRUE(helper.insertMarkSql(visible, true));
    installLog(env);
    auto cold = env.helper();
    EXPECT_FALSE(
        cold.updateMarkView(1, 7, true, true)); // S-36 finds nothing, visible: marks are visible by default
    EXPECT_EQ(env.count("MarkView"), 0);
    EXPECT_FALSE(helper.updateMarkView(1, 7, true, true)); // cached (id 0), visible: nothing either
    EXPECT_TRUE(helper.updateMarkView(1, 7, false, true)); // hidden: S-2C
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|0"));
    EXPECT_EQ(logOf(env), QStringLiteral("MarkView.ins"));
    const auto* view = MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewId, 1);
    EXPECT_FALSE(helper.updateMarkView(1, 7, false, true)); // equal: no statement
    EXPECT_EQ(env.count("MarkView"), 1);
}

TEST(MarkDbImplORMHelperUpdate, PreparedViewCacheSkipsTheSqlLookup) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedHiddenMark(env); // the row exists in the database
    auto helper = env.helper();
    MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->addPrepared(
        7, {}); // category 7 "prepared", no rows
    EXPECT_FALSE(
        helper.updateMarkView(1, 7, true, true)); // treated as no row (S-36 skipped): visible, nothing
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|0"));
    // hiding adds a second row for the same mark and user: the unique index refuses it, CheckID throws
    try {
        std::ignore = helper.updateMarkView(1, 7, false, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
    EXPECT_EQ(env.count("MarkView"), 1);
}

TEST(MarkDbImplORMHelperUpdate, StoredViewRowWithCategoryZeroThrowsIdIsEmpty) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedHiddenMark(env);
    env.exec("UPDATE MarkView SET mvCategory=0");
    auto helper = env.helper();
    try {
        std::ignore = helper.updateMarkView(1, 7, true, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|0|0"));
}

TEST(MarkDbImplORMHelperUpdate, ReadOnlyViewUpdateIsTrueWithoutSql) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    seedHiddenMark(env);
    auto helper = env.helper(true);
    EXPECT_TRUE(helper.updateMarkView(1, 7, true, true)); // S-36 reads, the cache takes the new state
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|0"));
    const auto* view = MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(1);
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->visible);
    EXPECT_EQ(view->viewId, 1);
}

// visibility by id list (u_MarkDbImplORM.pas:1128): UpdateMarkView(id, category, visible, False) per item
// inside the caller's single transaction
TEST(MarkDbImplORMHelperUpdate, VisibilityByIdListInTheCallersTransactionCommitsOrRollsBackAsOne) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    for (const char* name : {"a", "b", "c"}) {
        OrmMarkRec r = rec(QString::fromUtf8(name), makePoint(), OrmGeoType::Point, 1, 7, true);
        ASSERT_TRUE(helper.insertMarkSql(r, true));
    }
    auto& transaction = env.provider().transaction();
    auto tr = transaction.start(false);
    EXPECT_TRUE(helper.updateMarkView(1, 7, false, false));
    EXPECT_TRUE(helper.updateMarkView(3, 7, false, false));
    EXPECT_EQ(env.count("MarkView"), 0); // the caller's transaction is open
    transaction.rollBack(tr);
    EXPECT_EQ(env.count("MarkView"), 0);

    auto second = env.helper(); // a new helper: the rolled-back state was cached by the first
    auto tr2 = transaction.start(false);
    EXPECT_TRUE(second.updateMarkView(1, 7, false, false));
    EXPECT_TRUE(second.updateMarkView(3, 7, false, false));
    transaction.commit(tr2);
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|1|7|0;2|1|3|7|0"));
}

// ---- setMarksInCategoryVisibleSql (W-6) -----------------------------------------------------------------

namespace {
// Marks 1 (visible), 2 (hidden), 3 (visible) in category 7; mark 4 (hidden) in category 9; a view row of
// user 2.
void seedCategories(Env& env, MarkDbImplORMHelper& helper) {
    const bool visibility[] = {true, false, true, false};
    const qint64 categories[] = {7, 7, 7, 9};
    for (int i = 0; i < 4; ++i) {
        OrmMarkRec r = rec(QStringLiteral("m%1").arg(i + 1), makePoint(), OrmGeoType::Point, 1, categories[i],
                           visibility[i]);
        ASSERT_TRUE(helper.insertMarkSql(r, true));
    }
    env.exec("INSERT INTO User (uName) VALUES ('other')");
    env.exec("INSERT INTO MarkView (mvUser,mvMark,mvCategory,mvVisible) VALUES (2,1,7,1)");
}
} // namespace

TEST(MarkDbImplORMHelperUpdate, HideByCategoryUpdatesExistingRowsAndAddsMissingOnesOfThisUserOnly) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    seedCategories(env, helper);
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|2|7|0;2|1|4|9|0;3|2|1|7|1"));
    installLog(env);
    ASSERT_NE(MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(2), nullptr);

    EXPECT_TRUE(helper.setMarksInCategoryVisibleSql(7, false));
    // user 1 / category 7: row 1 (mark 2) stays hidden, marks 1 and 3 get rows; user 2's row and category 9
    // stay
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|2|7|0;2|1|4|9|0;3|2|1|7|1;4|1|1|7|0;6|1|3|7|0"));
    EXPECT_EQ(logOf(env),
              QStringLiteral("MarkView.ins,MarkView.ins,MarkView.mvVisible")); // 1 UPDATEd row, 2 INSERTs
    EXPECT_EQ(MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(2), nullptr); // cache reset
}

TEST(MarkDbImplORMHelperUpdate, ShowByCategoryOnlyUpdatesAndTheRowsStay) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    seedCategories(env, helper);
    ASSERT_TRUE(helper.setMarksInCategoryVisibleSql(7, false));
    installLog(env);
    EXPECT_TRUE(helper.setMarksInCategoryVisibleSql(7, true));
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|2|7|1;2|1|4|9|0;3|2|1|7|1;4|1|1|7|1;6|1|3|7|1"));
    EXPECT_EQ(logOf(env),
              QStringLiteral("MarkView.mvVisible,MarkView.mvVisible,MarkView.mvVisible")); // 3 rows
}

TEST(MarkDbImplORMHelperUpdate, CategoryWithoutMarksAndWithoutRowsIsStillTrue) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    EXPECT_TRUE(helper.setMarksInCategoryVisibleSql(5, false));
    EXPECT_TRUE(helper.setMarksInCategoryVisibleSql(5, true));
    EXPECT_EQ(env.count("MarkView"), 0);
}

TEST(MarkDbImplORMHelperUpdate, CategoryZeroThrowsAndWritesNothing) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    seedCategories(env, helper);
    const QString before = joined(env.views());
    try {
        std::ignore = helper.setMarksInCategoryVisibleSql(0, false);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
    EXPECT_EQ(joined(env.views()), before);
    EXPECT_NE(MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(2),
              nullptr); // no reset before checkId
}

TEST(MarkDbImplORMHelperUpdate, FailedInsertStatementThrowsExecuteFailedAndTheUpdateStaysCommitted) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    seedCategories(env, helper);
    env.exec("CREATE TRIGGER T100_fail BEFORE INSERT ON MarkView BEGIN SELECT RAISE(ABORT,'no'); END");
    try {
        std::ignore = helper.setMarksInCategoryVisibleSql(7, false);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Execute operation is failed!");
    }
    // S-38 ran in its own autocommit before S-39 failed; nothing was inserted
    EXPECT_EQ(joined(env.views()), QStringLiteral("1|1|2|7|0;2|1|4|9|0;3|2|1|7|1"));
}

TEST(MarkDbImplORMHelperUpdate, FailedUpdateStatementThrowsUpdateFailed) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    env.exec("DROP TABLE MarkView");
    try {
        std::ignore = helper.setMarksInCategoryVisibleSql(7, true);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: Update operation is failed!");
    }
}

// ---- the read-only branch of setMarksInCategoryVisibleSql (:1321-1344, _TryFastUpdate :1271-1287) --------

namespace {
// The state of a view cache row as text: view id / category / visible, "-" when there is none.
QString viewRow(MarkDbImplORMHelper& helper, qint64 markId) {
    const auto* row = MarkDbImplORMHelperTestAccess::cache(helper).markViewCache->find(markId);
    if (row == nullptr) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1/%2/%3").arg(row->viewId).arg(row->categoryId).arg(row->visible ? 1 : 0);
}

QString listText(MarkDbImplORMHelper& helper, qint64 category, bool hidden) {
    std::vector<OrmMarkRec> marks;
    helper.getMarkRecArray(category, hidden, false, false, marks);
    QStringList parts;
    for (const OrmMarkRec& m : marks) {
        parts.append(QStringLiteral("%1:%2").arg(m.markId).arg(m.visible ? 1 : 0));
    }
    return parts.join(QLatin1Char(','));
}
} // namespace

TEST(MarkDbImplORMHelperUpdate, ReadOnlyHideCategoryChangesTheViewCacheOnlyAndKeepsExistingRowsAndIds) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    {
        auto writer = env.helper();
        seedCategories(env, writer);
    }
    const QString before = joined(env.views());
    ASSERT_EQ(before, QStringLiteral("1|1|2|7|0;2|1|4|9|0;3|2|1|7|1"));
    auto readOnly = env.helper(true);
    EXPECT_TRUE(readOnly.setMarksInCategoryVisibleSql(7, false));
    EXPECT_EQ(joined(env.views()), before); // the database is not written
    // mark 1 and 3 had no row of this user: added with the fake id 0; mark 2 is hidden already: untouched
    EXPECT_EQ(viewRow(readOnly, 1), QStringLiteral("0/7/0"));
    EXPECT_EQ(viewRow(readOnly, 2), QStringLiteral("1/7/0"));
    EXPECT_EQ(viewRow(readOnly, 3), QStringLiteral("0/7/0"));
    EXPECT_EQ(viewRow(readOnly, 4), QStringLiteral("-")); // category 9 was not touched
    EXPECT_EQ(listText(readOnly, 7, false), QString());
    EXPECT_EQ(listText(readOnly, 7, true), QStringLiteral("1:0,2:0,3:0"));
}

TEST(MarkDbImplORMHelperUpdate, ReadOnlyShowCategoryTakesTheCachedRowsOwnViewIdForAChangedRow) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    {
        auto writer = env.helper();
        seedCategories(env, writer);
    }
    const QString before = joined(env.views());
    auto readOnly = env.helper(true);
    EXPECT_TRUE(readOnly.setMarksInCategoryVisibleSql(9, true)); // mark 4 is hidden in the database
    EXPECT_EQ(viewRow(readOnly, 4), QStringLiteral("2/9/1"));    // its own view id (2), now visible
    EXPECT_EQ(listText(readOnly, 9, false), QStringLiteral("4:1"));
    EXPECT_TRUE(
        readOnly.setMarksInCategoryVisibleSql(7, true)); // mark 2 is hidden, marks 1 and 3 have no row
    EXPECT_EQ(viewRow(readOnly, 2), QStringLiteral("1/7/1"));
    EXPECT_EQ(viewRow(readOnly, 1), QStringLiteral("0/7/1"));
    EXPECT_EQ(viewRow(readOnly, 3), QStringLiteral("0/7/1"));
    EXPECT_EQ(listText(readOnly, 7, false), QStringLiteral("1:1,2:1,3:1"));
    EXPECT_EQ(joined(env.views()), before);
}

TEST(MarkDbImplORMHelperUpdate, ReadOnlyCategoryWithoutMarksIsFalseAndTouchesNothing) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    {
        auto writer = env.helper();
        seedCategories(env, writer);
    }
    const QString before = joined(env.views());
    auto readOnly = env.helper(true);
    EXPECT_FALSE(readOnly.setMarksInCategoryVisibleSql(5, false));
    EXPECT_FALSE(readOnly.setMarksInCategoryVisibleSql(5, true));
    EXPECT_EQ(joined(env.views()), before);
    EXPECT_EQ(viewRow(readOnly, 1), QStringLiteral("-"));
    try {
        std::ignore = readOnly.setMarksInCategoryVisibleSql(0, false);
        ADD_FAILURE() << "no throw";
    } catch (const MarkSystemORMError& e) {
        EXPECT_STREQ(e.what(), "MarkSystemORM: ID is empty!");
    }
}

TEST(MarkDbImplORMHelperUpdate, ReadOnlyHideKeepsACachedRowThatIsAlreadyAsWantedWhateverItsCategory) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    {
        auto writer = env.helper();
        seedCategories(env, writer);
    }
    env.exec("UPDATE MarkView SET mvCategory=9 WHERE ID=1"); // mark 2 (category 7): hidden, row says 9
    auto readOnly = env.helper(true);
    std::vector<OrmMarkRec> all;
    readOnly.getMarkRecArray(0, true, false, false, all); // whole view table cached
    ASSERT_EQ(viewRow(readOnly, 2), QStringLiteral("1/9/0"));
    EXPECT_TRUE(readOnly.setMarksInCategoryVisibleSql(7, false));
    EXPECT_EQ(viewRow(readOnly, 2), QStringLiteral("1/9/0")); // untouched (a rewrite would give 1/7/0)
    EXPECT_EQ(viewRow(readOnly, 1), QStringLiteral("0/7/0"));
}

// ---- geometry decoders :267-299 ------------------------------------------------------------------------

TEST(MarkDbImplORMHelperUpdate, DecodersParseTheWkbAndMetaOfTheWriteHalf) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    const QByteArray wkb = QByteArray::fromHex(kHexLine1);
    const QByteArray metaBlob(kMetaElevation100);
    const PointsMeta meta = MarkDbImplORMHelperTestAccess::geometryMetaFromBlob(helper, metaBlob);
    ASSERT_EQ(meta.elevation.size(), 2U);
    EXPECT_EQ(meta.elevation[0], 100.5);
    EXPECT_EQ(meta.elevation[1], 200.25);
    EXPECT_TRUE(meta.timestamp.empty());

    const GeometryLonLatPtr withMeta = MarkDbImplORMHelperTestAccess::geometryFromBlob(helper, wkb, &meta);
    ASSERT_NE(withMeta, nullptr);
    EXPECT_TRUE(withMeta->isSameGeometry(*makeLineWithElevation()));
    const GeometryLonLatPtr without = MarkDbImplORMHelperTestAccess::geometryFromBlob(helper, wkb, nullptr);
    ASSERT_NE(without, nullptr);
    EXPECT_TRUE(without->isSameGeometry(*makeLine()));
    EXPECT_EQ(without->bounds().right, 12.5);
    EXPECT_EQ(without->bounds().top, 22.75);
}

TEST(MarkDbImplORMHelperUpdate, DecodersTreatAnEmptyOrMalformedMetaBlobAsNoMetaAndThrowOnBadGeometryBlobs) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/Marks.db3")));
    Env env;
    auto helper = env.helper();
    EXPECT_TRUE(MarkDbImplORMHelperTestAccess::geometryMetaFromBlob(helper, QByteArray()).isEmpty());
    // invalid JSON, and JSON without the geometry array: nil in legacy, not an error
    EXPECT_TRUE(MarkDbImplORMHelperTestAccess::geometryMetaFromBlob(helper, QByteArray("JSON{")).isEmpty());
    EXPECT_TRUE(
        MarkDbImplORMHelperTestAccess::geometryMetaFromBlob(helper, QByteArray("JSON{\"v\":1,\"t\":1}"))
            .isEmpty());
    EXPECT_THROW(std::ignore = MarkDbImplORMHelperTestAccess::geometryFromBlob(helper, QByteArray(), nullptr),
                 MarkSystemORMError);
    EXPECT_THROW(std::ignore = MarkDbImplORMHelperTestAccess::geometryFromBlob(
                     helper, QByteArray::fromHex("0109"), nullptr),
                 MarkSystemORMError);
    // big-endian byte order (the first byte 0) is refused
    EXPECT_THROW(std::ignore = MarkDbImplORMHelperTestAccess::geometryFromBlob(
                     helper, QByteArray::fromHex("00000000010000000000000000"), nullptr),
                 MarkSystemORMError);
}
