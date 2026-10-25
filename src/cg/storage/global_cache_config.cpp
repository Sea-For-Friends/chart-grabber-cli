// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/global_cache_config.h"

#include <QString>

#include <map>
#include <utility>

namespace cg::storage {

namespace {

// Ports u_GlobalCacheConfig.pas:255-286 (SetDefCache's `if AValue in [...]` whitelist), verbatim
// -- see the header comment for the legacy-vs-spec mismatch this reproduces.
bool isValidDefCache(CacheTypeId id) {
    switch (id) {
    case CacheTypeId::Gmv:
    case CacheTypeId::Sas:
    case CacheTypeId::Es:
    case CacheTypeId::Gm:
    case CacheTypeId::GmAux:
    case CacheTypeId::Mobac:
    case CacheTypeId::Tms:
    case CacheTypeId::Dbms:
    case CacheTypeId::Sqlite:
    case CacheTypeId::Ram:
    case CacheTypeId::Bdb:
    case CacheTypeId::BdbVersioned:
    case CacheTypeId::SqliteMbTiles:
    case CacheTypeId::SqliteOsmAnd:
    case CacheTypeId::SqliteLocus:
    case CacheTypeId::SqliteRMaps:
        return true;
    default:
        return false;
    }
}

} // namespace

GlobalCacheConfig::GlobalCacheConfig(core::PathConfigPtr cacheGlobalPath)
    : cacheGlobalPath_(std::move(cacheGlobalPath)) {
    std::map<QString, core::PathConfigPtr> byIdent; // dedup: several ids share one ini key
    for (const CacheTypeDescriptor& d : cacheTypes()) {
        if (d.pathIdent == nullptr) {
            continue; // Ram, SqliteOruxMaps: no [PathToCache] entry (legacy has no field for them)
        }
        const QString ident = QString::fromLatin1(d.pathIdent);
        auto it = byIdent.find(ident);
        if (it == byIdent.end()) {
            core::PathConfigPtr path =
                core::PathConfig::create(ident, QString::fromLatin1(d.defaultFolder), cacheGlobalPath_);
            addChild(path, core::SaveLoadSlot{QStringLiteral("PathToCache")});
            it = byIdent.emplace(ident, std::move(path)).first;
        }
        pathByType_.emplace(d.id, it->second);
    }
}

GlobalCacheConfig::~GlobalCacheConfig() = default;

core::PathConfigPtr GlobalCacheConfig::pathFor(CacheTypeId id) const {
    const auto it = pathByType_.find(id);
    return it == pathByType_.end() ? nullptr : it->second;
}

CacheTypeId GlobalCacheConfig::defCache() const {
    const auto guard = readLock();
    return defCache_;
}

void GlobalCacheConfig::setDefCache(CacheTypeId id) {
    const auto guard = writeLock();
    setDefCacheLocked(id);
}

void GlobalCacheConfig::setDefCacheLocked(CacheTypeId id) {
    if (!isValidDefCache(id)) {
        return;
    }
    if (defCache_ == id) {
        return;
    }
    defCache_ = id;
    setChanged();
}

void GlobalCacheConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    core::ConfigElementComplex::doReadConfig(src); // legacy `inherited`: reads every child path
    if (!src) {
        return;
    }
    if (const core::IConfigProviderPtr view = src->subItem(u"VIEW")) {
        setDefCacheLocked(
            static_cast<CacheTypeId>(view->readInt(u"DefCache", static_cast<qint32>(defCache_))));
    }
}

void GlobalCacheConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    core::ConfigElementComplex::doWriteConfig(dst);
    if (!dst) {
        return;
    }
    if (const core::IConfigWriterPtr view = dst->orCreateSubItem(u"VIEW")) {
        view->writeInt(u"DefCache", static_cast<qint32>(defCache_));
    }
}

} // namespace cg::storage
