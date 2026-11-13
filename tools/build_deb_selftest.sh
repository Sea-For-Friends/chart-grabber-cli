#!/usr/bin/env bash
# tools/build_deb_selftest.sh: proves what tools/build_deb.sh hands to cmake and cpack, without a build: a copy of the script runs in a throwaway
# tree with a fake cmake, cpack and /etc/os-release. One row per case: name ; environment ; exit ; pattern that must appear in the recorded calls
# (- = none) ; pattern that must NOT appear (- = none). The native rows pin the unchanged native build.
# Prints one line per failed row as "tools/build_deb_selftest.sh:1: SELFTEST: ..." and PASS/FAIL.
set -uo pipefail
here=$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
T=$(mktemp -d) || exit 2
trap 'rm -rf -- "$T"' EXIT
bad=0
fail() { echo "tools/build_deb_selftest.sh:1: SELFTEST: $*"; bad=1; }
mkdir -p "$T/tree/tools/lib" "$T/bin"
cp "$here/lib/public_identity.sh" "$T/tree/tools/lib/"; cp "$here/public-identity.conf" "$T/tree/tools/"
sed "s#/etc/os-release#$T/os-release#g" "$here/build_deb.sh" >"$T/tree/tools/build_deb.sh"; chmod +x "$T/tree/tools/build_deb.sh"
printf 'ID=debian\nVERSION_ID=13\n' >"$T/os-release"
printf 'set(CMAKE_SYSTEM_NAME Linux)\n' >"$T/tc.cmake"
cat >"$T/bin/cmake" <<'X'
#!/usr/bin/env bash
echo "cmake $*" >>"$T/calls"
[ "$1" = --build ] && { mkdir -p "$2/cli"; printf '#!/bin/sh\necho "chart-grabber-cli --version" >>"$T/calls"\n' >"$2/cli/chart-grabber-cli"; chmod +x "$2/cli/chart-grabber-cli"; }
exit 0
X
cat >"$T/bin/cpack" <<'X'
#!/usr/bin/env bash
echo "cpack $* [DEB_HOST_ARCH=${DEB_HOST_ARCH:-} DEB_HOST_GNU_TYPE=${DEB_HOST_GNU_TYPE:-}]" >>"$T/calls"
mkdir -p "$PWD" && : >"$PWD/x_1_${DEB_HOST_ARCH:-amd64}.deb"
X
printf '#!/usr/bin/env bash\necho "nproc"; echo 6\n' >"$T/bin/nproc"; sed -i '2s/.*/echo 6/' "$T/bin/nproc"
chmod +x "$T/bin/"*; export T
tc=$T/tc.cmake
while IFS=';' read -r name envs want must not; do
  [[ -n $name ]] || continue
  rm -f "$T/calls"; rm -rf "$T/b"; mkdir "$T/b"; mkdir -p "$T/b"
  rc=0; env -u CG_DEB_TOOLCHAIN -u CG_DEB_ARCH -u CG_DEB_JOBS ${envs//@TC@/$tc} PATH=$T/bin:$PATH "$T/tree/tools/build_deb.sh" "$T/b" >"$T/out" 2>&1 || rc=$?
  [[ $rc == "$want" ]] || { fail "$name: exit $rc, want $want: $(tail -2 "$T/out" | tr '\n' ' ')"; continue; }
  [[ $must == - ]] || grep -qE -- "$must" "$T/calls" "$T/out" 2>/dev/null || fail "$name: no /$must/ in the calls: $(tr '\n' ' ' <"$T/calls" 2>/dev/null | cut -c1-300)"
  [[ $not == - ]] || ! grep -qE -- "$not" "$T/calls" 2>/dev/null || fail "$name: /$not/ is in the calls"
done <<EOT
native;X=1;0;chart-grabber-cli --version;TOOLCHAIN|ARCHITECTURE|DEB_HOST_ARCH=[a-z]|SHLIBDEPS_PRIVATE
native-nproc;X=1;0;--build .* -j6$;-
native-jobs;CG_DEB_JOBS=2;0;--build .* -j2$;-
cross-armhf;CG_DEB_TOOLCHAIN=@TC@ CG_DEB_ARCH=armhf;0;-DCMAKE_TOOLCHAIN_FILE=$tc ;-
cross-armhf-arch;CG_DEB_TOOLCHAIN=@TC@ CG_DEB_ARCH=armhf;0;cpack -G DEB -D CPACK_DEBIAN_PACKAGE_ARCHITECTURE=armhf -D CPACK_DEBIAN_CLI_PACKAGE_ARCHITECTURE=armhf;-
cross-armhf-shlibdeps;CG_DEB_TOOLCHAIN=@TC@ CG_DEB_ARCH=armhf;0;SHLIBDEPS_PRIVATE_DIRS=/usr/lib/arm-linux-gnueabihf \[DEB_HOST_ARCH=armhf DEB_HOST_GNU_TYPE=arm-linux-gnueabihf\];-
cross-arm64;CG_DEB_TOOLCHAIN=@TC@ CG_DEB_ARCH=arm64;0;SHLIBDEPS_PRIVATE_DIRS=/usr/lib/aarch64-linux-gnu \[DEB_HOST_ARCH=arm64 DEB_HOST_GNU_TYPE=aarch64-linux-gnu\];-
cross-no-run;CG_DEB_TOOLCHAIN=@TC@ CG_DEB_ARCH=armhf;0;-;chart-grabber-cli --version
only-arch;CG_DEB_ARCH=armhf;2;go together;-
only-toolchain;CG_DEB_TOOLCHAIN=@TC@;2;go together;-
bad-arch;CG_DEB_TOOLCHAIN=@TC@ CG_DEB_ARCH=riscv64;2;want armhf or arm64;-
no-toolchain-file;CG_DEB_TOOLCHAIN=/nonexistent CG_DEB_ARCH=armhf;2;no toolchain file;-
EOT
[[ $bad -eq 0 ]] && { echo "selftest: build_deb cross mode ok"; echo PASS; exit 0; }
echo FAIL; exit 1
