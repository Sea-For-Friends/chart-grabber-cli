// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategory.pas, u_MarkCategory.pas
//
// A category of the marks database: the plain ICategory plus visibility and the zoom range in which
// its marks are shown. afterScale / beforeScale are "zoom + 1" values, as stored.

#include "cg/vector/category.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkCategory : public vector::ICategory {
public:
    [[nodiscard]] virtual bool visible() const = 0;
    [[nodiscard]] virtual int afterScale() const = 0;
    [[nodiscard]] virtual int beforeScale() const = 0;
};
using MarkCategoryPtr = std::shared_ptr<const IMarkCategory>;

class MarkCategory final : public IMarkCategory { // TMarkCategory
public:
    MarkCategory(QString name, bool visible, int afterScale, int beforeScale);

    [[nodiscard]] QString name() const override;
    // Identity, not by name (u_MarkCategory.pas:112-116).
    [[nodiscard]] bool isSame(const vector::ICategory* category) const override;
    // Same name (exact, case-sensitive) AND the argument is an IMarkCategory with equal visible /
    // afterScale / beforeScale; a plain vector::Category of the same name is NOT equal (:81-110).
    [[nodiscard]] bool isEqual(const vector::ICategory* category) const override;

    [[nodiscard]] bool visible() const override;
    [[nodiscard]] int afterScale() const override;
    [[nodiscard]] int beforeScale() const override;

private:
    QString name_;
    bool visible_;
    int afterScale_;
    int beforeScale_;
};

} // namespace cg::marks
