// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared helpers of the marks tests: a hint converter whose text shows both inputs, a point geometry, the
// legacy hash chain (UpdateHashBy* over little-endian bytes, UTF-16 strings), an ORM category and an SML
// category.

#include "cg/core/hash.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/sml/mark_category_sml_db_internal.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_data_item_base.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cg::marks::testutil {

class JoinConverter final : public vector::IHtmlToHintTextConverter {
public:
    [[nodiscard]] QString convert(const QString& name, const QString& description) const override {
        return name + QStringLiteral("|") + description;
    }
};

inline vector::HtmlToHintTextConverterPtr converter() {
    return std::make_shared<JoinConverter>();
}

inline geo::GeometryLonLatPtr point() {
    return std::make_shared<const geo::GeometryLonLatPoint>(1, geo::LonLat{1.0, 2.0});
}

// The other geometry kinds the import helpers tell apart: a line, a polygon, and a multipoint (no class of
// the import params matches it).
inline geo::GeometryLonLatPtr lineGeometry() {
    const std::vector<geo::LonLat> points{{10.5, 20.25}, {12.5, 22.75}};
    return geo::GeometryLonLatFactory().createLine(points, nullptr);
}
inline geo::GeometryLonLatPtr polygonGeometry() {
    const std::vector<geo::LonLat> points{{1.5, 2.5}, {3.5, 2.5}, {3.5, 4.5}};
    return geo::GeometryLonLatFactory().createPolygon(points);
}
inline geo::GeometryLonLatPtr multiPointGeometry() {
    const std::vector<geo::LonLat> points{{10.5, 20.25}, {12.5, 22.75}};
    return geo::GeometryLonLatFactory().createMultiPoint(points);
}

// A vector item with the given name and geometry: no description, no appearance.
inline vector::VectorDataItemPtr dataItem(const QString& name, geo::GeometryLonLatPtr geometry) {
    auto info = std::make_shared<const vector::VectorDataItemMainInfo>(0, converter(), name, QString());
    return std::make_shared<const vector::VectorDataItem>(0, nullptr, std::move(info), std::move(geometry));
}

// A mark (a vector item with the point geometry) whose main info is `info`: the mark side of isSameMark.
inline vector::VectorDataItemPtr markWithInfo(vector::VectorDataItemMainInfoPtr info) {
    return std::make_shared<const vector::VectorDataItem>(7, nullptr, std::move(info), point());
}
// Pictures by name, as IMarkPictureList answers them: indexByName searches `names`, get(i) answers items[i]
// (may be null), defaultPicture answers `fallback`. A default-constructed list has no pictures and no
// default.
class FakePictureList final : public vector::IMarkPictureList {
public:
    std::vector<QString> names;
    std::vector<vector::MarkPicturePtr> items;
    vector::MarkPicturePtr fallback;

    int count() const override { return static_cast<int>(items.size()); }
    vector::MarkPicturePtr get(int index) const override { return items.at(static_cast<std::size_t>(index)); }
    QString name(int index) const override { return names.at(static_cast<std::size_t>(index)); }
    int indexByName(const QString& value) const override {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == value) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    vector::MarkPicturePtr defaultPicture() const override { return fallback; }
    vector::MarkPicturePtr findByName(const QString&) const override { return nullptr; }
    vector::MarkPicturePtr findByNameOrDefault(const QString&) const override { return nullptr; }
};

// Forwards to a real transaction object and records "start:rw" / "start:ro" / "commit" / "rollback" (the
// event is recorded before the call is forwarded). `begins` counts the starts that opened a real transaction:
// a start() whose record is `isInternal` is the call that executes `BEGIN TRANSACTION`
// (mark_system_orm_transaction.cpp:17-38); SqliteDb has no sqlite3_trace hook, so this is the trace point.
// After every real COMMIT (an internal record) `probe`, when set, is called and its value is appended to
// `visibleAtCommit`: what a second connection sees.
class SpyTransaction final : public IMarkSystemORMTransaction {
public:
    explicit SpyTransaction(IMarkSystemORMTransaction& real, std::function<qint64()> probeFn = {})
        : probe(std::move(probeFn)), real_(real) {}
    TransactionRec start(bool isReadOnly) override {
        ++starts;
        events.append(isReadOnly ? QStringLiteral("start:ro") : QStringLiteral("start:rw"));
        const TransactionRec rec = real_.start(isReadOnly);
        if (rec.isInternal) {
            ++begins;
        }
        return rec;
    }
    void commit(TransactionRec& rec) override {
        events.append(QStringLiteral("commit"));
        const bool internal = rec.isInternal;
        real_.commit(rec);
        if (internal && probe) {
            visibleAtCommit.push_back(probe());
        }
    }
    void rollBack(TransactionRec& rec) noexcept override {
        events.append(QStringLiteral("rollback"));
        real_.rollBack(rec);
    }
    void reset() {
        events.clear();
        visibleAtCommit.clear();
        begins = 0;
        starts = 0;
    }
    std::function<qint64()> probe;
    QStringList events;
    std::vector<qint64> visibleAtCommit;
    int begins = 0;
    std::atomic<int> starts{0};

private:
    IMarkSystemORMTransaction& real_;
};

// An SQLite-backend category: row id `id` in the database `dbId`.
inline MarkCategoryPtr ormCategory(qint64 id, quintptr dbId = 1, const QString& name = QStringLiteral("c")) {
    return std::make_shared<const MarkCategoryDbInternalORM>(id, dbId, name, true, 3, 23);
}

// An SML-backend category: integer id `id` in the database `dbId` (visible, zooms 3 and 23).
inline MarkCategoryPtr smlCategory(int id, quintptr dbId = 1, const QString& name = QStringLiteral("c")) {
    return std::make_shared<const MarkCategorySmlDbInternal>(id, dbId, name, true, 3, 23);
}

// Legacy UpdateHashByHash / UpdateHashByString chain: CityHash64WithSeed over the bytes, seeded with the
// running hash. Used to write the expected values of the hash recipes from the Pascal, not from the code.
inline core::HashValue hashChainBytes(core::HashValue seed, const void* bytes, std::size_t size) {
    return core::cityHash64WithSeed(bytes, size, seed);
}
inline core::HashValue hashChainU64(core::HashValue seed, core::HashValue value) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<unsigned char>(value >> (8 * i));
    }
    return hashChainBytes(seed, b, 8);
}
// The UTF-16LE bytes of a string, as Delphi's string data is hashed.
inline std::string utf16LeBytes(const QString& s) {
    std::string out;
    for (const QChar c : s) {
        out.push_back(static_cast<char>(c.unicode() & 0xFF));
        out.push_back(static_cast<char>(c.unicode() >> 8));
    }
    return out;
}

} // namespace cg::marks::testutil
