// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/i_MarkCategoryFactoryDbInternal.pas (46): IMarkCategoryFactoryDbInternal and the
// constant CNotExistCategoryID (docs/specs/marks.md section 2.8.4). Interface and constant only; the class
// is in mark_category_factory_sml_db_internal.h.

#include "cg/marks/mark_category.h"

#include <QString>

namespace cg::marks {

// CNotExistCategoryID (i_MarkCategoryFactoryDbInternal.pas:42): the category id of a mark without an SML
// category. The SQLite backend uses 0 instead (see mark_id_orm.h).
inline constexpr int kNotExistCategoryId = -1;

class IMarkCategoryFactoryDbInternal {
public:
    virtual ~IMarkCategoryFactoryDbInternal() = default;
    [[nodiscard]] virtual MarkCategoryPtr createCategory(int id, const QString& name, bool visible,
                                                         int afterScale, int beforeScale) const = 0;
};

} // namespace cg::marks
