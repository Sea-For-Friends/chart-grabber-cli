// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Network/u_TileDownloadRequestBuilderPascalScript.pas (the whole unit - BuildRequest,
//        PrepareCompiledScript, and the ScriptBuffer half of SetVar); Src/Network/
//        u_TileDownloadRequestBuilder.pas:67-78 (the Lock/Unlock this class serialises
//        buildRequest() on); Src/PascalScript/u_PascalScriptCompiler.pas:83-90 (the compile-error
//        message shape - already reproduced verbatim by CompiledScript::compile, forwarded here
//        unchanged).
//
// The only type cg::maptype needs from this module (spec 2.13). One UrlScript per (map x
// download slot, spec 8.1): owns the compiled program, the per-instance lock and the
// ScriptBuffer that survives across calls. buildRequest() mirrors BuildRequest's Lock/Unlock scope
// exactly - the whole call body, not just the interpreter run - so a nested HTTP request
// (IScriptHost-reachable through the script) blocks every other tile of this map on this slot,
// exactly as in legacy (spec 8.2).

#include "cg/core/result.h"
#include "cg/core/sync.h"
#include "cg/script/compiled_script.h"
#include "cg/script/interpreter.h"
#include "cg/script/script_host.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <stop_token>

namespace cg::script {

// What buildRequest() returns (spec 2.13).
struct ScriptRequest {
    bool tileExists = true; // false when ResultURL came back empty - a normal outcome, not an error
    QByteArray url;
    QByteArray requestHead;
    QByteArray postData; // non-empty => POST
};

class UrlScript final {
public:
    // `sourceBytes` empty => isTemplateOnly() is true and buildRequest() must never be called
    // (legacy FUseUrlTemplateDirectly; maptype renders URLBase through the template engine
    // itself). A non-empty, whitespace-only `sourceBytes` is compiled and fails - legacy's
    // download path tests only `FScriptText = ''` (spec 1.3); there is no "IsScriptEmpty"-style
    // trim here, that helper exists only in the IDE. `host` is stored by reference for the
    // lifetime of this object (maptype's contract: one host per ZMP, outliving every UrlScript
    // built from it).
    [[nodiscard]] static core::Result<std::unique_ptr<UrlScript>>
    create(const QByteArray& sourceBytes, QString originName, IScriptHost& host);

    // Builds one instance over a program that is already compiled (spec 8.1, C-11, prerequisite
    // P-6). Legacy compiles once per ZMP (u_TileDownloadRequestBuilderFactoryPascalScript.pas:
    // 209-219) and hands the same bytecode to every download slot's builder, so the map's slots
    // share one `CompiledScript` and each keeps its own run state. `compiled` may be null, which
    // gives isTemplateOnly(). Compile errors never reach this overload: they come out of
    // CompiledScript::compile, once, before any instance exists.
    [[nodiscard]] static core::Result<std::unique_ptr<UrlScript>>
    create(std::shared_ptr<const CompiledScript> compiled, IScriptHost& host);

    [[nodiscard]] bool isTemplateOnly() const noexcept { return script_ == nullptr; }

    // FEAT-NET-003. Serialises on this instance's own mutex for the whole call, like
    // BuildRequest's Lock/Unlock. `vars.resultUrl`/`vars.postData` are cleared before the run and
    // `vars.scriptBuffer` is overwritten from the buffer this object holds - the caller does not
    // carry ScriptBuffer between calls, this object does (FEAT-NET-019). Must not be called when
    // isTemplateOnly() (asserted, a programmer error - maptype's contract per spec 2.13).
    //
    // `stop` is legacy's `ACancelNotifier`/`AOperationID` (spec 8.2): checked once before
    // attempting the lock and again immediately after acquiring it (legacy
    // BuildRequest:220,223 - the second check matters because the wait for the lock can be long,
    // another slot's script may be blocked in a nested HTTP request). Either check firing returns
    // `ScriptRequest{}` (the same "no request" outcome already used for an empty ResultURL) with
    // no error, without running the script and without touching ScriptBuffer - exactly what
    // legacy does: `Result` stays `nil`, nothing between `:224` and `:279` runs. The default
    // (never-cancelled) token means "run normally", so every existing caller that passes none is
    // unaffected.
    //
    // If the script raises, nothing is read back: no request is built, the error propagates,
    // ScriptBuffer keeps its previous value (legacy BuildRequest:248-255 re-raises). On success
    // (including `exit`) ScriptBuffer is always persisted; a non-empty ResultURL builds the
    // request (POST if PostData is non-empty, else GET) with RequestHead; an empty ResultURL
    // means no tile, not an error.
    [[nodiscard]] core::Result<ScriptRequest> buildRequest(ScriptVars vars, const std::stop_token& stop = {});

    // For the Map Type Editor (FEAT-NET-005) and for tests. Same mutex as buildRequest().
    [[nodiscard]] QByteArray scriptBuffer() const;
    void setScriptBuffer(QByteArray buffer);

    // No legacy equivalent (ADR-0003's deliberate deviation, spec 4.5/D-2); not synchronised on
    // mutex_ - a config knob set before concurrent use starts, like ExecBudget's own defaults.
    void setBudget(ExecBudget budget) { budget_ = budget; }

    // Public only so `std::make_unique` can reach it from `create()` (idioms.md forbids raw
    // `new`); construct through `create()`.
    UrlScript(std::shared_ptr<const CompiledScript> script, IScriptHost& host)
        : host_(host), script_(std::move(script)) {}

private:
    IScriptHost& host_; // never owned; must outlive this object (maptype's contract)
    std::shared_ptr<const CompiledScript> script_; // nullptr <=> isTemplateOnly()
    ExecBudget budget_;
    mutable core::Mutex mutex_;
    QByteArray scriptBuffer_; // guarded by mutex_; '' at construction (legacy PrepareCompiledScript:303)
};

} // namespace cg::script
