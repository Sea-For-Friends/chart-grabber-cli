// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Categories/i_MarkCategoryDbInternalORM.pas (42 lines, interface
// IMarkCategoryDbInternalORM) and Src/MarksDB/mORMot/Categories/u_MarkCategoryDbInternalORM.pas
// (170 lines, class MarkCategoryDbInternalORM). Spec: docs/specs/marks.md §2.9.6.
//
// MarkCategoryDbInternalORM is the category object of the SQLite backend: an IMarkCategory that also
// carries its row id and the dbId of the database it was read from.
//   isSame()  compares the row id only (legacy IsSame, :158-168); dbId is not compared (see the
//             L-193 row of docs/parity/legacy-defects.md).
//   isEqual() compares name, visible, afterScale and beforeScale; the row id is not compared (:132-156).
// The constructor's Assert calls (:88-92) are not ported: the shipped build has assertions off
// (SASPlanet.dproj:92, DCC_AssertionsAtRuntime False in the Release configuration), so every argument
// is accepted as is.

#include "cg/marks/mark_category.h"
#include "cg/marks/mormot/categories/mark_category_internal_orm.h"
#include "cg/vector/category.h"

#include <QString>
#include <QtGlobal>

#include <memory>

namespace cg::marks {

class IMarkCategoryDbInternalORM {
public:
    virtual ~IMarkCategoryDbInternalORM() = default;
    [[nodiscard]] virtual bool isCategoryFromThisDb(const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr getCategoryById(qint64 id) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr getFirstCategoryByName(const QString& name) const = 0;
    [[nodiscard]] virtual int getCategoryByNameCount(const QString& name) const = 0;
};

class MarkCategoryDbInternalORM final : public IMarkCategory, public IMarkCategoryInternalORM {
public:
    MarkCategoryDbInternalORM(qint64 id, quintptr dbId, QString name, bool visible, int afterScale,
                              int beforeScale);

    [[nodiscard]] qint64 id() const override;
    [[nodiscard]] quintptr dbId() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isSame(const vector::ICategory* category) const override;
    [[nodiscard]] bool isEqual(const vector::ICategory* category) const override;
    [[nodiscard]] bool visible() const override;
    [[nodiscard]] int afterScale() const override;
    [[nodiscard]] int beforeScale() const override;

private:
    qint64 id_;
    quintptr dbId_;
    QString name_;
    bool visible_;
    int afterScale_;
    int beforeScale_;
};

} // namespace cg::marks
