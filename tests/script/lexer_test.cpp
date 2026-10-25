// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT yet (T-0013 slice A). docs/specs/script.md 2.3, 11.2.
#include "cg/script/lexer.h"

#include <QByteArray>

#include <gtest/gtest.h>

using cg::script::DiagnosticList;
using cg::script::Lexer;
using cg::script::Token;
using cg::script::TokenKind;

namespace {

std::vector<Token> lex(const QByteArray& source, DiagnosticList* diagnostics = nullptr) {
    DiagnosticList local;
    DiagnosticList& target = diagnostics ? *diagnostics : local;
    Lexer lexer(source);
    return lexer.tokenize(target);
}

} // namespace

// --- one test per lexer rule L1-L12 (spec 2.3) --------------------------------------------------

TEST(Lexer, L1WhitespaceIsSkipped) {
    const auto tokens = lex(QByteArrayLiteral(" \t\r\nbegin\t \r\nend"));
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].keyword, cg::script::Keyword::Begin);
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
}

TEST(Lexer, L2LineCommentRunsToEndOfLine) {
    const auto tokens = lex(QByteArrayLiteral("begin // ignored until newline\nend"));
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].keyword, cg::script::Keyword::Begin);
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
}

TEST(Lexer, L3BraceCommentHasNoNesting) {
    const auto tokens = lex(QByteArrayLiteral("begin { a } end"));
    ASSERT_EQ(tokens.size(), 3u);
    // `{ a } end` closes at the first `}`, so `end` is a real token, not part of the comment.
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
}

TEST(Lexer, L4ParenStarCommentIsSkipped) {
    const auto tokens = lex(QByteArrayLiteral("begin (* comment *) end"));
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
}

TEST(Lexer, L5StringMayNotSpanALine) {
    DiagnosticList diagnostics;
    const auto tokens = lex(QByteArrayLiteral("'abc'"), &diagnostics);
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, QByteArrayLiteral("abc"));
    EXPECT_TRUE(diagnostics.empty());
}

TEST(Lexer, L6CharLiteralDecimalAndHexInOneRule) {
    const auto tokens = lex(QByteArrayLiteral("#65#$42"));
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, QByteArrayLiteral("AB"));
}

TEST(Lexer, L7HexIntegerLiteralRule) {
    const auto tokens = lex(QByteArrayLiteral("$30"));
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[0].intValue, 0x30);
}

TEST(Lexer, L8DecimalIntegerAndRealDot) {
    const auto tokens = lex(QByteArrayLiteral("123 1.5"));
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[0].intValue, 123);
    EXPECT_EQ(tokens[1].kind, TokenKind::RealLiteral);
    EXPECT_DOUBLE_EQ(tokens[1].realValue, 1.5);
}

TEST(Lexer, L9RealLiteralExponentForms) {
    const auto tokens = lex(QByteArrayLiteral("1e3 256.0 1.0e-2"));
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].kind, TokenKind::RealLiteral);
    EXPECT_DOUBLE_EQ(tokens[0].realValue, 1000.0);
    EXPECT_DOUBLE_EQ(tokens[1].realValue, 256.0);
    EXPECT_DOUBLE_EQ(tokens[2].realValue, 0.01);
}

TEST(Lexer, L10IdentifierCharset) {
    const auto tokens = lex(QByteArrayLiteral("_Foo1 Bar_2"));
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, QByteArrayLiteral("_Foo1"));
    EXPECT_EQ(tokens[1].text, QByteArrayLiteral("Bar_2"));
}

TEST(Lexer, L11TwoCharacterOperators) {
    const auto tokens = lex(QByteArrayLiteral(":= <> <= >= .. : < > ."));
    const std::vector<TokenKind> expected = {
        TokenKind::Assign, TokenKind::NotEqual, TokenKind::LessEq,  TokenKind::GreaterEq, TokenKind::DotDot,
        TokenKind::Colon,  TokenKind::Less,     TokenKind::Greater, TokenKind::Dot,       TokenKind::End,
    };
    ASSERT_EQ(tokens.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(tokens[i].kind, expected[i]) << i;
    }
}

TEST(Lexer, L12IllegalCharacterIsZeroToleranceCanary) {
    DiagnosticList diagnostics;
    const auto tokens = lex(QByteArrayLiteral("begin ~ end"), &diagnostics);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_TRUE(diagnostics[0].message.contains(QStringLiteral("illegal character")));
    // The lexer keeps going: 'begin' and 'end' are still tokenised around the bad byte.
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].keyword, cg::script::Keyword::Begin);
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
}

// --- the 16 named cases of spec 11.2 -------------------------------------------------------------

TEST(Lexer, ApostropheInsideBraceComment) {
    // The real byte sequence from zoomify_usage_example.zmp: an apostrophe inside a `{ }` comment
    // must not be mistaken for the start of a string literal.
    const auto tokens = lex(QByteArrayLiteral("{ Zoom'a }\r\nbegin end."));
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].keyword, cg::script::Keyword::Begin);
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
    EXPECT_EQ(tokens[2].kind, TokenKind::Dot);
}

