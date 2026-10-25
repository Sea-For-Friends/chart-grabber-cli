// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-009 (the file:// I/O cases only -- see below) -- docs/specs/net.md §7.15.
//
// Per the Tech Lead ruling moving these 4 cases here from T-0010A (docs/handoffs/T-0010A.md open
// questions): FileUrlIsRead, MissingFileGives404DataNotExists, ContentTypeComesFromExtension and
// UnknownExtensionTriggersDetection exercise file I/O and ContentTypeRegistry lookups -- the full
// processFileSystemRequest(), which only exists once CurlDownloader (T-0010B) does. The other 4
// cases test filePathFromUrl() itself (T-0010A) directly.
#include "cg/core/content_type.h"
#include "cg/core/platform/native_path.h"
#include "cg/net/curl_downloader.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/file_url.h"
#include "cg/net/inet_config.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

using cg::net::CurlDownloader;
using cg::net::DownloaderOptions;
using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::filePathFromUrl;
using cg::net::InetSettings;
using cg::net::ResultKind;

namespace {

[[nodiscard]] DownloadRequest fileRequest(const QString& fileUrl) {
    DownloadRequest req;
    req.url = fileUrl.toUtf8();
    req.inet = std::make_shared<InetSettings>();
    return req;
}

// "file:///" + the path without its leading '/': /tmp/x -> file:///tmp/x, and C:/x -> file:///C:/x (T-0335).
QString fileUrlOf(const QString& path) {
    return QStringLiteral("file:///") + (path.startsWith(u'/') ? path.mid(1) : path);
}

} // namespace

TEST(FileUrl, FileUrlKeepsForwardSlashes) {
    const auto path = filePathFromUrl(QByteArrayLiteral("file:///tmp/example/a.png"));
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, QByteArrayLiteral("/tmp/example/a.png"));
}

TEST(FileUrl, HttpUrlIsNotIntercepted) {
    EXPECT_FALSE(filePathFromUrl(QByteArrayLiteral("http://127.0.0.1/a.png")).has_value());
    EXPECT_FALSE(filePathFromUrl(QByteArrayLiteral("https://127.0.0.1/a.png")).has_value());
    EXPECT_FALSE(filePathFromUrl(QByteArrayLiteral("ftp://127.0.0.1/a.png")).has_value());
}

TEST(FileUrl, WindowsDriveFormIsRejected) {
    EXPECT_FALSE(filePathFromUrl(QByteArrayLiteral("C:\\dir\\a.png")).has_value());
}

TEST(FileUrl, UncFormIsRejected) {
    EXPECT_FALSE(filePathFromUrl(QByteArrayLiteral("\\\\server\\share\\a.png")).has_value());
}

TEST(FileUrl, FileUrlIsRead) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("data.bin"));
    {
        QFile f(filePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("file-contents-here");
    }

    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              nullptr);
    const DownloadResult result = downloader.doRequest(fileRequest(fileUrlOf(filePath)), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.data, QByteArrayLiteral("file-contents-here"));
}

TEST(FileUrl, MissingFileGives404DataNotExists) {
    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              nullptr);
    const DownloadResult result = downloader.doRequest(
        fileRequest(QStringLiteral("file:///no/such/path/tile-xyz.bin")), std::stop_token{});
    EXPECT_EQ(result.kind, ResultKind::DataNotExists);
    EXPECT_EQ(result.statusCode, 404u);
}

TEST(FileUrl, ContentTypeComesFromExtension) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("tile.png"));
    {
        QFile f(filePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("not-a-real-png-but-has-the-right-extension");
    }

    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              nullptr);
    const DownloadResult result = downloader.doRequest(fileRequest(fileUrlOf(filePath)), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.contentType, QByteArrayLiteral("image/png"));
    EXPECT_EQ(result.rawResponseHeader, QByteArrayLiteral("Content-Type: image/png")); // no trailing CRLF
}

TEST(FileUrl, UnknownExtensionTriggersDetection) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("tile.weirdext"));
    {
        QFile f(filePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray::fromHex("89504e470d0a1a0a") + "restofpngdata"); // PNG magic
    }

    CurlDownloader downloader(DownloaderOptions{}, cg::core::ContentTypeRegistry::standard(), nullptr,
                              nullptr);
    const DownloadResult result = downloader.doRequest(fileRequest(fileUrlOf(filePath)), std::stop_token{});
    ASSERT_EQ(result.kind, ResultKind::Ok);
    EXPECT_EQ(result.contentType, QByteArrayLiteral("image/png"));
}

// T-0335: the drive letter form needs "C:/x", not "/C:/x" on Windows; POSIX keeps the leading '/' as it
// always did.
TEST(FileUrl, DriveLetterUrlLosesTheLeadingSlashOnWindowsOnly) {
    const bool windows = cg::core::platform::kWindowsHost;
    EXPECT_EQ(*filePathFromUrl(QByteArrayLiteral("file:///C:/maps/a.png")),
              QByteArray(windows ? "C:/maps/a.png" : "/C:/maps/a.png"));
    EXPECT_EQ(*filePathFromUrl(QByteArrayLiteral("file:///d:/x")), QByteArray(windows ? "d:/x" : "/d:/x"));
    EXPECT_EQ(*filePathFromUrl(QByteArrayLiteral("file:///tmp/example/a.png")),
              QByteArrayLiteral("/tmp/example/a.png"));
    EXPECT_EQ(*filePathFromUrl(QByteArrayLiteral("file:///1:/x/y")),
              QByteArrayLiteral("/1:/x/y")); // not a letter: untouched everywhere
}
