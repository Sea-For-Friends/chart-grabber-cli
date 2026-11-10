#!/usr/bin/env bash
# Quality gate. Output is one line per finding: file:line: RULE: message. Ends with PASS or FAIL.
# Usage: tools/validate.sh [--fast] [--changed[=REV]] [--tsan] [--asan] [--no-cache] [--selftest]
#   --fast  skips clang-tidy
#   --changed[=REV]  developer mode: clang-tidy only on src units changed against REV (default: merge-base
#           with master) plus the units that include a changed header; falls back to all units when .clang-tidy,
#           a CMakeLists.txt or the gate scripts changed. Never use for the final gate (no flag).
#   --no-cache  control run: compilation cache off, no precompiled headers, the build directories this
#           run uses are wiped first, so everything is compiled from scratch. Slow and trusted; must give the
#           same verdict, test count and findings as the cached run. Combine freely with --tsan / CG_MODULES.
#   --asan builds/runs the AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer variant (the
#           `asan` preset, or tools/nocmake_build.sh with ASAN=1 when scoped); same skips as --tsan. Any sanitizer
#           report fails the run: ASan aborts the process, UBSan halts (UBSAN_OPTIONS=halt_on_error=1, no rebuild with
#           -fno-sanitize-recover needed), LSan exits 1 at process end; ctest / the nocmake runner see a non-zero exit. With
#           --changed, a diff whose every file is on the docs allow-list (docs/*, top-level *.md, .gitignore,
#           .gitattributes, and the globs of CG_DOCS_GLOBS) is not gated: "gate: skipped, docs-only diff", PASS, exit 0; any
#           other path counts as code. tools/ only -> the selftests of the changed tools (validate*.sh -> --selftest; the plain
#           run too when a script the gate itself runs changed, or a path of the globs of CG_GATE_GLOBS).
#   --classify=REV prints the diff class of REV..HEAD (docs|tools|tools-gate|code|code-tools) and exits; used by the landing script.
#   --selftest checks the diff classifier above and the skip modes in a throwaway repo (tools/validate_selftest.sh); no build.
#   --tsan builds/runs the ThreadSanitizer preset (or nocmake fallback when scoped)
#           instead of the debug build; skips format/hygiene/tidy (unrelated to tsan).
# Environment: CG_DOCS_GLOBS / CG_GATE_GLOBS  extra space-separated path globs of the docs class / of the gate scripts (for a
#           wrapper that keeps more files in the same checkout); CG_REQUIRE_FIXTURES  never set here, an inherited value is
#           honoured (tests/corpus_skip.h: 1 turns a missing fixture into a failure instead of a skip).
# Logs go to build/logs/ (configure.log, test.log, nocmake.log, nocmake-opt.log).
set -u
cd "$(dirname "$0")/.."
source tools/lib/env_compat.sh   # SAS_* -> CG_* (REMOVE-AFTER: next release); before any CG_* is read
LOGS=build/logs
CLASSIFY_BASE=""; SELFTEST=0; FAST=0; TSAN=0; ASAN=0; NOCACHE=0; CHANGED_BASE=""
for a in "$@"; do
  case "$a" in
    --fast) FAST=1 ;;
    --tsan) TSAN=1 ;;
    --asan) ASAN=1 ;;
    --no-cache) NOCACHE=1 ;;
    --selftest) SELFTEST=1 ;;
    --changed) CHANGED_BASE=$(git merge-base HEAD master 2>/dev/null || git merge-base HEAD main 2>/dev/null || echo HEAD) ;;
    --changed=*) CHANGED_BASE="${a#--changed=}" ;;
    --classify=*) CLASSIFY_BASE="${a#--classify=}" ;;
  esac
done
# The win10 VM runs this gate under MSYS2; the selftests of the Linux-only release tooling (deb/yaml, export, SPDX CRLF) are skipped there
is_windows_host() { case "${CG_UNAME:-$(uname -s)}" in MINGW*|MSYS*|CYGWIN*) return 0 ;; esac; return 1; }

