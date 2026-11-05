// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (T-0454: licence text of the bundled map sources). The `License=` line of a bundled
// params.txt is the `license` field that the sources record shows to users. The expected strings are the
// public texts settled for T-0454 (Esri via Vantor; EMODnet Bathymetry layers under CC BY 4.0, the world
// layer with GEBCO 2019), written here as literals, not read from the bundle.
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

namespace {

// The bundled map-source tree: the CMake build passes CG_MAP_SOURCES_DIR; the tools/nocmake_build.sh
// fallback passes nothing, so the directory is found by walking up from the working directory.
QString bundledDir() {
#ifdef CG_MAP_SOURCES_DIR
    return QStringLiteral(CG_MAP_SOURCES_DIR);
#else
    QDir dir = QDir::current();
    for (int level = 0; level < 8; ++level) {
        if (dir.exists(QStringLiteral("data/map-sources"))) {
            return dir.filePath(QStringLiteral("data/map-sources"));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QStringLiteral("data/map-sources");
#endif
}

// The first `License=` line of a params.txt, without its CR; a marker text when the file cannot be read.
QString licenceLine(const QString& relPath) {
    QFile file(bundledDir() + QStringLiteral("/") + relPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("<cannot open ") + relPath + QStringLiteral(">");
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        if (line.startsWith(QStringLiteral("License="))) {
            return line;
        }
    }
    return QString();
}

TEST(BundledSourceLicence, SettledTextOfEachLayer) {
    struct Row {
        const char* relPath;
        const char* licence;
    };
    const Row rows[] = {
        {"Satellite/Esri-World-Imagery.zmp/params.txt",
         "License=Esri, Vantor, Earthstar Geographics, and the GIS User Community"},
        {"Sea/EMODnet-Bathymetry-Europe.zmp/params.txt", "License=EMODnet Bathymetry Consortium, CC BY 4.0"},
        {"Sea/EMODnet-Bathymetry-World.zmp/params.txt",
         "License=EMODnet Bathymetry Consortium, CC BY 4.0; includes GEBCO 2019"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.relPath);
        EXPECT_EQ(licenceLine(QString::fromLatin1(row.relPath)), QString::fromLatin1(row.licence));
    }
}

} // namespace
