// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors.

// Covers: FEAT-GEO-002 (T-0376: proj.db next to the exe of a relocated Linux CLI).
#include "cg/geo/proj/proj_data_dir.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cg::geo::bundledProjDataDir;

TEST(ProjDataDirTest, UsesExeDirWhenProjDbIsThere) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile f(dir.filePath("proj.db"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    EXPECT_EQ(bundledProjDataDir(dir.path(), false), dir.path());
}

TEST(ProjDataDirTest, EmptyWhenProjDbIsMissing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    EXPECT_TRUE(bundledProjDataDir(dir.path(), false).isEmpty());
}

TEST(ProjDataDirTest, EmptyWhenProjDbIsADirectory) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QDir(dir.path()).mkdir("proj.db"));
    EXPECT_TRUE(bundledProjDataDir(dir.path(), false).isEmpty());
}

TEST(ProjDataDirTest, ExplicitProjDataWins) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile f(dir.filePath("proj.db"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    EXPECT_TRUE(bundledProjDataDir(dir.path(), true).isEmpty());
}
