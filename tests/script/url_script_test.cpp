// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-003, FEAT-NET-019. docs/specs/script.md 2.13, 5.1, 7, 8.2, 11.8 - the §11.8
// list minus `.NestedHttpRequestUsesTheFakeHost`/`.GlobalStoreSharedBetweenTwoUrlScriptsOfTheSameMap`,
// which need interface method dispatch (T-0015B) and move to T-0015E per the ticket.
#include "cg/script/url_script.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "script_host_fake.h"

using namespace cg::script;
using cg::script::test::FakeScriptHost;

namespace {

cg::core::Result<std::unique_ptr<UrlScript>> makeUrlScript(const QByteArray& source, IScriptHost& host) {
    return UrlScript::create(source, QStringLiteral("test"), host);
}

std::unique_ptr<UrlScript> mustCreate(const QByteArray& source, IScriptHost& host) {
    auto created = makeUrlScript(source, host);
    if (!created.has_value()) {
        ADD_FAILURE() << created.error().what().toStdString();
        return nullptr;
    }
    return std::move(*created);
}

// For the cancellation-while-waiting-for-the-lock scenario (finding 1, T-0015C rework): a
// WriteLn call blocks (inside buildRequest's lock) until the test releases it, giving a
// deterministic window in which a second, concurrent buildRequest call is guaranteed to still be
// waiting on the mutex. No sleeps.
class BlockingWriteLnHost : public FakeScriptHost {
public:
    void writeLn(const QByteArray& text) override {
        std::unique_lock lock(mutex_);
        blocked_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] { return released_; });
        lock.unlock();
        FakeScriptHost::writeLn(text);
    }

    void waitUntilBlocked() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return blocked_; });
    }

    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool blocked_ = false;
    bool released_ = false;
};

} // namespace

// --- create() / isTemplateOnly() (spec 2.13, ticket §Scope "create") ------------------------------

TEST(UrlScript, EmptySourceIsTemplateOnly) {
    FakeScriptHost host;
    auto created = makeUrlScript(QByteArray(), host);
    ASSERT_TRUE(created.has_value());
    EXPECT_TRUE((*created)->isTemplateOnly());
}

TEST(UrlScript, WhitespaceOnlySourceIsACompileError) {
    // spec 1.3: legacy's download path tests only `FScriptText = ''`, so a whitespace-only,
    // non-empty script still reaches the compiler and fails (no `end.`), disabling the map - it
    // does not fall back to isTemplateOnly().
    FakeScriptHost host;
    auto created = makeUrlScript(QByteArrayLiteral("   \n\t  \r\n  "), host);
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code(), cg::core::ErrorCode::ParseError);
}

TEST(UrlScript, CompileErrorMessageShapeCarriesEveryDiagnostic) {
    // u_ResStrings.pas:109 + u_PascalScriptCompiler.pas:84-90: 'Compilation error in script '#13#10'%0:s'
    // with every message followed by #13#10, so the text is the 27 bytes, 20 0D 0A, then each
    // "[Line N, Column M] ..." followed by 0D 0A. Two independent, guaranteed-separate lexer
    // diagnostics (spec L5: an unterminated string is a diagnostic and the lexer resynchronises at
    // the next line) prove every one of them reaches create()'s error, not just the first.
    FakeScriptHost host;
    auto created = makeUrlScript(
        QByteArrayLiteral("var A: string;\nbegin\n  A := 'unterminated1\n  A := 'unterminated2\nend."), host);
    ASSERT_FALSE(created.has_value());
    const QByteArray what = created.error().what().toUtf8();
    EXPECT_EQ(what, QByteArrayLiteral("Compilation error in script \r\n"
                                      "[Line 3, Column 8] unterminated string\r\n"
                                      "[Line 4, Column 8] unterminated string\r\n"
                                      "[Line 4, Column 3] expected 'end'\r\n"
                                      "[Line 4, Column 3] expected '.'\r\n"))
        << what.toStdString();
}

TEST(UrlScript, CompileErrorTextKeepsANonAsciiDiagnosticAsItsUtf8Bytes) {
    // A byte >= 0x80 outside a string or comment is an "illegal character" diagnostic whose
    // argument is that byte as a Latin-1 code point (lexer.cpp default branch). 0xC0 is U+00C0,
    // i.e. UTF-8 C3 80 in the outward text. The source has no other error, so the parser adds
    // nothing: exactly one diagnostic line, then the legacy CR LF terminator.
    FakeScriptHost host;
    auto created = makeUrlScript(QByteArrayLiteral("begin \xC0 end."), host);
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().what().toUtf8(),
              QByteArrayLiteral("Compilation error in script \r\n"
                                "[Line 1, Column 7] illegal character '\xC3\x80'\r\n"));
}

