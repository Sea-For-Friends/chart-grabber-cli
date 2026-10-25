// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test helper: resolves tests/fixtures. The CMake build passes CG_FIXTURES_DIR as a compile
// definition; the tools/nocmake_build.sh fallback passes no definitions, so the directory is
// found by walking up from the working directory instead. Kept local to tests/geo (rather than
// reusing tests/core/fixture_path.h) per the parallel development rule in
// docs/conventions/agent-protocol.md -- each module only touches its own test directory.

#include <QDir>
#include <QString>

[[nodiscard]] inline QString cgGeoFixturesDir() {
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

[[nodiscard]] inline QString cgGeoFixture(const QString& relativePath) {
    return QDir(cgGeoFixturesDir()).filePath(relativePath);
}

// T-0321: skip a test that reads the private map catalogue when it is absent (tests/corpus_skip.h).
#include "../corpus_skip.h"
#define CG_SKIP_IF_NO_CORPUS() CG_SKIP_IF_NO_CORPUS_IN(cgGeoFixturesDir())
