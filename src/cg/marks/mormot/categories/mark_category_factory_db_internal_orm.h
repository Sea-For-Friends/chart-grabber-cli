// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Categories/i_MarkCategoryFactoryDbInternalORM.pas (48 lines, interface
// IMarkCategoryFactoryDbInternalORM) and Src/MarksDB/mORMot/Categories/u_MarkCategoryFactoryDbInternalORM.pas
// (99 lines, class MarkCategoryFactoryDbInternalORM). Spec: docs/specs/marks.md §2.9.6.
//
// The factory stores the dbId of its database and stamps it on every category it creates.

#include "cg/marks/mark_category.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QString>
#include <QtGlobal>

namespace cg::marks {

class IMarkCategoryFactoryDbInternalORM {
public:
    virtual ~IMarkCategoryFactoryDbInternalORM() = default;
    [[nodiscard]] virtual MarkCategoryPtr createCategory(qint64 id, const QString& name, bool visible,
                                                         quint8 afterScale, quint8 beforeScale) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr createCategory(const OrmCategoryRec& rec) const = 0;
};

class MarkCategoryFactoryDbInternalORM final : public IMarkCategoryFactoryDbInternalORM {
public:
    explicit MarkCategoryFactoryDbInternalORM(quintptr dbId);

    [[nodiscard]] MarkCategoryPtr createCategory(qint64 id, const QString& name, bool visible,
                                                 quint8 afterScale, quint8 beforeScale) const override;
    [[nodiscard]] MarkCategoryPtr createCategory(const OrmCategoryRec& rec) const override;

private:
    quintptr dbId_;
};

} // namespace cg::marks
