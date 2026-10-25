// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (NEW code, ADR-0014). The `shared` store of a JavaScript source, one per factory, modelled
// on u_PascalScriptGlobal.pas: a script-visible non-recursive lock (shared.locked) and an item lock that only
// guards the map and is never held across a call.

#include "cg/core/sync.h"

#include <QString>

#include <map>
#include <optional>

namespace cg::maptype {

class JsSharedStore {
public:
    [[nodiscard]] std::optional<QString> get(const QString& key) const;
    void set(const QString& key, const QString& value);
    void lock();
    void unlock();

private:
    core::Mutex lockMutex_;          // the script-visible lock
    mutable core::Mutex itemsMutex_; // memory safety of items_ only
    std::map<QString, QString> items_;
};

} // namespace cg::maptype