TEST(Lexer, SlashSlashInsideStringLiteral) {
    const auto tokens = lex(QByteArrayLiteral("'http://x' end"));
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, QByteArrayLiteral("http://x"));
    EXPECT_EQ(tokens[1].keyword, cg::script::Keyword::End);
}

TEST(Lexer, SlashSlashInsideParenStarComment) {
    const auto tokens = lex(QByteArrayLiteral("(* http://x *) end"));
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].keyword, cg::script::Keyword::End);
}

TEST(Lexer, BraceCommentSpansLines) {
    const auto tokens = lex(QByteArrayLiteral("{ line one\r\nline two }end"));
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].keyword, cg::script::Keyword::End);
    EXPECT_EQ(tokens[0].line, 2);
}

TEST(Lexer, UnterminatedStringIsDiagnostic) {
    DiagnosticList diagnostics;
    const auto tokens = lex(QByteArrayLiteral("'abc\r\nend"), &diagnostics);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_TRUE(diagnostics[0].message.contains(QStringLiteral("unterminated string")));
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens.back().kind, TokenKind::End);
}

TEST(Lexer, UnterminatedBraceCommentIsDiagnostic) {
    DiagnosticList diagnostics;
    lex(QByteArrayLiteral("{ never closes"), &diagnostics);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_TRUE(diagnostics[0].message.contains(QStringLiteral("unterminated comment")));
}

TEST(Lexer, DoubledQuoteInsideString) {
    const auto tokens = lex(QByteArrayLiteral("'it''s'"));
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].stringValue, QByteArrayLiteral("it's"));
}

TEST(Lexer, CharLiteralDecimalAndHex) {
    const auto a = lex(QByteArrayLiteral("#13#10"));
    ASSERT_FALSE(a.empty());
    EXPECT_EQ(a[0].stringValue, QByteArrayLiteral("\r\n"));
    const auto b = lex(QByteArrayLiteral("#$0D"));
    ASSERT_FALSE(b.empty());
    EXPECT_EQ(b[0].stringValue, QByteArrayLiteral("\r"));
}

TEST(Lexer, AdjacentStringAndCharPiecesConcatenate) {
    const auto tokens = lex(QByteArrayLiteral("'a'#13#10'b'"));
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, QByteArrayLiteral("a\r\nb"));
}

TEST(Lexer, SeparatedStringPiecesDoNotConcatenate) {
    // spec 2.3 L6 (F3): only strictly-adjacent pieces fuse; a space, comment or newline between
    // them yields two independent tokens (legacy: a syntax error unless joined by '+').
    const auto tokens = lex(QByteArrayLiteral("'a' 'b'"));
    ASSERT_EQ(tokens.size(), 3u); // 'a', 'b', End
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, QByteArrayLiteral("a"));
    EXPECT_EQ(tokens[1].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[1].stringValue, QByteArrayLiteral("b"));
}

TEST(Lexer, HexIntegerLiteral) {
    const auto tokens = lex(QByteArrayLiteral("$30"));
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].intValue, 48);
}

TEST(Lexer, RangeDotsAreNotARealLiteral) {
    const auto tokens = lex(QByteArrayLiteral("1..5"));
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[0].intValue, 1);
    EXPECT_EQ(tokens[1].kind, TokenKind::DotDot);
    EXPECT_EQ(tokens[2].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[2].intValue, 5);
}

TEST(Lexer, RealLiteralUsesCLocale) {
    const auto tokens = lex(QByteArrayLiteral("40075031.216128"));
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].kind, TokenKind::RealLiteral);
    EXPECT_DOUBLE_EQ(tokens[0].realValue, 40075031.216128);
}

TEST(Lexer, KeywordsAreCaseInsensitive) {
    const auto tokens = lex(QByteArrayLiteral("BEGIN Begin begin"));
    ASSERT_EQ(tokens.size(), 4u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(tokens[i].keyword, cg::script::Keyword::Begin) << i;
    }
}

TEST(Lexer, CrLfAndLfBothWork) {
    const auto crlf = lex(QByteArrayLiteral("a\r\nb"));
    ASSERT_EQ(crlf.size(), 3u);
    EXPECT_EQ(crlf[1].line, 2);
    const auto lf = lex(QByteArrayLiteral("a\nb"));
    ASSERT_EQ(lf.size(), 3u);
    EXPECT_EQ(lf[1].line, 2);
}

TEST(Lexer, TabCountsAsOneColumn) {
    const auto tokens = lex(QByteArrayLiteral("\tbegin"));
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].column, 2);
}

TEST(Lexer, LineAndColumnAreOneBased) {
    const auto tokens = lex(QByteArrayLiteral("begin"));
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[0].column, 1);
}
