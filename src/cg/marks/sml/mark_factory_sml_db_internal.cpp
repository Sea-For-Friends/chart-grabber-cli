// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/SML/u_MarkFactorySmlDbInternal.pas (424). Line references in the comments are to that
// unit. Ported in the Pascal order: CalcMultiGeometryCount, Create, CreatePoint, CreateInternalMark,
// CreateLine, CreatePoly, CreateMark.
#include "cg/marks/sml/mark_factory_sml_db_internal.h"

#include "cg/core/hash.h"
#include "cg/marks/sml/mark_db_sml_internal.h"
#include "cg/marks/sml/mark_id.h"
#include "cg/vector/vector_data_item_base.h"

#include <QStringView>

#include <utility>

namespace cg::marks {

namespace {

// CalcMultiGeometryCount (:121-133): the part count of a multi line or a multi polygon, else 0. The SQLite
// twin gives 1 for a single geometry (L-168); this one gives 0.
int smlMultiGeometryCount(const geo::GeometryLonLatPtr& geometry) {
    if (const auto* line = dynamic_cast<const geo::IGeometryLonLatMultiLine*>(geometry.get())) {
        return static_cast<int>(line->count());
    }
    if (const auto* poly = dynamic_cast<const geo::IGeometryLonLatMultiPolygon*>(geometry.get())) {
        return static_cast<int>(poly->count());
    }
    return 0;
}

} // namespace

// TMarkFactorySmlDbInternal.Create (:137-155)
MarkFactorySmlDbInternal::MarkFactorySmlDbInternal(
    quintptr dbId, vector::MarkPictureListPtr markPictureList,
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
    std::shared_ptr<const IMarkFactory> markFactory, vector::HtmlToHintTextConverterPtr hintConverter,
    std::shared_ptr<const IMarkCategoryDBSmlInternal> categoryDb)
    : dbId_(dbId), markPictureList_(std::move(markPictureList)),
      appearanceOfMarkFactory_(std::move(appearanceOfMarkFactory)), markFactory_(std::move(markFactory)),
      hintConverter_(std::move(hintConverter)), categoryDb_(std::move(categoryDb)) {}

// CreatePoint (:157-227)
vector::VectorDataItemPtr
MarkFactorySmlDbInternal::createPoint(int id, const QString& name, bool visible, const QString& picName,
                                      const vector::CategoryPtr& category, const QString& desc,
                                      const geo::GeometryLonLatPtr& geometry, quint32 textColor,
                                      quint32 textBgColor, int fontSize, int markerSize) const {
    // :176-188: the picture of the name; if there is none, the default picture. The appearance keeps the
    // found picture's name, or the requested name only when there is no picture at all.
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
    auto mainInfo = std::make_shared<const MarkId>(hintConverter_, hash, MarkIdType::Point, name, desc, id,
                                                   dbId_, smlMultiGeometryCount(geometry), category, visible);

    // :216-218: item hash = geometry hash, then main info hash, then appearance hash
    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());

    return std::make_shared<const vector::VectorDataItem>(hash, std::move(appearance), std::move(mainInfo),
                                                          geometry);
}

// CreateInternalMark (:229-260)
vector::VectorDataItemPtr
MarkFactorySmlDbInternal::createInternalMark(const vector::VectorDataItemPtr& mark) const {
    // Assert(Assigned(AMark)) (:237) is not ported. With a nil mark every branch below is skipped and the
    // result is nil (:259), so nil is returned here.
    if (mark == nullptr) {
        return nullptr;
    }
    const vector::VectorDataItemMainInfoPtr mainInfo = mark->mainInfo();

    // :239-244: a mark of this database (same dbId) is returned as it is, whatever its category
    const auto* markInternal = dynamic_cast<const IMarkSMLInternal*>(mainInfo.get());
    if (markInternal != nullptr && markInternal->dbId() == dbId_) {
        return mark;
    }

    // :245-257: the category of the mark, if it has one; a category from another database is replaced by the
    // first category of this database with the same name, if there is one (:249-256)
    const auto* withCategory = dynamic_cast<const vector::IVectorDataItemWithCategory*>(mainInfo.get());
    if (withCategory != nullptr) {
        const vector::CategoryPtr category = withCategory->category();
        if (category != nullptr && !categoryDb_->isCategoryFromThisDb(category)) {
            const MarkCategoryPtr found = categoryDb_->firstCategoryByName(category->name());
            if (found != nullptr) {
                return markFactory_->replaceCategory(mark, found);
            }
        }
    }

    // :258 Assert(Assigned(VCategory), ...) is not ported: a mark whose category is from another database and
    // has no same-named category here is returned unchanged (the SML row in docs/parity/legacy-defects.md).
    return mark;
}

