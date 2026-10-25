// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkSystemErrorHandler.pas

#include "cg/core/notifier.h"

namespace cg::marks {

// Called from inside a catch (...) block: logs the exception being handled
// (TExceptionManager.ShowExceptionInfo) and notifies errorNotifier (AErrorNotifier.Notify(nil)).
// Subscribers of errorNotifier must not throw: this function is noexcept (the only legacy subscriber,
// u_CmdLineArgProcessorAsync.pas:99-102, does not).
void catchException(core::Notifier& errorNotifier) noexcept;

} // namespace cg::marks
