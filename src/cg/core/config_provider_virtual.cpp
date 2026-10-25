// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_virtual.h"

#include <utility>

namespace cg::core {

ConfigProviderVirtualWithSubItem::ConfigProviderVirtualWithSubItem(QString subItemName,
                                                                   IConfigProviderPtr child)
    : subItemName_(std::move(subItemName)), subItem_(std::move(child)) {}

IConfigProviderPtr ConfigProviderVirtualWithSubItem::create(QString subItemName, IConfigProviderPtr child) {
    return std::make_shared<ConfigProviderVirtualWithSubItem>(std::move(subItemName), std::move(child));
}

IConfigProviderPtr ConfigProviderVirtualWithSubItem::subItem(QStringView id) const {
    return id == subItemName_ ? subItem_ : nullptr;
}

QVariant ConfigProviderVirtualWithSubItem::value(QStringView) const {
    return {};
}

QByteArray ConfigProviderVirtualWithSubItem::binary(QStringView) const {
    return {};
}

QStringList ConfigProviderVirtualWithSubItem::subItemNames() const {
    return {subItemName_};
}

QStringList ConfigProviderVirtualWithSubItem::valueNames() const {
    return {};
}

} // namespace cg::core