# Diff classifier. Reads changed paths on stdin, prints one of: code | tools-gate | tools | docs.
#   docs       allow-list only: docs/*, top-level *.md, .gitignore, .gitattributes, CG_DOCS_GLOBS: no gate
#   tools-gate a tools/ script the gate itself runs (or its selftest), CG_GATE_GLOBS: selftests, then the full gate
#   tools      other tools/ files: only their selftests
#   code       everything else (default: src/ tests/ third_party/ cmake/ CMake* presets clang config data/ cli/ ...): full gate
in_globs() { # <path> <space-separated globs>: true when the path matches one of them
  local g
  for g in $2; do
    # shellcheck disable=SC2053
    [[ $1 == $g ]] && return 0
  done
  return 1
}
diff_class() {
  local cls=docs p
  while IFS= read -r p; do
    [[ -n "$p" ]] || continue
    if in_globs "$p" "${CG_DOCS_GLOBS:-}"; then continue; fi
    if in_globs "$p" "${CG_GATE_GLOBS:-}"; then cls=tools-gate; continue; fi
    case "$p" in
      docs/*|.gitignore|.gitattributes) ;;
      tools/validate.sh|tools/validate_selftest.sh|tools/gate_env.sh|tools/lib/*|tools/nocmake_build.sh|tools/changed_units.py|tools/raw_new_check.py|tools/*suppressions.txt) cls=tools-gate ;;
      tools/*) [[ $cls == tools-gate ]] || cls=tools ;;
      */*) echo code; return ;;
      *.md) ;;
      *) echo code; return ;;
    esac
  done
  echo "$cls"
}
# --classify=REV prints the class of the diff REV..HEAD (docs | tools | tools-gate | code | code-tools) and exits.
# The landing script uses it to pick the landing gates (one classifier, no second path list). Fail closed: a git failure prints
# code.
if [[ -n "$CLASSIFY_BASE" ]]; then
  if CH=$(git diff --no-renames --name-only "$CLASSIFY_BASE" HEAD -- 2>/dev/null); then
    C=$(printf '%s\n' "$CH" | diff_class)
    if [[ $C == code ]] && printf '%s\n' "$CH" | grep -q '^tools/'; then C=code-tools; fi
    echo "$C"
  else echo code; fi
  exit 0
fi
# TODO form: only `TODO: <description>`; `TODO(x):`, `TODO ` and a bare `TODO:` fail. Prints "line:text" per finding.
todo_form(){ grep -nP '\bTODO\b(?!: \S)' "$1"; return 0; }

if [[ $SELFTEST -eq 1 ]]; then
  bad=0
  # TODO form rows: text <TAB> expected number of findings (ids built by concatenation)
  tt=$(mktemp); T=TODO; ID="T-0001"
  while IFS=$'\t' read -r txt want; do
    printf '%s\n' "$txt" >"$tt"; got=$(todo_form "$tt" | wc -l)
    [[ "$got" == "$want" ]] || { echo "tools/validate.sh:1: SELFTEST: TODO form [$txt] expected $want finding(s), got $got"; bad=1; }
  done <<ROWS
