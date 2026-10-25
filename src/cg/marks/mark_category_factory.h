// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkCategoryFactory.pas (interface, shared), u_MarkCategoryFactory.pas (MarkCategoryFactory,
// T-0091)

#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_factory_config.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkCategoryFactory {
public:
    virtual ~IMarkCategoryFactory() = default;
    [[nodiscard]] virtual MarkCategoryPtr createNew(const QString& name) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr modify(const MarkCategoryPtr& source, const QString& name,
                                                 bool visible, int afterScale, int beforeScale) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr modifyVisible(const MarkCategoryPtr& source,
                                                        bool visible) const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkCategoryFactoryConfig> config() const = 0;
};

// Ports TMarkCategoryFactory. Every call makes a new category; nothing is cached or stored.
class MarkCategoryFactory final : public IMarkCategoryFactory {
public:
    // A null config is refused (the legacy constructor stores nil and fails at first use).
    explicit MarkCategoryFactory(std::shared_ptr<IMarkCategoryFactoryConfig> config);

    [[nodiscard]] MarkCategoryPtr createNew(const QString& name) const override;
    // The source is not read: legacy Modify takes ASource and ignores it (u_MarkCategoryFactory.pas:106-132).
    [[nodiscard]] MarkCategoryPtr modify(const MarkCategoryPtr& source, const QString& name, bool visible,
                                         int afterScale, int beforeScale) const override;
    // A null source gives a null result (legacy: access violation at ASource.Name, no handler).
    [[nodiscard]] MarkCategoryPtr modifyVisible(const MarkCategoryPtr& source, bool visible) const override;
    [[nodiscard]] std::shared_ptr<IMarkCategoryFactoryConfig> config() const override;

private:
    std::shared_ptr<IMarkCategoryFactoryConfig> config_;
};

} // namespace cg::marks
