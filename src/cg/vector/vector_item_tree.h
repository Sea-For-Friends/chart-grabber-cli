// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemTree.pas, u_VectorItemTree.pas
//
// FEAT-GEO-037. A named node with an optional item subset and child nodes. No hash, no isEqual.

#include "cg/vector/vector_item_subset.h"

#include <QString>

#include <memory>
#include <vector>

namespace cg::vector {

class IVectorItemTree {
public:
    virtual ~IVectorItemTree() = default;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual int subTreeItemCount() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const IVectorItemTree>
    subTreeItem(int index) const = 0;                            // null if out of range
    [[nodiscard]] virtual VectorItemSubsetPtr items() const = 0; // may be null
};
using VectorItemTreePtr = std::shared_ptr<const IVectorItemTree>;

class VectorItemTree final : public IVectorItemTree { // TVectorItemTree
public:
    VectorItemTree(QString name, VectorItemSubsetPtr items, std::vector<VectorItemTreePtr> subTreeItemList);

    [[nodiscard]] QString name() const override;
    [[nodiscard]] int subTreeItemCount() const override;
    [[nodiscard]] VectorItemTreePtr subTreeItem(int index) const override;
    [[nodiscard]] VectorItemSubsetPtr items() const override;

private:
    QString name_;
    VectorItemSubsetPtr items_;
    std::vector<VectorItemTreePtr> subTreeItemList_;
};

} // namespace cg::vector
