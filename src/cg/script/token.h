// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PSPascalCompilerEx.pas (the RemObjects tokenizer it wraps, replaced
//        by lexer.h).
//
// One lexical token: its kind, source position and decoded payload. Keywords are folded into
// `Keyword` (kept even for keywords the v1 grammar rejects, e.g. `goto`/`uses`) so the parser can
// name the exact Pascal keyword in a "not supported" diagnostic instead of a generic "unexpected
// identifier" (spec 3.4).

#include <QByteArray>

#include <cstdint>

namespace cg::script {

enum class TokenKind : std::uint8_t {
    End,           // EOF
    Identifier,    // also keywords - see Token::keyword
    IntLiteral,    // 123, $1F
    RealLiteral,   // 1.5, 1e3, 256.0
    StringLiteral, // 'a''b', #13#10, and any concatenation of the two
    // punctuation / operators
    Plus,
    Minus,
    Star,
    Slash,
    Assign, // + - * / :=
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEq,
    GreaterEq, // = <> < > <= >=
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    Dot,
    DotDot,
    At,
    Caret,
};

// Every keyword the v1 grammar names, whether accepted or explicitly rejected (spec 3.4).
enum class Keyword : std::uint8_t {
    None,
    And,
    Array,
    Begin,
    Break,
    Case,
    Const,
    Continue,
    Div,
    Do,
    Downto,
    Else,
    End,
    Except,
    Exit,
    Finally,
    For,
    Function,
    Goto,
    If,
    In,
    Inherited,
    Label,
    Mod,
    Nil,
    Not,
    Of,
    Or,
    Out,
    Packed,
    Procedure,
    Program,
    Record,
    Repeat,
    Set,
    Shl,
    Shr,
    Then,
    To,
    Try,
    Type,
    Unit,
    Until,
    Uses,
    Var,
    While,
    With,
    Xor,
};

struct Token {
    TokenKind kind = TokenKind::End;
    Keyword keyword = Keyword::None; // != None only when kind == Identifier
    int line = 1;                    // 1-based
    int column = 1;                  // 1-based
    QByteArray text;                 // identifier spelling as written (case preserved, ASCII)
    qint64 intValue = 0;
    double realValue = 0.0;
    QByteArray stringValue; // decoded: quotes removed, '' -> ', #N -> one byte
};

} // namespace cg::script
