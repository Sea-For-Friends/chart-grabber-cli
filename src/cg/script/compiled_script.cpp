// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/compiled_script.h"

#include "cg/script/lexer.h"
#include "cg/script/parser.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace cg::script {

CompiledScript::CompiledScript(std::unique_ptr<SymbolTable> symbols, Program* program,
                               DiagnosticList warnings, QString originName, bool deterministic)
    : symbols_(std::move(symbols)), program_(program), warnings_(std::move(warnings)),
      originName_(std::move(originName)), deterministic_(deterministic) {}

// Out of line so `SymbolTable` (forward-declared in the header) is a complete type here, as
// `std::unique_ptr<SymbolTable>`'s destructor requires.
CompiledScript::~CompiledScript() = default;

int CompiledScript::frameSize() const noexcept {
    return symbols_->frameSize();
}

DeclaredType CompiledScript::declaredType(int slot) const {
    const std::vector<DeclaredType>& types = symbols_->declaredTypes();
    if (slot < 0 || static_cast<std::size_t>(slot) >= types.size()) {
        return DeclaredType::Unknown;
    }
    return types[static_cast<std::size_t>(slot)];
}

const Value* CompiledScript::constValue(int slot) const {
    return symbols_->constValueForSlot(slot);
}

const ArrayInfo* CompiledScript::arrayInfo(int slot) const {
    return symbols_->arrayInfoForSlot(slot);
}

core::Result<std::shared_ptr<const CompiledScript>> CompiledScript::compile(const QByteArray& sourceBytes,
                                                                            QString originName) {
    auto symbols = std::make_unique<SymbolTable>();
    DiagnosticList diagnostics;

    Lexer lexer(sourceBytes);
    std::vector<Token> tokens = lexer.tokenize(diagnostics);

    Parser parser(std::move(tokens), *symbols);
    core::Result<Program*> parsed = parser.parse(diagnostics);
    if (!parsed.has_value()) {
        // u_ResStrings.pas:109 'Compilation error in script '#13#10'%0:s' (the space before CR LF is
        // part of the literal); u_PascalScriptCompiler.pas:87 appends CR LF after every message.
        // The message bodies are the port's own diagnostics (ADR-0003).
        QString text = QStringLiteral("Compilation error in script \r\n");
        for (const Diagnostic& diagnostic : diagnostics) {
            text += diagnostic.toString() + QStringLiteral("\r\n");
        }
        return core::fail(core::ErrorCode::ParseError, text);
    }

    const bool deterministic = !symbols->sawNonDeterministicSymbol();
    return std::make_shared<CompiledScript>(std::move(symbols), *parsed, std::move(diagnostics),
                                            std::move(originName), deterministic);
}

} // namespace cg::script
