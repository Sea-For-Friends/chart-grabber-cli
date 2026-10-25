// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/pascal_script_global.h"

namespace cg::maptype {

// u_PascalScriptGlobal.pas:84-92
void PascalScriptGlobal::lock() {
    mutex_.lock();
}
void PascalScriptGlobal::unlock() {
    mutex_.unlock();
}

// :94-102
void PascalScriptGlobal::lockRead() {
    mutex_.lock_shared();
}
void PascalScriptGlobal::unlockRead() {
    mutex_.unlock_shared();
}

// :104-109
bool PascalScriptGlobal::exists(int varId) const {
    const core::ReadLock guard(itemsMutex_);
    return items_.contains(varId);
}

// :111-119
bool PascalScriptGlobal::existsTs(int varId) const {
    const core::ReadLock guard(mutex_);
    return exists(varId);
}

// :121-133: an existing id is overwritten, nothing is ever removed.
void PascalScriptGlobal::setVar(int varId, const script::Value& v) {
    const core::WriteLock guard(itemsMutex_);
    items_.insert_or_assign(varId, v);
}

// :135-143
void PascalScriptGlobal::setVarTs(int varId, const script::Value& v) {
    const core::WriteLock guard(mutex_);
    setVar(varId, v);
}

// :145-154: Unassigned (an Empty Value) when absent.
script::Value PascalScriptGlobal::getVar(int varId) const {
    const core::ReadLock guard(itemsMutex_);
    const auto it = items_.find(varId);
    return it == items_.end() ? script::Value() : it->second;
}

// :156-164
script::Value PascalScriptGlobal::getVarTs(int varId) const {
    const core::ReadLock guard(mutex_);
    return getVar(varId);
}

} // namespace cg::maptype
