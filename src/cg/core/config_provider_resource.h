// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ConfigDataProviderByResources.pas
//
// Legacy read RT_RCDATA resources from the exe, stripping the extension from the identifier
// (u_ConfigDataProviderByResources.pas:105-108). The port reads Qt resources under a prefix and
// does NOT strip the extension: the two real call sites ("sas:\Resource\ABOUTICON.png",
// "sas:\Resource\LOGOI.jpg") pass the full name, and the .qrc entries keep it too.

#include "cg/core/config_provider.h"

#include <memory>

namespace cg::core {

// FEAT-UI-203
class ConfigProviderResource final : public IConfigProvider {
public:
    explicit ConfigProviderResource(QString prefix);

    // prefix defaults to ":/cg". binary("ABOUTICON.png") -> ":/cg/ABOUTICON.png"
    [[nodiscard]] static IConfigProviderPtr create(QString prefix = QStringLiteral(":/cg"));

    [[nodiscard]] IConfigProviderPtr subItem(QStringView id) const override; // always nullptr
    [[nodiscard]] QVariant value(QStringView id) const override;             // always invalid
    [[nodiscard]] QByteArray binary(QStringView id) const override;
    [[nodiscard]] QStringList subItemNames() const override; // always empty
    [[nodiscard]] QStringList valueNames() const override;   // always empty

private:
    QString prefix_;
};

} // namespace cg::core
