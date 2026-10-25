// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/lexer.h"

#include <charconv>
#include <cstdlib>
#include <utility>

namespace cg::script {

namespace {

bool isAsciiDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isHexDigit(char c) {
    return isAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(char c) {
    return isIdentStart(c) || isAsciiDigit(c);
}

char toAsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive, ASCII-only compare (spec 2.2: identifier comparison folds ASCII case).
bool equalsFolded(const QByteArray& text, const char* keyword) {
    int i = 0;
    for (; i < text.size() && keyword[i] != '\0'; ++i) {
        if (toAsciiLower(text[i]) != keyword[i]) {
            return false;
        }
    }
    return i == text.size() && keyword[i] == '\0';
}

Keyword lookupKeyword(const QByteArray& text) {
    static constexpr std::pair<const char*, Keyword> kKeywords[] = {
        {"and", Keyword::And},
        {"array", Keyword::Array},
        {"begin", Keyword::Begin},
        {"break", Keyword::Break},
        {"case", Keyword::Case},
        {"const", Keyword::Const},
        {"continue", Keyword::Continue},
        {"div", Keyword::Div},
        {"do", Keyword::Do},
        {"downto", Keyword::Downto},
        {"else", Keyword::Else},
        {"end", Keyword::End},
        {"except", Keyword::Except},
        {"exit", Keyword::Exit},
        {"finally", Keyword::Finally},
        {"for", Keyword::For},
        {"function", Keyword::Function},
        {"goto", Keyword::Goto},
        {"if", Keyword::If},
        {"in", Keyword::In},
        {"inherited", Keyword::Inherited},
        {"label", Keyword::Label},
        {"mod", Keyword::Mod},
        {"nil", Keyword::Nil},
        {"not", Keyword::Not},
        {"of", Keyword::Of},
        {"or", Keyword::Or},
        {"out", Keyword::Out},
        {"packed", Keyword::Packed},
        {"procedure", Keyword::Procedure},
        {"program", Keyword::Program},
        {"record", Keyword::Record},
        {"repeat", Keyword::Repeat},
        {"set", Keyword::Set},
        {"shl", Keyword::Shl},
        {"shr", Keyword::Shr},
        {"then", Keyword::Then},
        {"to", Keyword::To},
        {"try", Keyword::Try},
        {"type", Keyword::Type},
        {"unit", Keyword::Unit},
        {"until", Keyword::Until},
        {"uses", Keyword::Uses},
        {"var", Keyword::Var},
        {"while", Keyword::While},
        {"with", Keyword::With},
        {"xor", Keyword::Xor},
    };
    for (const auto& [spelling, keyword] : kKeywords) {
        if (equalsFolded(text, spelling)) {
            return keyword;
        }
    }
    return Keyword::None;
}

} // namespace

Lexer::Lexer(QByteArray source) : source_(std::move(source)) {}

char Lexer::peek(int offset) const {
    const int p = pos_ + offset;
    return (p >= 0 && p < source_.size()) ? source_[p] : '\0';
}

bool Lexer::atEnd() const {
    return pos_ >= source_.size();
}

void Lexer::step() {
    if (atEnd()) {
        return;
    }
    const char c = source_[pos_];
    if (c == '\r') {
        ++pos_;
        if (!atEnd() && source_[pos_] == '\n') {
            ++pos_;
        }
        ++line_;
        column_ = 1;
    } else if (c == '\n') {
        ++pos_;
        ++line_;
        column_ = 1;
    } else {
        ++pos_;
        ++column_; // a tab also counts as one column (spec 2.3 L1)
    }
}

void Lexer::skipToLineEnd() {
    while (!atEnd() && peek() != '\r' && peek() != '\n') {
        step();
    }
}

void Lexer::addDiagnostic(int line, int column, QString message) {
    diagnostics_->push_back(Diagnostic{DiagnosticSeverity::Error, line, column, std::move(message)});
}

void Lexer::skipTrivia() {
    for (;;) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            step(); // a lone '\r' is whitespace too (spec 1.3); step() folds CRLF into one line
        } else if (c == '/' && peek(1) == '/') {
            skipToLineEnd();
        } else if (c == '{') {
            const int startLine = line_;
            const int startColumn = column_;
            step();
            bool closed = false;
            while (!atEnd()) {
                if (peek() == '}') {
                    step();
                    closed = true;
                    break;
                }
                step();
            }
            if (!closed) {
                addDiagnostic(startLine, startColumn, QStringLiteral("unterminated comment"));
            }
        } else if (c == '(' && peek(1) == '*') {
            const int startLine = line_;
            const int startColumn = column_;
            step();
            step();
            bool closed = false;
            while (!atEnd()) {
                if (peek() == '*' && peek(1) == ')') {
                    step();
                    step();
                    closed = true;
                    break;
                }
                step();
            }
            if (!closed) {
                addDiagnostic(startLine, startColumn, QStringLiteral("unterminated comment"));
            }
        } else {
            return;
        }
    }
}

