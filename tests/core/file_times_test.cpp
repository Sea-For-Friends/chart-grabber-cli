// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-013. Ports the date half of Src/TileStorage/FileSystem/u_FileSystemTools.pas
// (GetFileDate/SetFileDate) as re-derived for POSIX by docs/specs/storage.md §2.13.3.
#include "cg/core/platform/file_times.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cg::core::platform::fileMTime;
using cg::core::platform::setFileMTime;

namespace {

QString makeFile(const QTemporaryDir& dir, const QString& name) {
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("tile");
    f.close();
    return path;
}

} // namespace

TEST(FileTimes, RoundTripSetThenGetReturnsExactSecondsWholeSeconds) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("t.jpg"));

    constexpr qint64 kSecs = 1700000005; // an arbitrary POSIX second, deliberately not a 2s multiple
    ASSERT_TRUE(setFileMTime(path, kSecs).has_value());

    const auto read = fileMTime(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, kSecs); // no legacy 2-second DOS-grid quantisation (deviation D-5)
}

TEST(FileTimes, SetFileMTimeOnMissingFileFails) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString missing = QDir(dir.path()).filePath(QStringLiteral("nope.jpg"));

    const auto result = setFileMTime(missing, 1700000000);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::IoError);
}

TEST(FileTimes, FileMTimeOnMissingFileFails) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString missing = QDir(dir.path()).filePath(QStringLiteral("nope.jpg"));

    const auto result = fileMTime(missing);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::IoError);
}

// T-0335: a name with non-ASCII characters and a space (the Windows port builds wide paths; on POSIX the
// same test pins that nothing regressed).
TEST(FileTimes, RoundTripOnANonAsciiPathWithASpace) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = makeFile(
        dir,
        QString::fromUtf8("\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD1\x8B \xD1\x82\xD0\xB5\xD1\x81\xD1\x82.jpg"));

    constexpr qint64 kSecs = 1700000007;
    ASSERT_TRUE(setFileMTime(path, kSecs).has_value());
    const auto read = fileMTime(path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, kSecs);
}
