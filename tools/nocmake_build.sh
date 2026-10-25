#!/usr/bin/env bash
# Fallback build used by validate.sh while cmake is not installed (T-0001). Not the real build system.
# Compiles src/cg/**/*.cpp (+ third_party, warnings off) into build/nocmake/libcg.a, runs moc on Q_OBJECT
# headers, builds every tests/**/*_test.cpp into its own binary and runs it. Writes compile_commands.json for clang-tidy.
set -uo pipefail
cd "$(dirname "$0")/.."
source tools/gate_env.sh
cd "$CG_ROOT"
# T-0251: CG_MODULES = the modules whose TESTS run and whose sources are checked (TMODS). Their dependency modules
# (tools/gate_env.sh cg_module_deps, from the CMake target links) are only built into the library, never tested.
# MODS = tests + dependencies: the set of src/ trees that is compiled. Listing a dependency explicitly runs its tests.
TMODS="${CG_MODULES:-}"; MODS=$(echo $TMODS $([[ -n "$TMODS" ]] && cg_module_deps "$TMODS")); TSAN="${TSAN:-0}"; ASAN="${ASAN:-0}"
# T-0180: CG_BUILD_ONLY=1 compiles and links exactly what a normal run would, prints the path of every test binary and
# runs no test (a mutation run: build, then `timeout 120 <binary> --gtest_filter=...`). CG_TEST_TIMEOUT (seconds, default
# 600) bounds every test process of a normal run; a test that exceeds it fails the run with a message naming the limit.
BUILD_ONLY="${CG_BUILD_ONLY:-0}"; TLIM="${CG_TEST_TIMEOUT:-600}"
SUF="${TMODS:+-$(echo $TMODS | tr " " "_")}"; [[ "$TSAN" == "1" ]] && SUF="${SUF}-tsan"; [[ "$ASAN" == "1" ]] && SUF="${SUF}-asan"
# T-0258: the filters, the CG_TEST_TARGET resolution and the CG_PRINT_SETS exit come before the build dir:
# a print run creates no directory.
filt(){ if [[ -n "$MODS" ]]; then grep -E "^(src/cg|tests)/($(echo $MODS | tr ' ' '|'))/" || true; else cat; fi; }  # src trees built: tests + dependencies
tfilt(){ if [[ -n "$TMODS" ]]; then grep -E "^(src/cg|tests)/($(echo $TMODS | tr ' ' '|'))/" || true; else cat; fi; }  # T-0251: tests run, sources checked
# T-0256: CG_TEST_TARGET=<tests/**/*_test.cpp path, or its basename without .cpp> compiles, links and (unless CG_BUILD_ONLY=1)
# runs only that one test file (a mutation run relinks one binary, not the module's whole test set). The module libraries
# are built as without it; the helper .cpp files of tests/ are still compiled. tools/validate.sh never passes it on.
TT_FILE=""
if [[ -n "${CG_TEST_TARGET:-}" ]]; then
  tt="${CG_TEST_TARGET#./}"; tt="${tt%.cpp}"  # T-0258: ./tests/x_test.cpp matches the find output
  TT_FILE=$(find tests -name '*_test.cpp' | sort | tfilt | awk -v t="$tt" '{ b=$0; sub(/.*\//, "", b); sub(/\.cpp$/, "", b); p=$0; sub(/\.cpp$/, "", p); if (p == t || b == t) print }')
  case "$(printf '%s' "$TT_FILE" | grep -c .)" in
    1) ;;
    0) echo "nocmake: CG_TEST_TARGET '$CG_TEST_TARGET' not found (no tests/**/*_test.cpp of the scope matches)"; exit 1 ;;
    *) echo "nocmake: CG_TEST_TARGET '$CG_TEST_TARGET' is ambiguous, give the path:"; echo "$TT_FILE"; exit 1 ;;
  esac
