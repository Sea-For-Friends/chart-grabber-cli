// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// PascalScriptLogger (u_PascalScriptLogger.pas)
#include "cg/maptype/pascal_script_logger.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cg::maptype::PascalScriptLogger;

namespace {

QByteArray readAll(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return f.readAll();
}

} // namespace

TEST(PascalScriptLogger, FileNameFollowsAppIdAndTheZmpBaseName) {
    const QString logs = QStringLiteral("/logs/");
    EXPECT_EQ(PascalScriptLogger(1, logs, QStringLiteral("sas.maps/Google/x.zmp")).fileName(),
              QStringLiteral("/logs/zmp/x.log"));
    EXPECT_EQ(PascalScriptLogger(2, logs, QStringLiteral("sas.maps/Google/x.zmp")).fileName(),
              QStringLiteral("/logs/zmp/x.2.log"));
    EXPECT_EQ(PascalScriptLogger(3, QStringLiteral("/logs"), QStringLiteral("a\\b\\y.v2.zmp")).fileName(),
              QStringLiteral("/logs/zmp/y.v2.3.log"));
    EXPECT_EQ(PascalScriptLogger(1, logs, QStringLiteral("noext")).fileName(),
              QStringLiteral("/logs/zmp/noext.log"));
}

TEST(PascalScriptLogger, NoFileBeforeTheFirstNonEmptyWriteAndRecordFormat) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    PascalScriptLogger logger(2, dir.path(), QStringLiteral("m/x.zmp"));
    EXPECT_FALSE(QDir(dir.path() + QStringLiteral("/zmp")).exists());
    logger.write({});
    EXPECT_FALSE(QDir(dir.path() + QStringLiteral("/zmp")).exists());

    logger.write("first");
    logger.write("second\xE9");
    const QByteArray content = readAll(dir.path() + QStringLiteral("/zmp/x.2.log"));
    const QRegularExpression record(QStringLiteral(
        R"(^\[\d{8}\] \d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}: first\r\n\[\d{8}\] \d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}: second.\r\n$)"));
    EXPECT_TRUE(record.match(QString::fromLatin1(content)).hasMatch()) << content.toStdString();
    EXPECT_TRUE(content.contains("second\xE9\r\n")); // bytes as given
}

TEST(PascalScriptLogger, ReopeningTruncatesThePreviousLog) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/zmp/x.log");
    {
        PascalScriptLogger logger(1, dir.path(), QStringLiteral("x.zmp"));
        logger.write("old run");
    }
    EXPECT_TRUE(readAll(path).contains("old run"));
    PascalScriptLogger logger(1, dir.path(), QStringLiteral("x.zmp"));
    EXPECT_TRUE(readAll(path).contains("old run")); // untouched until the first write
    logger.write("new");
    const QByteArray content = readAll(path);
    EXPECT_FALSE(content.contains("old run"));
    EXPECT_TRUE(content.endsWith(": new\r\n"));
}
