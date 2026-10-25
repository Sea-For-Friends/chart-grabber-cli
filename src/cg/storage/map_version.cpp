// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/map_version.h"

namespace cg::storage {

bool MapVersionInfo::isSame(const MapVersionInfo& other) const noexcept {
    if (&other == this) {
        return true;
    }
    if (hash_ != core::kNoHash && other.hash_ != core::kNoHash && hash_ != other.hash_) {
        return false;
    }
    return other.value_ == value_;
}

bool MapVersionRequest::isValid(const MapVersionInfoPtr& v) const {
    if (showOtherVersions) {
        return true;
    }
    if (!baseVersion) {
        // Deviation D-1: legacy TMapVersionRequest.GetIsValidVersion dereferences FBaseVersion
        // unconditionally here and crashes on a null base version. The port treats "no base
        // version, not showing others" as "only the empty/unversioned request is valid".
        return v == nullptr;
    }
    // Legacy TMapVersionInfo.IsSame(nil) returns False; the port's isSame() takes a reference,
    // so the null check moves here, one level up, instead.
    return v && baseVersion->isSame(*v);
}

MapVersionInfoPtr MapVersionFactory::byStoreString(const QString& value) {
    const core::HashValue hash = core::hashOf(value);
    {
        core::ReadLock lock(mutex_);
        auto it = cache_.find(hash);
        if (it != cache_.end()) {
            if (auto existing = it->second.lock()) {
                return existing;
            }
        }
    }
    core::WriteLock lock(mutex_);
    auto it = cache_.find(hash);
    if (it != cache_.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
    }
    auto created = std::make_shared<const MapVersionInfo>(value, hash);
    cache_[hash] = created;
    return created;
}

MapVersionInfoPtr MapVersionFactory::byVersion(const MapVersionInfoPtr& v) {
    return byStoreString(v ? v->storeString() : QString());
}

} // namespace cg::storage
