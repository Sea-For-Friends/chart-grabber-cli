// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_SimpleTileStorageConfig.pas, u_SimpleTileStorageConfig.pas,
// u_SimpleTileStorageConfigStatic.pas. docs/specs/storage.md §2.11 (:868-918).
//
// A per-map ConfigElement holding the map's cache-type choice, subfolder name and
// read/scan/add/delete/replace permission overrides, layered on top of an immutable ZMP-supplied
// default (`defaults`, ports FDefConfig/ADefConfig). It publishes an immutable snapshot (idioms
// §4, cg/core/snapshot.h) rebuilt in doBeforeChangeNotify() -- the same shape as
// cg::net::ProxyConfig.
//
// useMemCache/memCacheCapacity/memCacheTTL/memCacheClearStrategy are legacy fields of this same
// Delphi class (i_SimpleTileStorageConfig.pas:48-58,101-115; u_SimpleTileStorageConfig.pas:45-48,
// 126-129,152-155,490-540; docs/specs/storage.md :882-885). They have no INI keys (ZMP-only) and
// are read by maptype (u_MapType.pas:386-391), not by this class's own doReadConfig/doWriteConfig.

#include "cg/core/config_element.h"
#include "cg/core/snapshot.h"
#include "cg/storage/cache_type.h"
#include "cg/storage/tile_info_mem_cache.h" // ClearStrategy
#include "cg/storage/tile_storage_abilities.h"

#include <QByteArray>
#include <QString>

#include <chrono>
#include <memory>

namespace cg::storage {

// Immutable per-map snapshot (legacy ISimpleTileStorageConfigStatic).
struct SimpleTileStorageConfigStatic {
    CacheTypeId cacheType = CacheTypeId::Default;
    QString nameInCache;
    QByteArray mainContentType; // from the ZMP, immutable
    QByteArray tileFileExt;     // from the ZMP, immutable, WITH the dot
    TileStorageAbilities abilities;
    bool useMemCache = true;                                         // u_ZmpConfig.pas:70
    int memCacheCapacity = 100;                                      // u_ZmpConfig.pas:71
    std::chrono::milliseconds memCacheTtl{60000};                    // u_ZmpConfig.pas:72
    ClearStrategy memCacheClearStrategy = ClearStrategy::ByYoungest; // u_ZmpConfig.pas:73 (=1)

    [[nodiscard]] bool operator==(const SimpleTileStorageConfigStatic&) const = default;
};
using SimpleTileStorageConfigStaticPtr = std::shared_ptr<const SimpleTileStorageConfigStatic>;

// FEAT-STO-034 (the per-map settings half; the mem-cache class itself is T-0021E1).
class SimpleTileStorageConfig final : public core::ConfigElement {
public:
    explicit SimpleTileStorageConfig(SimpleTileStorageConfigStaticPtr defaults);
    ~SimpleTileStorageConfig() override;

    [[nodiscard]] SimpleTileStorageConfigStaticPtr snapshot() const; // ports GetStatic

    [[nodiscard]] CacheTypeId cacheType() const;
    // No-op (no field change, no notification) when either the current or the requested id is
    // CacheTypeId::Ge (5): a GoogleEarth map cannot be converted to or from another cache type.
    // u_SimpleTileStorageConfig.pas:440-455.
    void setCacheType(CacheTypeId v);

    [[nodiscard]] QString nameInCache() const;
    // Re-runs the read-only rule with the CURRENT isReadOnly value so an empty name keeps forcing
    // read-only (u_SimpleTileStorageConfig.pas:476-488).
    void setNameInCache(QString v);

    [[nodiscard]] QByteArray mainContentType() const noexcept; // ZMP default, never changes
    [[nodiscard]] QByteArray tileFileExt() const noexcept;     // ZMP default, never changes

    [[nodiscard]] bool readOnly() const;
    // Stores `defaults.abilities.isReadOnly() || nameInCache.isEmpty() || v`: an empty
    // NameInCache forces the map read-only. Re-applies the narrowing allow*-setters on an actual
    // change (u_SimpleTileStorageConfig.pas:457-474).
    void setReadOnly(bool v);

    [[nodiscard]] bool allowRead() const;
    void setAllowRead(bool v); // stores defaults.abilities.allowRead && v
    [[nodiscard]] bool allowScan() const;
    void setAllowScan(bool v); // stores defaults.abilities.allowScan && v
    [[nodiscard]] bool allowDelete() const;
    void setAllowDelete(bool v); // stores defaults.abilities.allowDelete && !readOnly && v
    [[nodiscard]] bool allowAdd() const;
    void setAllowAdd(bool v); // stores defaults.abilities.allowAdd && !readOnly && v
    [[nodiscard]] bool allowReplace() const;
    void setAllowReplace(bool v); // stores defaults.abilities.allowReplace && !readOnly && v

    // Legacy fields with no narrowing rule (u_SimpleTileStorageConfig.pas:490-540): plain
    // write-if-differs setters, notify only on change. No INI keys (ZMP-only).
    [[nodiscard]] bool useMemCache() const;
    void setUseMemCache(bool v);
    [[nodiscard]] int memCacheCapacity() const;
    void setMemCacheCapacity(int v);
    [[nodiscard]] std::chrono::milliseconds memCacheTtl() const;
    void setMemCacheTtl(std::chrono::milliseconds v);
    [[nodiscard]] ClearStrategy memCacheClearStrategy() const;
    void setMemCacheClearStrategy(ClearStrategy v);

protected:
    void doReadConfig(const core::IConfigProviderPtr& src) override;
    void doWriteConfig(const core::IConfigWriterPtr& dst) const override;
    void doBeforeChangeNotify() override; // rebuilds the snapshot

private:
    // The "...Locked()" split (core.md 2.16): the ConfigElement mutex is non-recursive, so
    // doReadConfig() and the setters below that re-run one another's rule call these directly
    // instead of the public, self-locking setters.
    void setCacheTypeLocked(CacheTypeId v);
    void setNameInCacheLocked(QString v);
    void setReadOnlyLocked(bool v);
    void setAllowReadLocked(bool v);
    void setAllowScanLocked(bool v);
    void setAllowDeleteLocked(bool v);
    void setAllowAddLocked(bool v);
    void setAllowReplaceLocked(bool v);

    SimpleTileStorageConfigStaticPtr defaults_;

    CacheTypeId cacheType_;
    QString nameInCache_;
    bool isReadOnly_;
    bool allowRead_;
    bool allowScan_;
    bool allowDelete_;
    bool allowAdd_;
    bool allowReplace_;
    bool useMemCache_;
    int memCacheCapacity_;
    std::chrono::milliseconds memCacheTtl_;
    ClearStrategy memCacheClearStrategy_;

    core::Snapshot<SimpleTileStorageConfigStatic> snapshot_;
};

} // namespace cg::storage
