#!/usr/bin/env bash
# T-0228: proof 2 of the licence files. Usage: tools/check_deb_copyright.sh BUILD_DIR SCRATCH_DIR
# BUILD_DIR is configured and built (target chart-grabber-cli); SCRATCH_DIR is a fresh empty directory. Runs `cpack -G DEB`.
# dpkg-deb is not on every dev host: a .deb is an ar archive whose data.tar.* member holds the installed files.
set -u
build=${1:?build dir}; scratch=${2:?scratch dir}; bad=0
fail(){ echo "tools/check_deb_copyright.sh:1: DEBCOPY: $1"; bad=1; }
done_(){ [[ $bad -eq 0 ]] && echo "deb copyright: PASS" || echo "deb copyright: FAIL"; exit $bad; }
(cd "$build" && cpack -G DEB -B "$scratch" >/dev/null 2>&1) || fail "cpack -G DEB failed"
deb=$(ls "$scratch"/*.deb 2>/dev/null | head -1)
[[ -n $deb ]] || { fail "no .deb produced"; done_; }
mkdir -p "$scratch/x" && (cd "$scratch/x" && ar x "$deb" && tar -xf data.tar.*) || { fail "cannot unpack $deb"; done_; }
f=$scratch/x/usr/share/doc/chart-grabber-cli/copyright
[[ -f $f ]] || { fail "usr/share/doc/chart-grabber-cli/copyright is not in the .deb"; done_; }
[[ $(grep -c '^Files:' "$f") -ge 2 ]] || fail "copyright has fewer than 2 Files: stanzas"
grep -q '^Files: \*$' "$f" || fail "no Files: * stanza"
grep -q '^License: GPL-3.0+$' "$f" || fail "no GPL-3.0+ stanza"
[[ -d $scratch/x/usr/share/chart-grabber-cli ]] && fail "share/chart-grabber-cli is installed (T-0291: no map sources)"
grep -q '@CG_' "$f" && fail "unconfigured @VAR@ in copyright"
# every Files: path (relative to /usr) must match something installed
grep '^Files: ' "$f" | sed 's/^Files: //' | grep -v '^\*$' | grep -v '^third_party/' | while read -r pat; do
  compgen -G "$scratch/x/usr/$pat" >/dev/null || { fail "stanza path '$pat' matches nothing installed"; exit 1; }
done || bad=1
done_
