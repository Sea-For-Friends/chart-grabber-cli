#!/usr/bin/env bash
# Sourced by tools/validate.sh and tools/nocmake_build.sh (T-0090). Single place for the build-cache and
# load settings, so the nocmake fallback and the cmake presets behave the same.
#   CG_NO_CACHE=1  control configuration: compilation cache off, no precompiled headers, build dirs wiped by
#                   validate.sh (`tools/validate.sh --no-cache`). Slow, trusted.
# Exports: CG_ROOT (physical worktree root), CG_CCACHE (launcher word or empty), CG_PCH (1/0).
# T-0124: gate priority. Everything the gate starts (compile, link, tidy, tests, ccache) inherits the priority
# of the script process, so it is lowered once here, before any child exists. Switch (first match wins):
#   CG_GATE_PRIORITY=medium|low|normal    environment, one run
#   ${XDG_CONFIG_HOME:-$HOME/.config}/chart-grabber-core/gate-priority   file with `medium`, `low` or `normal`, read at every gate start
#   built-in default: medium (T-0230, owner ruling 2026-10-15; was low). Unknown value -> medium plus one warning.
#   medium: SCHED_BATCH (inherited), nice 10 (ananicy-cpp resets it to -4 within a second, measured with ps; the policy
#           stays), ionice best-effort 7. low: nice 19, ionice idle, SCHED_IDLE (see below). normal: nothing changes.
source "$(dirname "${BASH_SOURCE[0]}")/lib/env_compat.sh"   # T-0313: SAS_* -> CG_* (REMOVE-AFTER: next release)
if [[ -z "${CG_GATE_PRIORITY_DONE:-}" ]]; then
  _cg_prio="${CG_GATE_PRIORITY:-}"
  if [[ -z "$_cg_prio" ]]; then
    _cg_pf="${XDG_CONFIG_HOME:-$HOME/.config}/chart-grabber-core/gate-priority"
    [[ -f "$_cg_pf" && -r "$_cg_pf" ]] && _cg_prio=$(tr -d '[:space:]' < "$_cg_pf" 2>/dev/null)
    # REMOVE-AFTER: next release. T-0313: the file used to live in ~/.config/sas-planet-qt/; read it when the new one is absent.
    _cg_old="${XDG_CONFIG_HOME:-$HOME/.config}/sas-planet-qt/gate-priority"  # rename-keep
    [[ -z "$_cg_prio" && ! -e "$_cg_pf" && -f "$_cg_old" && -r "$_cg_old" ]] && _cg_prio=$(tr -d '[:space:]' < "$_cg_old" 2>/dev/null)
  fi
  case "$_cg_prio" in
    ""|medium) _cg_prio=medium ;;
    low|normal) ;;
    *) echo "gate priority: warning: unknown value '$_cg_prio' (use medium|low|normal), using medium"; _cg_prio=medium ;;
  esac
  if [[ "$_cg_prio" == "medium" ]]; then
    _cg_nice="nice 10"; renice -n 10 -p $$ >/dev/null 2>&1 || _cg_nice="nice failed"
    _cg_io="ionice best-effort 7"; ionice -c 2 -n 7 -p $$ >/dev/null 2>&1 || _cg_io="ionice failed"
    _cg_pol=""; chrt --batch -p 0 $$ >/dev/null 2>&1 && _cg_pol=", SCHED_BATCH"
    echo "gate priority: medium ($_cg_nice, $_cg_io$_cg_pol)"
  elif [[ "$_cg_prio" == "low" ]]; then
    _cg_io="ionice idle"; _cg_nice="nice 19"
    renice -n 19 -p $$ >/dev/null 2>&1 || { _cg_prio="low (renice failed)"; _cg_nice="nice failed"; }
    if ! ionice -c 3 -p $$ >/dev/null 2>&1; then
      if ionice -c 2 -n 7 -p $$ >/dev/null 2>&1; then _cg_io="ionice best-effort 7, idle refused"; else _cg_io="ionice failed"; fi
    fi
    # ananicy-cpp (CachyOS) re-nices every bash/zsh process to -4 within a second ("Doc-View" rule), and everything a
    # bash worker forks afterwards inherits that; nice 19 alone is therefore lost. The scheduling policy is not
    # touched by that rule, so SCHED_IDLE (inherited, unlike nice) is what keeps the gate below the desktop.
    _cg_pol=""; chrt --idle -p 0 $$ >/dev/null 2>&1 && _cg_pol=", SCHED_IDLE"
    echo "gate priority: $_cg_prio ($_cg_nice, $_cg_io$_cg_pol)"
  else
    echo "gate priority: normal"
  fi
  export CG_GATE_PRIORITY_DONE=1
  unset _cg_old _cg_prio _cg_pf _cg_io _cg_pol _cg_nice
