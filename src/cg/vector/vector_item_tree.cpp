// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-GEO-037
#include "cg/vector/vector_item_tree.h"

#include <utility>

namespace cg::vector {

VectorItemTree::VectorItemTree(QString name, VectorItemSubsetPtr items,
                               std::vector<VectorItemTreePtr> subTreeItemList)
    : name_(std::move(name)), items_(std::move(items)), subTreeItemList_(std::move(subTreeItemList)) {}

QString VectorItemTree::name() const {
    return name_;
}

int VectorItemTree::subTreeItemCount() const {
    return static_cast<int>(subTreeItemList_.size());
}

VectorItemTreePtr VectorItemTree::subTreeItem(int index) const {
    if (index < 0 || index >= subTreeItemCount()) {
        return nullptr;
    }
    return subTreeItemList_[static_cast<std::size_t>(index)];
}

VectorItemSubsetPtr VectorItemTree::items() const {
    return items_;
}

} // namespace cg::vector