// --- read-back policy (TechLead ruling, T-0015A landing) -------------------------------------------

TEST(UrlScript, PostDataSwitchesToPost) {
    FakeScriptHost host;
    auto script =
        mustCreate(QByteArrayLiteral("begin ResultURL := 'http://x/'; PostData := 'a=1'; end."), host);
    ASSERT_TRUE(script);
    auto result = script->buildRequest(ScriptVars{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->tileExists);
    EXPECT_EQ(result->url, QByteArrayLiteral("http://x/"));
    EXPECT_EQ(result->postData, QByteArrayLiteral("a=1"));
}

TEST(UrlScript, RequestHeadPrefilledFromParamsUnlessRewrittenByScript) {
    // The caller (maptype) fills `vars.requestHead` from params.txt before calling buildRequest;
    // a script that never touches RequestHead must see it pass straight through unchanged, and one
    // that assigns it must have that assignment win.
    FakeScriptHost host;
    auto passthrough = mustCreate(QByteArrayLiteral("begin ResultURL := 'http://x/'; end."), host);
    ASSERT_TRUE(passthrough);
    ScriptVars varsPassthrough;
    varsPassthrough.requestHead = QByteArrayLiteral("X-From-Params: 1");
    auto resultPassthrough = passthrough->buildRequest(varsPassthrough);
    ASSERT_TRUE(resultPassthrough.has_value());
    EXPECT_EQ(resultPassthrough->requestHead, QByteArrayLiteral("X-From-Params: 1"));

    auto rewritten = mustCreate(
        QByteArrayLiteral("begin ResultURL := 'http://x/'; RequestHead := 'X-Script: 2'; end."), host);
    ASSERT_TRUE(rewritten);
    ScriptVars varsRewritten;
    varsRewritten.requestHead = QByteArrayLiteral("X-From-Params: 1");
    auto resultRewritten = rewritten->buildRequest(varsRewritten);
    ASSERT_TRUE(resultRewritten.has_value());
    EXPECT_EQ(resultRewritten->requestHead, QByteArrayLiteral("X-Script: 2"));
}

TEST(UrlScript, ResultUrlAndPostDataClearedBeforeEveryRun) {
    // Even a caller that (mistakenly, or by struct reuse) passes a ScriptVars with stale
    // ResultURL/PostData must see them cleared before the run - a script that sets neither always
    // observes "tile does not exist", never last call's leftovers.
    FakeScriptHost host;
    auto script = mustCreate(QByteArrayLiteral("begin end."), host);
    ASSERT_TRUE(script);
    ScriptVars vars;
    vars.resultUrl = QByteArrayLiteral("stale-url");
    vars.postData = QByteArrayLiteral("stale-post");
    auto result = script->buildRequest(vars);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->tileExists);
    EXPECT_TRUE(result->url.isEmpty());
    EXPECT_TRUE(result->postData.isEmpty());
}

TEST(UrlScript, ScriptBufferPersistsAcrossCalls) {
    FakeScriptHost host;
    auto script = mustCreate(QByteArrayLiteral("begin ScriptBuffer := ScriptBuffer + 'x'; end."), host);
    ASSERT_TRUE(script);
    EXPECT_TRUE(script->scriptBuffer().isEmpty());
    ASSERT_TRUE(script->buildRequest(ScriptVars{}).has_value());
    EXPECT_EQ(script->scriptBuffer(), QByteArrayLiteral("x"));
    ASSERT_TRUE(script->buildRequest(ScriptVars{}).has_value());
    EXPECT_EQ(script->scriptBuffer(), QByteArrayLiteral("xx"));
}