// CreateLine (:262-310)
vector::VectorDataItemPtr MarkFactorySmlDbInternal::createLine(int id, const QString& name, bool visible,
                                                               const vector::CategoryPtr& category,
                                                               const QString& desc,
                                                               const geo::GeometryLonLatPtr& geometry,
                                                               quint32 lineColor, int lineWidth) const {
    // :278-282
    vector::AppearancePtr appearance = appearanceOfMarkFactory_->createLineAppearance(lineColor, lineWidth);

    // :284-298
    core::HashValue hash = core::hashOf(QStringView(name));
    core::hashUpdate(hash, QStringView(desc));
    auto mainInfo = std::make_shared<const MarkId>(hintConverter_, hash, MarkIdType::Line, name, desc, id,
                                                   dbId_, smlMultiGeometryCount(geometry), category, visible);

    // :300-302
    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());

    return std::make_shared<const vector::VectorDataItem>(hash, std::move(appearance), std::move(mainInfo),
                                                          geometry);
}

// CreatePoly (:312-361)
vector::VectorDataItemPtr MarkFactorySmlDbInternal::createPoly(
    int id, const QString& name, bool visible, const vector::CategoryPtr& category, const QString& desc,
    const geo::GeometryLonLatPtr& geometry, quint32 borderColor, quint32 fillColor, int lineWidth) const {
    // :328-333: CreatePolygonAppearance(border colour, line width, fill colour)
    vector::AppearancePtr appearance =
        appearanceOfMarkFactory_->createPolygonAppearance(borderColor, lineWidth, fillColor);

    // :335-349
    core::HashValue hash = core::hashOf(QStringView(name));
    core::hashUpdate(hash, QStringView(desc));
    auto mainInfo = std::make_shared<const MarkId>(hintConverter_, hash, MarkIdType::Poly, name, desc, id,
                                                   dbId_, smlMultiGeometryCount(geometry), category, visible);

    // :351-353
    hash = geometry->hash();
    core::hashUpdate(hash, mainInfo->hash());
    core::hashUpdate(hash, appearance->hash());

    return std::make_shared<const vector::VectorDataItem>(hash, std::move(appearance), std::move(mainInfo),
                                                          geometry);
}

// CreateMark (:363-422)
vector::VectorDataItemPtr MarkFactorySmlDbInternal::createMark(
    int id, const QString& name, bool visible, const QString& picName, int categoryId, const QString& desc,
    const geo::GeometryLonLatPtr& geometry, quint32 color1, quint32 color2, int scale1, int scale2) const {
    // :377-378: no geometry gives nil before the category is looked up
    if (geometry == nullptr) {
        return nullptr;
    }
    // :379: the category is looked up before the geometry kind is tested, so every kind pays this call
    const vector::CategoryPtr category = categoryDb_->categoryById(categoryId);

    // :380-420: point, then line, then polygon; any other kind (a multi-point) gives nil, silently
    if (dynamic_cast<const geo::IGeometryLonLatPoint*>(geometry.get()) != nullptr) {
        // colour1 text colour, colour2 text background, scale1 font size, scale2 marker size
        return createPoint(id, name, visible, picName, category, desc, geometry, color1, color2, scale1,
                           scale2);
    }
    if (dynamic_cast<const geo::IGeometryLonLatLine*>(geometry.get()) != nullptr) {
        // colour1 line colour, scale1 line width; colour2 and scale2 are not used
        return createLine(id, name, visible, category, desc, geometry, color1, scale1);
    }
    if (dynamic_cast<const geo::IGeometryLonLatPolygon*>(geometry.get()) != nullptr) {
        // colour1 border colour, colour2 fill colour, scale1 line width; scale2 is not used
        return createPoly(id, name, visible, category, desc, geometry, color1, color2, scale1);
    }
    return nullptr;
}

} // namespace cg::marks
