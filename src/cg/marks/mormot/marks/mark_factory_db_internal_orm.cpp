// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkFactoryDbInternalORM.pas (485). Line references in the comments are
// to that unit. Ported in the Pascal order: CreatePoint, CreateLine, CreatePoly, CreateMark (both overloads),
// GeoTypeToMarkType, CreateMarkId, CreateInternalMark.
#include "cg/marks/mormot/marks/mark_factory_db_internal_orm.h"

#include "cg/core/hash.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"
#include "cg/marks/mormot/marks/mark_id_orm.h"
#include "cg/vector/vector_data_item_base.h"

#include <QStringView>

#include <stdexcept>
#include <utility>

namespace cg::marks {

namespace {

// GeoTypeToMarkType (:413-423). Legacy raises plain Exception for both errors.
MarkIdType geoTypeToMarkType(OrmGeoType geoType) {
    switch (geoType) {
    case OrmGeoType::Point:
        return MarkIdType::Point;
    case OrmGeoType::Line:
        return MarkIdType::Line;
    case OrmGeoType::Poly:
        return MarkIdType::Poly;
    case OrmGeoType::Undef:
        throw std::runtime_error("MarkSystemORM: Undefined GeoType!"); // :419
    }
    throw std::runtime_error("MarkSystemORM: Unknown GeoType!"); // :421 (a value outside the enum)
}

} // namespace

MarkFactoryDbInternalORM::MarkFactoryDbInternalORM(
    quintptr dbId, vector::MarkPictureListPtr markPictureList,
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
    std::shared_ptr<const IMarkFactory> markFactory, vector::HtmlToHintTextConverterPtr hintConverter,
    std::shared_ptr<const IMarkCategoryDbInternalORM> categoryDb)
    : dbId_(dbId), markPictureList_(std::move(markPictureList)),
      appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)), markFactory_(std::move(markFactory)),
      hintConverter_(std::move(hintConverter)), categoryDb_(std::move(categoryDb)) {}

vector::VectorDataItemPtr
MarkFactoryDbInternalORM::createPoint(qint64 id, const QString& name, bool visible, const QString& picName,
                                      const vector::CategoryPtr& category, const QString& desc,
                                      const geo::GeometryLonLatPtr& geometry, quint32 textColor,
                                      quint32 textBgColor, qint32 fontSize, qint32 markerSize) const {
    // :176-187: the picture of the name; if there is none, the default picture (LC-5: the requested name
    // is replaced by the default's name). A found index whose picture is null keeps the requested name.
    const int picIndex = markPictureList_->indexByName(picName);
    vector::MarkPicturePtr pic =
        picIndex >= 0 ? markPictureList_->get(picIndex) : markPictureList_->defaultPicture();
    const QString usedPicName = pic != nullptr ? pic->name() : picName;

    // :190-198
    vector::AppearancePtr appearance = appearanceOfMarkFactory_->createPointAppearance(
        textColor, textBgColor, fontSize, usedPicName, pic, markerSize);

    // :200-214: main info hash = hash(name) chained with the description
    core::HashValue hash = core::hashOf(QStringView(name));
    core::hashUpdate(hash, QStringView(desc));
    auto mainInfo =
        std::make_shared<const MarkIdORM>(hintConverter_, hash, MarkIdType::Point, name, desc, id, dbId_,
                                          calcMultiGeometryCount(geometry), category, visible);

    // :216-218: item hash = geometry hash, then main info hash, then appearance hash
    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());

    return std::make_shared<const vector::VectorDataItem>(hash, std::move(appearance), std::move(mainInfo),
                                                          geometry);
}

vector::VectorDataItemPtr MarkFactoryDbInternalORM::createLine(qint64 id, const QString& name, bool visible,
                                                               const vector::CategoryPtr& category,
                                                               const QString& desc,
                                                               const geo::GeometryLonLatPtr& geometry,
                                                               quint32 lineColor, qint32 lineWidth) const {
    // :245-249
    vector::AppearancePtr appearance = appearanceOfMarkFactory_->createLineAppearance(lineColor, lineWidth);

    // :251-265
    core::HashValue hash = core::hashOf(QStringView(name));
    core::hashUpdate(hash, QStringView(desc));
    auto mainInfo =
        std::make_shared<const MarkIdORM>(hintConverter_, hash, MarkIdType::Line, name, desc, id, dbId_,
                                          calcMultiGeometryCount(geometry), category, visible);

    // :267-272
    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());

    return std::make_shared<const vector::VectorDataItem>(hash, std::move(appearance), std::move(mainInfo),
                                                          geometry);
}