TEST(UrlScript, ScriptBufferUnchangedAfterRuntimeError) {
    // legacy BuildRequest:248-255/:279: the write-back line is skipped entirely on an exception -
    // a script that mutates ScriptBuffer and then raises must not persist that mutation.
    FakeScriptHost host;
    auto script = mustCreate(
        QByteArrayLiteral("var A: Integer;\nbegin ScriptBuffer := 'mutated'; A := 1 div 0; end."), host);
    ASSERT_TRUE(script);
    script->setScriptBuffer(QByteArrayLiteral("keep"));
    auto result = script->buildRequest(ScriptVars{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::IoError);
    EXPECT_EQ(script->scriptBuffer(), QByteArrayLiteral("keep"));
}

TEST(UrlScript, GetUrlBaseMutationDoesNotLeakToNextCall) {
    // spec 2.7 tail (`GetURLBase[8] := chr(ord('a') + random(3))`, corpus-proven): each run must
    // see the CALLER's original GetURLBase, never a previous run's mutated copy. `random()` is
    // avoided here (its seed state would itself differ between the two calls) in favour of a
    // deterministic self-referential mutation that still detects a leak: if run 2 started from
    // run 1's mutated base, its own increment would land one byte further than run 1's.
    FakeScriptHost host;
    auto script =
        mustCreate(QByteArrayLiteral(
                       "begin GetURLBase[8] := chr(ord(GetURLBase[8]) + 1); ResultURL := GetURLBase; end."),
                   host);
    ASSERT_TRUE(script);
    ScriptVars vars;
    vars.getUrlBase = QByteArrayLiteral("http://x/abc"); // byte 8 (1-based) is 'x'
    auto first = script->buildRequest(vars);
    ASSERT_TRUE(first.has_value());
    auto second = script->buildRequest(vars); // same original vars, not `first`'s output
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->url, second->url);
    EXPECT_EQ(first->url, QByteArrayLiteral("http://y/abc"));
}

TEST(UrlScript, VersionIsInputOnly) {
    // spec 5.1 row 14: `Version` is bound in but has no read-back slot; a script can only observe
    // it, never mutate what the caller sees.
    FakeScriptHost host;
    auto script = mustCreate(QByteArrayLiteral("begin ResultURL := Version; end."), host);
    ASSERT_TRUE(script);
    ScriptVars vars;
    vars.version = QByteArrayLiteral("1.2.3");
    auto result = script->buildRequest(vars);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, QByteArrayLiteral("1.2.3"));
}

TEST(UrlScript, DownloaderNullWhenNotEnabled) {
    // spec 5.1 row 4: `Downloader` is nil unless the map enables it - maptype's job to set, not
    // UrlScript's; a caller-supplied null downloader must reach the script as `Assigned = False`.
    FakeScriptHost host;
    auto script =
        mustCreate(QByteArrayLiteral(
                       "begin if Assigned(Downloader) then ResultURL := 'yes' else ResultURL := 'no'; end."),
                   host);
    ASSERT_TRUE(script);
    ScriptVars vars; // downloader left null
    auto result = script->buildRequest(vars);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->url, QByteArrayLiteral("no"));
}

TEST(UrlScript, BudgetExhaustionIsCancelled) {
    FakeScriptHost host;
    auto script =
        mustCreate(QByteArrayLiteral("var I: Integer;\nbegin I := 0; while True do I := I + 1; end."), host);
    ASSERT_TRUE(script);
    ExecBudget tinyBudget;
    tinyBudget.maxSteps = 1000;
    script->setBudget(tinyBudget);
    auto result = script->buildRequest(ScriptVars{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), cg::core::ErrorCode::Cancelled);
}

TEST(UrlScript, VmReusableAfterRuntimeError) {
    // A runtime error must leave the UrlScript in a state where the next, valid call succeeds
    // normally - no VM/frame corruption survives an error.
    FakeScriptHost host;
    auto script = mustCreate(QByteArrayLiteral("var A: Integer;\n"
                                               "begin\n"
                                               "  if GetX = 0 then A := 1 div 0\n"
                                               "  else ResultURL := 'http://ok/';\n"
                                               "end."),
                             host);
    ASSERT_TRUE(script);

    ScriptVars failing;
    failing.getX = 0;
    auto first = script->buildRequest(failing);
    ASSERT_FALSE(first.has_value());

    ScriptVars ok;
    ok.getX = 1;
    auto second = script->buildRequest(ok);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->url, QByteArrayLiteral("http://ok/"));
}

