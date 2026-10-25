// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/u_MarkId.pas (315, class TMarkId; docs/specs/marks.md section 2.8.4). It implements
// IMarkId (the interface of mark_id.h in the marks root, landed and not edited here), IMarkSMLInternal
// (mark_db_sml_internal.h), IVectorDataItemMainInfo and IVectorDataItemWithCategory.
//
// Legacy Assert(AId >= 0) (u_MarkId.pas:111) is not ported: the shipped build has assertions off
// (SASPlanet.dproj:92).
// Differences from the SQLite twin (mormot/marks/mark_id_orm.h): the id is an Integer (int); a mark without
// an SML category has categoryId -1 (kNotExistCategoryId, :122), the twin uses 0; a category counts as an SML
// category only through IMarkCategorySMLInternal.
// Callers must pass a non-null converter: hintText() dereferences it without a check, as in MarkIdORM and
// MarkMainInfo (legacy u_MarkId.pas:158, assertions off). Traced by the reviewer: TMarkId.Create is called
// only at u_MarkFactorySmlDbInternal.pas:203,287,338 <- u_MarkSystemSml.pas:217 <-
// u_MarkSystemImplFactorySML.pas:109,137, whose Assert (:99) comes from u_GlobalState.pas:773,790, where the
// converter is always created.

#include "cg/core/hash.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/sml/mark_db_sml_internal.h"
#include "cg/vector/category.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

#include <atomic>

namespace cg::marks {

class MarkId : public IMarkId,
               public IMarkSMLInternal,
               public vector::IVectorDataItemMainInfo,
               public vector::IVectorDataItemWithCategory {
public:
    // categoryId is taken from category when it is an IMarkCategorySMLInternal (:123-127), else -1.
    MarkId(vector::HtmlToHintTextConverterPtr hintConverter, core::HashValue hash, MarkIdType type,
           QString name, QString desc, int id, quintptr dbId, int multiGeometryCount,
           vector::CategoryPtr category, bool visible);

    // IMarkId
    [[nodiscard]] vector::CategoryPtr category() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] int multiGeometryCount() const override;
    [[nodiscard]] MarkIdType markType() const override;
    [[nodiscard]] bool isSameId(const IMarkId* markId) const override;
    [[nodiscard]] bool isSameMark(const vector::IVectorDataItem* mark) const override;

    // IMarkSMLInternal
    [[nodiscard]] int id() const override;
    [[nodiscard]] quintptr dbId() const override;
    [[nodiscard]] int categoryId() const override;
    [[nodiscard]] bool visible() const override;
    void setVisible(bool value) const override;

    // IVectorDataItemMainInfo
    [[nodiscard]] core::HashValue hash() const noexcept override;
    [[nodiscard]] QString desc() const override;
    [[nodiscard]] bool isEqual(const vector::IVectorDataItemMainInfo* item) const override;
    [[nodiscard]] QString hintText() const override;
    [[nodiscard]] QString infoUrl() const override;
    [[nodiscard]] QString infoHtml() const override;
    [[nodiscard]] QString infoCaption() const override;

protected:
    // Legacy IsEqualInternal (:214-237): dbId, categoryId, id, visible and name. No caller in legacy Src.
    [[nodiscard]] bool isEqualInternal(const IMarkSMLInternal& markInternal) const;

private:
    // Legacy GetStringID (:204-207): IntToStr(FId).
    [[nodiscard]] QString stringId() const;

    vector::HtmlToHintTextConverterPtr hintConverter_;
    core::HashValue hash_;
    MarkIdType type_;
    QString name_;
    QString desc_;
    int id_;
    quintptr dbId_;
    int multiGeometryCount_;
    vector::CategoryPtr category_;
    int categoryId_;
    // Shared mark object: setVisible is const (IMarkSMLInternal), so the flag is atomic (spec 2.8.4).
    mutable std::atomic<bool> visible_;
};

} // namespace cg::marks
