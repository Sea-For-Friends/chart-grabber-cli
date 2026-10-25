// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_category_list.h"

#include <memory>
#include <utility>

namespace cg::marks {

MarkCategoryList::MarkCategoryList(std::vector<MarkCategoryPtr> list) : list_(std::move(list)) {}

MarkCategoryListPtr MarkCategoryList::build(std::vector<MarkCategoryPtr> list) {
    if (list.empty()) {
        return nullptr;
    }
    return std::make_shared<const MarkCategoryList>(std::move(list));
}

int MarkCategoryList::count() const {
    return static_cast<int>(list_.size());
}

MarkCategoryPtr MarkCategoryList::item(int index) const {
    return list_.at(
        static_cast<std::size_t>(index)); // a negative index wraps to a huge one: out_of_range too
}

} // namespace cg::marks
