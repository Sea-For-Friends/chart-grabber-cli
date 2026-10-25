// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemSubsetBuilder.pas, u_VectorItemSubsetBuilder.pas
//
// FEAT-GEO-037. Mutable, single-threaded. The legacy builder keeps the factory that made it and
// never uses it: not ported. The IHashFunction argument is core::hashUpdate (DV-2).

#include "cg/vector/vector_item_subset.h"

#include <memory>
#include <vector>

namespace cg::vector {

class IVectorItemSubsetBuilder {
public:
    virtual ~IVectorItemSubsetBuilder() = default;
    virtual void clear() = 0;
    virtual int add(const VectorDataItemPtr& item) = 0;                // index, or -1 for null
    [[nodiscard]] virtual VectorDataItemPtr item(int index) const = 0; // out of range: std::out_of_range
    [[nodiscard]] virtual int capacity() const = 0;
    virtual void setCapacity(int newCapacity) = 0;
    [[nodiscard]] virtual int count() const = 0;
    virtual void removeDuplicates() = 0;
    [[nodiscard]] virtual VectorItemSubsetPtr makeStaticAndClear() = 0;   // null when empty
    [[nodiscard]] virtual VectorItemSubsetPtr makeStaticCopy() const = 0; // null when empty
};

class IVectorItemSubsetBuilderFactory {
public:
    virtual ~IVectorItemSubsetBuilderFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<IVectorItemSubsetBuilder> build() const = 0;
};
using VectorItemSubsetBuilderFactoryPtr = std::shared_ptr<const IVectorItemSubsetBuilderFactory>;

class VectorItemSubsetBuilder final : public IVectorItemSubsetBuilder { // TVectorItemSubsetBuilder
public:
    void clear() override; // hash_ is not reset (L-150)
    int add(const VectorDataItemPtr& item) override;
    [[nodiscard]] VectorDataItemPtr item(int index) const override;
    [[nodiscard]] int capacity() const override;
    void setCapacity(int newCapacity) override;
    [[nodiscard]] int count() const override;
    void removeDuplicates() override;
    [[nodiscard]] VectorItemSubsetPtr makeStaticAndClear() override;
    [[nodiscard]] VectorItemSubsetPtr makeStaticCopy() const override;

private:
    std::vector<VectorDataItemPtr> list_;
    core::HashValue hash_ = 0;
};

class VectorItemSubsetBuilderFactory final : public IVectorItemSubsetBuilderFactory {
public:
    [[nodiscard]] std::unique_ptr<IVectorItemSubsetBuilder> build() const override;
};

} // namespace cg::vector
