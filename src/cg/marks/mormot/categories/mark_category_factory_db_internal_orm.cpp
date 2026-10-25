// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/categories/mark_category_factory_db_internal_orm.h"

#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"

namespace cg::marks {

// TMarkCategoryFactoryDbInternalORM.Create (:59-63)
MarkCategoryFactoryDbInternalORM::MarkCategoryFactoryDbInternalORM(quintptr dbId) : dbId_(dbId) {}

// TMarkCategoryFactoryDbInternalORM.CreateCategory(AId, ...) (:65-82)
MarkCategoryPtr MarkCategoryFactoryDbInternalORM::createCategory(qint64 id, const QString& name, bool visible,
                                                                 quint8 afterScale,
                                                                 quint8 beforeScale) const {
    return std::make_shared<const MarkCategoryDbInternalORM>(id, dbId_, name, visible, afterScale,
                                                             beforeScale);
}

// TMarkCategoryFactoryDbInternalORM.CreateCategory(ACategoryRec) (:84-97)
MarkCategoryPtr MarkCategoryFactoryDbInternalORM::createCategory(const OrmCategoryRec& rec) const {
    return std::make_shared<const MarkCategoryDbInternalORM>(rec.categoryId, dbId_, rec.name, rec.visible,
                                                             rec.minZoom, rec.maxZoom);
}

} // namespace cg::marks
