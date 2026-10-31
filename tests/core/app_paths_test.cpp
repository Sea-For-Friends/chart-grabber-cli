// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-004; docs/specs/core.md 2.19, test plan 7.12
#include "cg/core/app_paths.h"
#include "cg/core/platform/test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <unistd.h>

#include "fixture_path.h"

using cg::core::AppPaths;

namespace {

class AppPathsTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgEnsureCoreApplication();
        QCoreApplication::setApplicationName(QStringLiteral("chart-grabber"));
        QStandardPaths::setTestModeEnabled(true); // XDG cases must not touch the real home directory
        appDir_ = QCoreApplication::applicationDirPath();
    }

    QString appDir_;
};

} // namespace

TEST_F(AppPathsTest, PortableWhenFlagGiven) {
    const auto result = AppPaths::detect({QStringLiteral("--portable")});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->portable);
    EXPECT_EQ(result->configDir, appDir_);
    EXPECT_EQ(result->dataDir, appDir_);
}

// finding 7: these two exercise the "marker file next to the exe" detection without writing into
// the real build tree -- a QTemporaryDir stands in for applicationDirPath() via the test-only
// `applicationDirOverride` seam, so a crashed/filtered run leaves nothing behind for XdgOtherwise
// (or any other test in this binary) to trip over.
TEST_F(AppPathsTest, PortableWhenChartGrabberIniIsNextToExe) {
    QTemporaryDir fakeAppDir;
    ASSERT_TRUE(fakeAppDir.isValid());
    QFile marker(QDir(fakeAppDir.path()).filePath(QStringLiteral("chart-grabber.conf")));
    ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
    marker.close();
    const auto result = AppPaths::detect(QStringList(), fakeAppDir.path());
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->portable);
}

TEST_F(AppPathsTest, PortableWhenPortableTxtExists) {
    QTemporaryDir fakeAppDir;
    ASSERT_TRUE(fakeAppDir.isValid());
    QFile marker(QDir(fakeAppDir.path()).filePath(QStringLiteral("portable.txt")));
    ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
    marker.close();
    const auto result = AppPaths::detect(QStringList(), fakeAppDir.path());
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->portable);
}

TEST_F(AppPathsTest, XdgOtherwise) {
    const auto result = AppPaths::detect(QStringList());
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->portable);
    EXPECT_NE(result->configDir, appDir_);
    EXPECT_NE(result->dataDir, appDir_);
    EXPECT_TRUE(QDir(result->configDir).exists());
    EXPECT_TRUE(QDir(result->dataDir).exists());
}

TEST_F(AppPathsTest, CacheDirEqualsDataDirInBothModes) {
    const auto xdg = AppPaths::detect(QStringList());
    ASSERT_TRUE(xdg.has_value());
    EXPECT_EQ(xdg->cacheDir, xdg->dataDir);

    const auto portable = AppPaths::detect({QStringLiteral("--portable")});
    ASSERT_TRUE(portable.has_value());
    EXPECT_EQ(portable->cacheDir, portable->dataDir);
}

TEST_F(AppPathsTest, MainConfigFileNameIsChartGrabberIni) {
    const auto result = AppPaths::detect({QStringLiteral("--portable")});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->mainConfigFilePath(),
              QDir(result->configDir).filePath(QStringLiteral("chart-grabber.conf")));
}

TEST_F(AppPathsTest, DataDirOverridesAllThreeRootsAndWinsOverPortable) {
    // finding 7 (previously untested): --datadir= forces all three roots to the given path and
    // takes precedence over --portable (core.md 2.19's precedence, now written down).
    QTemporaryDir explicitDir;
    ASSERT_TRUE(explicitDir.isValid());
    const auto result =
        AppPaths::detect({QStringLiteral("--portable"), QStringLiteral("--datadir=") + explicitDir.path()});
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->portable);
    EXPECT_EQ(result->configDir, explicitDir.path());
    EXPECT_EQ(result->dataDir, explicitDir.path());
    EXPECT_EQ(result->cacheDir, explicitDir.path());
}

