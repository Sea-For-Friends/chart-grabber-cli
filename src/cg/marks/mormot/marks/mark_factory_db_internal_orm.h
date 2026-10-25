// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Marks/i_MarkFactoryDbInternalORM.pas (65) and
// Src/MarksDB/mORMot/Marks/u_MarkFactoryDbInternalORM.pas (485, class TMarkFactoryDbInternalORM;
// docs/specs/marks.md section 2.9.10). The interface is IMarkFactoryDbInternalORM; the class
// MarkFactoryDbInternalORM is declared next to it because the mark system implementation constructs it
// directly (legacy u_MarkSystemImplORM.pas:166). Its .cpp is mark_factory_db_internal_orm.cpp (ticket
// T-0083).
//
// Colours are TColor32 (quint32) and scales are Integer (qint32), as in OrmMarkRec.
//
// Legacy FHashFunction is not kept: the hashes are the core free functions, as in MarkFactory.
// Legacy Assert calls are not ported: the shipped build has assertions off (SASPlanet.dproj:92).
// Preconditions (legacy: none checked, a null collaborator is an access violation on first use): all
// collaborators are non-null. The hint converter is the one the mark objects keep: a null converter
// is dereferenced by hintText(), as in MarkMainInfo (see mark_id_orm.h).

#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::marks {

class IMarkFactoryDbInternalORM {
public:
    virtual ~IMarkFactoryDbInternalORM() = default;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    createMark(qint64 id, const QString& name, bool visible, const QString& picName, qint64 categoryId,
               const QString& desc, const geo::GeometryLonLatPtr& geometry, quint32 color1, quint32 color2,
               qint32 scale1, qint32 scale2) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr createMark(const OrmMarkRec& markRec) const = 0;
    [[nodiscard]] virtual MarkIdPtr createMarkId(const OrmMarkRec& markRec) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    createInternalMark(const vector::VectorDataItemPtr& mark) const = 0;
};

class MarkFactoryDbInternalORM final : public IMarkFactoryDbInternalORM {
public:
    // Legacy Create (:137-155). dbId is the identity of the database (legacy FDbId, NativeInt).
    MarkFactoryDbInternalORM(quintptr dbId, vector::MarkPictureListPtr markPictureList,
                             vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                             std::shared_ptr<const IMarkFactory> markFactory,
                             vector::HtmlToHintTextConverterPtr hintConverter,
                             std::shared_ptr<const IMarkCategoryDbInternalORM> categoryDb);

    // IMarkFactoryDbInternalORM
    [[nodiscard]] vector::VectorDataItemPtr
    createMark(qint64 id, const QString& name, bool visible, const QString& picName, qint64 categoryId,
               const QString& desc, const geo::GeometryLonLatPtr& geometry, quint32 color1, quint32 color2,
               qint32 scale1, qint32 scale2) const override;
    [[nodiscard]] vector::VectorDataItemPtr createMark(const OrmMarkRec& markRec) const override;
    [[nodiscard]] MarkIdPtr createMarkId(const OrmMarkRec& markRec) const override;
    [[nodiscard]] vector::VectorDataItemPtr
    createInternalMark(const vector::VectorDataItemPtr& mark) const override;

private:
    // Legacy CreatePoint (:157-227), CreateLine (:229-277), CreatePoly (:279-328).
    [[nodiscard]] vector::VectorDataItemPtr
    createPoint(qint64 id, const QString& name, bool visible, const QString& picName,
                const vector::CategoryPtr& category, const QString& desc,
                const geo::GeometryLonLatPtr& geometry, quint32 textColor, quint32 textBgColor,
                qint32 fontSize, qint32 markerSize) const;
    [[nodiscard]] vector::VectorDataItemPtr createLine(qint64 id, const QString& name, bool visible,
                                                       const vector::CategoryPtr& category,
                                                       const QString& desc,
                                                       const geo::GeometryLonLatPtr& geometry,
                                                       quint32 lineColor, qint32 lineWidth) const;
    [[nodiscard]] vector::VectorDataItemPtr
    createPoly(qint64 id, const QString& name, bool visible, const vector::CategoryPtr& category,
               const QString& desc, const geo::GeometryLonLatPtr& geometry, quint32 borderColor,
               quint32 fillColor, qint32 lineWidth) const;

    quintptr dbId_;
    vector::MarkPictureListPtr markPictureList_;
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;
    std::shared_ptr<const IMarkFactory> markFactory_;
    vector::HtmlToHintTextConverterPtr hintConverter_;
    std::shared_ptr<const IMarkCategoryDbInternalORM> categoryDb_;
};

} // namespace cg::marks
