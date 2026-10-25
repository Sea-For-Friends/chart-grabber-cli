// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptGlobal.pas (the whole unit)
//
// The per-ZMP variable store the script reaches through the `Global` bound variable. The raw
// methods take no lock (the script brackets them with lock()/unlock()); the *Ts methods lock
// themselves. One non-recursive reader/writer lock (core/sync.h).

#include "cg/core/sync.h"
#include "cg/script/script_host.h"
#include "cg/script/value.h"

#include <map>

namespace cg::maptype {

class PascalScriptGlobal final : public script::IScriptGlobal {
public:
    PascalScriptGlobal() = default;

    void setVar(int varId, const script::Value& v) override;
    [[nodiscard]] script::Value getVar(int varId) const override;
    [[nodiscard]] bool exists(int varId) const override;
    void setVarTs(int varId, const script::Value& v) override;
    [[nodiscard]] script::Value getVarTs(int varId) const override;
    [[nodiscard]] bool existsTs(int varId) const override;
    void lock() override;
    void unlock() override;
    void lockRead() override;
    void unlockRead() override;

private:
    mutable core::Mutex mutex_;      // the script-visible Lock/Unlock
    mutable core::Mutex itemsMutex_; // memory safety of items_ only, never held across a call
    std::map<int, script::Value> items_;
};

} // namespace cg::maptype
