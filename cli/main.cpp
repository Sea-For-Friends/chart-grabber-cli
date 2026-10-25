// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). docs/specs/app-cli.md 2.6. The platform is offscreen: a terminal program must
// not try xcb or wayland because a desktop variable happens to be set.
#include "cg/app/cli_main.h"
#include "cg/core/platform/termination_signal.h"

#include <QGuiApplication>

#include <cstdio>

#include "git_commit.h"

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(
        QStringLiteral("chart-grabber")); // one directory for both programs (Q-9)
    QCoreApplication::setApplicationVersion(QStringLiteral(CG_TILES_VERSION " (" CG_GIT_COMMIT ")"));
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    cg::core::platform::TerminationSignal termination;
    const int rc = cg::app::runCli(app.arguments(), termination.token(),
                                   [&termination] { return termination.watchStdin(); });
    if (termination.stoppedByStdin()) { // T-0332: one line, also when the stop came before the job started
        std::fputs("chart-grabber-cli: stop requested on stdin; exit 143\n", stderr);
    }
    return termination.exitCode() != 0 ? termination.exitCode() : rc;
}
