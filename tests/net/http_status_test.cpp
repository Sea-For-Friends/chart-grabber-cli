// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT yet (status-code classification only). docs/specs/net.md §7.1; legacy
// u_HttpStatusChecker.pas:112-158.
#include "cg/net/http_status.h"

#include <gtest/gtest.h>

using cg::net::isContentNotExistStatus;
using cg::net::isDownloadErrorStatus;
using cg::net::isOkStatus;

TEST(HttpStatus, OkSet) {
    for (quint32 code : {200u, 201u, 202u, 203u, 205u, 206u}) {
        EXPECT_TRUE(isOkStatus(code)) << code;
    }
}

TEST(HttpStatus, OkSetExcludes204) {
    EXPECT_FALSE(isOkStatus(204));
}

TEST(HttpStatus, DownloadErrorSet) {
    for (quint32 code : {500u, 501u, 502u, 503u, 504u}) {
        EXPECT_TRUE(isDownloadErrorStatus(code)) << code;
    }
}

TEST(HttpStatus, DownloadErrorExcludes505) {
    EXPECT_FALSE(isDownloadErrorStatus(505));
}

TEST(HttpStatus, ContentNotExistSet) {
    for (quint32 code : {204u, 400u, 404u}) {
        EXPECT_TRUE(isContentNotExistStatus(code)) << code;
    }
}

TEST(HttpStatus, Unknown301) {
    EXPECT_FALSE(isOkStatus(301));
    EXPECT_FALSE(isDownloadErrorStatus(301));
    EXPECT_FALSE(isContentNotExistStatus(301));
}

TEST(HttpStatus, Unknown304) {
    EXPECT_FALSE(isOkStatus(304));
    EXPECT_FALSE(isDownloadErrorStatus(304));
    EXPECT_FALSE(isContentNotExistStatus(304));
}

TEST(HttpStatus, Unknown403And429) {
    for (quint32 code : {403u, 429u}) {
        EXPECT_FALSE(isOkStatus(code)) << code;
        EXPECT_FALSE(isDownloadErrorStatus(code)) << code;
        EXPECT_FALSE(isContentNotExistStatus(code)) << code;
    }
}