// T-0360 item 1: the space form is accepted as parseJob accepts it; a missing value is rejected like an empty
// one.
TEST_F(AppPathsTest, DataDirSpaceFormIsAccepted) {
    QTemporaryDir explicitDir;
    ASSERT_TRUE(explicitDir.isValid());
    const auto result = AppPaths::detect({QStringLiteral("--datadir"), explicitDir.path()});
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->portable);
    EXPECT_EQ(result->dataDir, explicitDir.path());
    EXPECT_EQ(result->cacheDir, explicitDir.path());
    EXPECT_EQ(result->configDir, explicitDir.path());
}

TEST_F(AppPathsTest, DataDirSpaceFormWithoutValueIsRejected) {
    EXPECT_FALSE(AppPaths::detect({QStringLiteral("--datadir")}).has_value());
}

TEST_F(AppPathsTest, EmptyDataDirIsRejectedNotAutoDetected) {
    const auto result = AppPaths::detect({QStringLiteral("--datadir=")});
    ASSERT_FALSE(result.has_value());
}

// T-0382: cli/main.cpp puts the fontconfig sysroot into the data directory; it reads --datadir through this
// function.
TEST_F(AppPathsTest, ExplicitDataDirArgumentReadsBothFormsAndTheLastWins) {
    bool present = true;
    EXPECT_EQ(AppPaths::explicitDataDirArgument({QStringLiteral("download"), QStringLiteral("--portable")},
                                                &present),
              QString());
    EXPECT_FALSE(present);
    EXPECT_EQ(AppPaths::explicitDataDirArgument({QStringLiteral("--datadir=/a")}, &present),
              QStringLiteral("/a"));
    EXPECT_TRUE(present);
    EXPECT_EQ(
        AppPaths::explicitDataDirArgument({QStringLiteral("--datadir"), QStringLiteral("rel/b")}, &present),
        QStringLiteral("rel/b"));
    EXPECT_EQ(AppPaths::explicitDataDirArgument({QStringLiteral("--datadir=/a"), QStringLiteral("x"),
                                                 QStringLiteral("--datadir"), QStringLiteral("/c")}),
              QStringLiteral("/c"));
    EXPECT_EQ(AppPaths::explicitDataDirArgument({QStringLiteral("--datadir=")}, &present), QString());
    EXPECT_TRUE(present);
    EXPECT_EQ(AppPaths::explicitDataDirArgument({QStringLiteral("--datadir")}, &present), QString());
    EXPECT_TRUE(present);
}

// T-0311 (a): D rows of this ticket (directories SAS.Planet -> chart-grabber, one-time move). HOME points to
// a temporary directory, so the XDG test-mode roots (<HOME>/.qttest/config and /share) are private. T-0340:
// the roots are asked from Qt (the parent of the application folder it reports), because Windows has no XDG
// variables: HOME and USERPROFILE (Qt's home on Windows) both point to the temporary directory, and the test
// is skipped when the roots still lie outside it, so that the real profile is never moved.
namespace {

class DataDirMigrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgEnsureCoreApplication();
        QCoreApplication::setApplicationName(QStringLiteral("chart-grabber"));
        QStandardPaths::setTestModeEnabled(true);
        ASSERT_TRUE(home_.isValid());
        oldHome_ = qgetenv("HOME");
        profileWasSet_ = qEnvironmentVariableIsSet("USERPROFILE");
        oldProfile_ = qgetenv("USERPROFILE");
        qputenv("HOME", home_.path().toUtf8());
        qputenv("USERPROFILE", QDir::toNativeSeparators(home_.path()).toUtf8());
        configRoot_ =
            QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).absolutePath();
        dataRoot_ =
            QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).absolutePath();
        const QString home = QDir::cleanPath(home_.path());
        if (!configRoot_.startsWith(home, Qt::CaseInsensitive) ||
            !dataRoot_.startsWith(home, Qt::CaseInsensitive)) {
            GTEST_SKIP() << "the standard locations of this system do not follow HOME/USERPROFILE: "
                         << configRoot_.toStdString() << " and " << dataRoot_.toStdString()
                         << " are not below " << home.toStdString();
        }
    }
    void TearDown() override {
        chmod(false); // QTemporaryDir must be able to remove everything
        qputenv("HOME", oldHome_);
        if (profileWasSet_) {
            qputenv("USERPROFILE", oldProfile_);
        } else {
            qunsetenv("USERPROFILE");
        }
    }
    QString config(const QString& name) const { return configRoot_ + QLatin1Char('/') + name; }
    QString data(const QString& name) const { return dataRoot_ + QLatin1Char('/') + name; }
    void put(const QString& dir, const QString& file, const QByteArray& text) const {
        ASSERT_TRUE(QDir().mkpath(dir));
        QFile f(dir + QLatin1Char('/') + file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    static QByteArray read(const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
    }
    // Both parents read-only: rename(2) into them fails with EACCES (not for root).
    void chmod(bool readOnly) const {
        const auto perm = readOnly ? QFileDevice::ReadOwner | QFileDevice::ExeOwner
                                   : QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        for (const QString& d : {configRoot_, dataRoot_}) {
            QFile::setPermissions(d, perm);
        }
    }

    QTemporaryDir home_;
    QByteArray oldHome_;
    QByteArray oldProfile_;
    bool profileWasSet_ = false;
    QString configRoot_;
    QString dataRoot_;
};

} // namespace

