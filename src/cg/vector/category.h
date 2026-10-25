// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_Category.pas, u_Category.pas
//
// FEAT-GEO-037 (enabler). `marks` derives its own category classes from ICategory; Category is
// the plain named category used by importers.

#include <QString>

#include <memory>

namespace cg::vector {

class ICategory {
public:
    virtual ~ICategory() = default;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual bool isSame(const ICategory* category) const = 0; // identity
    [[nodiscard]] virtual bool isEqual(const ICategory* category) const = 0;
};
using CategoryPtr = std::shared_ptr<const ICategory>;

class Category final : public ICategory { // TCategory
public:
    explicit Category(QString name);

    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isSame(const ICategory* category) const override;
    [[nodiscard]] bool isEqual(const ICategory* category) const override;

private:
    QString name_;
};

} // namespace cg::vector
