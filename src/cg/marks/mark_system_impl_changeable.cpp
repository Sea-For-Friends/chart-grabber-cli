// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_MarkSystemImplChangeable.pas (class TMarkSystemImplChangeable, declared in
// mark_system_impl_changeable.h)
#include "cg/marks/mark_system_impl_changeable.h"

#include "cg/core/thread_config.h"
#include "cg/marks/mark_system_error_handler.h"

#include <QString>

#include <stdexcept>
#include <string>
#include <utility>

namespace cg::marks {

namespace {

// Legacy Assert(Assigned(...)) at :102 (base path), :103 (factory list), :104 (error notifier) is compiled
// out in the shipped build (SASPlanet.dproj:92). The config list has no Assert. A nil base path or config
// list then fails at :124-125 (no handler): the port refuses it at construction (widens D-95). The factory
// list is not checked here: its nil access violation is at :175 inside the task, reproduced there. The error
// notifier is a reference and cannot be nil.
template <class Ptr> Ptr requireSet(Ptr ptr, const char* what) {
    if (!ptr) {
        throw std::invalid_argument(std::string("MarkSystemImplChangeable: null ") + what);
    }
    return ptr;
}

} // namespace

// :93-130. Members in legacy order.
MarkSystemImplChangeable::MarkSystemImplChangeable(core::PathConfigPtr basePath,
                                                   std::shared_ptr<IMarkSystemConfigListChangeable> config,
                                                   MarkSystemImplFactoryListStaticPtr implFactoryList,
                                                   core::Notifier& errorNotifierInternal,
                                                   std::stop_token appStarted, std::stop_token appClosing)
    : basePath_(requireSet(std::move(basePath), "base path")),
      configList_(requireSet(std::move(config), "config list")), implFactoryList_(std::move(implFactoryList)),
      errorNotifierInternal_(errorNotifierInternal), appStarted_(std::move(appStarted)),
      stateInternal_(std::make_shared<core::ReadWriteStateInternalByOther>()),
      task_(std::make_unique<core::BackgroundTask>(
          std::move(appClosing), [this](std::stop_token s) { onInitialization(std::move(s)); },
          std::make_shared<core::ThreadConfig>(core::ThreadPriority::Normal),
          QStringLiteral("MarkSystemImplChangeable"))) {
    // :123-125. Subscribe to both lists before the one-shot below can start the task.
    basePathChanged_ = basePath_->changed().subscribe([this] { onConfigChanged(); });
    configListChanged_ = configList_->changed().subscribe([this] { onConfigChanged(); });
    // :127-128 Add(listener). A token that already fired runs the callback here, so legacy's explicit
    // OnAppStarted call at :129 is not written (spec ruling A.2).
    appStartedCallback_.emplace(appStarted_, std::function<void()>([this] { onAppStarted(); }));
}

// :132-151. Legacy order: stop and terminate the task, then remove the listeners. The member order
// (task_ last) makes ~BackgroundTask, which joins the running procedure, run while everything the
// procedure touches is still alive.
MarkSystemImplChangeable::~MarkSystemImplChangeable() {
    shutdown();
}

// :135-136 plus the join legacy lacks (D-57 widened, L-row of T-0137): after this returns the build has
// returned, so nothing of it can still notify a listener of this object. The task is destroyed here, which
// terminates and joins it; the subscriptions go first so that nothing starts it again. Idempotent. Not from
// the task's own thread.
void MarkSystemImplChangeable::shutdown() {
    if (!task_) {
        return;
    }
    task_->stopExecute(); // :135
    task_->terminate();   // :136
    basePathChanged_.reset();
    configListChanged_.reset();
    appStartedCallback_.reset();
    task_.reset();
}

// :153-204, statement for statement. Runs on the task's thread.
void MarkSystemImplChangeable::onInitialization(std::stop_token stop) {
    std::shared_ptr<IMarkSystemImplFactory> factory;
    std::shared_ptr<IMarkSystemImpl> built;
    if (appStarted_.stop_requested()) { // :162
        {
            const core::WriteLock lock(mutex_); // :164
            static_ = nullptr;                  // :166
            stateInternal_->setOther(nullptr);  // :167
        }
        fireChanged(); // :171

        const MarkSystemConfigStaticPtr config = configList_->activeConfig(); // :173
        if (config) {                                                         // :174
            // :175, outside the try. Legacy dereferences the lookup result without a check: a GUID that is
            // not registered, or a nil factory list (a nil list is the same call on nil), is an access
            // violation, caught by the task's thread loop (u_BackgroundTask.pas:153-158). The port reproduces
            // that flow with a defined error thrown from this procedure (spec ruling A.3, step 4).
            if (!implFactoryList_) {
                throw std::runtime_error("MarkSystem: no factory list");
            }
            const MarkSystemImplFactoryListElementPtr element = implFactoryList_->get(config->databaseGUID());
            if (!element) {
                throw std::runtime_error("MarkSystem: no backend is registered for database " +
                                         config->databaseGUID().toString().toStdString());
            }
            factory = element->factory(); // :175 (.Factory)
            if (factory) {                // :176, silent when nil
                try {                     // :177
                    built = factory->build(std::move(stop), basePath_->fullPath(),
                                           config->implConfig()); // :178-184
                } catch (...) {                                   // :185
                    built = nullptr;                              // :186
                    catchException(errorNotifierInternal_);       // :187
                }
            }
        }
    }
    {
        const core::WriteLock lock(mutex_);           // :192
        static_ = built;                              // :194
        if (built && factory) {                       // :195
            stateInternal_->setOther(built->state()); // :196
        } else {
            stateInternal_->setOther(nullptr); // :198
        }
    }
    fireChanged(); // :203
}

// :206-209
core::ReadWriteStateChangeablePtr MarkSystemImplChangeable::state() const {
    return stateInternal_;
}

// :211-219, under the read lock
std::shared_ptr<IMarkSystemImpl> MarkSystemImplChangeable::getStatic() const {
    const core::ReadLock lock(mutex_);
    return static_;
}

// :221-227
void MarkSystemImplChangeable::onAppStarted() {
    if (appStarted_.stop_requested()) {
        task_->start();
        task_->startExecute();
    }
}

// :229-233
void MarkSystemImplChangeable::onConfigChanged() {
    task_->stopExecute();
    task_->startExecute();
}

} // namespace cg::marks
