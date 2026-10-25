// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Marks/u_MarkIdORM.pas (322): TMarkIdORM, the mark row object of the SQLite
// backend (docs/specs/marks.md section 2.9.10). It implements IMarkId, IMarkInternalORM,
// IVectorDataItemMainInfo and IVectorDataItemWithCategory.
//
// Legacy methods without a caller in Src: IsEqualInternal (:221-244) is ported as protected, as in
// the Pascal, and has no caller in the port either.
// Legacy Assert(AId > 0) (u_MarkIdORM.pas:118) is not ported: the shipped build has assertions off
// (SASPlanet.dproj:92).
// The legacy hint converter has no assert; hintText() dereferences it without a nil check (:163-166),
// as the landed MarkMainInfo does. Callers must pass a non-null converter; O9 traces where it comes from.

#include "cg/core/hash.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/mormot/categories/mark_category_internal_orm.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"
#include "cg/vector/category.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

#include <atomic>

namespace cg::marks {

class MarkIdORM : public IMarkId,
                  public IMarkInternalORM,
                  public vector::IVectorDataItemMainInfo,
                  public vector::IVectorDataItemWithCategory {
public:
    // categoryId is taken from category when it is an IMarkCategoryInternalORM (:129-134), else 0.
    MarkIdORM(vector::HtmlToHintTextConverterPtr hintConverter, core::HashValue hash, MarkIdType type,
              QString name, QString desc, qint64 id, quintptr dbId, int multiGeometryCount,
              vector::CategoryPtr category, bool visible);

    // IMarkId
    [[nodiscard]] vector::CategoryPtr category() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] int multiGeometryCount() const override;
    [[nodiscard]] MarkIdType markType() const override;
    [[nodiscard]] bool isSameId(const IMarkId* markId) const override;
    [[nodiscard]] bool isSameMark(const vector::IVectorDataItem* mark) const override;

    // IMarkInternalORM
    [[nodiscard]] qint64 id() const override;
    [[nodiscard]] quintptr dbId() const override;
    [[nodiscard]] qint64 categoryId() const override;
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
    // Legacy IsEqualInternal (:221-244): dbId, categoryId, id, visible and name.
    [[nodiscard]] bool isEqualInternal(const IMarkInternalORM& markInternal) const;

private:
    vector::HtmlToHintTextConverterPtr hintConverter_;
    core::HashValue hash_;
    MarkIdType type_;
    QString name_;
    QString desc_;
    qint64 id_;
    quintptr dbId_;
    int multiGeometryCount_;
    vector::CategoryPtr category_;
    qint64 categoryId_;
    // Shared mark object: setVisible is const (IMarkInternalORM), so the flag is atomic (spec 2.9.10).
    mutable std::atomic<bool> visible_;
};

} // namespace cg::marks
