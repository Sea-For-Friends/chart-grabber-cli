#!/usr/bin/env bash
# Quality gate. Output is one line per finding: file:line: RULE: message. Ends with PASS or FAIL.
# Usage: tools/validate.sh [--fast] [--changed[=REV]] [--tsan] [--asan] [--no-cache] [--selftest]
#   --fast  skips clang-tidy
#   --changed[=REV]  developer mode (T-0090): clang-tidy only on src units changed against REV (default: merge-base
#           with master) plus the units that include a changed header; falls back to all units when .clang-tidy,
#           a CMakeLists.txt or the gate scripts changed. Never use for the landing gate (landing = no flag).
#   --no-cache  control run (T-0090): compilation cache off, no precompiled headers, the build directories this
#           run uses are wiped first, so everything is compiled from scratch. Slow and trusted; must give the
#           same verdict, test count and findings as the cached run. Combine freely with --tsan / CG_MODULES.
#   --asan  (T-0101) builds/runs the AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer variant (the
#           `asan` preset, or tools/nocmake_build.sh with ASAN=1 when scoped); same skips as --tsan. Any sanitizer
#           report fails the run: ASan aborts the process, UBSan halts (UBSAN_OPTIONS=halt_on_error=1, no rebuild
#           with -fno-sanitize-recover needed), LSan exits 1 at process end; ctest / the nocmake runner see a non-zero exit.
#           T-0236: with --changed, a diff whose every file is on the docs allow-list (docs/*, .claude/*, top-level *.md,
#           .gitignore, .gitattributes) is not gated: "gate: skipped, docs-only diff", PASS, exit 0; any other path counts
#           as code. tools/ only -> the selftests of the changed tools (validate*.sh -> --selftest; the plain run too
#           when a script the gate itself runs changed).
#   --selftest  (T-0236) checks the diff classifier above and the skip modes in a throwaway repo (tools/validate_selftest.sh); no build.
#   --tsan  (T-0025) builds/runs the ThreadSanitizer preset (or nocmake fallback when scoped)
#           instead of the debug build; skips format/hygiene/tidy/parity (unrelated to tsan).
set -u
cd "$(dirname "$0")/.."
source tools/lib/env_compat.sh   # T-0313: SAS_* -> CG_* (REMOVE-AFTER: next release); before any CG_* is read
# T-0320: public tree (the filtered export has no process machinery): the private-only steps are skipped, each with one line.
PUBLIC=0; { [[ ! -d .claude ]] || [[ ! -f tools/land.py ]] || [[ ! -d docs/parity ]]; } && PUBLIC=1
pub_skip(){ echo "validate: public tree, skipping $1"; }
SELFTEST=0; FAST=0; TSAN=0; ASAN=0; NOCACHE=0; CHANGED_BASE=""
for a in "$@"; do
  case "$a" in
    --fast) FAST=1 ;;
    --tsan) TSAN=1 ;;
    --asan) ASAN=1 ;;
    --no-cache) NOCACHE=1 ;;
    --selftest) SELFTEST=1 ;;
    --changed) CHANGED_BASE=$(git merge-base HEAD master 2>/dev/null || git merge-base HEAD main 2>/dev/null || echo HEAD) ;;
    --changed=*) CHANGED_BASE="${a#--changed=}" ;;
  esac
done
# T-0345: the win10 VM runs this gate under MSYS2; the selftests of the Linux-only release tooling (deb/yaml, export, SPDX CRLF) are skipped there
is_windows_host() { case "${CG_UNAME:-$(uname -s)}" in MINGW*|MSYS*|CYGWIN*) return 0 ;; esac; return 1; }

