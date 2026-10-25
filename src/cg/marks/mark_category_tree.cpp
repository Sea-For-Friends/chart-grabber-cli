// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_category_tree.h"

#include <utility>

namespace cg::marks {

MarkCategoryTree::MarkCategoryTree(MarkCategoryPtr markCategory, QString name,
                                   std::vector<MarkCategoryTreePtr> subItems)
    : markCategory_(std::move(markCategory)), name_(std::move(name)), subItems_(std::move(subItems)) {}

MarkCategoryPtr MarkCategoryTree::markCategory() const {
    return markCategory_;
}

QString MarkCategoryTree::name() const {
    return name_;
}

int MarkCategoryTree::subItemCount() const {
    return static_cast<int>(subItems_.size());
}

MarkCategoryTreePtr MarkCategoryTree::subItem(int index) const {
    if (index < 0 || index >= subItemCount()) {
        return nullptr;
    }
    return subItems_[static_cast<std::size_t>(index)];
}

} // namespace cg::marks