// (i) old present, new absent: both directories moved, content intact, the settings file renamed, exactly
// one stderr line.
TEST_F(DataDirMigrationTest, OldDirsAreMovedWithOneStderrLine) {
    put(config(QStringLiteral("SAS.Planet")), QStringLiteral("SASPlanet.ini"), "[Internet]\nx=1\n");
    put(config(QStringLiteral("SAS.Planet")), QStringLiteral("MainFormState.ini"), "state");
    put(data(QStringLiteral("SAS.Planet/cache_sqlite/osm")), QStringLiteral("tile.bin"), "tile-bytes");

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList());
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->configDir, config(QStringLiteral("chart-grabber")));
    EXPECT_EQ(result->dataDir, data(QStringLiteral("chart-grabber")));
    EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("SAS.Planet"))));
    EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("SAS.Planet"))));
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/cache_sqlite/osm/tile.bin"))),
              QByteArray("tile-bytes"));
    EXPECT_EQ(read(config(QStringLiteral("chart-grabber/MainFormState.ini"))), QByteArray("state"));
    EXPECT_EQ(read(config(QStringLiteral("chart-grabber/chart-grabber.conf"))),
              QByteArray("[Internet]\nx=1\n"));
    EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("chart-grabber/SASPlanet.ini"))));
    EXPECT_EQ(result->mainConfigFilePath(), config(QStringLiteral("chart-grabber/chart-grabber.conf")));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("moved"), std::string::npos);
}

// (ii) both present: nothing is moved, no line.
TEST_F(DataDirMigrationTest, BothPresentMovesNothing) {
    put(config(QStringLiteral("SAS.Planet")), QStringLiteral("SASPlanet.ini"), "old-ini");
    put(config(QStringLiteral("chart-grabber")), QStringLiteral("chart-grabber.conf"), "new-ini");
    put(data(QStringLiteral("SAS.Planet")), QStringLiteral("old.bin"), "old");
    put(data(QStringLiteral("chart-grabber")), QStringLiteral("new.bin"), "new");

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList());
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->dataDir, data(QStringLiteral("chart-grabber")));
    EXPECT_EQ(read(config(QStringLiteral("SAS.Planet/SASPlanet.ini"))), QByteArray("old-ini"));
    EXPECT_EQ(read(data(QStringLiteral("SAS.Planet/old.bin"))), QByteArray("old"));
    EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("chart-grabber/old.bin"))));
    EXPECT_EQ(result->mainConfigFilePath(), config(QStringLiteral("chart-grabber/chart-grabber.conf")));
    EXPECT_EQ(err, "");
}

// (iii) rename fails (read-only parents): the old directories are read, the reason is printed once.
TEST_F(DataDirMigrationTest, FailedMoveReadsOldDirAndPrintsReasonOnce) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root ignores directory permissions";
    }
    put(config(QStringLiteral("SAS.Planet")), QStringLiteral("SASPlanet.ini"), "old-ini");
    put(data(QStringLiteral("SAS.Planet")), QStringLiteral("old.bin"), "old");
    chmod(true);

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList());
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->configDir, config(QStringLiteral("SAS.Planet")));
    EXPECT_EQ(result->dataDir, data(QStringLiteral("SAS.Planet")));
    EXPECT_EQ(result->cacheDir, data(QStringLiteral("SAS.Planet")));
    EXPECT_EQ(result->mainConfigFilePath(), config(QStringLiteral("SAS.Planet/SASPlanet.ini")));
    EXPECT_EQ(read(data(QStringLiteral("SAS.Planet/old.bin"))), QByteArray("old"));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("Permission denied"), std::string::npos);
    EXPECT_NE(err.find("old directory"), std::string::npos);
}

