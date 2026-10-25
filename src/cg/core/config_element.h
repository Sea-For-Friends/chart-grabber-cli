// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ConfigDataElement.pas, u_ConfigDataElementBase.pas
//
// The locked, batched, persisted settings object every config-tree leaf and branch derives from.
// A WriteGuard coalesces every write in flight into exactly one change notification, fired only
// after the lock is released (idioms.md 6); readConfig()/writeConfig() are the persistence half.
// The lock is non-recursive (tech-lead-rulings.md, 2026-09-17): a subclass that needs to call one
// of its own locked setters while already holding the write lock must instead call a private,
// unlocked "...Locked()" helper (see path_config.h for the canonical example).

#include "cg/core/changeable.h"
#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"
#include "cg/core/sync.h"

#include <atomic>
#include <utility>

namespace cg::core {

// FEAT-UI-199
// Abstract base *class* with real logic, not a GUID-style pure interface -- the coding standard
// reserves the I-prefix for the latter (config_provider.h's IConfigProvider is the pure-interface
// case); .clang-tidy's AbstractClassPrefix heuristic does not distinguish the two.
// NOLINTNEXTLINE(readability-identifier-naming)
class ConfigElement : public Changeable {
public:
    ~ConfigElement() override;

    class ReadGuard {
    public:
        ~ReadGuard();
        ReadGuard(ReadGuard&&) noexcept;
        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(ReadGuard&&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;

    private:
        friend class ConfigElement;
        explicit ReadGuard(const ConfigElement* owner);
        const ConfigElement* owner_;
        ReadLock lock_;
    };

    class WriteGuard {
    public:
        ~WriteGuard(); // unlock, then (if the batch hit 0 and the object was dirty) notify
        WriteGuard(WriteGuard&&) noexcept;
        WriteGuard(const WriteGuard&) = delete;
        WriteGuard& operator=(WriteGuard&&) = delete;
        WriteGuard& operator=(const WriteGuard&) = delete;

    private:
        friend class ConfigElement;
        explicit WriteGuard(ConfigElement* owner);
        ConfigElement* owner_;
        WriteLock lock_;
    };

    [[nodiscard]] ReadGuard readLock() const;
    [[nodiscard]] WriteGuard writeLock();

    void readConfig(const IConfigProviderPtr& src);      // write-locked, batched
    void writeConfig(const IConfigWriterPtr& dst) const; // READ-locked (serialises out only)

    // Bumps the same notify-suppression counter as WriteGuard, without taking the mutex: lets a
    // parent (ConfigElementComplex's `propagateBatch` flag, core.md 2.17) coalesce a child's
    // notification into its own batch without also taking the child's lock. The child's own
    // mutations must still go through the child's own writeLock() elsewhere; this only defers
    // when its notification fires. Public (not protected) because the parent applies it to a
    // *child* ConfigElement, not to itself.
    class BatchToken {
    public:
        explicit BatchToken(ConfigElement& owner) : owner_(&owner) {
            owner_->batchDepth_.fetch_add(1, std::memory_order_relaxed);
        }
        ~BatchToken() {
            if (owner_ == nullptr) {
                return;
            }
            owner_->finishWrite();
        }
        BatchToken(BatchToken&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
        BatchToken(const BatchToken&) = delete;
        BatchToken& operator=(BatchToken&&) = delete;
        BatchToken& operator=(const BatchToken&) = delete;

    private:
        ConfigElement* owner_;
    };

protected:
    ConfigElement() = default;

    virtual void doReadConfig(const IConfigProviderPtr& src) = 0;
    virtual void doWriteConfig(const IConfigWriterPtr& dst) const = 0;

    // A change invalidates a hook run recorded by a writer's pre-pass (T-0094): a child's change can
    // land between that pre-pass and finishWrite(), and the hook must then run again. Order: clear
    // the record first, then publish dirty, so whoever consumes dirty also sees the cleared record.
    void setChanged() noexcept {
        beforeHookRun_.store(false, std::memory_order_relaxed);
        dirty_.store(true, std::memory_order_release);
    }

private:
    // Shared tail of WriteGuard's and BatchToken's destructors: decrement the batch counter and,
    // if it reached 0 while dirty, fire before->changed->after (core.md 3, ConfigElement row).
    void finishWrite();

    mutable Mutex mutex_;
    std::atomic<int> batchDepth_{0};
    std::atomic<bool> dirty_{false};
    // Rework finding 2: set (under the lock) by WriteGuard's pre-pass when it runs
    // doBeforeChangeNotify() early; consumed by finishWrite() so the hook fires exactly once per
    // coalesced batch instead of zero times (BatchToken path) or twice (WriteGuard path).
    std::atomic<bool> beforeHookRun_{false};
};

} // namespace cg::core
