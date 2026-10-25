# shellcheck shell=bash
# T-0370: sourced by tools/export_public.sh, tools/build_deb.sh and tools/vm/win10/package_in_vm.sh.
# cg_identity_load [conf]          sets PUBLIC_REPO_URL (the CLI repository), PLUGIN_REPO_URL and MAINTAINER from tools/public-identity.conf (or [conf])
# cg_identity_is_placeholder       0 when PUBLIC_REPO_URL, PLUGIN_REPO_URL or MAINTAINER still holds a placeholder (an angle-bracket URL, LOGIN)
# cg_identity_require_real WHAT    exit 2 with a message when a placeholder is set and CG_ALLOW_PLACEHOLDER is not 1
# cg_stamp_text VERSION            stdin -> stdout: <public repository URL>, v<version>, OWNER/chart-grabber-cli, OWNER/chartgrabber_pi, NOREPLY_ADDRESS
cg_identity_load() {
  local conf=${1:-${CG_IDENTITY_CONF:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/public-identity.conf}} l
  [[ -f $conf ]] || { echo "public identity: $conf missing" >&2; return 2; }
  PUBLIC_REPO_URL=""; PLUGIN_REPO_URL=""; MAINTAINER=""
  while IFS= read -r l; do
    l=${l%$'\r'}
    case $l in
      PUBLIC_REPO_URL=\"*\") PUBLIC_REPO_URL=${l#PUBLIC_REPO_URL=\"}; PUBLIC_REPO_URL=${PUBLIC_REPO_URL%\"} ;;
      PLUGIN_REPO_URL=\"*\") PLUGIN_REPO_URL=${l#PLUGIN_REPO_URL=\"}; PLUGIN_REPO_URL=${PLUGIN_REPO_URL%\"} ;;
      MAINTAINER=\"*\") MAINTAINER=${l#MAINTAINER=\"}; MAINTAINER=${MAINTAINER%\"} ;;
    esac
  done <"$conf"
  [[ -n $PUBLIC_REPO_URL && -n $PLUGIN_REPO_URL && -n $MAINTAINER ]] || { echo "public identity: $conf lacks PUBLIC_REPO_URL, PLUGIN_REPO_URL or MAINTAINER" >&2; return 2; }
}
cg_identity_is_placeholder() {
  [[ $PUBLIC_REPO_URL == *\<* || $PUBLIC_REPO_URL != https://* || $PLUGIN_REPO_URL == *\<* || $PLUGIN_REPO_URL != https://* || $MAINTAINER == *LOGIN* ]]
}
cg_identity_require_real() {
  cg_identity_is_placeholder || return 0
  [[ ${CG_ALLOW_PLACEHOLDER:-} == 1 ]] && return 0
  echo "$1: refusing: tools/public-identity.conf still holds a placeholder (PUBLIC_REPO_URL='$PUBLIC_REPO_URL', PLUGIN_REPO_URL='$PLUGIN_REPO_URL', MAINTAINER='$MAINTAINER'); set the real values, or CG_ALLOW_PLACEHOLDER=1 for a local test build" >&2
  exit 2
}
cg_stamp_text() {
  local ver=$1 addr=${MAINTAINER##*<}
  addr=${addr%>}
  # the version is always filled in; the URL and the address only when they are real (a placeholder must stay visible)
  if cg_identity_is_placeholder; then
    sed -e "s|tag v<version>|tag v$ver|"
    return
  fi
  sed -e "s|<public repository URL> tag v<version>|$PUBLIC_REPO_URL tag v$ver|" \
      -e "s|https://github.com/OWNER/chart-grabber-cli|${PUBLIC_REPO_URL%.git}|g" \
      -e "s|https://github.com/OWNER/chartgrabber_pi|${PLUGIN_REPO_URL%.git}|g" \
      -e "s|NOREPLY_ADDRESS|$addr|g" \
      -e 's| *<!-- TODO(owner): the GitHub noreply address -->||' \
      -e 's| *# TODO(owner): the repository URL$||' \
      -e 's| *<!-- TODO(owner): the plugin repository URL -->||'
}