// (iv) neither present: the new directories are created, no line.
TEST_F(DataDirMigrationTest, NeitherPresentCreatesNewDirs) {
    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList());
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->configDir, config(QStringLiteral("chart-grabber")));
    EXPECT_EQ(result->dataDir, data(QStringLiteral("chart-grabber")));
    EXPECT_TRUE(QFileInfo(result->configDir).isDir());
    EXPECT_TRUE(QFileInfo(result->dataDir).isDir());
    EXPECT_FALSE(QFileInfo::exists(config(QStringLiteral("SAS.Planet"))));
    EXPECT_EQ(err, "");
}

// The directory moves (same parent) but its own mode forbids renaming the settings file inside it: the line
// says so and the old file name is still the one that is read.
TEST_F(DataDirMigrationTest, SettingsFileRenameFailureKeepsReadingOldName) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root ignores directory permissions";
    }
    put(config(QStringLiteral("SAS.Planet")), QStringLiteral("SASPlanet.ini"), "old-ini");
    ASSERT_TRUE(QFile::setPermissions(config(QStringLiteral("SAS.Planet")),
                                      QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList());
    const std::string err = testing::internal::GetCapturedStderr();
    QFile::setPermissions(config(QStringLiteral("chart-grabber")),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->configDir, config(QStringLiteral("chart-grabber")));
    EXPECT_EQ(result->mainConfigFilePath(), config(QStringLiteral("chart-grabber/SASPlanet.ini")));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("cannot rename"), std::string::npos);
}

// T-0360 item 4: the tile cache and AutoSave move once from the roaming data folder (Windows; the seam makes
// the same code run here) to the local one.
TEST_F(DataDirMigrationTest, RoamingDirIsMovedOnceWithOneLine) {
    const QString roaming = home_.path() + QStringLiteral("/Roaming/chart-grabber");
    put(roaming + QStringLiteral("/cache_sqlite/osm"), QStringLiteral("tile.bin"), "tile-bytes");
    put(roaming + QStringLiteral("/AutoSave/x"), QStringLiteral("final.sls"), "state");

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList(), QString(), roaming);
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/cache_sqlite/osm/tile.bin"))),
              QByteArray("tile-bytes"));
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/AutoSave/x/final.sls"))), QByteArray("state"));
    EXPECT_FALSE(QFileInfo::exists(roaming));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("moved"), std::string::npos);

    testing::internal::CaptureStderr(); // the second start: nothing to move
    ASSERT_TRUE(AppPaths::detect(QStringList(), QString(), roaming).has_value());
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

// Windows: the settings directory is the local data directory, so it exists with only the settings file; the
// children are moved one by one and the file stays.
TEST_F(DataDirMigrationTest, RoamingChildrenAreMovedIntoADirWithOnlyTheSettingsFile) {
    const QString roaming = home_.path() + QStringLiteral("/Roaming/chart-grabber");
    put(roaming + QStringLiteral("/cache_sqlite"), QStringLiteral("tile.bin"), "tile-bytes");
    put(data(QStringLiteral("chart-grabber")), QStringLiteral("chart-grabber.conf"), "ini");

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList(), QString(), roaming);
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/cache_sqlite/tile.bin"))), QByteArray("tile-bytes"));
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/chart-grabber.conf"))), QByteArray("ini"));
    EXPECT_FALSE(QFileInfo::exists(roaming));
    EXPECT_NE(err.find("moved"), std::string::npos);
}

