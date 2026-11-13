#!/bin/bash
# Build the chart-grabber-cli Debian package. Debian 12 or 13, Ubuntu 24.04 or 26.04 (Pi OS trixie too: ID=debian; Debian 12 package built in its own VM). Usage: tools/build_deb.sh [--commit <12 hex>] [BUILD_DIR]
# Build from `git archive vYYYY.MM.N` (the archive stamp names the commit); --commit overrides it.
# Cross build (optional, both variables together; unset = the native build above, unchanged): CG_DEB_TOOLCHAIN=<CMake toolchain file> and
# CG_DEB_ARCH=<Debian architecture of the package: armhf or arm64>. The package then gets that Architecture and its shlibdeps are taken
# against the target libraries (multiarch dir, cross objdump of the toolchain file); the binary is not run. CG_DEB_JOBS=<n> sets the build parallelism (default: all cores).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
# A release is not built with the placeholder identity of tools/public-identity.conf (CG_ALLOW_PLACEHOLDER=1 for a local test build). The env
# var counts as a real Maintainer; the repository URL (NOTICE, README) still has to be real.
. "$root/tools/lib/public_identity.sh"
cg_identity_load || exit 2
if [ -n "${CG_DEB_MAINTAINER:-}" ]; then MAINTAINER=$CG_DEB_MAINTAINER; fi
cg_identity_require_real build_deb.sh
os_id=""; os_ver=""
if [ -r /etc/os-release ]; then
  os_id=$(. /etc/os-release; echo "${ID:-}")
  os_ver=$(. /etc/os-release; echo "${VERSION_ID:-}")
fi
# Ubuntu 24.04 / 26.04 as well (ID=ubuntu).
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
tc=(); cp_env=(); cp_opt=(); cross=0
if [ -n "${CG_DEB_TOOLCHAIN:-}${CG_DEB_ARCH:-}" ]; then
  if [ -z "${CG_DEB_TOOLCHAIN:-}" ] || [ -z "${CG_DEB_ARCH:-}" ]; then echo "build_deb.sh: CG_DEB_TOOLCHAIN and CG_DEB_ARCH go together" >&2; exit 2; fi
  case "$CG_DEB_ARCH" in
    armhf) gnu=arm-linux-gnueabihf ;;
    arm64) gnu=aarch64-linux-gnu ;;
    *) echo "build_deb.sh: CG_DEB_ARCH='$CG_DEB_ARCH': want armhf or arm64" >&2; exit 2 ;;
  esac
  [ -r "$CG_DEB_TOOLCHAIN" ] || { echo "build_deb.sh: no toolchain file '$CG_DEB_TOOLCHAIN'" >&2; exit 2; }
  tc=(-DCMAKE_TOOLCHAIN_FILE="$CG_DEB_TOOLCHAIN")
  cp_env=(DEB_HOST_ARCH="$CG_DEB_ARCH" DEB_HOST_GNU_TYPE="$gnu")
  cp_opt=(-D CPACK_DEBIAN_PACKAGE_ARCHITECTURE="$CG_DEB_ARCH" -D CPACK_DEBIAN_CLI_PACKAGE_ARCHITECTURE="$CG_DEB_ARCH" -D CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS="/usr/lib/$gnu")
  cross=1
fi
# The Maintainer line is the env var CG_DEB_MAINTAINER ("Name <address>") when set, else the MAINTAINER of the conf. Always
# passed (never left to the CMake cache), so a reused build dir cannot keep a stale Maintainer.
cmake -S "$root" -B "$build" -G Ninja "${tc[@]}" -DCMAKE_BUILD_TYPE=Release -DCG_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr -DCHART_GRABBER_COMMIT="$commit" -DCG_DEB_MAINTAINER="$MAINTAINER"
cmake --build "$build" --target chart-grabber-cli -j"${CG_DEB_JOBS:-$(nproc)}"
(cd "$build" && env "${cp_env[@]}" cpack -G DEB "${cp_opt[@]}")
[ "$cross" = 1 ] || "$build/cli/chart-grabber-cli" --version
ls -1 "$build"/*.deb
