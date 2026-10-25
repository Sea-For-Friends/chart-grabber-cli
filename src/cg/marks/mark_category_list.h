// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategoryList.pas, u_MarkCategoryList.pas
//
// An immutable, never-empty list of categories (legacy: a null list stands for "none").

#include "cg/marks/mark_category.h"

#include <memory>
#include <vector>

namespace cg::marks {

class IMarkCategoryList {
public:
    virtual ~IMarkCategoryList() = default;
    [[nodiscard]] virtual int count() const = 0;
    // Out of range: std::out_of_range (legacy TList raises EListError).
    [[nodiscard]] virtual MarkCategoryPtr item(int index) const = 0;
};
using MarkCategoryListPtr = std::shared_ptr<const IMarkCategoryList>;

class MarkCategoryList final : public IMarkCategoryList { // TMarkCategoryList
public:
    explicit MarkCategoryList(std::vector<MarkCategoryPtr> list);
    // nullptr for an empty list (u_MarkCategoryList.pas:69-76).
    [[nodiscard]] static MarkCategoryListPtr build(std::vector<MarkCategoryPtr> list);

    [[nodiscard]] int count() const override;
    [[nodiscard]] MarkCategoryPtr item(int index) const override;

private:
    std::vector<MarkCategoryPtr> list_;
};

} // namespace cg::marks
