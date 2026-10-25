// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/SML/u_MarkCategoryFactorySmlDbInternal.pas (78). Line references are to that unit.
#include "cg/marks/sml/mark_category_factory_sml_db_internal.h"

#include "cg/marks/sml/mark_category_sml_db_internal.h"

namespace cg::marks {

// TMarkCategoryFactorySmlDbInternal.Create (:54-58)
MarkCategoryFactorySmlDbInternal::MarkCategoryFactorySmlDbInternal(quintptr dbId) : dbId_(dbId) {}

// TMarkCategoryFactorySmlDbInternal.CreateCategory (:60-76)
MarkCategoryPtr MarkCategoryFactorySmlDbInternal::createCategory(int id, const QString& name, bool visible,
                                                                 int afterScale, int beforeScale) const {
    return std::make_shared<const MarkCategorySmlDbInternal>(id, dbId_, name, visible, afterScale,
                                                             beforeScale);
}

} // namespace cg::marks
