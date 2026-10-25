// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-200 (real-corpus anchor); docs/specs/core.md 7.19
// The two remaining cases of 7.19 (GuidValuesParse, MimeTypeSubstValuesParse) need
// IConfigProvider::readUuid and ContentTypeSubst, which land with T-0002B.
#include "cg/core/ini_file.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include "fixture_path.h"

using cg::core::IniFile;

namespace {

QStringList allParamsFiles() {
    QStringList paths;
    QDirIterator it(QDir(cgFixturesDir()).filePath(QStringLiteral("zmp/sas.maps")),
                    QStringList{QStringLiteral("params.txt")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        paths.append(it.next());
    }
    paths.sort();
    return paths;
}

} // namespace

TEST(ZmpParams, EveryParamsTxtRoundTripsByteIdentical) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList paths = allParamsFiles();
    ASSERT_EQ(paths.size(), 514) << "the ZMP corpus fixture changed size";
    for (const QString& path : paths) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
        const QByteArray raw = file.readAll();
        const auto parsed = IniFile::fromBytes(raw);
        ASSERT_TRUE(parsed.has_value()) << path.toStdString();
        EXPECT_FALSE(parsed->isDirty()) << path.toStdString();
        EXPECT_EQ(parsed->toBytes(), raw) << path.toStdString();
    }
}

TEST(ZmpParams, EveryParamsTxtHasParamsSection) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList paths = allParamsFiles();
    ASSERT_FALSE(paths.isEmpty());
    for (const QString& path : paths) {
        const auto parsed = IniFile::fromFile(path);
        ASSERT_TRUE(parsed.has_value()) << path.toStdString();
        EXPECT_TRUE(parsed->hasSection(u"PARAMS")) << path.toStdString();
        EXPECT_FALSE(parsed->readString(u"params", u"GUID").isEmpty()) << path.toStdString();
        EXPECT_FALSE(parsed->readString(u"params", u"name").isEmpty()) << path.toStdString();
    }
}
