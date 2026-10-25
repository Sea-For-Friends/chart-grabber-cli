// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategoryDBImpl.pas  (the facade over it is mark_category_db.h)

#include "cg/core/notifier.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_list.h"
#include "cg/vector/category.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkCategoryDBImpl {
public:
    virtual ~IMarkCategoryDBImpl() = default;
    [[nodiscard]] virtual MarkCategoryPtr firstCategoryByName(const QString& name) const = 0;
    [[nodiscard]] virtual int categoryByNameCount(const QString& name) const = 0;
    virtual MarkCategoryPtr updateCategory(const MarkCategoryPtr& oldCategory,
                                           const MarkCategoryPtr& newCategory) = 0;
    virtual MarkCategoryListPtr updateCategoryList(const MarkCategoryListPtr& oldList,
                                                   const MarkCategoryListPtr& newList) = 0;
    [[nodiscard]] virtual bool isCategoryFromThisDb(const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkCategoryListPtr categoriesList() const = 0;
    virtual void setAllCategoriesVisible(bool visible) = 0;
    [[nodiscard]] virtual core::Notifier& changeNotifier() = 0;
};

} // namespace cg::marks