fi
tsel(){ awk -v t="$TT_FILE" 't == "" || $0 !~ /_test\.cpp$/ || $0 == t'; }  # keeps the target test (all tests when none is set) and every non-test file
# T-0251/T-0256: CG_PRINT_SETS=1 prints the resolved sets (and the one test file of CG_TEST_TARGET) and stops.
[[ "${CG_PRINT_SETS:-0}" == "1" ]] && { echo "nocmake: tests=[${TMODS}] built=[${MODS}]"; [[ -z "$TT_FILE" ]] || echo "nocmake: test-target=[$TT_FILE] test-files=[$(find tests -name '*_test.cpp' | sort | tfilt | tsel | tr '\n' ' ' | sed 's/ $//')]"; exit 0; }
OUT=build/nocmake${SUF}; mkdir -p "$OUT/obj" "$OUT/bin" "$OUT/moc"
CXX=${CXX:-g++}; J=${J:-$CG_JOBS}; CXXL="${CG_CCACHE:+$CG_CCACHE }$CXX"  # compile through ccache (tools/gate_env.sh); link stays plain
QT=$(pkg-config --cflags Qt6Core Qt6Gui Qt6Network Qt6Qml | sed 's/-I/-isystem /g')
WARN="-Wall -Wextra -Wpedantic -Werror -Wshadow -Wnon-virtual-dtor -Wold-style-cast"
# T-0025 (tsan): -fsanitize=thread on both compile and link when TSAN=1 (used by validate.sh --tsan).
SANFLAGS=""; [[ "$TSAN" == "1" ]] && SANFLAGS="-fsanitize=thread -fno-omit-frame-pointer"
# T-0101 (asan): AddressSanitizer + UBSan + LSan, same flags as the cmake `asan` preset (CMakeLists.txt CG_ENABLE_ASAN).
[[ "$ASAN" == "1" ]] && SANFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
BASE="-std=c++23 -fPIC -g -O0 -ffile-prefix-map=$CG_ROOT=. -Isrc -isystem third_party $QT $SANFLAGS"
# T-0155: libjpeg / libpng / libwebp are listed for the codec tickets T-0160..T-0162; nothing links them yet.
LIBS="$(pkg-config --libs Qt6Core Qt6Gui Qt6Network Qt6Qml) -lgtest -lgtest_main -pthread $SANFLAGS $(pkg-config --libs zlib libbrotlidec libbrotlienc libzstd 2>/dev/null) $(pkg-config --libs libjpeg libpng16 libwebp libwebpmux 2>/dev/null) $(pkg-config --libs libcurl 2>/dev/null || echo -lcurl) $(pkg-config --libs sqlite3 2>/dev/null || echo -lsqlite3) $(pkg-config --libs libarchive 2>/dev/null || echo -larchive)"  # T-0012A: cg::net content_decoder (+enc for its test fixtures); T-0010B: curl_session.cpp real libcurl calls; T-0022A: cg::storage::sqlite SqliteDb (system sqlite3, ADR-0005 decision 2); T-0024: cg::storage::ArchiveTileStorage (system libarchive, ADR-0005 decision 4)
# T-0333: no Berkeley DB probe any more (the backend was removed, owner 2026-10-21; tag archive/with-berkeleydb).
# T-0010B: docs/specs/net.md §7.0 -- no test may reach the real network; CurlDownloader reads this
# once (qEnvironmentVariableIsSet) and refuses any non-loopback host when it is set. The real
# CMake build sets it per-test via ctest's ENVIRONMENT property (tests/net/CMakeLists.txt); this
# fallback build has no such per-binary mechanism, so it is exported for every test binary here.
export CG_NET_NO_EXTERNAL_NETWORK=1
# T-0020 (geo): PROJ >= 9 is optional, mirroring CMake's find_package(PROJ 9 QUIET). Global (not
# scoped to geo) like the T-0012A libs above -- CG_HAVE_PROJ is only read by proj/proj_converter.cpp
# and proj_converter_test.cpp, so the extra -I/-D/-l are inert everywhere else.
if pkg-config --exists 'proj >= 9' 2>/dev/null; then
  BASE="$BASE -DCG_HAVE_PROJ=1 $(pkg-config --cflags proj)"
  LIBS="$LIBS $(pkg-config --libs proj)"