# T-0236: diff classifier. Reads changed paths on stdin, prints one of: code | tools-gate | tools | docs.
#   docs       allow-list only: docs/*, .claude/*, top-level *.md, .gitignore, .gitattributes: no gate
#   tools-gate a tools/ script the gate itself runs (or its selftest): selftests, then the full gate
#   tools      other tools/ files: only their selftests
#   code       everything else (default: src/ tests/ third_party/ cmake/ CMake* presets clang config data/ cli/ ...): full gate
diff_class() {
  local cls=docs p
  while IFS= read -r p; do
    [[ -n "$p" ]] || continue
    case "$p" in
      docs/*|.claude/*|.gitignore|.gitattributes) ;;
      tools/validate.sh|tools/validate_selftest.sh|tools/gate_env.sh|tools/lib/*|tools/nocmake_build.sh|tools/changed_units.py|tools/parity_check.py|tools/raw_new_check.py|tools/*suppressions.txt) cls=tools-gate ;;
      tools/*) [[ $cls == tools-gate ]] || cls=tools ;;
      */*) echo code; return ;;
      *.md) ;;
      *) echo code; return ;;
    esac
  done
  echo "$cls"
}
# T-0269: parse check of the workflow scripts (docs/conventions/workflows/*.js). A script is the body of an async function
# (top-level await and return, `export const meta` -> `const meta`), so it is checked wrapped like that with `node --check`.
# Prints one finding line per broken file to stdout; WF_N = files checked, WF_BAD = files that failed. No node: warning only.
WF_N=0; WF_BAD=0
check_workflows() {
  WF_N=0; WF_BAD=0
  if ! command -v node >/dev/null 2>&1; then echo "warning: node not found, workflow parse check skipped"; return 0; fi
  local f t msg
  for f in "$@"; do
    [[ -f "$f" ]] || continue
    WF_N=$((WF_N + 1))
    t=$(mktemp --suffix=.js) || { echo "$f:1: WORKFLOW-PARSE: mktemp failed"; WF_BAD=$((WF_BAD + 1)); continue; }
    { echo '(async () => {'; sed '0,/^export const meta/s//const meta/' "$f"; printf '\n})();\n'; } >"$t"
    if ! msg=$(node --check "$t" 2>&1); then
      echo "$f:1: WORKFLOW-PARSE: $(printf '%s\n' "$msg" | grep -m1 -E '^[A-Za-z]*Error' || printf '%s\n' "$msg" | head -1)"
      WF_BAD=$((WF_BAD + 1))
    fi
    rm -f "$t"
  done
  return 0
}

if [[ $SELFTEST -eq 1 ]]; then
  bad=0
  chk(){ local got; got=$(printf '%s\n' "${@:2}" | diff_class); [[ "$got" == "$1" ]] || { echo "tools/validate.sh:1: SELFTEST: expected $1 got $got for: ${*:2}"; bad=1; }; }
  chk docs docs/status/RESUME.md CHANGELOG.md .claude/agents/core-dev.md
  chk docs
  chk tools tools/land.py tools/land_selftest.sh
  chk tools-gate tools/land.py tools/validate.sh
  chk tools-gate tools/raw_new_check.py
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
  if [[ $bad -eq 0 ]]; then echo "selftest: 20 cases"; exec tools/validate_selftest.sh; else echo "FAIL (selftest)"; exit 1; fi
