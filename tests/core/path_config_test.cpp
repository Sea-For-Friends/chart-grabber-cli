// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-UI-199; docs/specs/core.md 2.18, test plan 7.11 (ports Test/u_PathConfig_Test.pas
// with Windows paths replaced by POSIX ones: C:\Windows\System32 -> /usr/lib, C:\Windows -> /usr)
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/core/path_config.h"
#include "cg/core/platform/native_path.h"

#include <QDir>
#include <QString>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::IniFile;
using cg::core::PathConfig;

TEST(PathConfigWithoutBase, SimpleFullPath) {
    const auto p = PathConfig::create(QString(), QStringLiteral("/usr/lib"), nullptr);
    EXPECT_FALSE(p->isRelative());
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/lib"));
}

TEST(PathConfigWithoutBase, SimpleRelativePath) {
    const auto p = PathConfig::create(QString(), QStringLiteral("Test.txt"), nullptr);
    EXPECT_TRUE(p->isRelative());
    const QString expected = QDir::cleanPath(QDir::current().absoluteFilePath(QStringLiteral("Test.txt")));
    EXPECT_EQ(p->fullPath(), expected);
    p->setPath(QStringLiteral("./Test.txt")); // DUnit TestSimpleRelativePath: '.\' + name
    EXPECT_EQ(p->fullPath(), expected);
}

TEST(PathConfigWithoutBase, AdvRelativePath) {
    const auto p = PathConfig::create(QString(), QStringLiteral("../Test.txt"), nullptr);
    EXPECT_TRUE(p->isRelative());
    EXPECT_EQ(p->fullPath(),
              QDir::cleanPath(QDir::current().absoluteFilePath(QStringLiteral("../Test.txt"))));
}

TEST(PathConfigWithoutBase, EmptyPath) {
    const auto p = PathConfig::create(QString(), QString(), nullptr);
    const QString expected = QDir::current().path();
    EXPECT_EQ(p->fullPath(), expected);
    EXPECT_FALSE(p->fullPath().endsWith(QLatin1Char('/')));
    p->setPath(QStringLiteral("."));
    EXPECT_EQ(p->fullPath(), expected);
}

// core.md 2.18 rule 1; u_PathConfig.pas:262 (VFullPath := FPath): an absolute path is kept verbatim,
// an explicit trailing delimiter included, with or without a base. The Windows form is normalised to
// '/' by the separator rule (core.md 2.18); the POSIX rows are the same shape.
TEST(PathConfigAbsolute, KeptVerbatimWithAndWithoutTrailingDelimiter) {
    struct Row {
        const char16_t* configured;
        const char16_t* expected;
    };
    const Row rows[] = {
        {u"/usr/lib", u"/usr/lib"},       {u"/usr/lib/", u"/usr/lib/"},
        {u"/usr/Город/", u"/usr/Город/"}, {u"/", u"/"},
        {u"D:\\Marks\\", u"D:/Marks/"},   {u"D:\\", u"D:/"},
    };
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    for (const Row& row : rows) {
        const auto noBase = PathConfig::create(QString(), QStringLiteral("./Maps"), nullptr);
        noBase->setPath(QString::fromUtf16(row.configured));
        EXPECT_FALSE(noBase->isRelative());
        EXPECT_EQ(noBase->fullPath(), QString::fromUtf16(row.expected));

        const auto withBase = PathConfig::create(QString(), QStringLiteral("./Maps"), base);
        withBase->setPath(QString::fromUtf16(row.configured));
        EXPECT_FALSE(withBase->isRelative());
        EXPECT_EQ(withBase->fullPath(), QString::fromUtf16(row.expected));
    }
}

TEST(PathConfigWithBase, SimpleFullPath) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("/etc"), base);
    EXPECT_FALSE(p->isRelative());
    EXPECT_EQ(p->fullPath(), QStringLiteral("/etc"));
    p->setPath(QStringLiteral("/opt")); // DUnit TestSimpleFullPath: second value
    EXPECT_EQ(p->fullPath(), QStringLiteral("/opt"));
}

TEST(PathConfigWithBase, SimpleRelativePath) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("Test.txt"), base);
    EXPECT_TRUE(p->isRelative());
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/Test.txt"));
    p->setPath(QStringLiteral("./Test.txt")); // DUnit TestSimpleRelativePath: '.\' + name
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/Test.txt"));
}

// The base's trailing delimiter does not change a relative child: legacy IncludeTrailingPathDelimiter
// on the base FullPath (u_PathConfig.pas:256) and the port's QDir resolution (path_config.cpp) agree.
TEST(PathConfigWithBase, RelativeChildOfAbsoluteBaseWithTrailingDelimiterResolvesUnchanged) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr/lib/"), nullptr);
    EXPECT_EQ(base->fullPath(), QStringLiteral("/usr/lib/"));
    const auto p = PathConfig::create(QString(), QStringLiteral("Test.txt"), base);
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/lib/Test.txt"));
    p->setPath(QStringLiteral("../Test.txt"));
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/Test.txt"));
    p->setPath(QString());
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/lib"));
}

TEST(PathConfigWithBase, AdvRelativePath) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("../Test.txt"), base);
    EXPECT_TRUE(p->isRelative());
    EXPECT_EQ(p->fullPath(), QStringLiteral("/Test.txt"));
}

TEST(PathConfigWithBase, EmptyPath) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QString(), base);
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr"));
    p->setPath(QStringLiteral(".")); // DUnit TestEmptyPath: '.' then ''
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr"));
    p->setPath(QString());
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr"));
}

TEST(PathConfigWithBase, ChangeBasePathForFull) {
    const auto base1 = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("/etc"), base1);
    ASSERT_EQ(p->fullPath(), QStringLiteral("/etc"));
    const auto base2 = PathConfig::create(QString(), QStringLiteral("/opt"), nullptr);
    p->setBasePathConfig(base2);
    EXPECT_EQ(p->fullPath(), QStringLiteral("/etc")); // absolute path ignores the base
}

