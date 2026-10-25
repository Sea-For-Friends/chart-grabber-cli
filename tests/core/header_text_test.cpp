// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// FEAT-NET-011 -- docs/specs/net.md §2.11, §7.4. T-0028: moved (not copied) from
// tests/net/http_headers_test.cpp along with the implementation these cases exercise
// (getHeaderValueUp/replaceHeaderValueUp/deleteHeaderValueUp/scriptSetHeaderValue), which now
// lives in cg::core::header_text. tests/net/http_headers_test.cpp keeps the cases for the
// functions that stayed in cg::net (addHeaderValue, parseStatusLine).
#include "cg/core/header_text.h"

#include <QByteArray>

#include <gtest/gtest.h>

using cg::core::deleteHeaderValueUp;
using cg::core::getHeaderValueUp;
using cg::core::replaceHeaderValueUp;
using cg::core::scriptGetHeaderValue;
using cg::core::scriptSetHeaderValue;

namespace {
constexpr auto kStatus = "HTTP/1.1 200 OK\r\n";
}

TEST(HeaderTextTest, GetValueSimple) {
    const QByteArray headers = QByteArray(kStatus) + "Content-Type: text/html\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "CONTENT-TYPE"), QByteArray("text/html"));
}

TEST(HeaderTextTest, GetValueCaseInsensitiveName) {
    const QByteArray headers = QByteArray(kStatus) + "content-type: text/html\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "CONTENT-TYPE"), QByteArray("text/html"));
}

TEST(HeaderTextTest, GetValueSkipsSpacesAndColons) {
    const QByteArray headers = QByteArray(kStatus) + "Name:::  v\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "NAME"), QByteArray("v"));
}

TEST(HeaderTextTest, GetValueNoColonAtAll) {
    const QByteArray headers = QByteArray(kStatus) + "Name v\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "NAME"), QByteArray("v"));
}

TEST(HeaderTextTest, GetValueKeepsTrailingSpaces) {
    const QByteArray headers = QByteArray(kStatus) + "Name: v  \r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "NAME"), QByteArray("v  "));
}

TEST(HeaderTextTest, GetValueAbsentReturnsEmpty) {
    const QByteArray headers = QByteArray(kStatus) + "Content-Type: text/html\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "X-MISSING"), QByteArray());
}

TEST(HeaderTextTest, GetValuePresentButEmptyReturnsEmpty) {
    const QByteArray headers = QByteArray(kStatus) + "Name: \r\n\r\n";
    // "" both for absent and for present-but-empty -- indistinguishable (§2.12.1 relies on this).
    EXPECT_EQ(getHeaderValueUp(headers, "NAME"), QByteArray());
}

TEST(HeaderTextTest, GetValuePrefixMatchQuirk) {
    // "CONTENT-TYPE" is a prefix of "Content-Type-Options", so it matches that line instead --
    // legacy behavior, not a bug, and the value is whatever text follows the matched prefix.
    const QByteArray headers = QByteArray(kStatus) + "Content-Type-Options: nosniff\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "CONTENT-TYPE"), QByteArray("-Options: nosniff"));
}

TEST(HeaderTextTest, GetValueFirstOccurrenceOnly) {
    const QByteArray headers = QByteArray(kStatus) + "Set-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "SET-COOKIE"), QByteArray("a=1"));
}

TEST(HeaderTextTest, GetValueFromStatusLineBlob) {
    // The status line is just "line zero" to the scanner; lookups still find real headers after it.
    const QByteArray headers = QByteArray("HTTP/1.1 404 Not Found\r\n") + "Content-Type: text/plain\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "CONTENT-TYPE"), QByteArray("text/plain"));
}

TEST(HeaderTextTest, ReplaceKeepsNameAndCrlf) {
    const QByteArray original = QByteArray(kStatus) + "Content-Type: text/html\r\nX: y\r\n\r\n";
    QByteArray headers = original;
    ASSERT_TRUE(replaceHeaderValueUp(headers, "CONTENT-TYPE", "application/json"));
    const QByteArray expected = QByteArray(kStatus) + "Content-Type: application/json\r\nX: y\r\n\r\n";
    EXPECT_EQ(headers, expected);

    // scriptSetHeaderValue's successful-replace path must produce the identical result.
    EXPECT_EQ(scriptSetHeaderValue(original, "Content-Type", "application/json"), expected);
}

TEST(HeaderTextTest, ReplaceReturnsFalseWhenAbsent) {
    const QByteArray original = QByteArray(kStatus) + "X: y\r\n\r\n";
    QByteArray headers = original;
    EXPECT_FALSE(replaceHeaderValueUp(headers, "CONTENT-TYPE", "application/json"));
    EXPECT_EQ(headers, original);
}

