#!/usr/bin/env bash
# end-to-end selftest of the --changed skip modes of tools/validate.sh in a throwaway git repo (no build).
# docs-only diff -> skipped; tools-only diff -> selftests, no build; src diff -> leaves the skip path (full gate).
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
parent=${CG_SELFTEST_TMP:-/tmp/cg-validate-selftest}; mkdir -p -- "$parent" || exit 1
d=$(mktemp -d "$parent/run.XXXXXX") || exit 1; trap 'cd /; rm -rf -- "${d:?}"' EXIT
cd "$d" || exit 1
git init -q -b master . && git config user.email t@t && git config user.name t
mkdir tools docs src; mkdir -p src/cg/core   # CG_MODULES tokens must be directories
cp "$here/tools/validate.sh" "$here/tools/gate_env.sh" tools/; cp -r "$here/tools/lib" tools/lib
echo a > docs/a.md; echo a > tools/helper.py; echo a > src/a.cpp
git add -A && git commit -qm base && git checkout -qb work
bad=0
expect(){ # name, regex the output must match
  local out; out=$(CG_MODULES=core timeout 20 tools/validate.sh --changed 2>&1)
  grep -qE "$2" <<<"$out" || { echo "tools/validate_selftest.sh:1: SELFTEST: $1: output did not match /$2/: $(head -c 300 <<<"$out")"; bad=1; }
}
echo b >> docs/a.md; git commit -qam docs
expect "docs-only diff" '^gate: skipped, docs-only diff$'
echo x > stray.kdev4; mkdir -p scratch.kdev4; echo x > scratch.kdev4/f # Untracked files never count
expect "docs-only diff with untracked files" '^gate: skipped, docs-only diff$'
rm -rf stray.kdev4 scratch.kdev4
echo b >> tools/helper.py; git commit -qam tools
expect "tools-only diff" '^gate: tools-only diff, selftests run$'
echo b >> src/a.cpp # An unstaged edit of a tracked code file still counts as code
expect "unstaged code edit" 'scoped to modules'
git commit -qam src
out=$(CG_MODULES=core timeout 20 tools/validate.sh --changed 2>&1)
if grep -qE '^gate: (skipped|tools-only)' <<<"$out"; then echo "tools/validate_selftest.sh:1: SELFTEST: src diff was skipped"; bad=1; fi
grep -q 'scoped to modules' <<<"$out" || { echo "tools/validate_selftest.sh:1: SELFTEST: src diff did not reach the gate"; bad=1; }
# A CG_MODULES token that is not a module directory fails loudly (a comma list used to run 0 tests and PASS)
out=$(CG_MODULES=core,geo timeout 20 tools/validate.sh --changed 2>&1); rc=$?
{ [[ $rc -ne 0 ]] && grep -q "CG_MODULES: 'core,geo' is not a directory" <<<"$out" && grep -q '^FAIL' <<<"$out" && ! grep -q 'scoped to modules' <<<"$out"; } \
  || { echo "tools/validate_selftest.sh:1: SELFTEST: a bad CG_MODULES token was not refused: $(head -c 300 <<<"$out")"; bad=1; }
# A tests/ directory that is not a src/cg module (tests/fixtures) is refused too
mkdir -p tests/fixtures
out=$(CG_MODULES=fixtures timeout 20 tools/validate.sh --changed 2>&1); rc=$?
{ [[ $rc -ne 0 ]] && grep -q "CG_MODULES: 'fixtures' is not a directory" <<<"$out" && ! grep -q 'scoped to modules' <<<"$out"; } \
  || { echo "tools/validate_selftest.sh:1: SELFTEST: tests/fixtures was accepted as a CG_MODULES token: $(head -c 300 <<<"$out")"; bad=1; }
