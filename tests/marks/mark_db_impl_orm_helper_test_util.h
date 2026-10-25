// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared by the tests of the SQLite mark helper (tests/marks/mark_db_impl_orm_helper_*_test.cpp): a temp copy
// of tests/fixtures/marks/Marks.db3 with the provider and a second plain connection for read-back, the
// geometry makers and the mark record builder. Hoisted from the write-half test (T-0095) by T-0100.
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

#include <memory>
#include <ostream>
#include <tuple>
#include <vector>

#include "fixture_path.h"

// gtest prints a QString as its text (it would print the UTF-16 code units).
inline void PrintTo(const QString& text, std::ostream* os) {
    *os << text.toStdString();
}

namespace cg::marks {
// Declared a friend by the helper: the caches and the private methods.
struct MarkDbImplORMHelperTestAccess {
    static OrmMarkDbCache& cache(MarkDbImplORMHelper& h) { return h.cache_; }
    static qint64 addMarkImage(MarkDbImplORMHelper& h, const QString& n) { return h.addMarkImage(n); }
    static void readMarkImage(MarkDbImplORMHelper& h, OrmMarkRec& r) { h.readMarkImage(r); }
    static qint64 addMarkAppearance(MarkDbImplORMHelper& h, quint32 c1, quint32 c2, qint32 s1, qint32 s2) {
        return h.addMarkAppearance(c1, c2, s1, s2);
    }
    static void readMarkAppearance(MarkDbImplORMHelper& h, OrmMarkRec& r) { h.readMarkAppearance(r); }
    static geo::GeometryLonLatPtr geometryFromBlob(MarkDbImplORMHelper& h, const QByteArray& b,
                                                   const geo::PointsMeta* m) {
        return h.geometryFromBlob(b, m);
    }
    static geo::PointsMeta geometryMetaFromBlob(MarkDbImplORMHelper& h, const QByteArray& b) {
        return h.geometryMetaFromBlob(b);
    }
    static int fillPrepareMarkIdIndex(MarkDbImplORMHelper& h, qint64 c) {
        return h.fillPrepareMarkIdIndex(c);
    }
    static void fillPrepareMarkIdCache(MarkDbImplORMHelper& h, qint64 c) { h.fillPrepareMarkIdCache(c); }
    static void fillPrepareMarkGeometryCache(MarkDbImplORMHelper& h, qint64 c) {
        h.fillPrepareMarkGeometryCache(c);
    }
    static void fillPrepareMarkViewCache(MarkDbImplORMHelper& h, qint64 c) { h.fillPrepareMarkViewCache(c); }
};
} // namespace cg::marks