vector::VectorDataItemPtr MarkFactoryDbInternalORM::createPoly(
    qint64 id, const QString& name, bool visible, const vector::CategoryPtr& category, const QString& desc,
    const geo::GeometryLonLatPtr& geometry, quint32 borderColor, quint32 fillColor, qint32 lineWidth) const {
    // :295-299: CreatePolygonAppearance(border colour, line width, fill colour)
    vector::AppearancePtr appearance =
        appearanceOfMarkFactory_->createPolygonAppearance(borderColor, lineWidth, fillColor);

    // :302-316
    core::HashValue hash = core::hashOf(QStringView(name));
    core::hashUpdate(hash, QStringView(desc));
    auto mainInfo =
        std::make_shared<const MarkIdORM>(hintConverter_, hash, MarkIdType::Poly, name, desc, id, dbId_,
                                          calcMultiGeometryCount(geometry), category, visible);

    // :318-323
    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());

    return std::make_shared<const vector::VectorDataItem>(hash, std::move(appearance), std::move(mainInfo),
                                                          geometry);
}

vector::VectorDataItemPtr MarkFactoryDbInternalORM::createMark(qint64 id, const QString& name, bool visible,
                                                               const QString& picName, qint64 categoryId,
                                                               const QString& desc,
                                                               const geo::GeometryLonLatPtr& geometry,
                                                               quint32 color1, quint32 color2, qint32 scale1,
                                                               qint32 scale2) const {
    // :346-348: no geometry gives nil before the category is looked up
    if (geometry == nullptr) {
        return nullptr;
    }
    // :348: the category is looked up before the geometry kind is tested, so every kind pays this call
    const vector::CategoryPtr category = categoryDb_->getCategoryById(categoryId);

    // :349-363: Supports(AGeometry, IGeometryLonLatPoint): colour1 text, colour2 text background,
    // scale1 font size, scale2 marker size
    if (dynamic_cast<const geo::IGeometryLonLatPoint*>(geometry.get()) != nullptr) {
        return createPoint(id, name, visible, picName, category, desc, geometry, color1, color2, scale1,
                           scale2);
    }
    // :364-375: a line takes colour1 and scale1; scale2 is not used
    if (dynamic_cast<const geo::IGeometryLonLatLine*>(geometry.get()) != nullptr) {
        return createLine(id, name, visible, category, desc, geometry, color1, scale1);
    }
    // :376-389: a polygon takes colour1 (border), colour2 (fill) and scale1 (line width)
    if (dynamic_cast<const geo::IGeometryLonLatPolygon*>(geometry.get()) != nullptr) {
        return createPoly(id, name, visible, category, desc, geometry, color1, color2, scale1);
    }
    // :346 Result := nil: a multi-point (or any other kind) gives nil, silently
    return nullptr;
}

vector::VectorDataItemPtr MarkFactoryDbInternalORM::createMark(const OrmMarkRec& markRec) const {
    // :397-410
    return createMark(markRec.markId, markRec.name, markRec.visible, markRec.picName, markRec.categoryId,
                      markRec.desc, markRec.geometry, markRec.color1, markRec.color2, markRec.scale1,
                      markRec.scale2);
}

MarkIdPtr MarkFactoryDbInternalORM::createMarkId(const OrmMarkRec& markRec) const {
    // :432-433
    core::HashValue hash = core::hashOf(QStringView(markRec.name));
    core::hashUpdate(hash, QStringView(markRec.desc));

    // :435
    const vector::CategoryPtr category = categoryDb_->getCategoryById(markRec.categoryId);

    // :437-450: the mark type is converted while the arguments are built, after the category lookup
    return std::make_shared<const MarkIdORM>(hintConverter_, hash, geoTypeToMarkType(markRec.geoType),
                                             markRec.name, markRec.desc, markRec.markId, dbId_,
                                             markRec.geoCount, category, markRec.visible);
}

vector::VectorDataItemPtr
MarkFactoryDbInternalORM::createInternalMark(const vector::VectorDataItemPtr& mark) const {
    // Assert(Assigned(AMark)) (:460) is not ported. With a nil mark every branch below is skipped and the
    // result is nil (:482), so nil is returned here.
    if (mark == nullptr) {
        return nullptr;
    }
    const vector::VectorDataItemMainInfoPtr mainInfo = mark->mainInfo();

    // :462-467: a mark of this database (same dbId) is returned as it is, whatever its category
    const auto* markInternal = dynamic_cast<const IMarkInternalORM*>(mainInfo.get());
    if (markInternal != nullptr && markInternal->dbId() == dbId_) {
        return mark;
    }

    // :469-471: the category of the mark, if it has one (a nil category is not checked further)
    const auto* withCategory = dynamic_cast<const vector::IVectorDataItemWithCategory*>(mainInfo.get());
    if (withCategory != nullptr) {
        const vector::CategoryPtr category = withCategory->category();
        // :472: a category from another database is replaced by the first category of this database with
        // the same name, if there is one (:473-476)
        if (category != nullptr && !categoryDb_->isCategoryFromThisDb(category)) {
            const MarkCategoryPtr found = categoryDb_->getFirstCategoryByName(category->name());
            if (found != nullptr) {
                return markFactory_->replaceCategory(mark, found);
            }
        }
    }

    // :482 Result := AMark. The Assert of :481 is not ported: a mark whose category is from another database
    // and has no same-named category here is returned unchanged (legacy-defects.md, L-204
    // CreateInternalMark).
    return mark;
}

} // namespace cg::marks