TEST(HeaderTextTest, ReplaceRejectsEmptyValue) {
    const QByteArray original = QByteArray(kStatus) + "Content-Type: text/html\r\n\r\n";
    QByteArray headers = original;
    EXPECT_FALSE(replaceHeaderValueUp(headers, "CONTENT-TYPE", ""));
    EXPECT_EQ(headers, original);
}

TEST(HeaderTextTest, DeleteRemovesWholeLineAndTerminators) {
    // B's own CRLF and the blank-line CRLF right after it form one control-char run; deleting B
    // swallows that whole run in one operation.
    QByteArray headers = "A: 1\r\nB: 2\r\n\r\nC: 3\r\n";
    deleteHeaderValueUp(headers, "B");
    EXPECT_EQ(headers, QByteArray("A: 1\r\nC: 3\r\n"));
}

TEST(HeaderTextTest, DeleteLastLineKeepsBlankLine) {
    // A blank line elsewhere in the blob is untouched by deleting an unrelated, later line.
    QByteArray headers = "A: 1\r\n\r\nB: 2\r\n";
    deleteHeaderValueUp(headers, "B");
    EXPECT_EQ(headers, QByteArray("A: 1\r\n\r\n"));
}

TEST(HeaderTextTest, DeleteAbsentIsNoOp) {
    const QByteArray original = QByteArray(kStatus) + "X: y\r\n\r\n";
    QByteArray headers = original;
    deleteHeaderValueUp(headers, "MISSING");
    EXPECT_EQ(headers, original);
}

TEST(HeaderTextTest, EmbeddedNulStopsHeaderScan) {
    // tech-lead-rulings.md 2026-09-17 "\0 header-scan stop": a literal NUL byte inside the blob
    // aborts the scan entirely (u_NetworkStrFunc.pas:157-163) rather than being skipped like any
    // other control byte -- so a header that appears only after the NUL is never found.
    QByteArray headers = QByteArray(kStatus) + "A: 1\r\n";
    headers += '\0';
    headers += "B: 2\r\n\r\n";
    EXPECT_EQ(getHeaderValueUp(headers, "A"), QByteArray("1"));
    EXPECT_EQ(getHeaderValueUp(headers, "B"), QByteArray());
}

TEST(HeaderTextTest, DeleteStopsAtEmbeddedNul) {
    // The matched line's own CRLF is still swallowed, but the run-extension halts at the NUL
    // instead of consuming it (u_NetworkStrFunc.pas:227-231).
    QByteArray headers = QByteArray("A: 1\r\n");
    headers += '\0';
    headers += "B: 2\r\n";
    deleteHeaderValueUp(headers, "A");
    QByteArray expected;
    expected += '\0';
    expected += "B: 2\r\n";
    EXPECT_EQ(headers, expected);
}

TEST(HeaderTextTest, ControlByteInNameYieldsEmptyValue) {
    // L-27/D-24 (docs/handoffs/reliability-audit-2026-09-28.md): when the NAME itself carries a
    // control byte that the header blob also carries at the matching position, idemPChar's
    // full-byte compare (D-24) can match past the line's own end, landing valueStart after
    // lineEnd. getHeaderValueUp must return '' there, exactly like legacy's
    // Copy(AHeaders, I, K-I) with a negative K-I (u_NetworkStrFunc.pas:156) -- not slice with a
    // negative length.
    QByteArray nameWithNul("A", 1);
    nameWithNul += '\0';
    nameWithNul += 'Z';
    QByteArray headersWithNul("A", 1);
    headersWithNul += '\0';
    headersWithNul += "Z: 1";
    EXPECT_EQ(getHeaderValueUp(headersWithNul, nameWithNul), QByteArray());

    EXPECT_EQ(getHeaderValueUp("X\r\nY: v", "X\r\nY"), QByteArray());

    // D-24 pin: the name's embedded NUL stops legacy's IdemPChar compare after "A", so legacy
    // matches the "AB: 1" line and returns "1"; the port's full-byte compare never matches, so it
    // stays empty regardless of this fix.
    QByteArray pinName("A", 1);
    pinName += '\0';
    pinName += 'Z';
    EXPECT_EQ(scriptGetHeaderValue("AB: 1", pinName), QByteArray());
}

TEST(HeaderTextTest, ScriptSetHeaderValuePrependsOnEmptyBlob) {
    // scriptSetHeaderValue's prepend-on-miss path on an empty blob (moved from
    // HttpHeadersTest.AddToEmptyBlob, which keeps the addHeaderValue half of that case in net).
    EXPECT_EQ(scriptSetHeaderValue(QByteArray(), "X", "1"), QByteArray("X: 1\r\n"));
}
