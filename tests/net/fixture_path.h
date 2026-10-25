// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test helper: resolves tests/fixtures. The CMake build passes CG_FIXTURES_DIR as a compile
// definition (docs/specs/core.md 7); the tools/nocmake_build.sh fallback passes no definitions,
// so the directory is found by walking up from the working directory instead.

#include <QCoreApplication>
#include <QDir>
#include <QString>

[[nodiscard]] inline QString cgFixturesDir() {
#ifdef CG_FIXTURES_DIR
    return QStringLiteral(CG_FIXTURES_DIR);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.filePath(QStringLiteral("tests/fixtures"));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("tests/fixtures");
#endif
}

[[nodiscard]] inline QString cgFixture(const QString& relativePath) {
    return QDir(cgFixturesDir()).filePath(relativePath);
}

// T-0010B: HttpTestServer (QTcpServer-based) needs a thread event dispatcher for its blocking
// waitFor* calls; same rationale as tests/core/fixture_path.h's copy (AppPaths). gtest's
// own main() does not create a QCoreApplication, so tests that need one call this first; it is
// created once and shared by the whole test binary (QCoreApplication cannot be instantiated twice).
inline void cgEnsureCoreApplication() {
    if (QCoreApplication::instance() != nullptr) {
        return;
    }
    static int argc = 1;
    static char argv0[] = "cg_net_test";
    static char* argv[] = {argv0, nullptr};
    static const QCoreApplication app(argc, argv);
}

// T-0321: skip a test that reads the private map catalogue when it is absent (tests/corpus_skip.h).
#include "../corpus_skip.h"
#define CG_SKIP_IF_NO_CORPUS() CG_SKIP_IF_NO_CORPUS_IN(cgFixturesDir())
