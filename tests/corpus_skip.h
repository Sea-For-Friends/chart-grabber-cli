// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// The private map catalogue (tests/fixtures/zmp/sas.maps and the CSVs derived from it) is not part of the
// public tree. A test that reads it calls CG_SKIP_IF_NO_CORPUS() first (each test module's fixture header
// defines that macro over its own fixtures-dir function); it must stand directly in a TEST body or a SetUp
// (GTEST_SKIP returns from a void function). The one reason string below is what the public-tree simulation
// counts.

#include <QByteArray>
#include <QFileInfo>
#include <QString>

#include <gtest/gtest.h>

// In the private checkout every fixture exists, so a missing one must never turn into a silent skip.
// The internal gate exports CG_REQUIRE_FIXTURES=1 there: the macros below then FAIL with the same message
// instead of skipping. The public tools/validate.sh never sets it, so in a public tree the tests skip.
[[nodiscard]] inline bool cgRequireFixtures() {
    return qgetenv("CG_REQUIRE_FIXTURES") == QByteArrayLiteral("1");
}

[[nodiscard]] inline bool cgCorpusAvailableIn(const QString& fixturesDir) {
    return QFileInfo::exists(fixturesDir + QStringLiteral("/zmp/sas.maps"));
}

#define CG_SKIP_IF_NO_CORPUS_IN(fixturesDir)                                                                 \
    do {                                                                                                     \
        if (!cgCorpusAvailableIn(fixturesDir)) {                                                             \
            if (cgRequireFixtures()) {                                                                       \
                GTEST_FAIL()                                                                                 \
                    << "private map catalogue tests/fixtures/zmp/sas.maps not present (public tree)";        \
            }                                                                                                \
            GTEST_SKIP() << "private map catalogue tests/fixtures/zmp/sas.maps not present (public tree)";   \
        }                                                                                                    \
    } while (false)

// Third-party fixtures the owner has not yet cleared for publication (Marks.db3, the stock .zmp samples, the
// legacy test data, opencpn_foo.gpx) are left out of the public tree (tools/public-manifest.txt). A test
// that reads one calls CG_SKIP_IF_NO_FIXTURE(<path of that file>) first, under the same placement rule as
// above.
#define CG_SKIP_IF_NO_FIXTURE(path)                                                                          \
    do {                                                                                                     \
        if (!QFileInfo::exists(path)) {                                                                      \
            if (cgRequireFixtures()) {                                                                       \
                GTEST_FAIL() << "third-party fixture not present (public tree): "                            \
                             << QFileInfo(path).fileName().toStdString();                                    \
            }                                                                                                \
            GTEST_SKIP() << "third-party fixture not present (public tree): "                                \
                         << QFileInfo(path).fileName().toStdString();                                        \
        }                                                                                                    \
    } while (false)