Token Lexer::readString() {
    const int startLine = line_;
    const int startColumn = column_;
    QByteArray value;
    for (;;) {
        // spec 2.3 L6: pieces fuse only when strictly adjacent - no whitespace/comment between
        // them (Delphi/PascalScript rule; corpus-verified zero-reliance on the looser form, see
        // T-0013 handoff F3). Do NOT skipTrivia() here: a separated `'a' 'b'` must fall through to
        // two tokens, not one.
        if (peek() == '\'') {
            step();
            bool terminated = false;
            while (!atEnd()) {
                const char c = peek();
                if (c == '\r' || c == '\n') {
                    break; // a string may not span a line (spec 2.3 L5)
                }
                if (c == '\'') {
                    step();
                    if (peek() == '\'') {
                        value.append('\'');
                        step();
                        continue;
                    }
                    terminated = true;
                    break;
                }
                value.append(c);
                step();
            }
            if (!terminated) {
                addDiagnostic(startLine, startColumn, QStringLiteral("unterminated string"));
                skipToLineEnd();
                break;
            }
        } else if (peek() == '#') {
            step();
            QByteArray digits;
            const bool isHex = peek() == '$';
            if (isHex) {
                step();
                while (isHexDigit(peek())) {
                    digits.append(peek());
                    step();
                }
            } else {
                while (isAsciiDigit(peek())) {
                    digits.append(peek());
                    step();
                }
            }
            if (digits.isEmpty()) {
                addDiagnostic(startLine, startColumn, QStringLiteral("malformed character literal"));
            } else {
                const unsigned long code = std::strtoul(digits.constData(), nullptr, isHex ? 16 : 10);
                value.append(static_cast<char>(code & 0xFF));
            }
        } else {
            break;
        }
    }
    Token token;
    token.kind = TokenKind::StringLiteral;
    token.line = startLine;
    token.column = startColumn;
    token.stringValue = value;
    return token;
}

Token Lexer::readNumber() {
    const int startLine = line_;
    const int startColumn = column_;
    Token token;
    token.line = startLine;
    token.column = startColumn;

    if (peek() == '$') {
        step();
        QByteArray hex;
        while (isHexDigit(peek())) {
            hex.append(peek());
            step();
        }
        token.kind = TokenKind::IntLiteral;
        if (hex.isEmpty()) {
            addDiagnostic(startLine, startColumn, QStringLiteral("malformed hex literal"));
        } else {
            token.intValue = static_cast<qint64>(std::strtoull(hex.constData(), nullptr, 16));
        }
        return token;
    }

    QByteArray digits;
    while (isAsciiDigit(peek())) {
        digits.append(peek());
        step();
    }
    QByteArray real = digits;
    bool isReal = false;
    // Maximal-munch exception (spec 2.3 L8): `1..5` lexes as `1` `..` `5`, never `1.` `.5`.
    if (peek() == '.' && peek(1) != '.' && isAsciiDigit(peek(1))) {
        isReal = true;
        real.append('.');
        step();
        while (isAsciiDigit(peek())) {
            real.append(peek());
            step();
        }
    }
    if (peek() == 'e' || peek() == 'E') {
        int aheadOffset = 1;
        if (peek(aheadOffset) == '+' || peek(aheadOffset) == '-') {
            ++aheadOffset;
        }
        if (isAsciiDigit(peek(aheadOffset))) {
            isReal = true;
            real.append(peek());
            step();
            if (peek() == '+' || peek() == '-') {
                real.append(peek());
                step();
            }
            while (isAsciiDigit(peek())) {
                real.append(peek());
                step();
            }
        }
    }
    if (isReal) {
        token.kind = TokenKind::RealLiteral;
        double value = 0.0;
        std::from_chars(real.constData(), real.constData() + real.size(), value);
        token.realValue = value;
    } else {
        token.kind = TokenKind::IntLiteral;
        token.intValue = digits.toLongLong();
    }
    return token;
}

