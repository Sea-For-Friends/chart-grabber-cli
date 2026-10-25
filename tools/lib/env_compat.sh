# shellcheck shell=bash
# REMOVE-AFTER: next release. T-0313: the tool variables were renamed SAS_* -> CG_*. This file is sourced FIRST by
# tools/gate_env.sh and tools/vm/common.sh. For every name in env_compat.names: when SAS_<X> is set and CG_<X> is not,
# CG_<X> is exported with the value of SAS_<X>. If both are set CG_* wins, silently. When anything was mapped, ONE warning
# line goes to stderr per process tree (CG_ENV_COMPAT_WARNED=1 is exported, children and python tools stay quiet).
# The python tools do the same through tools/lib/env_compat.py (same names file).
_cg_ec_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
_cg_ec_first=""; _cg_ec_n=0
while IFS= read -r _cg_ec_x; do
  [[ -z $_cg_ec_x || $_cg_ec_x == \#* ]] && continue
  _cg_ec_old=SAS_$_cg_ec_x; _cg_ec_new=CG_$_cg_ec_x
  if [[ -n ${!_cg_ec_old:-} && -z ${!_cg_ec_new:-} ]]; then
    export "$_cg_ec_new=${!_cg_ec_old}"
    [[ -z $_cg_ec_first ]] && _cg_ec_first=$_cg_ec_x
    _cg_ec_n=$((_cg_ec_n + 1))
  fi
done <"$_cg_ec_dir/env_compat.names"
if ((_cg_ec_n > 0)) && [[ -z ${CG_ENV_COMPAT_WARNED:-} ]]; then
  _cg_ec_more=""; ((_cg_ec_n > 1)) && _cg_ec_more=" (and $((_cg_ec_n - 1)) more)"
  echo "warning: SAS_${_cg_ec_first}${_cg_ec_more} is deprecated, use CG_${_cg_ec_first}; the SAS_* names are removed in the next release" >&2
  export CG_ENV_COMPAT_WARNED=1
fi
unset _cg_ec_dir _cg_ec_first _cg_ec_n _cg_ec_x _cg_ec_old _cg_ec_new _cg_ec_more