// Both in use: nothing is merged, the local one is kept, one line.
TEST_F(DataDirMigrationTest, RoamingAndLocalBothInUseMovesNothing) {
    const QString roaming = home_.path() + QStringLiteral("/Roaming/chart-grabber");
    put(roaming + QStringLiteral("/cache_sqlite"), QStringLiteral("old.bin"), "old");
    put(data(QStringLiteral("chart-grabber/cache_sqlite")), QStringLiteral("new.bin"), "new");

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList(), QString(), roaming);
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->dataDir, data(QStringLiteral("chart-grabber")));
    EXPECT_EQ(read(roaming + QStringLiteral("/cache_sqlite/old.bin")), QByteArray("old"));
    EXPECT_FALSE(QFileInfo::exists(data(QStringLiteral("chart-grabber/cache_sqlite/old.bin"))));
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/cache_sqlite/new.bin"))), QByteArray("new"));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("not moving"), std::string::npos);
}

// --datadir and --portable never look at the roaming folder.
TEST_F(DataDirMigrationTest, ExplicitDataDirLeavesRoamingAlone) {
    const QString roaming = home_.path() + QStringLiteral("/Roaming/chart-grabber");
    put(roaming, QStringLiteral("keep.bin"), "keep");
    QTemporaryDir explicitDir;
    testing::internal::CaptureStderr();
    ASSERT_TRUE(AppPaths::detect({QStringLiteral("--datadir=") + explicitDir.path()}, QString(), roaming)
                    .has_value());
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
    EXPECT_EQ(read(roaming + QStringLiteral("/keep.bin")), QByteArray("keep"));
}

// T-0362 item 2: a name found in both folders is not moved (never merged); the one note names it, so the
// user knows what to resolve.
TEST_F(DataDirMigrationTest, RoamingNameInBothFoldersIsNamedInTheNote) {
    const QString roaming = home_.path() + QStringLiteral("/Roaming/chart-grabber");
    put(roaming + QStringLiteral("/cache_sqlite"), QStringLiteral("tile.bin"), "tile-bytes");
    put(roaming, QStringLiteral("chart-grabber.conf"), "roaming-ini");
    put(data(QStringLiteral("chart-grabber")), QStringLiteral("chart-grabber.conf"), "local-ini");

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList(), QString(), roaming);
    const std::string err = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/cache_sqlite/tile.bin"))), QByteArray("tile-bytes"));
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/chart-grabber.conf"))), QByteArray("local-ini"));
    EXPECT_EQ(read(roaming + QStringLiteral("/chart-grabber.conf")), QByteArray("roaming-ini"));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("the name is in both folders: chart-grabber.conf"), std::string::npos) << err;
}

// T-0362 item 1: a move that fails partway leaves the data split; the note says what was moved and what
// stayed, not "the cache starts empty". The read-only directory b_dir cannot be renamed to another parent.
TEST_F(DataDirMigrationTest, RoamingPartialMoveNamesWhatMovedAndWhatStayed) {
    if (cg::core::platform::directoryPermissionsIgnored()) {
        GTEST_SKIP() << "root ignores directory permissions";
    }
    const QString roaming = home_.path() + QStringLiteral("/Roaming/chart-grabber");
    put(roaming, QStringLiteral("a_first.bin"), "first");
    put(roaming + QStringLiteral("/b_dir"), QStringLiteral("inner.bin"), "inner");
    put(roaming, QStringLiteral("c_last.bin"), "last");
    put(data(QStringLiteral("chart-grabber")), QStringLiteral("chart-grabber.conf"), "ini");
    ASSERT_TRUE(QFile::setPermissions(roaming + QStringLiteral("/b_dir"),
                                      QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    testing::internal::CaptureStderr();
    const auto result = AppPaths::detect(QStringList(), QString(), roaming);
    const std::string err = testing::internal::GetCapturedStderr();
    QFile::setPermissions(roaming + QStringLiteral("/b_dir"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(read(data(QStringLiteral("chart-grabber/a_first.bin"))), QByteArray("first"));
    EXPECT_EQ(read(roaming + QStringLiteral("/b_dir/inner.bin")), QByteArray("inner"));
    EXPECT_EQ(read(roaming + QStringLiteral("/c_last.bin")), QByteArray("last"));
    EXPECT_EQ(QString::fromStdString(err).count(QLatin1Char('\n')), 1);
    EXPECT_NE(err.find("moved: a_first.bin; left in "), std::string::npos) << err;
    EXPECT_NE(err.find(": b_dir, c_last.bin"), std::string::npos) << err;
    EXPECT_EQ(err.find("starts empty"), std::string::npos) << err;
}
