// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT yet (T-0013 slice A). docs/specs/script.md 3.7, 11.4 - the blocking gate: every
// real GetUrlScript.txt in the corpus must lex and parse. One failure fails the build; fix the
// parser, not the test (ticket T-0013 notes).
#include "cg/script/compiled_script.h"
#include "cg/script/lexer.h"
#include "cg/script/parser.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <memory>

#include "fixture_path.h"

using namespace cg::script;

namespace {

// Every `tests/fixtures/zmp/sas.maps/**/GetUrlScript.txt`. Directory names contain spaces
// (`_tourism/ukraine/Chernogora 2010.zmp`), so this iterates rather than shell-globs.
QStringList allScriptPaths() {
    QStringList paths;
    const QString root = QDir(cgScriptFixturesDir()).filePath(QStringLiteral("zmp/sas.maps"));
    QDirIterator it(root, QStringList{QStringLiteral("GetUrlScript.txt")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        paths.append(it.next());
    }
    paths.sort();
    return paths;
}

QByteArray readRawBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// Two fixtures are genuinely malformed Pascal, not a parser gap (ticket T-0013 notes: "if a
// script is genuinely malformed in legacy too, list it in the handoff with evidence"). Both are
// under `_broken_maps` and both are missing the ';' between an assignment and the `if` that
// follows it on the next line (`step:=round(...)` / `if gety<3 then step:=...;`, no separator) -
// a `stmt-list` requires one (spec 3.1). See docs/handoffs/T-0013.md.
bool isKnownMalformed(const QString& path) {
    return path.endsWith(QStringLiteral("_broken_maps/Balticsmap/balticmaps.eu.map.zmp/GetUrlScript.txt")) ||
           path.endsWith(QStringLiteral("_broken_maps/Balticsmap/balticmaps.eu.topo.zmp/GetUrlScript.txt"));
}

} // namespace

TEST(Corpus, AllParse) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList paths = allScriptPaths();
    ASSERT_GE(paths.size(), 350) << "the ZMP script corpus fixture changed size";
    int failures = 0;
    int skippedKnownMalformed = 0;
    for (const QString& path : paths) {
        if (isKnownMalformed(path)) {
            ++skippedKnownMalformed;
            continue;
        }
        const QByteArray source = readRawBytes(path);
        auto compiled = CompiledScript::compile(source, path);
        if (!compiled.has_value()) {
            ++failures;
            ADD_FAILURE() << path.toStdString() << ":\n" << compiled.error().what().toStdString();
        }
    }
    EXPECT_EQ(failures, 0) << "zero tolerance: every corpus script must compile (spec 11.4)";
    EXPECT_EQ(skippedKnownMalformed, 2) << "the known-malformed exclusion list no longer matches the corpus";
}

TEST(Corpus, NoUnknownIdentifiers) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList paths = allScriptPaths();
    ASSERT_GE(paths.size(), 350);
    for (const QString& path : paths) {
        const QByteArray source = readRawBytes(path);
        DiagnosticList diagnostics;
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize(diagnostics);
        SymbolTable symbols;
        Parser parser(std::move(tokens), symbols);
        (void)parser.parse(diagnostics);
        for (const Diagnostic& diagnostic : diagnostics) {
            EXPECT_FALSE(diagnostic.message.contains(QStringLiteral("unknown identifier")))
                << path.toStdString() << ": " << diagnostic.toString().toStdString();
        }
    }
}

TEST(Corpus, NoIllegalCharacters) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList paths = allScriptPaths();
    ASSERT_GE(paths.size(), 350);
    for (const QString& path : paths) {
        const QByteArray source = readRawBytes(path);
        DiagnosticList diagnostics;
        Lexer lexer(source);
        (void)lexer.tokenize(diagnostics);
        for (const Diagnostic& diagnostic : diagnostics) {
            EXPECT_FALSE(diagnostic.message.contains(QStringLiteral("illegal character")))
                << path.toStdString() << ": " << diagnostic.toString().toStdString();
        }
    }
}

// D-4 (ruling 2026-09-18 / review recommendation 4): the two-file exclusion above must not become
// a silent mask for an unrelated parser bug in those files - assert each fails compilation and
// that its diagnostic actually names the missing-separator defect (F0's evidence), not some other
// failure.
TEST(Corpus, ExcludedBalticsmapScriptsFailWithExpectedDiagnostic) {
    CG_SKIP_IF_NO_CORPUS();
    const QString root = QDir(cgScriptFixturesDir()).filePath(QStringLiteral("zmp/sas.maps"));
    const QStringList excluded = {
        QDir(root).filePath(QStringLiteral("_broken_maps/Balticsmap/balticmaps.eu.map.zmp/GetUrlScript.txt")),
        QDir(root).filePath(
            QStringLiteral("_broken_maps/Balticsmap/balticmaps.eu.topo.zmp/GetUrlScript.txt")),
    };
    for (const QString& path : excluded) {
        ASSERT_TRUE(QFile::exists(path)) << path.toStdString();
        const QByteArray source = readRawBytes(path);
        auto compiled = CompiledScript::compile(source, path);
        ASSERT_FALSE(compiled.has_value())
            << path.toStdString() << ": expected to fail (F0: missing ';' at line 9/10)";
        DiagnosticList diagnostics;
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize(diagnostics);
        SymbolTable symbols;
        Parser parser(std::move(tokens), symbols);
        (void)parser.parse(diagnostics);
        bool foundExpected = false;
        for (const Diagnostic& diagnostic : diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Error && diagnostic.line == 10 &&
                (diagnostic.message.contains(QStringLiteral("end")) ||
                 diagnostic.message.contains(QStringLiteral("';'")))) {
                foundExpected = true;
                break;
            }
        }
        EXPECT_TRUE(foundExpected) << path.toStdString()
                                   << ": no diagnostic at line 10 mentioning 'end'/';' - "
                                   << "the exclusion may now be masking a different defect";
    }
}

TEST(Corpus, DeterministicCountMatches) {
    CG_SKIP_IF_NO_CORPUS();
    const QStringList paths = allScriptPaths();
    ASSERT_GE(paths.size(), 350);
    int deterministic = 0;
    int nonDeterministic = 0;
    for (const QString& path : paths) {
        if (isKnownMalformed(path)) {
            continue;
        }
        const QByteArray source = readRawBytes(path);
        auto compiled = CompiledScript::compile(source, path);
        ASSERT_TRUE(compiled.has_value()) << path.toStdString();
        if ((*compiled)->isDeterministic()) {
            ++deterministic;
        } else {
            ++nonDeterministic;
        }
    }
    // Spec 3.7/11.4 measured 240/110 over all 350; both excluded scripts are simple arithmetic
    // (no Random/Downloader/GetUnixTime/Global/ScriptBuffer/ResponseHead/TileCache/
    // SaveToLocalFile - spec 2.12) so they would count as deterministic if they compiled:
    // 238 + 2 == 240, confirming the split holds over the 348 this slice can actually compile.
    EXPECT_EQ(deterministic, 238);
    EXPECT_EQ(nonDeterministic, 110);
}
