// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ReadWriteSyncFactory.pas, i_Synchronizer.pas, u_ReadWriteSyncCriticalSection.pas,
//        u_ReadWriteSyncSRW.pas, u_ReadWriteSyncRtlResource.pas, u_SynchronizerSimple.pas
//
// The whole of Src/Synchronize. Legacy picked one of four lock backends through a global
// factory with eight named contention "tiers"; the port uses one standard reader/writer lock.
// Locks are non-recursive by Tech Lead ruling (tech-lead-rulings.md, 2026-09-17, idioms §16 #2):
// a class that needs to re-enter itself splits into a locked public method and an unlocked
// `…Locked()` private.

#include "cg/core/platform/shared_mutex.h"

#include <mutex>
#include <shared_mutex>

namespace cg::core {

// T-0335: std::shared_mutex everywhere but Windows, where it is the SRW lock (platform/shared_mutex.h).
using Mutex = platform::SharedMutex;
using ReadLock = std::shared_lock<Mutex>;
using WriteLock = std::unique_lock<Mutex>;

} // namespace cg::core
