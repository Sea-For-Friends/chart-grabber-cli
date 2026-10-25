#!/usr/bin/env bash
# Non-interactive smoke run of docs/test-plans/stage0-acceptance.md (cases AC-01..AC-08, AC-15,
# AC-17, AC-19, AC-21, AC-24). Writes only into RESULT_DIR (must not exist) and the cache directory.
# Session files (.sls) are in AutoSave/ of the data directory (spec 1.6), not here. Never uses --overwrite, never needs root, deletes nothing. Needs real network for AC-04..AC-08.
# Usage: run_smoke.sh --maps DIR --bbox W,S,E,N --sat ID --chart ID --overlay ID [options]
#   --bin PATH       chart-grabber-cli binary (default: chart-grabber-cli on PATH; refuses to start if missing)
#   --zooms LIST     0-based zooms (default 10-14)     --route FILE.gpx --corridor-nm N  (AC-07)
#   --nonmercator ID a source with EPSG other than 3857 (AC-15, e.g. "Yandex - Map")
#   --cache DIR      cache root (default RESULT_DIR/cache)   --out RESULT_DIR (default ./smoke-DATE)
set -u
BIN=chart-grabber-cli; ZOOMS=10-14; OUT="./smoke-$(date +%Y%m%d-%H%M%S)"
MAPS=; BBOX=; SAT=; CHART=; OVL=; ROUTE=; CNM=2; NONM=; CACHE=
while [ $# -gt 0 ]; do
  case $1 in --bin) BIN=$2;; --maps) MAPS=$2;; --bbox) BBOX=$2;; --sat) SAT=$2;; --chart) CHART=$2;;
    --overlay) OVL=$2;; --zooms) ZOOMS=$2;; --route) ROUTE=$2;; --corridor-nm) CNM=$2;;
    --nonmercator) NONM=$2;; --cache) CACHE=$2;; --out) OUT=$2;; *) echo "unknown option $1" >&2; exit 2;;
  esac; shift 2 || { echo "option needs a value" >&2; exit 2; }
done
command -v "$BIN" >/dev/null 2>&1 || [ -x "$BIN" ] || { echo "chart-grabber-cli not found (PATH or --bin)" >&2; exit 2; }
[ -d "$MAPS" ] && [ -n "$BBOX" ] && [ -n "$SAT" ] && [ -n "$CHART" ] && [ -n "$OVL" ] || {
  echo "need --maps DIR (existing), --bbox, --sat, --chart, --overlay (see header)" >&2; exit 2; }
[ -e "$OUT" ] && { echo "$OUT exists; refusing to reuse it" >&2; exit 2; }
mkdir -p "$OUT/files" || exit 2; CACHE=${CACHE:-$OUT/cache}
COM=(--maps "$MAPS" --cache "$CACHE" --no-progress)
printf 'id\texpected\tgot\tseconds\tcommand\n' >"$OUT/summary.tsv"; FAIL=0
t() { # t ID EXPECTED_EXIT args... : runs one case, logs stdout/stderr/exit code
  local id=$1 want=$2 s=$SECONDS; shift 2
  local c=("${COM[@]}"); [ "$id" = AC-01 ] && c=()
  "$BIN" "$@" "${c[@]}" >"$OUT/$id.out" 2>"$OUT/$id.err" </dev/null; local rc=$?
  printf '%s\t%s\t%s\t%s\t%s %s\n' "$id" "$want" "$rc" "$((SECONDS - s))" "$BIN" "$*" >>"$OUT/summary.tsv"
  [ "$rc" = "$want" ] || { FAIL=$((FAIL + 1)); echo "DIFF $id: expected $want, got $rc"; }
}
F=$OUT/files
t AC-01 0 --version
t AC-02 0 sources
t AC-03 0 estimate --bbox "$BBOX" --zooms "$ZOOMS" --source "$SAT"
t AC-04 0 run --bbox "$BBOX" --zooms "$ZOOMS" --source "$SAT" --out "$F/sat.mbtiles"
t AC-05 0 run --bbox "$BBOX" --zooms "$ZOOMS" --source "$CHART" --out "$F/chart.mbtiles"
t AC-06 0 run --bbox "$BBOX" --zooms "$ZOOMS" --overlay "$OVL" --out "$F/seamarks.mbtiles"
t AC-08a 3 run --bbox "$BBOX" --zooms "$ZOOMS" --source "$SAT" --out "$F/sat.mbtiles" # existing file refused
t AC-08b 0 download --bbox "$BBOX" --zooms "$ZOOMS" --source "$SAT" -v # cache only: look for 0 requests
if [ -n "$ROUTE" ]; then
  t AC-07 0 run --route "$ROUTE" --corridor-nm "$CNM" --zooms "$ZOOMS" --source "$SAT" --out "$F/route.mbtiles"
fi
[ -n "$NONM" ] && t AC-15 3 estimate --bbox "$BBOX" --zooms "$ZOOMS" --source "$NONM"
t AC-17 3 estimate --bbox "$BBOX" --zooms 22 --source "$SAT"
t AC-19 3 estimate --bbox 179,10,-179,12 --zooms "$ZOOMS" --source "$SAT"
t AC-24 0 download --bbox "$BBOX" --zooms "$ZOOMS" --source "$SAT" --strict
# AC-21: dead proxy in a fresh cache, must stop with exit 4 after the stall timeout (about 30 s)
COM=(--maps "$MAPS" --cache "$OUT/cache-dead" --no-progress)
t AC-21 4 download --bbox "$BBOX" --zooms 10 --source "$SAT" --proxy http://127.0.0.1:9 --stall-timeout 30
{ date -Is; "$BIN" --version 2>&1; uname -a; command -v "$BIN"; } >"$OUT/environment.txt"
find "$OUT/files" "$CACHE" -type f -exec ls -l {} + >"$OUT/files.txt" 2>&1
echo "done: $OUT/summary.tsv ($FAIL case(s) differ from expectation); attach the whole directory to a report"
[ "$FAIL" = 0 ]
