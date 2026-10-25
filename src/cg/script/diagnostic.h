// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptCompiler.pas (TPascalScriptCompiler.Compile error
//        aggregation), Src/PascalScript/u_PSPascalCompilerEx.pas.
//
// One compile- or run-time message with a source position. The legacy compiler collected every
// message from RemObjects and concatenated them into a single EPascalScriptCompileError; the port
// keeps the list so the UI can show them one per line and jump to a position.

#include <QString>

#include <vector>

namespace cg::script {

enum class DiagnosticSeverity : std::uint8_t { Error, Warning, Hint };

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    int line = 0;    // 1-based, as Delphi reports
    int column = 0;  // 1-based
    QString message; // developer/author-facing, not tr()'d - see diagnostic.cpp

    // "[Line 12, Column 7] Unknown identifier 'Foo'" - the legacy shape, for the map-disabled text.
    [[nodiscard]] QString toString() const;
};

using DiagnosticList = std::vector<Diagnostic>;

// Joined with '\n'. Used only for `Parser::parse`'s own error text; the map's disabled-state text
// is built in `CompiledScript::compile`.
[[nodiscard]] QString formatDiagnostics(const DiagnosticList& list);

} // namespace cg::script
