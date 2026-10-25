// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-013. docs/specs/net.md §7.3, §2.5 message table -- exact English strings, since
// docs/parity texts, .mo translation keys and the tile-error log all key off them.
#include "cg/net/download_result_factory.h"

#include <gtest/gtest.h>

using namespace cg::net;

TEST(DownloadResultFactory, BadContentLengthActualThenExpected) {
    const DownloadResult r = result::badContentLength(12, 34, 200, QByteArray());
    EXPECT_EQ(r.kind, ResultKind::BadContentLength);
    EXPECT_EQ(r.text, QStringLiteral("Unexpected content length: 12 bytes (expecting: 34 bytes)"));
}

TEST(DownloadResultFactory, BadContentType) {
    const DownloadResult r = result::badContentType(QByteArrayLiteral("text/html"), 200, QByteArray());
    EXPECT_EQ(r.kind, ResultKind::BadContentType);
    EXPECT_EQ(r.text, QStringLiteral("Unexpected content type text/html"));
}

TEST(DownloadResultFactory, Banned) {
    const DownloadResult r = result::banned(200, QByteArray());
    EXPECT_EQ(r.kind, ResultKind::Banned);
    EXPECT_EQ(r.text, QStringLiteral("Most likely you've been banned by the server!"));
}

TEST(DownloadResultFactory, DataNotExists204) {
    const DownloadResult r = result::dataNotExistsByStatusCode(204, QByteArray());
    EXPECT_EQ(r.text, QStringLiteral("HTTP 204 No Content"));
}

TEST(DownloadResultFactory, DataNotExists400) {
    const DownloadResult r = result::dataNotExistsByStatusCode(400, QByteArray());
    EXPECT_EQ(r.text, QStringLiteral("HTTP 400 Bad Request"));
}

TEST(DownloadResultFactory, DataNotExists404) {
    const DownloadResult r = result::dataNotExistsByStatusCode(404, QByteArray());
    EXPECT_EQ(r.text, QStringLiteral("HTTP 404 Not Found"));
}

TEST(DownloadResultFactory, DataNotExistsOther) {
    const DownloadResult r = result::dataNotExistsByStatusCode(418, QByteArray());
    EXPECT_EQ(r.text, QStringLiteral("HTTP 418 Unknown Error"));
}

TEST(DownloadResultFactory, ZeroSize) {
    const DownloadResult r = result::dataNotExistsZeroSize(200, QByteArray());
    EXPECT_EQ(r.text, QStringLiteral("Size of answer is zero"));
}

TEST(DownloadResultFactory, LoadErrorByStatusCode) {
    const DownloadResult r = result::loadErrorByStatusCode(503);
    EXPECT_EQ(r.kind, ResultKind::LoadError);
    EXPECT_EQ(r.text, QStringLiteral("Download error. Status code 503"));
}

TEST(DownloadResultFactory, LoadErrorByUnknownStatusCode) {
    const DownloadResult r = result::loadErrorByUnknownStatusCode(999);
    EXPECT_EQ(r.kind, ResultKind::LoadError);
    EXPECT_EQ(r.text, QStringLiteral("Unknown status code 999"));
}

TEST(DownloadResultFactory, NoConnectToServer) {
    const DownloadResult r = result::noConnectToServer(7);
    EXPECT_EQ(r.kind, ResultKind::NoConnectToServer);
    EXPECT_EQ(r.text, QStringLiteral("Connect to server error. Error code 7"));
}
