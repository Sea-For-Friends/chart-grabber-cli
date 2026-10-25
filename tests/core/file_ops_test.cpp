// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/core/platform/file_ops and native_path (new code, T-0335, Windows port: the two rename
// semantics and non-ASCII paths).
#include "cg/core/platform/file_ops.h"
#include "cg/core/platform/native_path.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cg::core::platform::nativePath;
using cg::core::platform::renamePath;
using cg::core::platform::replaceFile;

namespace {

void writeFile(const QString& path, const QByteArray& content) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// "Карты тест": non-ASCII and a space
const QString kName =
    QString::fromUtf8("\xD0\x9A\xD0\xB0\xD1\x80\xD1\x82\xD1\x8B \xD1\x82\xD0\xB5\xD1\x81\xD1\x82");

} // namespace

TEST(FileOps, ReplaceFileReplacesAnExistingTarget) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString from = QDir(dir.path()).filePath(kName + QStringLiteral(".part"));
    const QString to = QDir(dir.path()).filePath(kName + QStringLiteral(".mbtiles"));
    writeFile(from, "new");
    writeFile(to, "old");

    EXPECT_FALSE(replaceFile(from, to));
    EXPECT_EQ(readFile(to), QByteArrayLiteral("new"));
    EXPECT_FALSE(QFile::exists(from));
}

TEST(FileOps, ReplaceFileMovesToAnAbsentTarget) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString from = QDir(dir.path()).filePath(QStringLiteral("a.part"));
    const QString to = QDir(dir.path()).filePath(QStringLiteral("b.mbtiles"));
    writeFile(from, "data");

    EXPECT_FALSE(replaceFile(from, to));
    EXPECT_EQ(readFile(to), QByteArrayLiteral("data"));
}

TEST(FileOps, ReplaceFileOfAMissingSourceFailsAndKeepsTheTarget) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString to = QDir(dir.path()).filePath(QStringLiteral("keep.mbtiles"));
    writeFile(to, "old");

    EXPECT_TRUE(static_cast<bool>(replaceFile(QDir(dir.path()).filePath(QStringLiteral("none.part")), to)));
    EXPECT_EQ(readFile(to), QByteArrayLiteral("old"));
}

TEST(FileOps, RenamePathMovesADirectoryToAnAbsentTargetWithANonAsciiName) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString from = QDir(dir.path()).filePath(QStringLiteral("old"));
    const QString to = QDir(dir.path()).filePath(kName);
    ASSERT_TRUE(QDir().mkpath(from));
    writeFile(QDir(from).filePath(QStringLiteral("x.ini")), "ini");

    ASSERT_TRUE(renamePath(from, to));
    EXPECT_EQ(readFile(QDir(to).filePath(QStringLiteral("x.ini"))), QByteArrayLiteral("ini"));
    EXPECT_FALSE(QFileInfo::exists(from));
}

TEST(FileOps, RenamePathOfAMissingSourceFails) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    EXPECT_FALSE(renamePath(QDir(dir.path()).filePath(QStringLiteral("none")),
                            QDir(dir.path()).filePath(QStringLiteral("to"))));
}

// The behaviour the two functions are named for: the Windows C runtime rename does not replace (POSIX
// rename(2) does).
TEST(FileOps, RenamePathFailsOnWindowsWhenTheTargetExists) {
    if (!cg::core::platform::kWindowsHost) {
        GTEST_SKIP() << "POSIX rename(2) replaces an existing file";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString from = QDir(dir.path()).filePath(QStringLiteral("a"));
    const QString to = QDir(dir.path()).filePath(QStringLiteral("b"));
    writeFile(from, "new");
    writeFile(to, "old");

    EXPECT_FALSE(renamePath(from, to));
    EXPECT_EQ(readFile(to), QByteArrayLiteral("old"));
}

TEST(FileOps, NativePathKeepsNonAsciiCharacters) {
    const std::filesystem::path path = nativePath(QStringLiteral("C:/x/") + kName);
    EXPECT_EQ(QString::fromStdU16String(path.filename().u16string()), kName);
}
