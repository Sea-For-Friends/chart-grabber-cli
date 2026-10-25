// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkSystemImplChangeable.pas (interface, M1) and u_MarkSystemImplChangeable.pas (class
// TMarkSystemImplChangeable, ticket T-0102, spec marks.md section 2.6 ruling A).

#include "cg/core/background_task.h"
#include "cg/core/changeable.h"
#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/core/read_write_state.h"
#include "cg/core/read_write_state_internal_by_other.h"
#include "cg/core/sync.h"
#include "cg/marks/mark_system_config.h"
#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_factory.h"

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

namespace cg::marks {

class IMarkSystemImplChangeable : public core::Changeable {
public:
    [[nodiscard]] virtual core::ReadWriteStateChangeablePtr state() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkSystemImpl> getStatic() const = 0; // null while (re)opening
};

// Owns one background task that builds the backend of the active configuration item. Publishes the built
// backend through getStatic() and its read/write state through state(). Each (re)build notifies changed()
// twice: once when the old backend is dropped, once when the new one is published. A build runs on the
// task's thread with no lock of this object held.
class MarkSystemImplChangeable final : public IMarkSystemImplChangeable {
public:
    // errorNotifierInternal is owned by the caller and outlives this object. A nil base path or config list
    // throws std::invalid_argument (widens D-95). A nil factory list is accepted: its access violation of
    // legacy happens at the first build and is reproduced there (L-row, T-0102).
    MarkSystemImplChangeable(core::PathConfigPtr basePath,                            // ABasePath
                             std::shared_ptr<IMarkSystemConfigListChangeable> config, // AConfig
                             MarkSystemImplFactoryListStaticPtr implFactoryList,      // AImplFactoryList
                             core::Notifier& errorNotifierInternal, // AErrorNotifierInternal
                             std::stop_token appStarted,            // AAppStartedNotifier
                             std::stop_token appClosing);           // AAppClosingNotifier
    ~MarkSystemImplChangeable() override;

    // Ends the build and joins its thread (not an interface method). The owner calls it before it releases
    // anything a listener of changed() can reach (MarkSystem, the facades). Idempotent.
    void shutdown();

    [[nodiscard]] core::ReadWriteStateChangeablePtr state() const override;
    [[nodiscard]] std::shared_ptr<IMarkSystemImpl> getStatic() const override; // null while (re)opening

private:
    void onConfigChanged();
    void onAppStarted();
    void onInitialization(std::stop_token stop);

    core::PathConfigPtr basePath_;
    std::shared_ptr<IMarkSystemConfigListChangeable> configList_;
    MarkSystemImplFactoryListStaticPtr implFactoryList_;
    core::Notifier& errorNotifierInternal_;
    std::stop_token appStarted_;
    std::shared_ptr<core::ReadWriteStateInternalByOther> stateInternal_; // FStateInternal, also FState
    mutable core::Mutex mutex_;                                          // CS of the legacy base class
    std::shared_ptr<IMarkSystemImpl> static_;                            // FStatic, guarded by mutex_
    core::Subscription basePathChanged_;                                 // FConfigChangeListener
    core::Subscription configListChanged_;                               // the same listener, on the list
    std::optional<std::stop_callback<std::function<void()>>> appStartedCallback_; // FAppStartedListener
    std::unique_ptr<core::BackgroundTask>
        task_; // FBackgroundTask: the LAST data member, destroyed first (D-57)
};

} // namespace cg::marks
