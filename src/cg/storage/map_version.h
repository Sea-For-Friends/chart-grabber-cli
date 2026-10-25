// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapVersionInfo.pas, u_MapVersionInfo.pas, i_MapVersionFactory.pas,
//        u_MapVersionFactorySimpleString.pas, i_MapVersionRequest.pas, u_MapVersionRequest.pas,
//        i_MapVersionListStatic.pas, u_MapVersionListStatic.pas
//
// A version is a free-form string. Legacy exposes it three ways (UrlString, StoreString,
// Caption) and TMapVersionInfo returns the *same* field from all three; the three accessors are
// therefore one accessor in the port -- storeString, the name that matters because it is what
// lands on disk.

#include "cg/core/hash.h"
#include "cg/core/sync.h"

#include <QString>

#include <memory>
#include <unordered_map>
#include <vector>

namespace cg::storage {

// FEAT-STO-032
class MapVersionInfo final {
public:
    MapVersionInfo() = default; // the empty/default version
    MapVersionInfo(QString value, core::HashValue hash) : value_(std::move(value)), hash_(hash) {}

    [[nodiscard]] const QString& storeString() const noexcept { return value_; }
    [[nodiscard]] core::HashValue hash() const noexcept { return hash_; }
    [[nodiscard]] bool isEmpty() const noexcept { return value_.isEmpty(); }

    // Ports TMapVersionInfo.IsSame: identity first, then a hash *mismatch* short-circuit (only
    // when BOTH hashes are non-zero -- kNoHash means "not computed", core.md 2.9), then string
    // equality. Reproduce the short-circuit: it is what makes interned versions cheap.
    [[nodiscard]] bool isSame(const MapVersionInfo& other) const noexcept;

private:
    QString value_;
    core::HashValue hash_ = core::kNoHash;
};
using MapVersionInfoPtr = std::shared_ptr<const MapVersionInfo>;

// Ports TMapVersionFactorySimpleString: an *interning* factory. Legacy interns through a 2Q hash
// cache of 2^10 buckets keyed by CalcHashByString(value) (u_MapVersionFactorySimpleString.pas:
// Create). The port interns in an unordered_map<HashValue, weak_ptr<const MapVersionInfo>> under
// a Mutex; the 2Q eviction policy has no observable effect and is not ported.
class MapVersionFactory final {
public:
    [[nodiscard]] MapVersionInfoPtr byStoreString(const QString& value);
    [[nodiscard]] MapVersionInfoPtr byVersion(const MapVersionInfoPtr& v); // nullptr -> ""

private:
    mutable core::Mutex mutex_;
    std::unordered_map<core::HashValue, std::weak_ptr<const MapVersionInfo>> cache_;
};
using MapVersionFactoryPtr = std::shared_ptr<MapVersionFactory>;

// Ports the settable half of IMapVersionFactoryChangeableInternal (i_MapVersionFactory.pas:44-46,
// `SetFactory`); GetStatic / the change notifier stay on the implementing maptype class.
class IMapVersionFactoryChangeableInternal {
public:
    virtual ~IMapVersionFactoryChangeableInternal() = default;
    virtual void setFactory(const MapVersionFactoryPtr& value) = 0;
};

// Ports IMapVersionRequest / TMapVersionRequest.
struct MapVersionRequest {
    MapVersionInfoPtr baseVersion; // may be null
    bool showOtherVersions = false;

    // Ports GetIsValidVersion: showOtherVersions OR baseVersion->isSame(v).
    // NOTE the legacy null-deref: TMapVersionRequest.GetIsValidVersion dereferences FBaseVersion
    // unconditionally when FShowOtherVersions is false. The port returns `v == nullptr` in that
    // case instead of crashing (deviation D-1).
    [[nodiscard]] bool isValid(const MapVersionInfoPtr& v) const;
};

// Ports IMapVersionListStatic: an immutable, optionally sorted list.
using MapVersionList = std::shared_ptr<const std::vector<MapVersionInfoPtr>>;

} // namespace cg::storage
