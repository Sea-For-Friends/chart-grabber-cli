// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategoryTree.pas, u_MarkCategoryTree.pas

#include "cg/marks/mark_category.h"

#include <QString>

#include <memory>
#include <vector>

namespace cg::marks {

class IMarkCategoryTree;
using MarkCategoryTreePtr = std::shared_ptr<const IMarkCategoryTree>;

class IMarkCategoryTree {
public:
    virtual ~IMarkCategoryTree() = default;
    [[nodiscard]] virtual MarkCategoryPtr markCategory() const = 0; // null for a pure folder node
    [[nodiscard]] virtual QString name() const = 0;                 // this level only, no backslashes
    [[nodiscard]] virtual int subItemCount() const = 0;
    [[nodiscard]] virtual MarkCategoryTreePtr subItem(int index) const = 0; // null when out of range
};

class MarkCategoryTree final : public IMarkCategoryTree { // TMarkCategoryTree
public:
    MarkCategoryTree(MarkCategoryPtr markCategory, QString name, std::vector<MarkCategoryTreePtr> subItems);

    [[nodiscard]] MarkCategoryPtr markCategory() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] int subItemCount() const override;
    [[nodiscard]] MarkCategoryTreePtr subItem(int index) const override;

private:
    MarkCategoryPtr markCategory_;
    QString name_;
    std::vector<MarkCategoryTreePtr> subItems_;
};

} // namespace cg::marks
