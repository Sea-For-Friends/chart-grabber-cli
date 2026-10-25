// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_MarkSystemErrorHandler.pas
#include "cg/marks/mark_system_error_handler.h"

#include "cg/core/log.h"

#include <QString>

#include <exception>

namespace cg::marks {

void catchException(core::Notifier& errorNotifier) noexcept { // CatchException, :36-41
    QString message;
    try {
        throw; // the exception being handled by the caller's catch block
    } catch (const std::exception& e) {
        message = QString::fromUtf8(e.what());
    } catch (...) {
        message = QStringLiteral("unknown exception");
    }
    // Deviation D-83 (docs/parity/deviations.md, row "CatchException dialog"): legacy shows an error dialog
    // here (u_ExceptionManager.pas:44-53); the port logs only.
    qCCritical(core::logMarks).noquote() << "Mark system error:" << message; // :38
    errorNotifier.notify();                                                  // :40
}

} // namespace cg::marks
