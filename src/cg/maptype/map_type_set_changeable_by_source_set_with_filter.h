// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MapTypeSetChangeableBySourceSetWithFilter.pas (the base class and its three subclasses, as the
//        unit does; docs/backlog/T-0146 is the specification, maptype.md 2.11.8 is an outline)
//
// A set of maps that follows a source set and keeps only the maps passing a filter. The listener of the
// source republishes only when a map that passes the filter was added or removed.
//
// Must be owned by a std::shared_ptr and subscribe() called once after construction (the contract of
// TileDownloadSubsystemState): legacy calls the virtual IsValidMapType from the base constructor (:95 ->
// CreateStatic :118), which in C++ would reach the base version, so the constructor body :91-95 lives in
// subscribe(). The source slot holds only a weak reference, so a notification racing the destructor is safe
// (`core::Subscription::reset` does not wait for a callback in flight, core/notifier.cpp). The owner must
// not rely on the destructor to wait for a running callback, and must not call subscribe() twice.

#include "cg/core/sync.h"
#include "cg/maptype/map_type_set_changeable.h"

#include <memory>

namespace cg::maptype {

class MapTypeSetChangeableBySourceSetWithFilter
    : public IMapTypeSetChangeable,
      public std::enable_shared_from_this<MapTypeSetChangeableBySourceSetWithFilter> {
public:
    // Only stores the collaborators (:88-89). Null arguments: std::invalid_argument (legacy: access violation
    // in the constructor).
    MapTypeSetChangeableBySourceSetWithFilter(std::shared_ptr<const IMapTypeSetBuilderFactory> factory,
                                              std::shared_ptr<IMapTypeSetChangeable> sourceSet);
    ~MapTypeSetChangeableBySourceSetWithFilter() override; // :98-106

    // Ports :91-95: listener first, then (under the write lock) read the source and build the static.
    void subscribe();

    [[nodiscard]] IMapTypeSetPtr getStatic() const override; // :126-134, may be null (an empty set is null)

protected:
    [[nodiscard]] virtual bool isValidMapType(const IMapTypePtr& mapType) const; // :136-140, true

private:
    void onActiveMapsSetChange();                      // :142-186
    [[nodiscard]] IMapTypeSetPtr createStatic() const; // :108-124, caller holds the lock

    std::shared_ptr<const IMapTypeSetBuilderFactory> factory_;
    std::shared_ptr<IMapTypeSetChangeable> sourceSet_;

    mutable core::Mutex mutex_;
    IMapTypeSetPtr prevSourceSetStatic_;
    IMapTypeSetPtr static_;

    // Declared last: torn down first.
    core::Subscription subscription_;
};

class MapTypeSetChangeableBySourceSetWithFilterBitmap final
    : public MapTypeSetChangeableBySourceSetWithFilter {
public:
    using MapTypeSetChangeableBySourceSetWithFilter::MapTypeSetChangeableBySourceSetWithFilter;

protected:
    [[nodiscard]] bool isValidMapType(const IMapTypePtr& mapType) const override; // :190-195
};

class MapTypeSetChangeableBySourceSetWithFilterVector final
    : public MapTypeSetChangeableBySourceSetWithFilter {
public:
    using MapTypeSetChangeableBySourceSetWithFilter::MapTypeSetChangeableBySourceSetWithFilter;

protected:
    [[nodiscard]] bool isValidMapType(const IMapTypePtr& mapType) const override; // :199-204
};

class MapTypeSetChangeableBySourceSetWithFilterLicenseNotEmpty final
    : public MapTypeSetChangeableBySourceSetWithFilter {
public:
    using MapTypeSetChangeableBySourceSetWithFilter::MapTypeSetChangeableBySourceSetWithFilter;

protected:
    [[nodiscard]] bool isValidMapType(const IMapTypePtr& mapType) const override; // :208-215
};

} // namespace cg::maptype