Token Lexer::readIdentifier() {
    const int startLine = line_;
    const int startColumn = column_;
    QByteArray text;
    while (isIdentChar(peek())) {
        text.append(peek());
        step();
    }
    Token token;
    token.kind = TokenKind::Identifier;
    token.line = startLine;
    token.column = startColumn;
    token.text = text;
    token.keyword = lookupKeyword(text);
    return token;
}

bool Lexer::readPunctuation(Token& out) {
    const int startLine = line_;
    const int startColumn = column_;
    const char c = peek();
    auto emitToken = [&](TokenKind kind, int width) {
        for (int i = 0; i < width; ++i) {
            step();
        }
        out = Token{};
        out.kind = kind;
        out.line = startLine;
        out.column = startColumn;
        return true;
    };
    switch (c) {
    case ':':
        return peek(1) == '=' ? emitToken(TokenKind::Assign, 2) : emitToken(TokenKind::Colon, 1);
    case '<':
        if (peek(1) == '>') {
            return emitToken(TokenKind::NotEqual, 2);
        }
        return peek(1) == '=' ? emitToken(TokenKind::LessEq, 2) : emitToken(TokenKind::Less, 1);
    case '>':
        return peek(1) == '=' ? emitToken(TokenKind::GreaterEq, 2) : emitToken(TokenKind::Greater, 1);
    case '.':
        return peek(1) == '.' ? emitToken(TokenKind::DotDot, 2) : emitToken(TokenKind::Dot, 1);
    case '=':
        return emitToken(TokenKind::Equal, 1);
    case '+':
        return emitToken(TokenKind::Plus, 1);
    case '-':
        return emitToken(TokenKind::Minus, 1);
    case '*':
        return emitToken(TokenKind::Star, 1);
    case '/':
        return emitToken(TokenKind::Slash, 1);
    case '(':
        return emitToken(TokenKind::LParen, 1);
    case ')':
        return emitToken(TokenKind::RParen, 1);
    case '[':
        return emitToken(TokenKind::LBracket, 1);
    case ']':
        return emitToken(TokenKind::RBracket, 1);
    case ',':
        return emitToken(TokenKind::Comma, 1);
    case ';':
        return emitToken(TokenKind::Semicolon, 1);
    case '@':
        return emitToken(TokenKind::At, 1);
    case '^':
        return emitToken(TokenKind::Caret, 1);
    default:
        addDiagnostic(startLine, startColumn,
                      QStringLiteral("illegal character '%1'").arg(QChar(static_cast<uchar>(c))));
        step();
        return false;
    }
}

std::vector<Token> Lexer::tokenize(DiagnosticList& diagnostics) {
    diagnostics_ = &diagnostics;
    pos_ = 0;
    line_ = 1;
    column_ = 1;
    std::vector<Token> tokens;
    for (;;) {
        skipTrivia();
        if (atEnd()) {
            Token end;
            end.kind = TokenKind::End;
            end.line = line_;
            end.column = column_;
            tokens.push_back(end);
            break;
        }
        const char c = peek();
        if (c == '\'' || c == '#') {
            tokens.push_back(readString());
        } else if (isAsciiDigit(c) || c == '$') {
            tokens.push_back(readNumber());
        } else if (isIdentStart(c)) {
            tokens.push_back(readIdentifier());
        } else {
            Token token;
            if (readPunctuation(token)) {
                tokens.push_back(token);
            }
        }
    }
    diagnostics_ = nullptr;
    return tokens;
}

} // namespace cg::script
