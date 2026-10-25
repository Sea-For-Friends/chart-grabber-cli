// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-NET-011 -- docs/specs/net.md §2.11, §7.4. T-0028: the cases for the moved-to-core
// implementation (getHeaderValueUp/replaceHeaderValueUp/deleteHeaderValueUp/scriptSetHeaderValue)
// moved to tests/core/header_text_test.cpp; this file keeps the cases for what stayed in
// cg::net (addHeaderValue, parseStatusLine, which -- via idemPChar -- also exercises the core
// forward net now calls).
#include "cg/net/http_headers.h"

#include <QByteArray>

#include <gtest/gtest.h>

using cg::net::addHeaderValue;
using cg::net::parseStatusLine;

namespace {
constexpr auto kStatus = "HTTP/1.1 200 OK\r\n";
}

TEST(HttpHeadersTest, AddToEmptyBlob) {
    QByteArray headers;
    addHeaderValue(headers, "X", "1");
    EXPECT_EQ(headers, QByteArray("X: 1\r\n"));
}

TEST(HttpHeadersTest, AddSplicesBeforeTrailingBlankLine) {
    QByteArray headers = "A: 1\r\n\r\n";
    addHeaderValue(headers, "X", "1");
    EXPECT_EQ(headers, QByteArray("A: 1\r\nX: 1\r\n\r\n"));
}

TEST(HttpHeadersTest, AddWhenNoTrailingTerminator) {
    QByteArray headers = "A: 1";
    addHeaderValue(headers, "X", "1");
    EXPECT_EQ(headers, QByteArray("A: 1\r\nX: 1\r\n"));
}

TEST(HttpHeadersTest, AddNeverDeduplicates) {
    QByteArray headers = "X: old\r\n";
    addHeaderValue(headers, "X", "new");
    EXPECT_EQ(headers, QByteArray("X: old\r\nX: new\r\n"));
}

TEST(HttpHeadersTest, ParseStatusLine200) {
    const QByteArray headers = QByteArray(kStatus) + "Content-Type: text/html\r\n\r\n";
    EXPECT_EQ(parseStatusLine(headers), 200u);
}

TEST(HttpHeadersTest, ParseStatusLineGarbageReturnsZero) {
    EXPECT_EQ(parseStatusLine(QByteArray("not a status line at all")), 0u);
}
