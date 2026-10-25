// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/u_MarkFactorySmlDbInternal.pas (424, class TMarkFactorySmlDbInternal;
// docs/specs/marks.md section 2.8.4). It implements IMarkFactorySmlInternal (mark_factory_sml_internal.h).
// Ported in the Pascal order: CalcMultiGeometryCount (:121-133), Create (:137-155), CreatePoint (:157-227),
// CreateInternalMark (:229-260), CreateLine (:262-310), CreatePoly (:312-361), CreateMark (:363-422).
//
// Legacy FHashFunction is not kept: the hashes are the core free functions (core/hash.h), as in MarkFactory.
// Legacy Assert calls are not ported: the shipped build has assertions off (SASPlanet.dproj:92).
// Preconditions (legacy: none checked): the picture list, appearance factory, mark factory, hint converter
// and category database are non-null. The hint converter is dereferenced by the mark objects
// (MarkId::hintText) without a check, as in legacy (u_MarkFactorySmlDbInternal.pas:203, :287, :338); the
// composition root (the SML impl factory) guarantees it. The main info of a database mark is MarkId
// (sml/mark_id.h), as in legacy TMarkId.Create (:203, :287, :338).

#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/sml/mark_category_db_sml_internal.h"
#include "cg/marks/sml/mark_factory_sml_internal.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::marks {

class MarkFactorySmlDbInternal final : public IMarkFactorySmlInternal {
public:
    // dbId is the identity of the database (legacy FDbId, NativeInt).
    MarkFactorySmlDbInternal(quintptr dbId, vector::MarkPictureListPtr markPictureList,
                             vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                             std::shared_ptr<const IMarkFactory> markFactory,
                             vector::HtmlToHintTextConverterPtr hintConverter,
                             std::shared_ptr<const IMarkCategoryDBSmlInternal> categoryDb);

    // IMarkFactorySmlInternal
    [[nodiscard]] vector::VectorDataItemPtr createMark(int id, const QString& name, bool visible,
                                                       const QString& picName, int categoryId,
                                                       const QString& desc,
                                                       const geo::GeometryLonLatPtr& geometry, quint32 color1,
                                                       quint32 color2, int scale1, int scale2) const override;
    [[nodiscard]] vector::VectorDataItemPtr
    createInternalMark(const vector::VectorDataItemPtr& mark) const override;

private:
    // Legacy CreatePoint (:157-227), CreateLine (:262-310), CreatePoly (:312-361).
    [[nodiscard]] vector::VectorDataItemPtr
    createPoint(int id, const QString& name, bool visible, const QString& picName,
                const vector::CategoryPtr& category, const QString& desc,
                const geo::GeometryLonLatPtr& geometry, quint32 textColor, quint32 textBgColor, int fontSize,
                int markerSize) const;
    [[nodiscard]] vector::VectorDataItemPtr createLine(int id, const QString& name, bool visible,
                                                       const vector::CategoryPtr& category,
                                                       const QString& desc,
                                                       const geo::GeometryLonLatPtr& geometry,
                                                       quint32 lineColor, int lineWidth) const;
    [[nodiscard]] vector::VectorDataItemPtr
    createPoly(int id, const QString& name, bool visible, const vector::CategoryPtr& category,
               const QString& desc, const geo::GeometryLonLatPtr& geometry, quint32 borderColor,
               quint32 fillColor, int lineWidth) const;

    quintptr dbId_;
    vector::MarkPictureListPtr markPictureList_;
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;
    std::shared_ptr<const IMarkFactory> markFactory_;
    vector::HtmlToHintTextConverterPtr hintConverter_;
    std::shared_ptr<const IMarkCategoryDBSmlInternal> categoryDb_;
};

} // namespace cg::marks