namespace cg::marks::testutil {

using geo::GeometryLonLatFactory;
using geo::GeometryLonLatLineBuilder;
using geo::GeometryLonLatPtr;
using geo::LonLat;
using geo::PointsMeta;
using storage::sqlite::SqliteDb;
using storage::sqlite::SqliteStmt;

class Env {
public:
    Env() {
        EXPECT_TRUE(dir_.isValid());
        const QString path = QDir(dir_.path()).filePath(QStringLiteral("Marks.db3"));
        EXPECT_TRUE(QFile::copy(cgMarksFixture(QStringLiteral("marks/Marks.db3")), path));
        EXPECT_TRUE(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        provider_ = std::make_unique<MarkSystemImplORMClientProvider>(
            dir_.path(), MarkSystemImplConfigORM({}, false, {}, {}, {}));
        auto db = SqliteDb::open(path, SqliteDb::OpenMode::ReadWrite);
        if (!db) {
            ADD_FAILURE() << db.error().what().toStdString();
            std::abort();
        }
        plain_ = std::make_unique<SqliteDb>(std::move(*db));
        std::ignore = plain_->createCollation("SYSTEMNOCASE", &systemNoCaseCompare);
    }

    MarkSystemImplORMClientProvider& provider() { return *provider_; }
    MarkDbImplORMHelper helper(bool readOnly = false, quint32 cacheSizeMb = 1);

    void exec(const char* sql) {
        const auto done = plain_->exec(sql);
        ASSERT_TRUE(done.has_value()) << sql;
    }

    QStringList rows(const char* sql) {
        QStringList out;
        auto stmt = plain_->prepare(sql);
        if (!stmt) {
            ADD_FAILURE() << stmt.error().what().toStdString();
            return out;
        }
        for (auto step = stmt->step(); step && *step == SqliteStmt::StepResult::Row; step = stmt->step()) {
            QStringList columns;
            for (int i = 0; i < stmt->columnCount(); ++i) {
                columns.append(stmt->columnText(i));
            }
            out.append(columns.join(QLatin1Char('|')));
        }
        return out;
    }

    QStringList marks() {
        return rows("SELECT ID,mCategory,mImage,mAppearance,mName,mDesc,mGeoType,mGeoCount,mGeoLonSize,"
                    "mGeoLatSize,typeof(mGeoWKB),lower(hex(mGeoWKB)) FROM Mark ORDER BY ID");
    }
    QStringList fts() { return rows("SELECT RowID,mName,mDesc FROM MarkFTS ORDER BY RowID"); }
    QStringList rtree() {
        return rows("SELECT RowID,mLeft,mRight,mBottom,mTop FROM MarkRTree ORDER BY RowID");
    }
    QStringList views() {
        return rows("SELECT ID,mvUser,mvMark,mvCategory,mvVisible FROM MarkView ORDER BY ID");
    }
    QStringList metas() { return rows("SELECT ID,mMark,CAST(mMeta AS TEXT) FROM MarkMeta ORDER BY ID"); }
    QStringList images() { return rows("SELECT ID,miName FROM MarkImage ORDER BY ID"); }
    QStringList appearances() {
        return rows("SELECT ID,maColor1,maColor2,maScale1,maScale2 FROM MarkAppearance ORDER BY ID");
    }
    qint64 count(const char* table) {
        return rows((QByteArray("SELECT COUNT(*) FROM ") + table).constData()).value(0).toLongLong();
    }

private:
    QTemporaryDir dir_;
    std::unique_ptr<MarkSystemImplORMClientProvider> provider_;
    std::unique_ptr<SqliteDb> plain_;
};

inline const GeometryLonLatFactory kFactory;

inline GeometryLonLatPtr makePoint() {
    return kFactory.createPoint(LonLat{10.5, 20.25});
}
inline GeometryLonLatPtr makeLine() {
    const std::vector<LonLat> p{{10.5, 20.25}, {12.5, 22.75}};
    return kFactory.createLine(p, nullptr);
}
inline GeometryLonLatPtr makeLineWithElevation() {
    const std::vector<LonLat> p{{10.5, 20.25}, {12.5, 22.75}};
    PointsMeta meta;
    meta.elevation = {100.5, 200.25};
    return kFactory.createLine(p, &meta);
}
inline GeometryLonLatPtr makePolygon() {
    const std::vector<LonLat> p{{1.5, 2.5}, {3.5, 2.5}, {3.5, 4.5}};
    return kFactory.createPolygon(p);
}
inline GeometryLonLatPtr makeMultiLine() {
    GeometryLonLatLineBuilder builder;
    builder.addLine(std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{0.0, 0.0}, {1.0, 2.0}}));
    builder.addLine(std::make_shared<const std::vector<LonLat>>(std::vector<LonLat>{{3.0, 4.0}, {5.0, 6.0}}));
    return builder.makeStaticAndClear();
}

inline OrmMarkRec rec(const QString& name, GeometryLonLatPtr geometry, OrmGeoType type, int geoCount = 1,
                      qint64 categoryId = 7, bool visible = true, const QString& pic = {}) {
    OrmMarkRec r;
    r.categoryId = categoryId;
    r.name = name;
    r.geometry = std::move(geometry);
    r.geoType = type;
    r.geoCount = geoCount;
    r.visible = visible;
    r.picName = pic;
    r.color1 = 4279312947U;
    r.color2 = 4294967295U;
    r.scale1 = 12;
    r.scale2 = -3;
    return r;
}

inline MarkDbImplORMHelper Env::helper(bool readOnly, quint32 cacheSizeMb) {
    return {readOnly, cacheSizeMb, kFactory, *provider_};
}

inline const QString kTown = QString::fromUtf8("Город");

} // namespace cg::marks::testutil