fi
CG_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
CG_CCACHE=""; CG_PCH=1
if [[ "${CG_NO_CACHE:-0}" == "1" ]]; then
  export CCACHE_DISABLE=1          # presets / CMakeLists also see this and add no launcher
  CG_PCH=0
else
  # Two worktrees at different paths must hit the same entries:
  #  CCACHE_BASEDIR   absolute paths below the worktree root are hashed as relative ones
  #  CCACHE_NOHASHDIR the compile cwd is not hashed (debug-info comp_dir is made path independent by
  #                   -ffile-prefix-map=<root>=. , added to every compile in CMakeLists.txt / nocmake_build.sh)
  #  CCACHE_MAXSIZE   the user-level default (5 GiB) is smaller than one day of sanitizer + debug objects of
  #                   several worktrees and thrashes (observed: 400+ cleanups, 24 % hit rate); raised for our
  #                   invocations only (nothing is written to ~/.config/ccache or the cache dir config)
  unset CCACHE_SLOPPINESS          # an inherited sloppiness (time_macros, include_file_mtime, ...) would make hits less exact
  export CCACHE_BASEDIR="$CG_ROOT" CCACHE_NOHASHDIR=1 CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-20Gi}"
  command -v ccache >/dev/null 2>&1 && CG_CCACHE=ccache
fi
[[ "${CG_NO_PCH:-0}" == "1" ]] && CG_PCH=0
export CG_ROOT CG_CCACHE CG_PCH

# Load-aware parallelism (T-0090). Several gates share the machine; each one limits itself to the cores and
# stops starting new work while the 1-minute load average is above CG_LOAD_LIMIT (off by default; the
# average lags, and a gate's own 20 workers already put it near the core count, so a lower limit starves the gate),
# the same rule as `ninja -l` / `make -l` / `ctest --test-load`.
#   CG_JOBS        max parallel jobs of this gate (default: number of cores)
#   CG_LOAD_LIMIT  load average above which no new job is started. Default: off (1000000). Measured with four
#                   concurrent cold scoped gates: limit 1.5 x cores 221 s, no limit 179 s (old scripts 255 s), so
#                   the limit costs wall time and is opt-in, e.g. CG_LOAD_LIMIT=30. CG_JOBS (cores) still caps each gate.
# T-0097: tests log to stderr, never to the system journal. Without a terminal on stderr (ctest, nocmake capture)
# Qt routes messages to journald; libsystemd creates its socket lazily on the first send, and two free-running
# test threads logging concurrently race inside sd_journal_sendv (socket vs sendmsg, third-party; TSan-visible
# only when each test is its own process, i.e. under ctest). Forcing the console handler removes the race
# and keeps the journal clean. The shipped application is not affected by this setting.
export QT_LOGGING_TO_CONSOLE=1

CG_NPROC=$(nproc)
export CG_JOBS="${CG_JOBS:-$CG_NPROC}" CG_LOAD_LIMIT="${CG_LOAD_LIMIT:-1000000}"
# Waits (at most 10 s per job, so a permanently busy machine still makes progress) while the machine is overloaded.
cg_wait_load() {
  local i=0
  while ((i < 5)) && awk -v l="$CG_LOAD_LIMIT" '{ exit !($1 > l) }' /proc/loadavg; do
    sleep 2; i=$((i + 1))
  done
  return 0
}
export -f cg_wait_load

# T-0251: cg_module_deps "<modules>" prints the transitive closure of the modules the given ones link (one name per
# word, without the given ones), derived from the `target_link_libraries(cg_<m> ...)` blocks of
# src/cg/<m>/CMakeLists.txt (comments stripped; cg_warnings and the module itself ignored). No second list to maintain.
cg_module_deps() {
  local todo="$1" seen=" $1 " out="" m d
  while [[ -n "${todo// /}" ]]; do
    m=${todo%% *}; todo=${todo#"$m"}; todo=${todo# }
    [[ -n "$m" && -f "$CG_ROOT/src/cg/$m/CMakeLists.txt" ]] || continue
    for d in $(sed 's/#.*//' "$CG_ROOT/src/cg/$m/CMakeLists.txt" \
        | awk -v t="cg_$m" '/target_link_libraries[ \t]*\(/ {on = ($0 ~ "\\([ \t]*" t "([ \t)]|$)")} on {print} /\)/ {on=0}' \
        | grep -oE '\bcg_[a-z0-9_]+\b' | sort -u); do
      d=${d#cg_}
      [[ "$d" == warnings || "$d" == "$m" || "$seen" == *" $d "* ]] && continue
      seen+="$d "; out+="$d "; todo+=" $d"
    done
  done
  echo $out
}