fi
# T-0236: --changed on a diff without code needs no build gate. Fail closed: any git failure means "code".
# T-0232: tracked files only (committed + staged + unstaged edits of tracked files); untracked files never count.
if [[ -n "$CHANGED_BASE" ]]; then
  if CH=$(git diff --no-renames --name-only "$CHANGED_BASE" -- 2>/dev/null); then
    # T-0269: a changed workflow script is parse-checked even when the diff is docs-only (the class stays docs)
    WFC=$(printf '%s\n' "$CH" | grep -E '^docs/conventions/workflows/[^/]*\.js$' || true)
    if [[ -n "$WFC" ]]; then
      check_workflows $WFC
      if [[ $WF_BAD -ne 0 ]]; then echo "FAIL ($WF_BAD findings)"; exit 1; fi
      [[ $WF_N -eq 0 ]] || echo "workflow parse: $WF_N file(s) OK"
    fi
    # T-0345: a diff of class code that also touches tools/ runs the touched tools' selftests too, then the full gate
    DCLS=$(printf '%s\n' "$CH" | diff_class); TF=$(printf '%s\n' "$CH" | grep '^tools/' || true)
    [[ $DCLS != code || -z "$TF" ]] || DCLS=code-tools
    case "$DCLS" in
      docs) echo "gate: skipped, docs-only diff"; echo PASS; exit 0 ;;
      tools|tools-gate|code-tools)
        TB=0
        if grep -qE '^tools/(validate(_selftest)?\.sh|lib/.*)$' <<<"$TF"; then tools/validate.sh --selftest >/dev/null 2>&1 || { echo "tools/validate.sh:1: SELFTEST: tools/validate.sh --selftest failed"; TB=1; }; fi
        if grep -qE '^tools/raw_new_check\.py$' <<<"$TF"; then python3 tools/raw_new_check.py --self-test || TB=1; fi
        if [[ $PUBLIC -eq 0 ]] && grep -qE '^tools/ci_selftest\.sh$' <<<"$TF"; then tools/ci_selftest.sh >/dev/null 2>&1 || { echo "tools/ci_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi
        if [[ $PUBLIC -eq 0 ]] && grep -qE '^tools/(export_public(_selftest)?\.sh|public-.*\.txt)$' <<<"$TF"; then tools/export_public_selftest.sh >/dev/null 2>&1 || { echo "tools/export_public_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi
        if [[ $PUBLIC -eq 0 ]] && grep -qE '^tools/(public_identity_selftest\.sh|public-identity\.conf|lib/public_identity\.sh|build_deb\.sh|vm/package_win\.sh|vm/win10/package_in_vm\.sh)$|^NOTICE$' <<<"$TF"; then tools/public_identity_selftest.sh >/dev/null 2>&1 || { echo "tools/public_identity_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi
        if [[ $PUBLIC -eq 0 ]] && grep -qE '^tools/add_spdx(_selftest)?\.sh$' <<<"$TF"; then tools/add_spdx_selftest.sh >/dev/null 2>&1 || { echo "tools/add_spdx_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi
        if grep -qE '^tools/(land(_selftest)?\.(py|sh)|lib/.*)$' <<<"$TF"; then if [[ $PUBLIC -eq 1 ]]; then pub_skip "land_selftest"; else tools/land_selftest.sh >/dev/null 2>&1 || { echo "tools/land_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi; fi
        if grep -qE '^tools/vm/' <<<"$TF" && [[ -d tools/vm ]]; then tools/vm/lock_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/lock_selftest.sh:1: SELFTEST: failed"; TB=1; }; tools/vm/derive_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/derive_selftest.sh:1: SELFTEST: failed"; TB=1; }; tools/vm/migrate_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/migrate_selftest.sh:1: SELFTEST: failed"; TB=1; }; tools/vm/win_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/win_selftest.sh:1: SELFTEST: failed"; TB=1; }; tools/vm/image_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/image_selftest.sh:1: SELFTEST: failed"; TB=1; }; tools/vm/bundle_check_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/bundle_check_selftest.sh:1: SELFTEST: failed"; TB=1; }; tools/vm/watch_selftest.sh >/dev/null 2>&1 || { echo "tools/vm/watch_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi  # T-0377: bundle_check
        if grep -qE '^tools/agents/' <<<"$TF" && [[ -d tools/agents ]]; then tools/agents/watch_selftest.sh >/dev/null 2>&1 || { echo "tools/agents/watch_selftest.sh:1: SELFTEST: failed"; TB=1; }; fi  # T-0390
        if [[ $TB -ne 0 ]]; then echo "FAIL (tool selftests)"; exit 1; fi
        if [[ $DCLS == tools ]]; then echo "gate: tools-only diff, selftests run"; echo PASS; exit 0; fi
        if [[ $DCLS == code-tools ]]; then echo "gate: code diff touching tools/, tool selftests passed, full gate follows"; else echo "gate: tools diff, selftests passed, plain run follows"; fi ;;
    esac
  fi
fi
# T-0345/T-0349: every CG_MODULES token must be a module directory under src/cg (a comma list used to match nothing,
# run 0 tests and print PASS; tests/fixtures and other tests/ directories are not modules)
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
# T-0370: in the private checkout every fixture exists: a test that finds one missing FAILS (tests/corpus_skip.h reads this
# variable) instead of skipping with "not present (public tree)", so a missing fixture can never become a silent skip.
# The log scan below catches the same text if a test prints it anyway. In an exported tree the tests skip.
if [[ $PUBLIC -eq 0 ]]; then export CG_REQUIRE_FIXTURES=1; else unset CG_REQUIRE_FIXTURES; fi
F=$(mktemp); trap 'rm -f "$F"' EXIT
fail(){ echo "$*" | tee -a "$F"; }
note(){ echo "note: $*"; }
T0=$SECONDS; stage(){ note "time: $1 $((SECONDS - T0))s"; T0=$SECONDS; }  # T-0090: per-stage wall time

# Parallel clang-tidy (T-0090): was one serial process (5-7 minutes). Candidate units on stdin; $1 = compile database
# dir, $2 = changed_units.py kind (nocmake|ninja), $3 = build dir holding the dependency information.
# Output: the same "file:line:col: message: check" findings as before, sorted and de-duplicated.
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
  note "tsan mode: format/hygiene/tidy/parity skipped, ThreadSanitizer build+test only"
  note "reviewer caveat (T-0002A): this is what makes log_test.cpp's MinimumLevelIsAtomicUnderConcurrentAccess a real regression net"
  # Suppressions (tools/tsan_suppressions.txt); exported here so they reach both the ctest child
  # processes below and tools/nocmake_build.sh's scoped fallback.
  export TSAN_OPTIONS="suppressions=$(pwd)/tools/tsan_suppressions.txt"
elif [[ $ASAN -eq 1 ]]; then
  note "asan mode: format/hygiene/tidy/parity skipped, AddressSanitizer+UBSan+LeakSanitizer build+test only"
  # T-0101: every sanitizer report must fail the run. ASan aborts on the first error by default (halt_on_error=1 stated
  # explicitly); UBSan only prints and continues unless halted at run time; LSan reports at exit with status 23.
  # Exported so the ctest child processes and tools/nocmake_build.sh's scoped fallback inherit them.
  # Set, never appended to: an inherited detect_leaks=0 / exitcode=0 would silently weaken the run (T-0101 review).
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
  # RAW-NEW (T-0090: tools/raw_new_check.py, same rule as the former grep, ignores text inside comments only; fails closed)
  python3 tools/raw_new_check.py --self-test || fail "tools/raw_new_check.py:1: RAW-NEW: checker self-test failed (see stderr above)"
  if rn=$(python3 tools/raw_new_check.py $SRC_FILES); then
    while IFS=: read -r f n; do [[ -n "$f" ]] && fail "$f:$n: RAW-NEW: use make_shared/make_unique (Qt types with parent are exempt)"; done <<<"$rn"
  else fail "tools/raw_new_check.py:1: RAW-NEW: the check itself failed (python3 missing or crash) -- treated as a finding"; fi
  for f in $SRC_FILES; do
    grep -nP 'TODO(?!\(T-\d{4}\))' "$f" | while IFS=: read -r n _; do fail "$f:$n: TODO-WITHOUT-TICKET: use TODO(T-NNNN)"; done
    if [[ "$f" != src/cg/core/platform/* ]]; then
      grep -nE '^\s*#\s*if(def|ndef)?\s.*(Q_OS_|_WIN32|__linux__|__APPLE__)' "$f" | while IFS=: read -r n _; do fail "$f:$n: PLATFORM-IFDEF: only allowed under src/cg/core/platform/"; done
    fi
    if [[ "$f" == src/cg/* && "$f" != src/cg/ui/* && "$f" != src/cg/app/* ]]; then
      grep -nE '#include\s*<Q(Widget|Dialog|MainWindow|Application|PushButton|Label|Menu|Action|MessageBox|FileDialog|tWidgets)' "$f" | while IFS=: read -r n _; do fail "$f:$n: WIDGETS-IN-CORE: Qt Widgets allowed only in src/cg/ui and src/cg/app"; done
    fi
    if [[ "$f" == src/*.h ]]; then
      grep -q 'Ports:' "$f" || fail "$f:1: MISSING-PORTS-COMMENT: header must state 'Ports: <legacy unit>' (or 'Ports: nothing')"
    fi
    # T-0010A / ADR-0004: the tile transport is libcurl only; QNAM must never appear in the
    # downloader (proxy_resolver.cpp is the one net file allowed to touch Qt Network).
    if [[ "$f" == src/cg/net/* && "$f" != src/cg/net/proxy_resolver.cpp ]]; then
      grep -nE 'QNetworkAccessManager|QNetworkReply|QNetworkRequest' "$f" \
        | while IFS=: read -r n _; do
            fail "$f:$n: QNAM-IN-DOWNLOADER: ADR-0004 — the tile transport is libcurl only"
          done
    fi
  done

  # T-0322: SPDX licence header on every source (tools/add_spdx.sh --check; unscoped runs use the script's own scope,
  # scoped runs check their files)
  if [[ -n "${CG_MODULES:-}" ]]; then spdx=$(tools/add_spdx.sh --check $SRC_FILES 2>&1); else spdx=$(tools/add_spdx.sh --check 2>&1); fi
  if [[ -n "$spdx" ]]; then while IFS= read -r l; do fail "$l"; done <<<"$spdx"; fi

  # 2b. T-0020: PROJ / geodesic confinement (docs/specs/geo.md §12.3, ADR-0006 decisions 3 & 7).
  for f in $(echo "$SRC_FILES" | grep '^src/' || true); do
    if [[ "$f" != src/cg/geo/proj/proj_converter.cpp ]] && grep -q '#include *<proj\.h>' "$f"; then
      fail "$f:1: PROJ-LEAK: <proj.h> only in src/cg/geo/proj/proj_converter.cpp"
    fi
    if [[ "$f" != src/cg/geo/datum.cpp ]] && grep -q '#include *"geodesic\.h"' "$f"; then
      fail "$f:1: GEODESIC-LEAK: geodesic.h only in src/cg/geo/datum.cpp"
    fi
    # T-0022A: docs/specs/storage.md §2.15, ADR-0005 decision 2 -- <sqlite3.h> only in SqliteDb.
    if [[ "$f" != "src/cg/storage/sqlite/sqlite_db.h" && "$f" != "src/cg/storage/sqlite/sqlite_db.cpp" ]] \
       && grep -q '#include *<sqlite3\.h>' "$f"; then
      fail "$f:1: SQLITE3-LEAK: <sqlite3.h> only in src/cg/storage/sqlite/sqlite_db.{h,cpp}"
    fi
    # T-0024: docs/specs/storage.md §2.19, ADR-0005 decision 4 -- <archive.h>/<archive_entry.h>
    # only in ArchiveTileStorage's own translation unit (the header stays libarchive-free, using
    # only an opaque `struct archive;` forward declaration).
    # T-0061 (ADR-0012): and ArchiveReaderZip's, the zip reader of core::ConfigProviderZip.
    if [[ "$f" != "src/cg/storage/archive_tile_storage.cpp" && "$f" != "src/cg/core/archive_reader_zip.cpp" ]] \
       && grep -qE '#include *<archive(_entry)?\.h>' "$f"; then
      fail "$f:1: ARCHIVE-LEAK: <archive.h>/<archive_entry.h> only in src/cg/storage/archive_tile_storage.cpp and src/cg/core/archive_reader_zip.cpp"
    fi
    # T-0225 (ADR-0014 decision 1): Qt Qml headers only in the three translation units of the JavaScript builder.
    if [[ "$f" != "src/cg/maptype/tile_download_request_builder_js.cpp" \
          && "$f" != "src/cg/maptype/tile_download_request_builder_factory_js.cpp" \
          && "$f" != "src/cg/maptype/js_script_watchdog.cpp" ]] \
       && grep -qE '#include *<(QJS|QtQml|QQml)' "$f"; then
      fail "$f:1: QML-LEAK: <QJS...>/<QtQml...>/<QQml...> only in src/cg/maptype/tile_download_request_builder_js.cpp, tile_download_request_builder_factory_js.cpp and js_script_watchdog.cpp"
    fi
  done
fi

# 2c. T-0269: the workflow scripts parse (cheap; every run that gets past the --changed skip, sanitizer modes included)
if [[ $PUBLIC -eq 1 ]]; then pub_skip "workflow js check"; else
WFO=$(mktemp); check_workflows docs/conventions/workflows/*.js >"$WFO"
while IFS= read -r l; do
  case "$l" in
    *WORKFLOW-PARSE:*) fail "$l" ;;
    "") ;;
    *) echo "$l" ;;
  esac
done <"$WFO"
rm -f "$WFO"
[[ $WF_N -eq 0 || $WF_BAD -ne 0 ]] || echo "workflow parse: $WF_N file(s) OK"
# T-0320: the exporter's selftest (private mode only; plain and scoped runs, not the sanitizer variants)
if is_windows_host; then note "windows host: ci/export/spdx selftests skipped (Linux release tooling; T-0345)"
elif [[ $TSAN -eq 0 && $ASAN -eq 0 ]]; then
  tools/ci_selftest.sh >build-ci-selftest.log 2>&1 || { grep -E ': SELFTEST: ' build-ci-selftest.log | head -10 | while read -r l; do fail "$l"; done; fail "tools/ci_selftest.sh:1: SELFTEST: failed (see build-ci-selftest.log)"; }  # T-0324
  tools/export_public_selftest.sh >build-export-selftest.log 2>&1 || { grep -E ': SELFTEST: ' build-export-selftest.log | head -10 | while read -r l; do fail "$l"; done; fail "tools/export_public_selftest.sh:1: SELFTEST: failed (see build-export-selftest.log)"; }
  # T-0370: the public identity (placeholder refusal, stamping)
  tools/public_identity_selftest.sh >build-identity-selftest.log 2>&1 || { grep -E ': SELFTEST: ' build-identity-selftest.log | head -10 | while read -r l; do fail "$l"; done; fail "tools/public_identity_selftest.sh:1: SELFTEST: failed (see build-identity-selftest.log)"; }
  # T-0322: the SPDX header script's selftest (private mode only, like the exporter's)
  tools/add_spdx_selftest.sh >build-spdx-selftest.log 2>&1 || { grep -E ': SELFTEST: ' build-spdx-selftest.log | head -10 | while read -r l; do fail "$l"; done; fail "tools/add_spdx_selftest.sh:1: SELFTEST: failed (see build-spdx-selftest.log)"; }
fi
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
  if cmake --preset "$PRESET" >build-configure.log 2>&1; then
    cmake --build --preset "$PRESET" -- -j "$CG_JOBS" -l "$CG_LOAD_LIMIT" 2>&1 | grep -vE '^\[[0-9]+/[0-9]+\] ' | grep -E '(error|warning)[: ]' | head -50 | while read -r l; do fail "$l"; done
    stage "cmake build"
    if ! ctest --preset "$PRESET" -j "$CG_JOBS" --test-load "$CG_LOAD_LIMIT" >build-test.log 2>&1; then
      grep -E '\*\*\*Failed|Not Run' build-test.log | while read -r l; do fail "tests: $l"; done
      grep -E 'ThreadSanitizer' build-test.log | head -20 | while read -r l; do fail "tsan: $l"; done
      grep -E 'ERROR: (Address|Leak)Sanitizer|runtime error:' build-test.log | sed -E 's/^==[0-9]+==//' | awk '{k=$0; gsub(/[0-9]+/,"N",k); if(!seen[k]++) print}' | head -20 | while read -r l; do fail "asan: $l"; done
      fail "tests:0: TESTS: ctest --preset $PRESET failed (see build-test.log)"
    else note "$(grep -E 'tests passed' build-test.log)"; fi
    stage "ctest"
    if [[ $TSAN -eq 0 && $ASAN -eq 0 && $FAST -eq 0 ]] && command -v clang-tidy >/dev/null && [[ -f build/debug/compile_commands.json ]]; then
      CPP=$(echo "$SRC_FILES" | grep '\.cpp$' | grep '^src/' || true)
      mkdir -p build/debug/tidy && sed 's/ -mno-direct-extern-access//g' build/debug/compile_commands.json > build/debug/tidy/compile_commands.json  # GCC-only flag from Qt's CMake config
      printf '%s\n' "$CPP" | tidy_run build/debug/tidy ninja build/debug | while read -r l; do fail "$l"; done
    fi
  else fail "CMakeLists.txt:1: CONFIGURE: cmake --preset $PRESET failed (see build-configure.log)"; tail -15 build-configure.log; fi
else
  note "scoped or cmake-less run — using tools/nocmake_build.sh fallback build"
  TSAN=$TSAN ASAN=$ASAN tools/nocmake_build.sh 2>&1 | tee build-nocmake.log | grep -E ': (fatal )?error: |\[  FAILED  \]|ThreadSanitizer|ERROR: (Address|Leak)Sanitizer|runtime error:|^nocmake: .*(failed|FAILED)|undefined reference' | sed -E 's/==[0-9]+==//' | awk '{k=$0; gsub(/[0-9]+/,"N",k); if ($0 ~ /_test\.cpp: \[  FAILED  \] [^ ]+$/) { if (!ex[$0]++) print } else if(!seen[k]++) print}' | head -40 | while read -r l; do fail "$l"; done
  grep -q '^nocmake: OK' build-nocmake.log || fail "tools/nocmake_build.sh:1: BUILD: fallback build/test failed (see build-nocmake.log)"
  stage "nocmake build+link+tests"
  NCDIR=build/nocmake${CG_MODULES:+-$(echo $CG_MODULES | tr " " "_")}
  if [[ $TSAN -eq 0 && $ASAN -eq 0 && $FAST -eq 0 ]] && command -v clang-tidy >/dev/null && [[ -f $NCDIR/compile_commands.json ]]; then
    CPP=$(echo "$SRC_FILES" | grep '\.cpp$' | grep '^src/' || true)
    printf '%s\n' "$CPP" | tidy_run "$NCDIR" nocmake "$NCDIR" | while read -r l; do fail "$l"; done
  fi
  stage "clang-tidy"
  # T-0016 ruling (gate gap): -O0 hides some diagnostics (e.g. maybe-uninitialized); run a
  # compile-only -O2 pass over the same scoped sources, incremental via nocmake_build.sh's own
  # staleness check, to surface them. Skipped with --fast alongside tidy.
  if [[ $TSAN -eq 0 && $ASAN -eq 0 && $FAST -eq 0 ]]; then
    OPTCHECK=1 CG_MODULES="${CG_MODULES:-}" tools/nocmake_build.sh 2>&1 | tee build-nocmake-opt.log | grep -E ': (fatal )?error: |^nocmake-opt: .*FAILED' | head -40 | while read -r l; do fail "$l"; done
    grep -q '^nocmake-opt: OK' build-nocmake-opt.log || fail "tools/nocmake_build.sh:1: OPT-BUILD: -O2 compile check failed (see build-nocmake-opt.log)"
  fi
fi

stage "clang-tidy (cmake path) or -O2 pass (nocmake path)"
if [[ $PUBLIC -eq 1 && $TSAN -eq 0 && $ASAN -eq 0 ]]; then pub_skip "parity check"; fi
if [[ $PUBLIC -eq 0 && $TSAN -eq 0 && $ASAN -eq 0 ]]; then
  # 4. Parity consistency. Only DUP-FEAT/UNDEFINED-FEAT/SCOPE-MISSING lines are findings; the "parity: ..." and
  # "scope: ..." lines are status-report metrics, not failures, so they go through note() and must not flip PASS to FAIL.
  # T-0175: the regenerated matrix goes to build/parity-matrix.generated.md and is only COMPARED (date line ignored) with
  # the committed docs/parity/parity-matrix.md, so a gate run no longer dirties the tree. tools/land.sh sets
  # CG_MATRIX_INPLACE=1: the landing wants the tree matrix regenerated (it reports the moved FEATs and may commit it).
  # What is checked is unchanged: the matrix is generated by the same code; a stale committed matrix is a note, as before.
  if [[ "${CG_MATRIX_INPLACE:-0}" == "1" ]]; then MATRIX_OUT=docs/parity/parity-matrix.md; else MATRIX_OUT=build/parity-matrix.generated.md; fi
  mkdir -p build
  while IFS= read -r l; do
    case "$l" in
      *DUP-FEAT:*|*UNDEFINED-FEAT:*|*SCOPE-MISSING:*) fail "$l" ;;
      *) note "$l" ;;
    esac
  done < <(python tools/parity_check.py --quiet --matrix-out "$MATRIX_OUT" 2>&1)
  if [[ "$MATRIX_OUT" != docs/parity/parity-matrix.md ]]; then
    if diff <(tail -n +2 docs/parity/parity-matrix.md) <(tail -n +2 "$MATRIX_OUT") >/dev/null 2>&1; then
      note "parity matrix: committed docs/parity/parity-matrix.md is current"
    else
      note "parity matrix: committed docs/parity/parity-matrix.md differs from the regenerated one (build/parity-matrix.generated.md); land.sh regenerates it"
    fi
  fi
fi

if [[ $PUBLIC -eq 0 ]]; then
  _lg=build-test.log; [[ -n "${CG_MODULES:-}" ]] && _lg=build-nocmake.log  # the log of THIS run's test path
  [[ -f $_lg ]] && grep -q 'not present (public tree)' "$_lg" && fail "tests:0: MISSING-FIXTURE: a test reports 'not present (public tree)' in the private checkout (see $_lg); the fixture must exist here"
fi
if [[ -s "$F" ]]; then echo "FAIL ($(wc -l <"$F") findings)"; exit 1; else echo "PASS"; fi
