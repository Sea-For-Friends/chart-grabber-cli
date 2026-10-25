// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-011 groundwork (MimeTypeSubst parsing); docs/specs/core.md 2.10, plan 7.17
#include "cg/core/delimited_text.h"

#include <QStringList>

#include <gtest/gtest.h>

using cg::core::parseDelimitedText;

TEST(DelimitedText, SimpleSplit) {
    EXPECT_EQ(parseDelimitedText(u"a,b,c", u','),
              (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

TEST(DelimitedText, QuotedTokenKeepsDelimiter) {
    EXPECT_EQ(parseDelimitedText(uR"("a,b",c)", u','),
              (QStringList{QStringLiteral("a,b"), QStringLiteral("c")}));
}

TEST(DelimitedText, DoubledQuoteIsOneLiteralQuote) {
    // The real .zmp shape: "" around a MIME type that itself contains a quote-delimited pair.
    EXPECT_EQ(parseDelimitedText(uR"("""text/xml; charset=UTF-8""=image/png")", u','),
              (QStringList{QStringLiteral(R"("text/xml; charset=UTF-8"=image/png)")}));
}

TEST(DelimitedText, WhitespaceAlsoDelimits) {
    EXPECT_EQ(parseDelimitedText(u"a b\tc", u','),
              (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

TEST(DelimitedText, ConsecutiveDelimitersYieldEmptyTokens) {
    EXPECT_EQ(parseDelimitedText(u"a,,b", u','),
              (QStringList{QStringLiteral("a"), QString(), QStringLiteral("b")}));
    EXPECT_EQ(parseDelimitedText(u"a,", u','), (QStringList{QStringLiteral("a"), QString()}));
}

TEST(DelimitedText, EmptyInputYieldsEmptyList) {
    EXPECT_TRUE(parseDelimitedText(u"", u',').isEmpty());
    EXPECT_TRUE(parseDelimitedText(u"   ", u',').isEmpty());
}

TEST(DelimitedText, UnterminatedQuoteTakesTheRest) {
    EXPECT_EQ(parseDelimitedText(uR"("a,b)", u','), (QStringList{QStringLiteral("a,b")}));
}

TEST(DelimitedText, AlternateDelimiterEquals) {
    EXPECT_EQ(parseDelimitedText(u"image/png=image/jpeg", u'='),
              (QStringList{QStringLiteral("image/png"), QStringLiteral("image/jpeg")}));
}

// StrictDelimiter = True (u_ZmpInfo.pas:947-950): blanks are data, quotes still work.
TEST(DelimitedText, StrictDelimiterTable) {
    const auto strict = [](const QString& text) { return parseDelimitedText(text, u',', u'"', true); };
    EXPECT_EQ(strict(QStringLiteral("1 2,3")), (QStringList{QStringLiteral("1 2"), QStringLiteral("3")}));
    EXPECT_EQ(strict(QStringLiteral(" 1, 2")), (QStringList{QStringLiteral(" 1"), QStringLiteral(" 2")}));
    EXPECT_EQ(strict(QStringLiteral("1,")), (QStringList{QStringLiteral("1"), QString()}));
    EXPECT_EQ(strict(QString()), QStringList{});
    EXPECT_EQ(strict(QStringLiteral("\"a,b\",c")), (QStringList{QStringLiteral("a,b"), QStringLiteral("c")}));
}
