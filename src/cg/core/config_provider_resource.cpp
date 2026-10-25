// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_provider_resource.h"

#include <QFile>

#include <utility>

namespace cg::core {

ConfigProviderResource::ConfigProviderResource(QString prefix) : prefix_(std::move(prefix)) {}

IConfigProviderPtr ConfigProviderResource::create(QString prefix) {
    return std::make_shared<ConfigProviderResource>(std::move(prefix));
}

IConfigProviderPtr ConfigProviderResource::subItem(QStringView) const {
    return nullptr; // u_ConfigDataProviderByResources.pas:96-99
}

QVariant ConfigProviderResource::value(QStringView) const {
    return {};
}

QByteArray ConfigProviderResource::binary(QStringView id) const {
    QFile file(prefix_ + QLatin1Char('/') + id.toString());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QStringList ConfigProviderResource::subItemNames() const {
    return {};
}

QStringList ConfigProviderResource::valueNames() const {
    return {};
}

} // namespace cg::core