TEST(PathConfigWithBase, ChangeBasePathForRelative) {
    const auto base1 = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("Test.txt"), base1);
    ASSERT_EQ(p->fullPath(), QStringLiteral("/usr/Test.txt"));
    const auto base2 = PathConfig::create(QString(), QStringLiteral("/opt"), nullptr);
    p->setBasePathConfig(base2);
    EXPECT_EQ(p->fullPath(), QStringLiteral("/opt/Test.txt")); // relative path follows the base
}

// DUnit TestChangeBasePathForFull, POSIX form: the BASE's own value is changed (base->setPath), so the
// change reaches the child through the base's changed() notifier; an absolute child ignores it.
TEST(PathConfigWithBase, ChangeBaseValueForFull) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("/etc/Wind"), base);
    base->setPath(QStringLiteral("/opt"));
    EXPECT_EQ(p->fullPath(), QStringLiteral("/etc/Wind"));
    p->setPath(QStringLiteral("/opt/Windows"));
    base->setPath(QStringLiteral("/usr"));
    EXPECT_EQ(p->fullPath(), QStringLiteral("/opt/Windows"));
    base->setPath(QStringLiteral("/")); // DUnit's root base (C:\): the absolute child still ignores it
    EXPECT_EQ(p->fullPath(), QStringLiteral("/opt/Windows"));
}

// DUnit TestChangeBasePathForRelative, POSIX form: the base's own value is changed (base->setPath) and
// the relative child follows through the base's changed() notifier.
TEST(PathConfigWithBase, ChangeBaseValueForRelative) {
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("./Test.txt"), base);
    ASSERT_EQ(p->fullPath(), QStringLiteral("/usr/Test.txt"));
    base->setPath(QStringLiteral("/opt"));
    EXPECT_EQ(p->fullPath(), QStringLiteral("/opt/Test.txt"));
    // DUnit uses a root base (D:\) here: POSIX root is '/', and the child resolves with no doubled delimiter.
    base->setPath(QStringLiteral("/"));
    EXPECT_EQ(p->fullPath(), QStringLiteral("/Test.txt"));
}

TEST(PathConfig, ReadConfigUsesStoreIdent) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Paths");
    section->writeString(u"Maps", QStringLiteral("/srv/maps"));

    const auto p = PathConfig::create(QStringLiteral("Maps"), QStringLiteral("./Maps"), nullptr);
    p->readConfig(root->subItem(u"Paths"));
    EXPECT_EQ(p->path(), QStringLiteral("/srv/maps"));
}

TEST(PathConfig, WriteConfigWritesStoreIdent) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"Paths");

    const auto p = PathConfig::create(QStringLiteral("Maps"), QStringLiteral("./Maps"), nullptr);
    p->writeConfig(section);
    EXPECT_EQ(section->readString(u"Maps", QString()), QStringLiteral("./Maps"));
}

TEST(PathConfig, BackslashesInStoredValueAreNormalised) {
    const auto p = PathConfig::create(QString(), QStringLiteral("./Maps"), nullptr);
    p->setPath(QStringLiteral(".\\Maps"));
    EXPECT_EQ(p->path(), QStringLiteral("./Maps"));
}

TEST(PathConfig, WindowsAbsolutePathIsKeptAndFlaggedAbsolute) {
    const auto p = PathConfig::create(QString(), QStringLiteral("./Maps"), nullptr);
    p->setPath(QStringLiteral("D:\\Cache"));
    EXPECT_EQ(p->path(), QStringLiteral("D:/Cache"));
    EXPECT_FALSE(p->isRelative());
    EXPECT_EQ(p->fullPath(), QStringLiteral("D:/Cache"));
}

TEST(PathConfig, WindowsPathWarningFiresOnlyOnce) {
    // Rework finding 5a: the warning is diagnostic noise on every setPath()/doReadConfig(), not
    // just the first, for a Windows install's config -- must fire once per instance.
    static int warnings = 0;
    warnings = 0;
    const QtMessageHandler previous =
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
            if (type == QtWarningMsg && qstrcmp(ctx.category, "cg.config") == 0 &&
                msg.contains(QStringLiteral("Windows path kept verbatim"))) {
                ++warnings;
            }
        });
    const auto p = PathConfig::create(QString(), QStringLiteral("./Maps"), nullptr);
    p->setPath(QStringLiteral("D:\\Cache"));
    p->setPath(QStringLiteral("E:\\Other"));
    p->setPath(QStringLiteral("F:\\Third"));
    qInstallMessageHandler(previous);
    // T-0335: on Windows a drive-letter path is native, so there is no warning there
    EXPECT_EQ(warnings, cg::core::platform::kWindowsHost ? 0 : 1);
}

TEST(PathConfigWithBase, RepeatedSetPathDoesNotLeakBaseSubscriptions) {
    // Rework finding 5b: setPathLocked() used to tear down and rebuild the base subscription on
    // every call even when relative_/base_ did not change; assert it never ends up with more (or
    // fewer) than the one subscription a relative PathConfig needs.
    const auto base = PathConfig::create(QString(), QStringLiteral("/usr"), nullptr);
    const auto p = PathConfig::create(QString(), QStringLiteral("Test.txt"), base);
    ASSERT_EQ(base->changed().subscriberCount(), 1u);
    p->setPath(QStringLiteral("Other.txt"));
    p->setPath(QStringLiteral("Another.txt"));
    EXPECT_EQ(base->changed().subscriberCount(), 1u);
    EXPECT_EQ(p->fullPath(), QStringLiteral("/usr/Another.txt"));
}