fi
# T-0328: versionString() / version_test read the CMake PROJECT_VERSION (the one version source).
CG_PROJECT_VERSION=$(sed -n 's/^project(.* VERSION \([0-9][0-9.]*\)).*/\1/p' CMakeLists.txt | head -1)
[[ -n "$CG_PROJECT_VERSION" ]] || { echo "nocmake_build: no PROJECT_VERSION in CMakeLists.txt" >&2; exit 2; }
BASE="$BASE -DCG_PROJECT_VERSION_RAW=$CG_PROJECT_VERSION" # a pp-number: no shell quoting to survive
fail=0
# T-0225: the Qt tool path differs by distribution (Arch /usr/lib/qt6/moc, Debian and Ubuntu /usr/lib/qt6/libexec/moc)
qt_tool(){
  local t c
  for c in "$(pkg-config --variable=libexecdir Qt6Core 2>/dev/null)/$1" "/usr/lib/qt6/$1" "/usr/lib/qt6/libexec/$1" "$(command -v "$1-qt6" 2>/dev/null)" "$(command -v "$1" 2>/dev/null)"; do
    [[ -x "$c" && -f "$c" ]] && { echo "$c"; return; }
  done
  echo "nocmake_build: no Qt tool $1 found" >&2; echo "$1"
}
MOC=$(qt_tool moc); RCC=$(qt_tool rcc)
# moc
for h in $(grep -rl 'Q_OBJECT' src 2>/dev/null | filt); do
  m="$OUT/moc/moc_$(echo "$h" | tr '/' '_' | sed 's/\.h$/.cpp/')"; [[ "$m" -nt "$h" ]] || "$MOC" "$h" -o "$m" || fail=1
done
# T-0002B (core): rcc for Qt resource .qrc files (e.g. tests/core/test_resources.qrc). Output lands
# in $OUT/moc/ so the existing "find $OUT/moc -name '*.cpp'" glob below picks it up as a compile
# unit, and it is linked into every test binary of the owning module like any other library object.
for q in $(find tests -name '*.qrc' 2>/dev/null | sort | tfilt); do
  qname=$(basename "$q" .qrc)
  qcpp="$PWD/$OUT/moc/qrc_${qname}.cpp"
  [[ "$qcpp" -nt "$q" ]] || (cd "$(dirname "$q")" && "$RCC" --name "$qname" "$(basename "$q")" -o "$qcpp") || fail=1