// $T: x	0
// $T($ID): x	1
// $T x	1
// $T:	1
// $T(x): y	1
// no marker here	0
ROWS
  rm -f "$tt"
  chk(){ local got; got=$(printf '%s\n' "${@:2}" | diff_class); [[ "$got" == "$1" ]] || { echo "tools/validate.sh:1: SELFTEST: expected $1 got $got for: ${*:2}"; bad=1; }; }
  chk docs docs/user/guide.md CHANGELOG.md
  chk docs
  chk tools tools/helper.py tools/helper_selftest.sh
  chk tools-gate tools/helper.py tools/validate.sh
  chk tools tools/a/b.sh tools/a/c.txt
  chk tools-gate tools/raw_new_check.py
  # The extra globs of a wrapper: CG_DOCS_GLOBS adds to the docs class, CG_GATE_GLOBS to the gate scripts
  chk code extra/a.md
  CG_DOCS_GLOBS='extra/* other/*' chk docs extra/a.md other/b.txt docs/a.md
  CG_DOCS_GLOBS='extra/*' chk code extra/a.md other/b.txt
  chk tools tools/gate_helper.py
  CG_GATE_GLOBS='tools/gate_helper.py' chk tools-gate tools/gate_helper.py tools/helper.py
  chk docs docs/a.md .gitignore .gitattributes README.md
  chk code docs/a.md src/cg/core/a.cpp
  chk code cli/main.cpp
  chk code data/x
  chk code docs/a.md sub/dir/README.md
  chk tools-gate tools/validate_selftest.sh
  chk tools-gate tools/lib/env_compat.sh
  chk code tests/core/a_test.cpp tools/validate.sh
  chk code third_party/x/y.c
  chk code CMakeLists.txt
  chk code src/cg/core/CMakeLists.txt
  chk code CMakePresets.json
  chk code cmake/x.txt
  CG_UNAME=MINGW64_NT-10.0-19045 is_windows_host || { echo "tools/validate.sh:1: SELFTEST: MINGW64 not recognised as a Windows host"; bad=1; }
  CG_UNAME=Linux is_windows_host && { echo "tools/validate.sh:1: SELFTEST: Linux taken for a Windows host"; bad=1; }
  if [[ $bad -eq 0 ]]; then echo "selftest: 24 cases"; exec tools/validate_selftest.sh; else echo "FAIL (selftest)"; exit 1; fi
fi
# --changed on a diff without code needs no build gate. Fail closed: any git failure means "code". Tracked files
# only (committed + staged + unstaged edits of tracked files); untracked files never count.
if [[ -n "$CHANGED_BASE" ]]; then
  if CH=$(git diff --no-renames --name-only "$CHANGED_BASE" -- 2>/dev/null); then
    # A diff of class code that also touches tools/ runs the touched tools' selftests too, then the full gate
    DCLS=$(printf '%s\n' "$CH" | diff_class); TF=$(printf '%s\n' "$CH" | grep '^tools/' || true)
    [[ $DCLS != code || -z "$TF" ]] || DCLS=code-tools
    case "$DCLS" in
      docs) echo "gate: skipped, docs-only diff"; echo PASS; exit 0 ;;
      tools|tools-gate|code-tools)
        TB=0
        if grep -qE '^tools/(validate(_selftest)?\.sh|lib/.*)$' <<<"$TF"; then tools/validate.sh --selftest >/dev/null 2>&1 || { echo "tools/validate.sh:1: SELFTEST: tools/validate.sh --selftest failed"; TB=1; }; fi
        if grep -qE '^tools/raw_new_check\.py$' <<<"$TF"; then python3 tools/raw_new_check.py --self-test || TB=1; fi
        if [[ $TB -ne 0 ]]; then echo "FAIL (tool selftests)"; exit 1; fi
        if [[ $DCLS == tools ]]; then echo "gate: tools-only diff, selftests run"; echo PASS; exit 0; fi
        if [[ $DCLS == code-tools ]]; then echo "gate: code diff touching tools/, tool selftests passed, full gate follows"; else echo "gate: tools diff, selftests passed, plain run follows"; fi ;;
    esac
  fi
