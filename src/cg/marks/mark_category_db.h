// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategoryDB.pas  (the implementation is the facade mark_category_db_by_impl, ticket M5b)

#include "cg/core/notifier.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkCategoryDB {
public:
    virtual ~IMarkCategoryDB() = default;
    [[nodiscard]] virtual MarkCategoryPtr firstCategoryByName(const QString& name) const = 0;
    [[nodiscard]] virtual int categoryByNameCount(const QString& name) const = 0;
    // The category first, then every "name\..." child.
    [[nodiscard]] virtual MarkCategoryListPtr
    categoryWithSubCategories(const MarkCategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkCategoryListPtr
    subCategoryListForCategory(const MarkCategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkCategoryListPtr categoriesList() const = 0;
    [[nodiscard]] virtual MarkCategoryListPtr visibleCategories(quint8 zoom) const = 0;
    [[nodiscard]] virtual MarkCategoryListPtr visibleCategoriesIgnoreZoom() const = 0;
    [[nodiscard]] virtual MarkCategoryTreePtr
    categoryListToStaticTree(const MarkCategoryListPtr& list) const = 0;
    [[nodiscard]] virtual MarkCategoryListPtr
    filterVisibleCategories(const MarkCategoryListPtr& source) const = 0;
    virtual MarkCategoryPtr updateCategory(const MarkCategoryPtr& oldCategory,
                                           const MarkCategoryPtr& newCategory) = 0;
    virtual MarkCategoryListPtr updateCategoryList(const MarkCategoryListPtr& oldList,
                                                   const MarkCategoryListPtr& newList) = 0;
    virtual void setAllCategoriesVisible(bool visible) = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkCategoryFactory> factory() const = 0;
    [[nodiscard]] virtual core::Notifier& changeNotifier() = 0;
};

} // namespace cg::marks