done
# compile list: path|flags
> "$OUT/units.txt"
filt(){ if [[ -n "$MODS" ]]; then grep -E "^(src/cg|tests)/($(echo $MODS | tr ' ' '|'))/" || true; else cat; fi; }
for s in $(find src -name '*.cpp' | sort | filt) $(find "$OUT/moc" -name '*.cpp' 2>/dev/null); do echo "$s|$WARN" >> "$OUT/units.txt"; done
for s in $(find third_party -name '*.cpp' -o -name '*.cc' -o -name '*.c' 2>/dev/null | sort); do echo "$s|-w" >> "$OUT/units.txt"; done  # T-0016: vendored geodesic.c (g++ compiles .c as C by extension, same as cmake's add_library)
for s in $(find tests -name '*.cpp' | sort | tfilt | tsel); do echo "$s|$WARN" >> "$OUT/units.txt"; done
obj(){ echo "$OUT/obj/$(echo "$1" | tr '/' '_').o"; }
export -f obj; export OUT CXX BASE CXXL
{ echo '['; while IFS='|' read -r s f; do printf '{"directory":"%s","file":"%s","command":"%s %s %s -c %s"},\n' "$PWD" "$PWD/$s" "$CXX" "$BASE" "$f" "$s"; done < "$OUT/units.txt" | sed '$ s/,$//'; echo ']'; } > "$OUT/compile_commands.json"
is_stale(){
  # T-0002B rework #2: fixed inverted early return. Semantics now match the name: return 0 (true)
  # when the object IS stale and must be rebuilt, return 1 (false) when it is genuinely fresh.
  # Stale if: the object is missing, OR the source is newer than the object, OR the -MMD dep file
  # is missing, OR any dependency listed in it (headers pulled in transitively) is newer than the
  # object or has since been deleted. This is what catches a header-only edit (no change to the
  # .cpp itself) so it still triggers a rebuild -- otherwise a stale .o silently links against
  # whatever signature the last build saw (e.g. the ContentTypeRegistry::addByType/addByExt
  # signature-mismatch link error with no source change in sight).
  o="$1"; s="$2"; d="${o%.o}.d"
  [[ -f "$o" ]] || return 0
  [[ "$s" -nt "$o" ]] && return 0
  [[ -f "$d" ]] || return 0
  # -MP emits a phony "header.h:" line per dependency (no other tokens) so a deleted header
  # doesn't break `make`; strip that trailing ':' or every header reads as a nonexistent path
  # and is_stale reports stale unconditionally.
  deps=$(sed 's/\\$//' "$d" | tr -s ' \t' '\n' | sed '1d;/^$/d;s/:$//' | sort -u)
  for dep in $deps; do
    [[ -f "$dep" ]] || return 0
    [[ "$dep" -nt "$o" ]] && return 0
  done
  return 1
}
export -f is_stale
# T-0016 ruling (gate gap): the normal build above is -O0, which hid a real maybe-uninitialized
# diagnostic that only appears with optimization on. OPTCHECK=1 runs a separate, compile-only pass
# of just the scoped modules' own sources (no third_party, no moc/rcc glue) at -O2 with the same
# full warning set + -Werror, into its own object dir, incremental via the same is_stale(). No
# linking, no test run -- it exists purely to surface -O0-hidden diagnostics.
if [[ "${OPTCHECK:-0}" == "1" ]]; then
  OOUT="build/nocmake-opt${SUF}"; mkdir -p "$OOUT/obj"
  OBASE="-std=c++23 -fPIC -O2 -DCG_PROJECT_VERSION_RAW=$CG_PROJECT_VERSION -ffile-prefix-map=$CG_ROOT=. -Isrc -isystem third_party $QT"
  oobj(){ echo "$OOUT/obj/$(echo "$1" | tr '/' '_').o"; }
  export -f oobj; export OOUT OBASE CXXL
  > "$OOUT/units.txt"
  for s in $(find src -name '*.cpp' | sort | tfilt) $(find tests -name '*.cpp' | sort | tfilt | tsel); do echo "$s|$WARN" >> "$OOUT/units.txt"; done
  optcompile(){ s="${1%%|*}"; f="${1#*|}"; o=$(oobj "$s"); is_stale "$o" "$s" || exit 0; cg_wait_load; $CXXL $OBASE $f -MMD -MP -c "$s" -o "$o" 2>&1 | sed "s#^#$s: #" | head -20; exit "${PIPESTATUS[0]}"; }
  export -f optcompile
  ofail=0
  xargs -P "$J" -I{} bash -c 'optcompile "$@"' _ {} < "$OOUT/units.txt" || ofail=1
  [[ $ofail -eq 0 ]] && echo "nocmake-opt: OK" || { echo "nocmake-opt: COMPILE FAILED"; exit 1; }
  exit 0
