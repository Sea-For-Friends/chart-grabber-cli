// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemSubset.pas, u_VectorItemSubset.pas
//
// FEAT-GEO-037. An immutable ordered list of vector items with a running hash. A null
// VectorItemSubsetPtr means "no items" throughout (the builder returns null for an empty list).
// GetEnum (IEnumUnknown) is not ported: callers index 0 .. count()-1.

#include "cg/vector/vector_data_item_simple.h"

#include <memory>
#include <vector>

namespace cg::vector {

class IVectorItemSubset {
public:
    virtual ~IVectorItemSubset() = default;
    [[nodiscard]] virtual bool isEmpty() const = 0;
    [[nodiscard]] virtual bool isEqual(const IVectorItemSubset* subset) const = 0;
    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual VectorDataItemPtr item(int index) const = 0; // out of range: std::out_of_range
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
};
using VectorItemSubsetPtr = std::shared_ptr<const IVectorItemSubset>;

class VectorItemSubset final : public IVectorItemSubset { // TVectorItemSubset
public:
    VectorItemSubset(core::HashValue hash, std::vector<VectorDataItemPtr> list);

    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] bool isEqual(const IVectorItemSubset* subset) const override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] VectorDataItemPtr item(int index) const override;
    [[nodiscard]] core::HashValue hash() const noexcept override;

private:
    core::HashValue hash_;
    std::vector<VectorDataItemPtr> list_;
};

} // namespace cg::vector
