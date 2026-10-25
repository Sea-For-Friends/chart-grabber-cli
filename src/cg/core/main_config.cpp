// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/main_config.h"

#include "cg/core/config_provider_ini.h"
#include "cg/core/config_provider_resource.h"
#include "cg/core/config_provider_virtual.h"
#include "cg/core/config_provider_with_global.h"

#include <memory>

namespace cg::core {

Result<IConfigWriterPtr> makeMainConfigProvider(const AppPaths& paths) {
    Result<std::shared_ptr<ConfigProviderIni>> main = ConfigProviderIni::open(paths.mainConfigFilePath());
    if (!main.has_value()) {
        return std::unexpected(main.error());
    }
    IConfigProviderPtr global = ConfigProviderVirtualWithSubItem::create(QStringLiteral("Resource"),
                                                                         ConfigProviderResource::create());
    return ConfigProviderWithGlobal::create(main.value(), QStringLiteral("sas:\\"), std::move(global));
}

Result<void> saveMainConfig(const IConfigWriterPtr& provider) {
    const auto withGlobal = std::dynamic_pointer_cast<ConfigProviderWithGlobal>(provider);
    if (!withGlobal) {
        return fail(ErrorCode::Unsupported, QStringLiteral("saveMainConfig: not a main config provider"));
    }
    const auto ini = std::dynamic_pointer_cast<ConfigProviderIni>(withGlobal->mainProvider());
    if (!ini) {
        return fail(ErrorCode::Unsupported, QStringLiteral("saveMainConfig: main branch is not an ini file"));
    }
    return ini->save();
}

} // namespace cg::core