fi
compile(){ s="${1%%|*}"; f="${1#*|}"; o=$(obj "$s"); is_stale "$o" "$s" || exit 0; cg_wait_load; $CXXL $BASE $f -MMD -MP -c "$s" -o "$o" 2>&1 | sed "s#^#$s: #" | head -20; exit "${PIPESTATUS[0]}"; }
export -f compile
xargs -P "$J" -I{} bash -c 'compile "$@"' _ {} < "$OUT/units.txt" || fail=1
[[ $fail -eq 0 ]] || { echo "nocmake: compile failed"; exit 1; }
LIBOBJS=$(grep -vE '^tests/' "$OUT/units.txt" | cut -d'|' -f1 | while read -r s; do obj "$s"; done)
# T-0010B: a shared test fixture that is not itself a *_test.cpp binary (e.g.
# tests/net/http_test_server.cpp) is compiled above into units.txt but otherwise never linked
# anywhere -- link every such helper into every test binary of ITS OWN MODULE ONLY (finding 10,
# T-0010B rework: this used to link every module's fixture .cpp into every test binary, harmless
# only until two modules ship a fixture with a colliding symbol), like a tiny per-module fixture
# library, so a fixture .cpp can live next to the header-only ones already here.
# T-0090: link + run every test binary in parallel (was a serial for-loop: ~100 links and ~60 s of tests in a
# row). Each worker writes its own log; the logs are replayed in sorted order below so the output is deterministic.
# Binaries and logs are keyed on the full path (tests/maptype/map_version_test.cpp and tests/storage/... share a basename).
# Passing binaries print only their "[  PASSED  ]" line; a failing one prints its whole output (first 80 lines)
# and a "nocmake: TEST FAILED <file>" marker -- validate.sh keys on the markers, never on the file name.
mkdir -p "$OUT/log"; rm -f "$OUT"/log/*
export LIBS LIBOBJS BUILD_ONLY TLIM
# T-0175: which test CASES failed. failed_cases <output> <cases that exited non-zero, one per line, may be empty> prints
# "[  FAILED  ] Suite.Case" once per case: the gtest lines of the output (the first 80 lines printed below may cut them
# off) plus the cases whose process failed without a gtest line (a sanitizer report, a crash, a filter that ran nothing).
failed_cases(){
  { printf '%s\n' "$1" | sed -n -E 's/^\[  FAILED  \] ([^ ,]+\.[^ ,]+)([ ,].*)?$/\1/p'; printf '%s' "$2" | grep -v '^$' || true; } | awk '!s[$0]++' | sed 's/^/[  FAILED  ] /'
}
# zero_ran <output>: gtest ran no test at all (a --gtest_filter that matched nothing exits 0).
zero_ran(){ printf '%s\n' "$1" | grep -qE '^\[==========\] 0 tests from'; }
# T-0180: tmo_note <exit code> <output> prints the timeout message (first, so the 80-line cut keeps it) and the output when the process was killed by `timeout`.
tmo_note(){ [[ "$1" -ne 124 ]] || printf 'nocmake: TEST TIMEOUT: killed after %s s (limit CG_TEST_TIMEOUT=%s)\n' "$TLIM" "$TLIM"; printf '%s' "$2"; }
export -f failed_cases zero_ran tmo_note
runtest(){
  t="$1"; n=$(echo "${t%.cpp}" | tr "/" "_"); b="$OUT/bin/$n"; lg="$OUT/log/$n"; moddir=$(dirname "$t")
  cg_wait_load
  helperobjs=$(grep -E "^${moddir}/" "$OUT/units.txt" | cut -d'|' -f1 | grep -v '_test\.cpp$' | while read -r s; do obj "$s"; done)
  if [[ ! "$b" -nt "$(obj "$t")" ]] || [[ -n "$(find $LIBOBJS $helperobjs -newer "$b" 2>/dev/null | head -1)" ]]; then
    if ! lo=$($CXX "$(obj "$t")" $LIBOBJS $helperobjs -o "$b" $LIBS 2>&1); then
      { printf '%s\n' "$lo" | head -30 | sed "s#^#$t: #"; echo "nocmake: link failed for $t"; } > "$lg"; echo 1 > "$lg.rc"; return
    fi
  fi
  if [[ "$BUILD_ONLY" == "1" ]]; then echo "$t: built $b" > "$lg"; echo 0 > "$lg.rc"; return; fi  # T-0180
  if [[ "$TSAN" == "1" ]]; then
    # T-0149: the cmake path (gtest_discover_tests) runs every test CASE in its own fresh process. A race in test code
    # ordered only by std::latch is found far more often that way than in one process holding all the cases (measured:
    # 20/20 per case vs 18/20 whole on an idle machine, ~50 % whole under gate load). Do the same here, with the same
    # TSAN_OPTIONS and the same failure criterion (any non-zero exit, any report). DISABLED_ cases are skipped, as ctest does.
    out=""; rc=0; npass=0; badc=""
    while read -r c; do
      o=$(timeout -k 5 "$TLIM" "$b" --gtest_brief=1 --gtest_filter="$c" 2>&1); r=$?; o=$(tmo_note "$r" "$o")
      # T-0175: a per-case filter that matches nothing runs 0 tests and exits 0 -- that is a failure, not a pass.
      if [[ $r -eq 0 ]] && ! zero_ran "$o"; then npass=$((npass + 1)); else [[ $r -ne 0 ]] || { r=1; o+=$'\n'"nocmake: filter \"$c\" ran 0 tests"; }; rc=$r; badc+="$c"$'\n'; out+="$o"$'\n'; fi
    done < <("$b" --gtest_list_tests | awk '/^[^ ]/ {s=$1} /^  / {print s $1}' | grep -v 'DISABLED_')
    [[ $rc -ne 0 || $npass -gt 0 ]] || rc=1  # a binary that lists no case is a failure, not a pass
    [[ $rc -eq 0 ]] && out="[  PASSED  ] $npass tests (one process each)."
  else
    out=$(timeout -k 5 "$TLIM" "$b" --gtest_brief=1 2>&1); rc=$?; badc=""; out=$(tmo_note "$rc" "$out")
  fi
  if [[ $rc -eq 0 ]]; then printf '%s\n' "$out" | grep -E '^\[  PASSED  \]' | sed "s#^#$t: #" > "$lg"
  else { failed_cases "$out" "$badc" | sed "s#^#$t: #"; printf '%s\n' "$out" | head -80 | sed "s#^#$t: #"; echo "nocmake: TEST FAILED $t (exit $rc)"; } > "$lg"; fi
  echo $rc > "$lg.rc"
}
export -f runtest
export TSAN
find tests -name '*_test.cpp' | sort | tfilt | tsel | xargs -P "$J" -I{} bash -c 'runtest "$@"' _ {}
# T-0149: a race whose threads are ordered only by std::latch is reported by TSan only when the waiting thread really
# blocks before the signal (a wait that returns at once is an instrumented acquire load and orders the accesses; the
# blocking wait is inside the uninstrumented libstdc++.so). Whether the waiter blocks depends on the CPU share of the
# machine at that moment, so a run can miss the race for a few seconds in a row (measured: 3 reps within 40 ms all
# passed, 3 of 6 gate runs missed). After the parallel pass, every case of a test file that starts threads and passed
# is therefore run again, in fresh processes, CG_TSAN_RUNS-1 more rounds (default 3 in all), with a pause before each
# round and fewer processes at a time. Stricter than the full gate (one run per case), never weaker.
retest(){
  t="$1"; n=$(echo "${t%.cpp}" | tr "/" "_"); b="$OUT/bin/$n"; lg="$OUT/log/$n"
  [[ "$(cat "$lg.rc" 2>/dev/null)" == "0" ]] || return 0
  grep -qE 'std::(latch|barrier|counting_semaphore|binary_semaphore|promise|future|shared_future|async)|QSemaphore|QThread|QFuture|QtConcurrent' "$t" || return 0
  while read -r c; do
    o=$(timeout -k 5 "$TLIM" "$b" --gtest_brief=1 --gtest_filter="$c" 2>&1); r=$?; o=$(tmo_note "$r" "$o")
    if [[ $r -eq 0 ]] && zero_ran "$o"; then r=1; o+=$'\n'"nocmake: filter \"$c\" ran 0 tests"; fi
    if [[ $r -ne 0 ]]; then
      { failed_cases "$o" "$c" | sed "s#^#$t: #"; printf '%s\n' "$o" | head -80 | sed "s#^#$t: #"; echo "nocmake: TEST FAILED $t (exit $r)"; } > "$lg"; echo $r > "$lg.rc"; return 0
    fi
  done < <("$b" --gtest_list_tests | awk '/^[^ ]/ {s=$1} /^  / {print s $1}' | grep -v 'DISABLED_')
}
export -f retest
if [[ "$TSAN" == "1" && "$BUILD_ONLY" != "1" ]]; then
  for ((rd = 1; rd < ${CG_TSAN_RUNS:-3}; rd++)); do
    sleep 1
    find tests -name '*_test.cpp' | sort | tfilt | tsel | xargs -P "$((J / 3 + 1))" -I{} bash -c 'retest "$@"' _ {}
  done
fi
for t in $(find tests -name '*_test.cpp' | sort | tfilt | tsel); do
  lg="$OUT/log/$(echo "${t%.cpp}" | tr "/" "_")"
  cat "$lg" 2>/dev/null
  [[ "$(cat "$lg.rc" 2>/dev/null || echo 1)" == "0" ]] || { fail=1; [[ -f "$lg.rc" ]] || echo "nocmake: TEST FAILED $t (no result)"; }
done
[[ $fail -eq 0 && "$BUILD_ONLY" == "1" ]] && { echo "nocmake: BUILD OK (CG_BUILD_ONLY, no test run)"; exit 0; }
[[ $fail -eq 0 ]] && echo "nocmake: OK" || { echo "nocmake: TESTS FAILED"; exit 1; }
