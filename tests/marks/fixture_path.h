// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test helper: resolves tests/fixtures. The CMake build passes CG_FIXTURES_DIR as a compile
// definition; the tools/nocmake_build.sh fallback passes none, so the directory is found by walking
// up from the working directory. Local to tests/marks (parallel development rule).

#include <QDir>
#include <QString>

[[nodiscard]] inline QString cgMarksFixture(const QString& relativePath) {
#ifdef CG_FIXTURES_DIR
    return QDir(QStringLiteral(CG_FIXTURES_DIR)).filePath(relativePath);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.filePath(QStringLiteral("tests/fixtures/") + relativePath);
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("tests/fixtures/") + relativePath;
#endif
}
