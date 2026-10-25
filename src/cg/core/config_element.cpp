// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/config_element.h"

namespace cg::core {

ConfigElement::~ConfigElement() = default;

ConfigElement::ReadGuard::ReadGuard(const ConfigElement* owner) : owner_(owner), lock_(owner->mutex_) {}

ConfigElement::ReadGuard::~ReadGuard() = default;

ConfigElement::ReadGuard::ReadGuard(ReadGuard&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), lock_(std::move(other.lock_)) {}

ConfigElement::WriteGuard::WriteGuard(ConfigElement* owner) : owner_(owner) {
    // Ports LockWrite = StopNotify; BeginWrite (u_ConfigDataElementBase.pas:142-146): the counter
    // is bumped BEFORE the lock is acquired, so a writer that is merely queued (blocked on the
    // mutex below) already suppresses notifications from whoever currently holds it.
    owner_->batchDepth_.fetch_add(1, std::memory_order_relaxed);
    lock_ = WriteLock(owner_->mutex_);
}

ConfigElement::WriteGuard::WriteGuard(WriteGuard&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), lock_(std::move(other.lock_)) {}

ConfigElement::WriteGuard::~WriteGuard() {
    if (owner_ == nullptr) {
        return; // moved-from
    }
    ConfigElement* const owner = owner_;
    // Best-effort "am I the writer whose unlock will fire the notification?" check: no other
    // writeLock()/BatchToken is currently in flight, and something is actually dirty. A
    // writeLock() racing in right after this read may still end up being the one that reaches
    // depth 0 below -- harmless: finishWrite() below re-checks beforeHookRun_ and calls the hook
    // itself if this pre-pass turns out not to have been the winning one.
    if (owner->batchDepth_.load(std::memory_order_relaxed) == 1 &&
        owner->dirty_.load(std::memory_order_relaxed)) {
        // Ports the recursive-lock trick of TConfigDataElementWithStaticBase.DoBeforeChangeNotify
        // (u_ConfigDataElementBase.pas:221-238): rebuild derived/published state here, still under
        // the write lock, instead of re-entering it (idioms.md 6; core.md 2.16). Record that it
        // ran so finishWrite() -- which fires unconditionally, including for the BatchToken path
        // that never takes this lock at all -- does not call it a second time (rework finding 2).
        //
        // T-0094: the record is stored BEFORE the hook runs; setChanged() (a child's change landing
        // during or after the hook, before finishWrite) clears it, so finishWrite() re-runs the
        // hook and the notification never publishes state older than a change that preceded it.
        // Legacy gets this by running the hook at notification time, after the counter reached 0
        // (u_ConfigDataElementBase.pas:163-173, u_ChangeableBase.pas:87-95). No lock is added: only
        // atomics changed, so the lock order (element lock -> a child's lock) is untouched.
        owner->beforeHookRun_.store(true, std::memory_order_release);
        owner->doBeforeChangeNotify();
    }
    lock_.unlock(); // ports UnlockWrite's FLock.EndWrite (u_ConfigDataElementBase.pas:185)
    owner->finishWrite();
}

void ConfigElement::finishWrite() {
    // Ports StartNotify: decrement, and only at 0 with the dirty flag set, fire once
    // (u_ConfigDataElementBase.pas:163-178). Firing happens after the lock is released (core.md 3,
    // ConfigElement row): a slot may observe a newer state than the one that triggered it, which
    // is legacy behavior, not a bug to "fix" here.
    //
    // doBeforeChangeNotify() -> before -> doChangeNotify -> changed -> doAfterChangeNotify -> after
    // (u_ChangeableBase.pas:87-95). This is the ONLY place a BatchToken-terminated batch runs the
    // before-hook, since BatchToken never takes the lock (rework finding 2); beforeHookRun_ avoids
    // calling it again when WriteGuard's own pre-pass above already ran it under the lock.
    const int depth = batchDepth_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (depth == 0 && dirty_.exchange(false, std::memory_order_acq_rel)) {
        {
            // T-0094 part 2: legacy rebuilds under LockWrite (u_ConfigDataElementBase.pas:221-238),
            // so rebuilds are serialised with writers and with each other. Here the batch is at 0,
            // so no WriteGuard is in flight; take the element's own lock for the hook only, and
            // release it BEFORE any subscriber runs (idioms.md 6). The record is consumed under
            // the lock, so a writer's fresh pre-pass that ran meanwhile is honoured.
            // Lock order (unchanged, now also reached from here): element lock -> a hook's
            // private mutex (MapTypeGuiConfigList::rebuildMutex_) -> a child's read lock.
            // Precondition: the caller must not hold this element's ReadGuard (non-recursive).
            const WriteLock hookLock(mutex_);
            if (!beforeHookRun_.exchange(false, std::memory_order_acq_rel)) {
                doBeforeChangeNotify();
            }
        }
        beforeChanged().notify();
        doChangeNotify();
        changed().notify();
        doAfterChangeNotify();
        afterChanged().notify();
    }
}

ConfigElement::ReadGuard ConfigElement::readLock() const {
    return ReadGuard(this);
}

ConfigElement::WriteGuard ConfigElement::writeLock() {
    return WriteGuard(this);
}

void ConfigElement::readConfig(const IConfigProviderPtr& src) {
    const WriteGuard guard = writeLock();
    doReadConfig(src);
}

void ConfigElement::writeConfig(const IConfigWriterPtr& dst) const {
    const ReadGuard guard = readLock();
    doWriteConfig(dst);
}

} // namespace cg::core
