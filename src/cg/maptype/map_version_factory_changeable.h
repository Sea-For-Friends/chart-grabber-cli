// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MapVersionFactoryChangeable.pas, and IMapVersionFactoryChangeable /
//        IMapVersionFactoryChangeableInternal of i_MapVersionFactory.pas
//
// Holder of the current version factory. TileStorageOfMapType swaps it when the cache type
// changes (spec maptype.md section 2.9). Legacy's IMapVersionFactoryChangeable (GetStatic + IChangeable)
// stays on this class; the setter is the storage-side interface
// storage::IMapVersionFactoryChangeableInternal.

#include "cg/core/changeable.h"
#include "cg/core/sync.h"
#include "cg/storage/map_version.h"

namespace cg::maptype {

// FEAT-NET-002
class MapVersionFactoryChangeable final : public core::Changeable,
                                          public storage::IMapVersionFactoryChangeableInternal {
public:
    explicit MapVersionFactoryChangeable(storage::MapVersionFactoryPtr defFactory);
    ~MapVersionFactoryChangeable() override;

    [[nodiscard]] storage::MapVersionFactoryPtr getStatic() const;

    // Legacy swaps (and notifies) unless FFactory.IsSameFactoryClass(AValue). The port has one
    // factory class (storage::MapVersionFactory, ports TMapVersionFactorySimpleString), so every
    // non-null value is "the same class" and is ignored; only nullptr replaces the factory.
    void setFactory(const storage::MapVersionFactoryPtr& value) override;

private:
    mutable core::Mutex mutex_;
    storage::MapVersionFactoryPtr factory_;
};

} // namespace cg::maptype