fi
# Every CG_MODULES token must be a module directory under src/cg (a comma list used to match nothing, run 0 tests
# and print PASS; tests/fixtures and other tests/ directories are not modules)
read -ra _mods <<<"${CG_MODULES:-}"
for _m in "${_mods[@]}"; do
  if [[ "$_m" == */* || "$_m" == .* || ! -d "src/cg/$_m" ]]; then
    echo "tools/validate.sh:1: CG_MODULES: '$_m' is not a directory under src/cg (separate modules with spaces, not commas)"
    echo "FAIL (1 findings)"; exit 1
  fi
done
[[ $NOCACHE -eq 1 ]] && export CG_NO_CACHE=1
if [[ $TSAN -eq 1 && $ASAN -eq 1 ]]; then echo "tools/validate.sh:1: USAGE: --tsan and --asan cannot be combined (run them separately)"; echo "FAIL (1 findings)"; exit 1; fi
source tools/gate_env.sh
# CG_REQUIRE_FIXTURES is left as inherited: unset, a test whose fixture is absent skips with "not present (public tree)".
mkdir -p "$LOGS"
F=$(mktemp); trap 'rm -f "$F"' EXIT
fail(){ echo "$*" | tee -a "$F"; }
note(){ echo "note: $*"; }
T0=$SECONDS; stage(){ note "time: $1 $((SECONDS - T0))s"; T0=$SECONDS; }  # Per-stage wall time

# Parallel clang-tidy: was one serial process (5-7 minutes). Candidate units on stdin; $1 = compile database dir,
# $2 = changed_units.py kind (nocmake|ninja), $3 = build dir holding the dependency information. Output: the same
# "file:line:col: message: check" findings as before, sorted and de-duplicated.
tidy_run() {
  local db="$1" kind="$2" bdir="$3" units sel
  units=$(cat); [[ -n "$units" ]] || return 0
  if [[ -n "$CHANGED_BASE" ]]; then
    # fail closed: any failure of the selector (python3 missing, crash, bad base) analyses everything
    if ! sel=$(printf '%s\n' "$units" | python3 tools/changed_units.py --base "$CHANGED_BASE" --kind "$kind" --builddir "$bdir"); then
      sel=ALL; note "changed-files mode: selector failed -- analysing all units" >&2
    fi
    if [[ "$sel" == "ALL" ]]; then note "changed-files mode: build/tidy configuration changed against $CHANGED_BASE -- analysing all units" >&2
    else
      note "changed-files mode: $(printf '%s\n' "$sel" | grep -c . || true) of $(printf '%s\n' "$units" | grep -c .) units analysed (base ${CHANGED_BASE:0:12})" >&2
      units="$sel"
    fi
    [[ -n "$units" ]] || return 0
  fi
  # one output file per clang-tidy job, concatenated afterwards: concurrent jobs can never interleave their lines
  local td; td=$(mktemp -d); export CG_TIDY_TMP="$td"
  printf '%s\n' "$units" | xargs -P "$CG_JOBS" -n 3 bash -c 'cg_wait_load; o=$(mktemp -p "$CG_TIDY_TMP") || exit 255; clang-tidy -p "$0" "$@" >"$o" 2>/dev/null' "$db"
  # clang-tidy exits 1 on findings (xargs 123); 124-127 = a job could not even start (mktemp failed, killed, ...): a gate failure
  local xrc=$?; [[ $xrc -ge 124 ]] && echo "tools/validate.sh:1: TIDY: a clang-tidy job could not run (xargs exit $xrc, e.g. mktemp failed); results incomplete"
  cat "$td"/* 2>/dev/null | grep -E '(warning|error):' | sed 's/ \[\([a-z0-9,-]*\)\]$/: \1/' | sort -u
  rm -f "$td"/* 2>/dev/null; rmdir "$td" 2>/dev/null; return 0
}

SRC_FILES=$(find src tests -type f \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null | sort)
# Scope to modules when several developers work in parallel: CG_MODULES="core script" tools/validate.sh
if [[ -n "${CG_MODULES:-}" ]]; then
  MODRE="^(src/cg|tests)/($(echo $CG_MODULES | tr ' ' '|'))/"
  SRC_FILES=$(echo "$SRC_FILES" | grep -E "$MODRE" || true)
  note "scoped to modules: $CG_MODULES"
fi

if [[ $TSAN -eq 1 ]]; then
  note "tsan mode: format/hygiene/tidy skipped, ThreadSanitizer build+test only"
  note "caveat: this is what makes log_test.cpp's MinimumLevelIsAtomicUnderConcurrentAccess a real regression net"
  # Suppressions (tools/tsan_suppressions.txt); exported here so they reach both the ctest child
  # processes below and tools/nocmake_build.sh's scoped fallback.
  export TSAN_OPTIONS="suppressions=$(pwd)/tools/tsan_suppressions.txt"
elif [[ $ASAN -eq 1 ]]; then
  note "asan mode: format/hygiene/tidy skipped, AddressSanitizer+UBSan+LeakSanitizer build+test only"
  # Every sanitizer report must fail the run. ASan aborts on the first error by default (halt_on_error=1 stated
  # explicitly); UBSan only prints and continues unless halted at run time; LSan reports at exit with status 23.
  # Exported so the ctest child processes and tools/nocmake_build.sh's scoped fallback inherit them. Set, never
  # appended to: an inherited detect_leaks=0 / exitcode=0 would silently weaken the run.
  export ASAN_OPTIONS="halt_on_error=1:detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1"
  export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
  # The suppression file is empty (nothing is suppressed); entries need a reason there.
  export LSAN_OPTIONS="suppressions=$(pwd)/tools/lsan_suppressions.txt:print_suppressions=0"
else
  # 1. Format
  if command -v clang-format >/dev/null && [[ -n "$SRC_FILES" ]]; then
    for f in $SRC_FILES; do
      clang-format --dry-run --Werror "$f" >/dev/null 2>&1 || fail "$f:1: FORMAT: not clang-format clean (run clang-format -i)"
    done
  else note "format skipped (no clang-format or no sources)"; fi

  # 2. Hygiene rules
  # RAW-NEW (tools/raw_new_check.py, same rule as the former grep, ignores text inside comments only; fails closed)
  python3 tools/raw_new_check.py --self-test || fail "tools/raw_new_check.py:1: RAW-NEW: checker self-test failed (see stderr above)"
  if rn=$(python3 tools/raw_new_check.py $SRC_FILES); then
    while IFS=: read -r f n; do [[ -n "$f" ]] && fail "$f:$n: RAW-NEW: use make_shared/make_unique (Qt types with parent are exempt)"; done <<<"$rn"
  else fail "tools/raw_new_check.py:1: RAW-NEW: the check itself failed (python3 missing or crash) -- treated as a finding"; fi
  for f in $SRC_FILES; do
    todo_form "$f" | while IFS=: read -r n _; do fail "$f:$n: TODO-FORM: use 'TODO: <description>' (no ticket id)"; done
    if [[ "$f" != src/cg/core/platform/* ]]; then
      grep -nE '^\s*#\s*if(def|ndef)?\s.*(Q_OS_|_WIN32|__linux__|__APPLE__)' "$f" | while IFS=: read -r n _; do fail "$f:$n: PLATFORM-IFDEF: only allowed under src/cg/core/platform/"; done
    fi
    if [[ "$f" == src/cg/* && "$f" != src/cg/ui/* && "$f" != src/cg/app/* ]]; then
      grep -nE '#include\s*<Q(Widget|Dialog|MainWindow|Application|PushButton|Label|Menu|Action|MessageBox|FileDialog|tWidgets)' "$f" | while IFS=: read -r n _; do fail "$f:$n: WIDGETS-IN-CORE: Qt Widgets allowed only in src/cg/ui and src/cg/app"; done
    fi
    if [[ "$f" == src/*.h ]]; then
      grep -q 'Ports:' "$f" || fail "$f:1: MISSING-PORTS-COMMENT: header must state 'Ports: <legacy unit>' (or 'Ports: nothing')"
    fi
    # The tile transport is libcurl only; QNAM must never appear in the downloader
    # (proxy_resolver.cpp is the one net file allowed to touch Qt Network).
    if [[ "$f" == src/cg/net/* && "$f" != src/cg/net/proxy_resolver.cpp ]]; then
      grep -nE 'QNetworkAccessManager|QNetworkReply|QNetworkRequest' "$f" \
        | while IFS=: read -r n _; do
            fail "$f:$n: QNAM-IN-DOWNLOADER: the tile transport is libcurl only"
          done
    fi
  done

  # SPDX licence header on every source (tools/add_spdx.sh --check; unscoped runs use the script's own scope, scoped
  # runs check their files)
  if [[ -n "${CG_MODULES:-}" ]]; then spdx=$(tools/add_spdx.sh --check $SRC_FILES 2>&1); else spdx=$(tools/add_spdx.sh --check 2>&1); fi
  if [[ -n "$spdx" ]]; then while IFS= read -r l; do fail "$l"; done <<<"$spdx"; fi

  # 2b. PROJ / geodesic confinement.
  for f in $(echo "$SRC_FILES" | grep '^src/' || true); do
    if [[ "$f" != src/cg/geo/proj/proj_converter.cpp ]] && grep -q '#include *<proj\.h>' "$f"; then
      fail "$f:1: PROJ-LEAK: <proj.h> only in src/cg/geo/proj/proj_converter.cpp"
    fi
    if [[ "$f" != src/cg/geo/datum.cpp ]] && grep -q '#include *"geodesic\.h"' "$f"; then
      fail "$f:1: GEODESIC-LEAK: geodesic.h only in src/cg/geo/datum.cpp"
    fi
    # <sqlite3.h> only in SqliteDb (a design decision).
    if [[ "$f" != "src/cg/storage/sqlite/sqlite_db.h" && "$f" != "src/cg/storage/sqlite/sqlite_db.cpp" ]] \
       && grep -q '#include *<sqlite3\.h>' "$f"; then
      fail "$f:1: SQLITE3-LEAK: <sqlite3.h> only in src/cg/storage/sqlite/sqlite_db.{h,cpp}"
    fi
    # A design decision: <archive.h>/<archive_entry.h> only in ArchiveTileStorage's own
    # translation unit (the header stays libarchive-free, using only an opaque `struct archive;`
    # forward declaration). And ArchiveReaderZip's, the zip reader of core::ConfigProviderZip.
    if [[ "$f" != "src/cg/storage/archive_tile_storage.cpp" && "$f" != "src/cg/core/archive_reader_zip.cpp" ]] \
       && grep -qE '#include *<archive(_entry)?\.h>' "$f"; then
      fail "$f:1: ARCHIVE-LEAK: <archive.h>/<archive_entry.h> only in src/cg/storage/archive_tile_storage.cpp and src/cg/core/archive_reader_zip.cpp"
    fi
    # Qt Qml headers only in the three translation units of the JavaScript builder.
    if [[ "$f" != "src/cg/maptype/tile_download_request_builder_js.cpp" \
          && "$f" != "src/cg/maptype/tile_download_request_builder_factory_js.cpp" \
          && "$f" != "src/cg/maptype/js_script_watchdog.cpp" ]] \
       && grep -qE '#include *<(QJS|QtQml|QQml)' "$f"; then
      fail "$f:1: QML-LEAK: <QJS...>/<QtQml...>/<QQml...> only in src/cg/maptype/tile_download_request_builder_js.cpp, tile_download_request_builder_factory_js.cpp and js_script_watchdog.cpp"
    fi
  done
fi

stage "format+hygiene"
# 3. Configure + build + test
PRESET=debug; [[ $TSAN -eq 1 ]] && PRESET=tsan; [[ $ASAN -eq 1 ]] && PRESET=asan
if [[ $NOCACHE -eq 1 ]]; then
  _suf="${CG_MODULES:+-$(echo $CG_MODULES | tr " " "_")}"; [[ $TSAN -eq 1 ]] && _suf="${_suf}-tsan"; [[ $ASAN -eq 1 ]] && _suf="${_suf}-asan"
  note "control run (--no-cache): ccache disabled, no PCH, wiping build/$PRESET build/nocmake${_suf} build/nocmake-opt${_suf}"
  rm -rf "build/$PRESET" "build/nocmake${_suf}" "build/nocmake-opt${_suf}"
fi
if command -v cmake >/dev/null && [[ -z "${CG_MODULES:-}" ]]; then
  if cmake --preset "$PRESET" >"$LOGS/configure.log" 2>&1; then
    cmake --build --preset "$PRESET" -- -j "$CG_JOBS" -l "$CG_LOAD_LIMIT" 2>&1 | grep -vE '^\[[0-9]+/[0-9]+\] ' | grep -E '(error|warning)[: ]' | head -50 | while read -r l; do fail "$l"; done
    stage "cmake build"
    if ! ctest --preset "$PRESET" -j "$CG_JOBS" --test-load "$CG_LOAD_LIMIT" >"$LOGS/test.log" 2>&1; then
      grep -E '\*\*\*Failed|Not Run' "$LOGS/test.log" | while read -r l; do fail "tests: $l"; done
      grep -E 'ThreadSanitizer' "$LOGS/test.log" | head -20 | while read -r l; do fail "tsan: $l"; done
      grep -E 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$LOGS/test.log" | sed -E 's/^==[0-9]+==//' | awk '{k=$0; gsub(/[0-9]+/,"N",k); if(!seen[k]++) print}' | head -20 | while read -r l; do fail "asan: $l"; done
      fail "tests:0: TESTS: ctest --preset $PRESET failed (see $LOGS/test.log)"
    else note "$(grep -E 'tests passed' "$LOGS/test.log")"; fi
    stage "ctest"
    if [[ $TSAN -eq 0 && $ASAN -eq 0 && $FAST -eq 0 ]] && command -v clang-tidy >/dev/null && [[ -f build/debug/compile_commands.json ]]; then
      CPP=$(echo "$SRC_FILES" | grep '\.cpp$' | grep '^src/' || true)
      mkdir -p build/debug/tidy && sed 's/ -mno-direct-extern-access//g' build/debug/compile_commands.json > build/debug/tidy/compile_commands.json  # GCC-only flag from Qt's CMake config
      printf '%s\n' "$CPP" | tidy_run build/debug/tidy ninja build/debug | while read -r l; do fail "$l"; done
    fi
  else fail "CMakeLists.txt:1: CONFIGURE: cmake --preset $PRESET failed (see $LOGS/configure.log)"; tail -15 "$LOGS/configure.log"; fi
else
  note "scoped or cmake-less run — using tools/nocmake_build.sh fallback build"
  TSAN=$TSAN ASAN=$ASAN tools/nocmake_build.sh 2>&1 | tee "$LOGS/nocmake.log" | grep -E ': (fatal )?error: |\[  FAILED  \]|ThreadSanitizer|ERROR: (Address|Leak)Sanitizer|runtime error:|^nocmake: .*(failed|FAILED)|undefined reference' | sed -E 's/==[0-9]+==//' | awk '{k=$0; gsub(/[0-9]+/,"N",k); if ($0 ~ /_test\.cpp: \[  FAILED  \] [^ ]+$/) { if (!ex[$0]++) print } else if(!seen[k]++) print}' | head -40 | while read -r l; do fail "$l"; done
  grep -q '^nocmake: OK' "$LOGS/nocmake.log" || fail "tools/nocmake_build.sh:1: BUILD: fallback build/test failed (see $LOGS/nocmake.log)"
  stage "nocmake build+link+tests"
  NCDIR=build/nocmake${CG_MODULES:+-$(echo $CG_MODULES | tr " " "_")}
  if [[ $TSAN -eq 0 && $ASAN -eq 0 && $FAST -eq 0 ]] && command -v clang-tidy >/dev/null && [[ -f $NCDIR/compile_commands.json ]]; then
    CPP=$(echo "$SRC_FILES" | grep '\.cpp$' | grep '^src/' || true)
    printf '%s\n' "$CPP" | tidy_run "$NCDIR" nocmake "$NCDIR" | while read -r l; do fail "$l"; done
  fi
  stage "clang-tidy"
  # Gate gap: -O0 hides some diagnostics (e.g. maybe-uninitialized); run a compile-only -O2
  # pass over the same scoped sources, incremental via nocmake_build.sh's own staleness check,
  # to surface them. Skipped with --fast alongside tidy.
  if [[ $TSAN -eq 0 && $ASAN -eq 0 && $FAST -eq 0 ]]; then
    OPTCHECK=1 CG_MODULES="${CG_MODULES:-}" tools/nocmake_build.sh 2>&1 | tee "$LOGS/nocmake-opt.log" | grep -E ': (fatal )?error: |^nocmake-opt: .*FAILED' | head -40 | while read -r l; do fail "$l"; done
    grep -q '^nocmake-opt: OK' "$LOGS/nocmake-opt.log" || fail "tools/nocmake_build.sh:1: OPT-BUILD: -O2 compile check failed (see $LOGS/nocmake-opt.log)"
  fi
fi

stage "clang-tidy (cmake path) or -O2 pass (nocmake path)"
if [[ -s "$F" ]]; then echo "FAIL ($(wc -l <"$F") findings)"; exit 1; else echo "PASS"; fi
