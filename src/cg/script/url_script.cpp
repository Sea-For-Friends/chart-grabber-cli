// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/script/url_script.h"

#include <utility>

namespace cg::script {

core::Result<std::unique_ptr<UrlScript>> UrlScript::create(const QByteArray& sourceBytes, QString originName,
                                                           IScriptHost& host) {
    if (sourceBytes.isEmpty()) {
        // legacy: FUseUrlTemplateDirectly := ACompiledData = ''; PrepareCompiledScript (and so
        // compilation) never runs, so there is no compiled program and no ScriptBuffer.
        return create(nullptr, host);
    }
    // A whitespace-only (but non-empty) source reaches the compiler exactly like any other
    // (spec 1.3) and, having no `end.`, fails - CompiledScript::compile's diagnostics already
    // carry the shape ticket §7 wants ("Compilation error in script \r\n" + every diagnostic + "\r\n",
    // u_PascalScriptCompiler.pas:83-90), so the error is forwarded unchanged.
    core::Result<std::shared_ptr<const CompiledScript>> compiled =
        CompiledScript::compile(sourceBytes, std::move(originName));
    if (!compiled.has_value()) {
        return std::unexpected(compiled.error());
    }
    return create(std::move(*compiled), host);
}

core::Result<std::unique_ptr<UrlScript>> UrlScript::create(std::shared_ptr<const CompiledScript> compiled,
                                                           IScriptHost& host) {
    // legacy PrepareCompiledScript:303: FScriptBuffer := '' right after a successful compile (the
    // member default). The compiled program is only read after this point (see compiled_script.h).
    return std::make_unique<UrlScript>(std::move(compiled), host);
}

core::Result<ScriptRequest> UrlScript::buildRequest(ScriptVars vars, const std::stop_token& stop) {
    Q_ASSERT(script_ && "UrlScript::buildRequest must not be called when isTemplateOnly()");

    // legacy BuildRequest:220: checked before the wait for the lock even begins. Same outcome as
    // an empty ResultURL (legacy leaves `Result` nil either way) - no request, no error, script
    // not run, ScriptBuffer untouched.
    if (stop.stop_requested()) {
        ScriptRequest noRequest;
        noRequest.tileExists = false;
        return noRequest;
    }

    // u_TileDownloadRequestBuilder.pas:67-78: Lock/Unlock wraps the whole call body below, not
    // just the interpreter run - a nested HTTP request the script makes through IScriptHost is
    // therefore made with this mutex held too (spec 8.2), exactly as legacy.
    core::WriteLock lock(mutex_);

    // legacy BuildRequest:223: checked again right after acquiring the lock - the wait above can
    // be long (another slot's script may be blocked in a nested HTTP request), so a cancellation
    // that landed during that wait must still stop the script from running.
    if (stop.stop_requested()) {
        ScriptRequest noRequest;
        noRequest.tileExists = false;
        return noRequest;
    }

    // spec 2.13 / ticket: ResultURL and PostData are cleared before every run regardless of what
    // the caller passed in; ScriptBuffer is supplied by this object, not by the caller, and
    // survives to the next call only through scriptBuffer_.
    vars.resultUrl.clear();
    vars.postData.clear();
    vars.scriptBuffer = scriptBuffer_;

    Interpreter interpreter(script_, budget_);
    core::Result<void> result = interpreter.run(host_, vars);
    if (!result.has_value()) {
        // legacy BuildRequest:248-255: `FPSExec.Stop; raise;` - nothing is read back, ScriptBuffer
        // (scriptBuffer_) keeps its previous value, and the next call starts clean.
        return std::unexpected(result.error());
    }

    // On success (including `exit`), ScriptBuffer is always persisted - legacy :279 sits outside
    // the `if FPSVars.ResultUrl <> ''` guard below.
    scriptBuffer_ = vars.scriptBuffer;

    ScriptRequest request;
    if (vars.resultUrl.isEmpty()) {
        // legacy :256: Result stays nil - a normal "tile does not exist" outcome, not an error.
        request.tileExists = false;
        return request;
    }
    request.tileExists = true;
    request.url = vars.resultUrl;
    request.requestHead = vars.requestHead;
    request.postData = vars.postData; // non-empty => POST (legacy :257-267)
    return request;
}

QByteArray UrlScript::scriptBuffer() const {
    core::ReadLock lock(mutex_);
    return scriptBuffer_;
}

void UrlScript::setScriptBuffer(QByteArray buffer) {
    core::WriteLock lock(mutex_);
    scriptBuffer_ = std::move(buffer);
}

} // namespace cg::script
