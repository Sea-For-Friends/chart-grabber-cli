// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/tile_storage_type.h"

#include "cg/core/config_provider_ini.h"

namespace cg::storage {

core::IConfigProviderPtr openStorageConfigIni(StorageClass storageClass, const QString& path) {
    QString iniPath;
    switch (storageClass) {
    case StorageClass::OneFile:
        iniPath = path + QStringLiteral(".StorageConfig.ini");
        break;
    case StorageClass::Folder:
    case StorageClass::InSeparateFiles:
        iniPath = path + QStringLiteral("StorageConfig.ini");
        break;
    case StorageClass::InMemory:
    case StorageClass::Other:
        return nullptr;
    }
    if (auto opened = core::ConfigProviderIni::open(iniPath)) {
        return *opened;
    }
    return nullptr;
}

TileStorageAbilities applyStorageConfigIni(TileStorageAbilities base, const TileStorageAbilities& forced,
                                           const core::IConfigProviderPtr& storageConfig) {
    if (storageConfig) {
        if (const core::IConfigProviderPtr common = storageConfig->subItem(u"Common")) {
            const bool isReadOnly = common->readBool(u"IsReadOnly", base.isReadOnly());
            const TileStorageAbilities iniAbilities{
                common->readBool(u"AllowRead", base.allowRead),
                common->readBool(u"AllowScan", base.allowScan),
                common->readBool(u"AllowAdd", base.allowAdd),
                common->readBool(u"AllowDelete", base.allowDelete),
                common->readBool(u"AllowReplace", base.allowReplace),
            };
            base = base.restrictedBy(iniAbilities, isReadOnly);
        }
    }
    return base.restrictedBy(forced, false);
}

TileStorageAbilities applyStorageConfigIni(TileStorageAbilities base, const TileStorageAbilities& forced,
                                           StorageClass storageClass, const QString& path) {
    return applyStorageConfigIni(base, forced, openStorageConfigIni(storageClass, path));
}

} // namespace cg::storage