TEST(UrlScript, ConcurrentCallsAreSerialised) {
    // 8 threads hammering one UrlScript: buildRequest's mutex must serialise every run, so
    // ScriptBuffer's read-modify-write (append one byte) never loses an update. Deterministic (no
    // sleeps): correctness is checked purely by the final length after every thread has joined -
    // TSan-clean per the ticket.
    FakeScriptHost host;
    auto script = mustCreate(QByteArrayLiteral("begin ScriptBuffer := ScriptBuffer + 'a'; end."), host);
    ASSERT_TRUE(script);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 100;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    UrlScript* const raw = script.get();
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([raw] {
            for (int i = 0; i < kPerThread; ++i) {
                auto result = raw->buildRequest(ScriptVars{});
                EXPECT_TRUE(result.has_value());
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(script->scriptBuffer().size(), kThreads * kPerThread);
}

TEST(UrlScript, CancellationSkipsTheScript) {
    // Finding 1 (T-0015C rework, Tech Lead ruling): legacy checks the cancel notifier before
    // taking the lock (u_TileDownloadRequestBuilderPascalScript.pas:220) and again right after
    // acquiring it (:223) - the second check matters because the wait for the lock can be long
    // (another slot's script may be blocked in a nested HTTP request). Either hit must skip the
    // script entirely: no request, no error, ScriptBuffer untouched.

    // --- cancelled before the call: caught by the pre-lock check -----------------------------
    {
        FakeScriptHost host;
        auto script =
            mustCreate(QByteArrayLiteral("begin ScriptBuffer := ScriptBuffer + 'x'; WriteLn('ran'); "
                                         "ResultURL := 'http://never/'; end."),
                       host);
        ASSERT_TRUE(script);

        std::stop_source cancelled;
        cancelled.request_stop();
        auto result = script->buildRequest(ScriptVars{}, cancelled.get_token());
        ASSERT_TRUE(result.has_value());
        EXPECT_FALSE(result->tileExists);
        EXPECT_TRUE(result->url.isEmpty());
        EXPECT_TRUE(script->scriptBuffer().isEmpty()); // script never ran
        EXPECT_TRUE(host.writtenLines().isEmpty());    // WriteLn never called
    }

    // --- cancelled while waiting for the lock: caught by the post-lock check -----------------
    {
        BlockingWriteLnHost host;
        auto script = mustCreate(
            QByteArrayLiteral("begin ScriptBuffer := ScriptBuffer + 'a'; WriteLn('block'); end."), host);
        ASSERT_TRUE(script);

        cg::core::Result<ScriptRequest> resultA;
        std::thread threadA([&] { resultA = script->buildRequest(ScriptVars{}); });
        host.waitUntilBlocked(); // call A now holds the lock, parked inside WriteLn

        std::stop_source stopB;
        std::promise<void> bAboutToCall;
        std::future<void> bAboutToCallFuture = bAboutToCall.get_future();
        cg::core::Result<ScriptRequest> resultB;
        std::thread threadB([&] {
            bAboutToCall.set_value();
            resultB = script->buildRequest(ScriptVars{}, stopB.get_token());
        });
        bAboutToCallFuture.wait(); // call B has started - A still holds the lock, so B can only be
                                   // about to check (a) or already parked waiting for the mutex
        stopB.request_stop();      // cancel while B cannot yet have run the script
        host.release();            // let A finish and release the lock

        threadA.join();
        threadB.join();

        ASSERT_TRUE(resultA.has_value());
        ASSERT_TRUE(resultB.has_value());
        EXPECT_FALSE(resultB->tileExists);
        EXPECT_TRUE(resultB->url.isEmpty());
        EXPECT_EQ(script->scriptBuffer(), QByteArrayLiteral("a")); // only A's run appended - B did not run
    }
}

// --- T-0082 (P-6, spec 8.1 C-11): one CompiledScript shared by several UrlScript objects -----------

namespace {

// Overlap probe for the two-thread test: a run's WriteLn parks until a second run is inside the
// host too (or 2 s pass). Two UrlScript objects own two mutexes, so both runs overlap; a shared
// lock would serialise them, nobody would ever see `inside_ == 2`, and overlapped() stays false.
class OverlapProbeHost : public FakeScriptHost {
public:
    void writeLn(const QByteArray& /*text*/) override {
        std::unique_lock lock(mutex_);
        ++inside_;
        if (inside_ == 2) {
            overlapped_ = true;
            cv_.notify_all();
        }
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return overlapped_; });
        --inside_;
    }

    bool overlapped() const {
        std::lock_guard lock(mutex_);
        return overlapped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int inside_ = 0;
    bool overlapped_ = false;
};

} // namespace

TEST(UrlScript, TwoObjectsOverOneCompiledScriptKeepIndependentRunState) {
    // One compile, two objects: the shared program is referenced by the local handles and one per
    // object, so use_count is 2 before the objects exist and 4 after both - the objects share the
    // program rather than recompiling or copying it. ScriptBuffer is per object, so the runs of
    // `first` must not show up in `second`.
    FakeScriptHost host;
    auto compiled = CompiledScript::compile(
        QByteArrayLiteral("begin ScriptBuffer := ScriptBuffer + 'x'; end."), QStringLiteral("test"));
    ASSERT_TRUE(compiled.has_value());
    const std::shared_ptr<const CompiledScript> program = *compiled;
    EXPECT_EQ(program.use_count(), 2); // the `compiled` result plus `program`

    auto first = UrlScript::create(program, host);
    auto second = UrlScript::create(program, host);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(program.use_count(), 4); // + one per object: shared, not copied
    EXPECT_FALSE((*first)->isTemplateOnly());
    EXPECT_FALSE((*second)->isTemplateOnly());

    ASSERT_TRUE((*first)->buildRequest(ScriptVars{}).has_value());
    ASSERT_TRUE((*first)->buildRequest(ScriptVars{}).has_value());
    EXPECT_EQ((*first)->scriptBuffer(), QByteArrayLiteral("xx"));
    EXPECT_TRUE((*second)->scriptBuffer().isEmpty()); // second has not run yet

    ASSERT_TRUE((*second)->buildRequest(ScriptVars{}).has_value());
    EXPECT_EQ((*second)->scriptBuffer(), QByteArrayLiteral("x")); // its own buffer, not "xxx"
    EXPECT_EQ((*first)->scriptBuffer(), QByteArrayLiteral("xx")); // first untouched by second
}

TEST(UrlScript, TwoObjectsOverOneCompiledScriptRunConcurrently) {
    // Thread-safety claim (handoff T-0082): the compiled program is read-only, and each object
    // serialises only its own runs. Both runs must be inside the host at the same time; the
    // probe waits up to 2 s for that, so a serialising regression fails rather than hangs.
    // The program touches the shared data on every run: a const, an array, string operations and
    // a regular-expression builtin. Each thread checks its own result against the same expected URL.
    OverlapProbeHost host;
    auto compiled = CompiledScript::compile(
        QByteArrayLiteral("const Base = 'ab';\n"
                          "var A: array[0..2] of Integer; S: string;\n"
                          "begin\n"
                          "  WriteLn('ran');\n"
                          "  A[0] := 7; A[1] := 8; A[2] := A[0] + A[1];\n"
                          "  S := RegExprGetMatchSubStr('abc123', '[0-9]+', 0);\n"
                          "  ResultURL := Base + IntToStr(A[2]) + '/' + Copy(S + 'xyz', 1, 3);\n"
                          "end."),
        QStringLiteral("test"));
    ASSERT_TRUE(compiled.has_value());
    auto first = UrlScript::create(*compiled, host);
    auto second = UrlScript::create(*compiled, host);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    UrlScript* const a = first->get();
    UrlScript* const b = second->get();
    const QByteArray expected = QByteArrayLiteral("ab15/123");
    auto check = [&expected](UrlScript* script) {
        auto result = script->buildRequest(ScriptVars{});
        ASSERT_TRUE(result.has_value()) << result.error().what().toStdString();
        EXPECT_EQ(result->url, expected);
    };
    std::thread threadA([&check, a] { check(a); });
    std::thread threadB([&check, b] { check(b); });
    threadA.join();
    threadB.join();

    EXPECT_TRUE(host.overlapped());
}

TEST(UrlScript, CompileErrorIsReportedOnceAtCompileNotPerObject) {
    // A failed compile yields exactly one error carrying the diagnostics; there is no compiled
    // program to share, so no UrlScript can repeat that error. A null program is template-only
    // and never fails.
    const QByteArray bad =
        QByteArrayLiteral("var A: string;\nbegin\n  A := 'unterminated1\n  A := 'unterminated2\nend.");
    auto compiled = CompiledScript::compile(bad, QStringLiteral("test"));
    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error().code(), cg::core::ErrorCode::ParseError);
    EXPECT_EQ(compiled.error().what().count(QStringLiteral("Compilation error in script")), 1);

    FakeScriptHost host;
    auto fromSource = makeUrlScript(bad, host);
    ASSERT_FALSE(fromSource.has_value());
    EXPECT_EQ(fromSource.error().what().count(QStringLiteral("Compilation error in script")), 1);

    auto templateOnly = UrlScript::create(std::shared_ptr<const CompiledScript>(), host);
    ASSERT_TRUE(templateOnly.has_value());
    EXPECT_TRUE((*templateOnly)->isTemplateOnly());
}