# A code diff that also touches the raw-new checker runs its selftest (a stub records the call), a failing one fails the gate
printf '%s\n' 'import os, sys' 'open(os.environ["CG_MARK"], "a").write("rawnew\n")' 'sys.exit(1 if os.environ.get("CG_STUB_FAIL") == "rawnew" else 0)' > tools/raw_new_check.py
git add tools/raw_new_check.py; git commit -qm rawnewstub
mk="$d/mark"; : > "$mk"
out=$(CG_MARK="$mk" CG_MODULES=core timeout 20 tools/validate.sh --changed 2>&1)
{ grep -q '^gate: code diff touching tools/' <<<"$out" && grep -q '^rawnew$' "$mk" && grep -q 'scoped to modules' <<<"$out"; } \
  || { echo "tools/validate_selftest.sh:1: SELFTEST: a code+tools diff did not run the raw-new selftest and go on to the gate: $(head -c 300 <<<"$out") marks=$(tr '\n' ' ' <"$mk")"; bad=1; }
: > "$mk"
out=$(CG_MARK="$mk" CG_STUB_FAIL=rawnew CG_MODULES=core timeout 20 tools/validate.sh --changed 2>&1)
{ grep -q '^FAIL (tool selftests)' <<<"$out" && ! grep -q 'scoped to modules' <<<"$out"; } \
  || { echo "tools/validate_selftest.sh:1: SELFTEST: a failing raw-new selftest did not fail a code+tools diff: $(head -c 300 <<<"$out")"; bad=1; }
