// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/js_shared_store.h"

namespace cg::maptype {

std::optional<QString> JsSharedStore::get(const QString& key) const {
    const core::ReadLock guard(itemsMutex_);
    const auto it = items_.find(key);
    return it == items_.end() ? std::nullopt : std::optional<QString>(it->second);
}

void JsSharedStore::set(const QString& key, const QString& value) {
    const core::WriteLock guard(itemsMutex_);
    items_.insert_or_assign(key, value);
}

void JsSharedStore::lock() {
    lockMutex_.lock();
}

void JsSharedStore::unlock() {
    lockMutex_.unlock();
}

} // namespace cg::maptype
