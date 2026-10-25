// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderVirtualWithSubItem.pas
//
// A node with exactly one named child and no values of its own.

#include "cg/core/config_provider.h"

#include <memory>

namespace cg::core {

class ConfigProviderVirtualWithSubItem final : public IConfigProvider {
public:
    ConfigProviderVirtualWithSubItem(QString subItemName, IConfigProviderPtr child);

    [[nodiscard]] static IConfigProviderPtr create(QString subItemName, IConfigProviderPtr child);

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override; // only subItemName_ resolves
    [[nodiscard]] QVariant value(QStringView id) const override;
    [[nodiscard]] QByteArray binary(QStringView id) const override;
    [[nodiscard]] QStringList subItemNames() const override; // {subItemName_}
    [[nodiscard]] QStringList valueNames() const override;

private:
    QString subItemName_;
    IConfigProviderPtr subItem_;
};

} // namespace cg::core
