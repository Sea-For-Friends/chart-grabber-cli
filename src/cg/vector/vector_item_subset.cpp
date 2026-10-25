// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-GEO-037
#include "cg/vector/vector_item_subset.h"

#include <stdexcept>
#include <utility>

namespace cg::vector {

VectorItemSubset::VectorItemSubset(core::HashValue hash, std::vector<VectorDataItemPtr> list)
    : hash_(hash), list_(std::move(list)) {}

bool VectorItemSubset::isEmpty() const {
    return list_.empty();
}

int VectorItemSubset::count() const {
    return static_cast<int>(list_.size());
}

core::HashValue VectorItemSubset::hash() const noexcept {
    return hash_;
}

VectorDataItemPtr VectorItemSubset::item(int index) const {
    if (index < 0 || index >= count()) {
        throw std::out_of_range("VectorItemSubset::item: index out of range");
    }
    return list_[static_cast<std::size_t>(index)];
}

bool VectorItemSubset::isEqual(const IVectorItemSubset* subset) const {
    if (subset == nullptr) {
        return false;
    }
    if (subset == this) {
        return true;
    }
    if (count() != subset->count()) {
        return false;
    }
    if (hash_ != 0 && subset->hash() != 0 && hash_ != subset->hash()) {
        return false;
    }
    for (int i = 0; i < count(); ++i) {
        if (!list_[static_cast<std::size_t>(i)]->isEqual(subset->item(i).get())) {
            return false;
        }
    }
    return true;
}

} // namespace cg::vector
