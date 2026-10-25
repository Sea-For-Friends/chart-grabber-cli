// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-GEO-037
#include "cg/vector/vector_item_subset_builder.h"

#include "cg/vector/sort_func.h"

#include <stdexcept>
#include <utility>

namespace cg::vector {

int VectorItemSubsetBuilder::add(const VectorDataItemPtr& item) {
    if (!item) {
        return -1;
    }
    list_.push_back(item);
    if (list_.size() == 1) {
        hash_ = item->hash();
    } else {
        core::hashUpdate(hash_, item->hash());
    }
    return static_cast<int>(list_.size()) - 1;
}

void VectorItemSubsetBuilder::clear() {
    list_.clear();
}

int VectorItemSubsetBuilder::capacity() const {
    return static_cast<int>(list_.capacity());
}

void VectorItemSubsetBuilder::setCapacity(int newCapacity) {
    if (newCapacity < count()) { // legacy TList.SetCapacity raises EListError in every build
        throw std::invalid_argument("VectorItemSubsetBuilder::setCapacity: below count");
    }
    if (newCapacity > 0) {
        list_.reserve(static_cast<std::size_t>(newCapacity));
    }
}

int VectorItemSubsetBuilder::count() const {
    return static_cast<int>(list_.size());
}

VectorDataItemPtr VectorItemSubsetBuilder::item(int index) const {
    if (index < 0 || index >= count()) {
        throw std::out_of_range("VectorItemSubsetBuilder::item: index out of range");
    }
    return list_[static_cast<std::size_t>(index)];
}

VectorItemSubsetPtr VectorItemSubsetBuilder::makeStaticAndClear() {
    if (list_.empty()) {
        return nullptr;
    }
    auto subset = std::make_shared<VectorItemSubset>(hash_, std::move(list_));
    list_.clear(); // the moved-from vector is valid but unspecified
    return subset;
}

VectorItemSubsetPtr VectorItemSubsetBuilder::makeStaticCopy() const {
    if (list_.empty()) {
        return nullptr;
    }
    return std::make_shared<VectorItemSubset>(hash_, list_);
}

namespace {

// CompareVectorItems: descending by unsigned hash.
int compareVectorItems(const VectorDataItemPtr& item1, const VectorDataItemPtr& item2) {
    const core::HashValue hash1 = item1->hash();
    const core::HashValue hash2 = item2->hash();
    if (hash1 == hash2) {
        return 0;
    }
    return hash1 < hash2 ? 1 : -1;
}

} // namespace

// FEAT-GEO-037 (LV-3, LV-4: reordered hash-descending, neighbours only, literal quicksort)
void VectorItemSubsetBuilder::removeDuplicates() {
    if (list_.size() <= 1) {
        return;
    }
    sortInterfaceListByCompareFunction(list_, compareVectorItems);
    std::size_t prevIndex = 0;
    VectorDataItemPtr itemPrev = list_[0];
    core::HashValue hash = itemPrev->hash();
    for (std::size_t i = 1; i < list_.size(); ++i) {
        const VectorDataItemPtr itemCurr = list_[i];
        if (!itemPrev->isEqual(itemCurr.get())) {
            core::hashUpdate(hash, itemCurr->hash());
            itemPrev = itemCurr;
            ++prevIndex;
            if (prevIndex != i) {
                list_[prevIndex] = itemCurr;
            }
        }
    }
    list_.resize(prevIndex + 1);
    hash_ = hash;
}

std::unique_ptr<IVectorItemSubsetBuilder> VectorItemSubsetBuilderFactory::build() const {
    return std::make_unique<VectorItemSubsetBuilder>();
}

} // namespace cg::vector