# A diff that touches tools/build_deb.sh runs its selftest (a stub records the call), a failing one fails the gate
printf '%s\n' '#!/usr/bin/env bash' 'echo bdeb >>"$CG_MARK"' '[[ ${CG_STUB_FAIL:-} != bdeb ]]' > tools/build_deb_selftest.sh; chmod +x tools/build_deb_selftest.sh
echo x > tools/build_deb.sh; git add tools/build_deb.sh tools/build_deb_selftest.sh; git commit -qm bdebstub
: > "$mk"
out=$(CG_MARK="$mk" CG_MODULES=core timeout 20 tools/validate.sh --changed 2>&1)
grep -q '^bdeb$' "$mk" || { echo "tools/validate_selftest.sh:1: SELFTEST: a diff touching tools/build_deb.sh did not run its selftest: $(head -c 300 <<<"$out")"; bad=1; }
: > "$mk"
out=$(CG_MARK="$mk" CG_STUB_FAIL=bdeb CG_MODULES=core timeout 20 tools/validate.sh --changed 2>&1)
grep -q '^FAIL (tool selftests)' <<<"$out" || { echo "tools/validate_selftest.sh:1: SELFTEST: a failing build_deb selftest did not fail the gate: $(head -c 300 <<<"$out")"; bad=1; }
# The gate never sets CG_REQUIRE_FIXTURES (an inherited value reaches the tests) and writes its logs under build/logs/, none at the
# repository root (a stub build records the value it sees). Rows: inherited value | value the build must see
printf '%s\n' '#!/usr/bin/env bash' 'echo "req=[${CG_REQUIRE_FIXTURES:-}]" > "$CG_MARK"' 'echo "nocmake: OK"' > tools/nocmake_build.sh; chmod +x tools/nocmake_build.sh
while IFS='|' read -r inh want; do
  rm -rf build; : > "$mk"
  if [[ -n $inh ]]; then CG_REQUIRE_FIXTURES=$inh CG_MARK="$mk" CG_MODULES=core timeout 20 tools/validate.sh --fast >/dev/null 2>&1
  else env -u CG_REQUIRE_FIXTURES CG_MARK="$mk" CG_MODULES=core timeout 20 tools/validate.sh --fast >/dev/null 2>&1; fi
  [[ $(cat "$mk") == "req=[$want]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: inherited CG_REQUIRE_FIXTURES [$inh]: the build saw $(cat "$mk"), expected req=[$want]"; bad=1; }
  [[ -f build/logs/nocmake.log ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: no build/logs/nocmake.log after a gate run"; bad=1; }
  rl=$(ls -d build-*.log 2>/dev/null | head -3)
  [[ -z $rl ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: log files at the repository root: $rl"; bad=1; }
done <<ROWS
|
1|1
ROWS
# CG_MODULES names the modules whose tests run; their dependencies (from the CMake target links) are built only.
cd "$here" || exit 1
sets(){ CG_PRINT_SETS=1 CG_MODULES="$1" tools/nocmake_build.sh 2>&1 | grep '^nocmake: tests='; }
o=$(sets app); [[ "$o" == "nocmake: tests=[app] built=[app "* && "$o" == *" core"* && "$o" == *" storage"* ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: app scope did not resolve dependencies: $o"; bad=1; }
o=$(sets core); [[ "$o" == "nocmake: tests=[core] built=[core]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: core scope has no dependencies: $o"; bad=1; }
o=$(sets "geo core"); [[ "$o" == "nocmake: tests=[geo core] built=[geo core]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: explicit dependency listed twice: $o"; bad=1; }
o=$(sets geo); [[ "$o" == "nocmake: tests=[geo] built=[geo core]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: geo scope: $o"; bad=1; }
# CG_TEST_TARGET narrows the test files of a build to the one named (path or basename); an unknown one stops at once.
t1=$(ls tests/core/*_test.cpp | head -1); t2=$(ls tests/core/*_test.cpp | sed -n 2p)
o=$(CG_PRINT_SETS=1 CG_MODULES=core CG_TEST_TARGET="$t1" tools/nocmake_build.sh 2>&1)
[[ "$o" == *"nocmake: test-target=[$t1] test-files=[$t1]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: CG_TEST_TARGET path did not narrow the build to one test: $o"; bad=1; }
o=$(CG_PRINT_SETS=1 CG_MODULES=core CG_TEST_TARGET="$(basename "$t2" .cpp)" tools/nocmake_build.sh 2>&1)
[[ "$o" == *"nocmake: test-target=[$t2] test-files=[$t2]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: CG_TEST_TARGET basename did not narrow the build to one test: $o"; bad=1; }
o=$(CG_PRINT_SETS=1 CG_MODULES=core CG_TEST_TARGET=no_such_test tools/nocmake_build.sh 2>&1); rc=$?
[[ $rc -ne 0 && "$o" == *"nocmake: CG_TEST_TARGET 'no_such_test' not found"* ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: unknown CG_TEST_TARGET was not refused: $o"; bad=1; }
o=$(CG_PRINT_SETS=1 CG_MODULES=core CG_TEST_TARGET="./$t1" tools/nocmake_build.sh 2>&1)
[[ "$o" == *"nocmake: test-target=[$t1] test-files=[$t1]" ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: CG_TEST_TARGET with a leading ./ did not narrow like the plain path: $o"; bad=1; }
# An ambiguous basename is refused with both paths; a print run creates no build directory
rm -rf build/nocmake-maptype_storage
o=$(CG_PRINT_SETS=1 CG_MODULES="maptype storage" CG_TEST_TARGET=map_version_test tools/nocmake_build.sh 2>&1); rc=$?
[[ $rc -ne 0 && "$o" == *"is ambiguous"* && "$o" == *"tests/maptype/map_version_test.cpp"* && "$o" == *"tests/storage/map_version_test.cpp"* ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: ambiguous CG_TEST_TARGET was not refused with both paths: $o"; bad=1; }
[[ ! -e build/nocmake-maptype_storage ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: a CG_PRINT_SETS run created a build directory"; bad=1; }
o=$(CG_PRINT_SETS=1 CG_MODULES=core tools/nocmake_build.sh 2>&1)
[[ "$o" != *test-target* ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: test-target printed without CG_TEST_TARGET: $o"; bad=1; }
# The tools derive the repository root from git or their own directory: no absolute repo directory constant (the move of the directory must not break them; the export tools name it deliberately).
h=$(git -C "$here" grep -nE 'Downloads/(sas-yacht|chart-grabber-core)' -- tools ':!tools/rename_env_vm.sh' ':!tools/rename_sas_to_cg.sh' ':!tools/export_public.sh' ':!tools/export_public_selftest.sh' | head -3)
[[ -z $h ]] || { echo "tools/validate_selftest.sh:1: SELFTEST: tools name the repository directory as a constant: $h"; bad=1; }
grep -q 'CG_REQUIRE_FIXTURES' "$here/tests/corpus_skip.h" || { echo "tools/validate_selftest.sh:1: SELFTEST: tests/corpus_skip.h does not read CG_REQUIRE_FIXTURES"; bad=1; }
[[ $bad -eq 0 ]] && { echo "validate_selftest: 25 cases"; echo PASS; exit 0; }
echo "FAIL (validate selftest)"; exit 1
