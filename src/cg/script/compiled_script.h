// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/PascalScript/u_PascalScriptCompiler.pas (TPascalScriptCompiler.Compile), the
// bytecode-caching half of Src/Network/u_TileDownloadRequestBuilderPascalScript.pas.
//
// The compiled, immutable form of one GetUrlScript.txt: lexing + parsing + compile-time name
// resolution happen once here and the result is cached in RAM for the map's lifetime (legacy
// caches RemObjects bytecode in RAM only, never on disk - ADR-0003). This slice implements only
// the "compile" half (lex, parse, resolve, decide determinism); executing the program is the
// interpreter's job (T-0014).

#include "cg/core/result.h"
#include "cg/script/ast.h"
#include "cg/script/diagnostic.h"
#include "cg/script/value.h" // DeclaredType (spec 2.6) - T-0014B

#include <QString>

#include <memory>

namespace cg::script {

class SymbolTable; // owns the AST arena; see parser.h. Kept out of this header on purpose so
                   // callers that only need `program()`/`isDeterministic()` need not see it.

class CompiledScript {
public:
    // Compiles once. The returned object is immutable and shareable - one per ZMP, cached in RAM.
    [[nodiscard]] static core::Result<std::shared_ptr<const CompiledScript>>
    compile(const QByteArray& sourceBytes, QString originName);

    // Public only so `std::make_shared` can reach it from `compile()` (idioms.md forbids raw
    // `new`); construct through `compile()`.
    CompiledScript(std::unique_ptr<SymbolTable> symbols, Program* program, DiagnosticList warnings,
                   QString originName, bool deterministic);
    ~CompiledScript();

    [[nodiscard]] const Program& program() const noexcept { return *program_; }
    [[nodiscard]] const DiagnosticList& warnings() const noexcept { return warnings_; }
    [[nodiscard]] const QString& originName() const noexcept { return originName_; }
    [[nodiscard]] int frameSize() const noexcept;

    // The `DeclaredType` (spec 2.6) `SymbolTable` resolved for this slot at compile time - see
    // parser.h. `DeclaredType::Unknown` for a bound variable, an out-of-range slot, or a local
    // whose type the parser could not resolve; the interpreter (T-0014B) range-wraps/rounds a
    // slot write per spec 4.1 N1/N2 using this.
    [[nodiscard]] DeclaredType declaredType(int slot) const;

    // T-0032 (PD-103): the compile-time-folded `Value` of a `const`-declared slot, or nullptr for
    // every other slot - `Interpreter::defaultValueForSlot` returns this instead of the ordinary
    // zero value (spec 4.4 I1/I3) when present.
    [[nodiscard]] const Value* constValue(int slot) const;
    // T-0032 (PD-1): the element type + declared bounds of a `DeclaredType::StaticArray` slot
    // (spec 3.6), or nullptr for every other slot.
    [[nodiscard]] const ArrayInfo* arrayInfo(int slot) const;

    // True when the script never touches Random/Downloader/GetUnixTime/Global/ScriptBuffer/
    // ResponseHead/TileCache/SaveToLocalFile (spec 2.12); computed once at compile time by
    // `SymbolTable::resolve` (parser.h).
    [[nodiscard]] bool isDeterministic() const noexcept { return deterministic_; }

private:
    std::unique_ptr<SymbolTable> symbols_; // owns the arena `program_` points into
    Program* program_;
    DiagnosticList warnings_;
    QString originName_;
    bool deterministic_;
};

} // namespace cg::script
