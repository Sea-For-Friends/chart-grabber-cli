#!/bin/bash
# T-0204: build the chart-grabber-cli Debian package. Debian 12 or 13, Ubuntu 24.04 or 26.04 (Pi OS trixie too: ID=debian; T-0296: Debian 12 package built in its own VM). Usage: tools/build_deb.sh [--commit <12 hex>] [BUILD_DIR]
# T-0264: build from `git archive vYYYY.MM.N` (the archive stamp names the commit); --commit overrides it.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
# T-0370: a release is not built with the placeholder identity of tools/public-identity.conf (CG_ALLOW_PLACEHOLDER=1 for a local test build).
# The env var counts as a real Maintainer; the repository URL (NOTICE, README) still has to be real.
. "$root/tools/lib/public_identity.sh"
cg_identity_load || exit 2
if [ -n "${CG_DEB_MAINTAINER:-}" ]; then MAINTAINER=$CG_DEB_MAINTAINER; fi
cg_identity_require_real build_deb.sh
os_id=""; os_ver=""
if [ -r /etc/os-release ]; then
  os_id=$(. /etc/os-release; echo "${ID:-}")
  os_ver=$(. /etc/os-release; echo "${VERSION_ID:-}")
fi
# T-0326: Ubuntu 24.04 / 26.04 as well (ID=ubuntu).
ok=0
if [ "$os_id" = "debian" ] && { [ "$os_ver" = "13" ] || [ "$os_ver" = "12" ]; }; then ok=1; fi
if [ "$os_id" = "ubuntu" ] && { [ "$os_ver" = "24.04" ] || [ "$os_ver" = "26.04" ]; }; then ok=1; fi
if [ "$ok" != 1 ]; then
  echo "build_deb.sh: refusing to run: needs Debian 12 or 13 or Ubuntu 24.04 or 26.04 (ID=debian VERSION_ID=12|13, ID=ubuntu VERSION_ID=24.04|26.04), found ID='$os_id' VERSION_ID='$os_ver'" >&2
  exit 2
fi
commit=""
if [ "${1:-}" = "--commit" ]; then commit=${2:?--commit needs 12 hex digits}; shift 2; fi
build=${1:-$root/build-deb}
# T-0324: the Maintainer line is the env var CG_DEB_MAINTAINER ("Name <address>") when set, else the MAINTAINER of the conf.
# T-0370: always passed (never left to the CMake cache), so a reused build dir cannot keep a stale Maintainer.
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCG_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr -DCHART_GRABBER_COMMIT="$commit" -DCG_DEB_MAINTAINER="$MAINTAINER"
cmake --build "$build" --target chart-grabber-cli -j"$(nproc)"
(cd "$build" && cpack -G DEB)
"$build/cli/chart-grabber-cli" --version
ls -1 "$build"/*.deb
