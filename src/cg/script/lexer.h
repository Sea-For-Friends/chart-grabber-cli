// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PSPascalCompilerEx.pas (RemObjects tokenizer, replaced), spec 1.3
// (byte handling).
//
// One left-to-right scan over the raw script bytes (spec 1.3: no decoding, ever - a Russian ZMP's
// CP1251 string literals must reach the AST unchanged). Comment-vs-string is decided at each
// position in the same pass so an apostrophe inside a `{ }` comment or a `//` inside a string
// literal can never desynchronise the scanner (corpus-proven, spec 1.3).

#include "cg/script/diagnostic.h"
#include "cg/script/token.h"

#include <QByteArray>

#include <vector>

namespace cg::script {

class Lexer {
public:
    // `source` is the raw bytes of GetUrlScript.txt (see spec 1.3). No decoding, ever.
    explicit Lexer(QByteArray source);

    // Produces the whole token vector in one pass, ending with one End token. Lexical errors
    // (unterminated string, unterminated comment, illegal character) are appended to
    // `diagnostics` and the lexer resynchronises at the next line.
    [[nodiscard]] std::vector<Token> tokenize(DiagnosticList& diagnostics);

private:
    [[nodiscard]] char peek(int offset = 0) const;
    [[nodiscard]] bool atEnd() const;
    // Consumes one logical position: a CRLF/CR/LF pair counts as one, tracked as one line break;
    // anything else advances the column by one (a tab counts as one column - spec 2.3 L1).
    void step();
    void skipTrivia(); // whitespace, `//`, `{ }`, `(* *)` - can recurse across all four
    void skipToLineEnd();
    void addDiagnostic(int line, int column, QString message);

    [[nodiscard]] Token readString();
    [[nodiscard]] Token readNumber();
    [[nodiscard]] Token readIdentifier();
    // False for an illegal character (spec 2.3 L12): a diagnostic was recorded and no token
    // should be pushed for this position.
    [[nodiscard]] bool readPunctuation(Token& out);

    QByteArray source_;
    int pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    DiagnosticList* diagnostics_ = nullptr;
};

} // namespace cg::script
