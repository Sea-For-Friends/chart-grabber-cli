#!/usr/bin/env bash
# T-0322: SPDX licence header on the sources (src/, cli/, tests/**/*.cpp and *.h; never third_party/, never tests/fixtures/).
# Usage: tools/add_spdx.sh [--check] [paths...]
#   no paths  the whole source scope above (find, sorted); paths given: exactly those files (absolute or relative)
#   default   inserts the header above the first line of each file that has none
#   --check   writes nothing; one line per file without the header ("<file>:1: SPDX: ...") and exit 1 if there is one
# The header is three lines: the copyright line is over 110 columns, clang-format (ColumnLimit 110) would reflow it into two
# comment lines, so the split is written here and the files stay clang-format clean. A file that already has a line
# starting with "// SPDX-License-Identifier:" is left alone (idempotent). A shebang line stays first, CRLF files keep
# CRLF, an empty file is skipped. Legacy units are not touched (this tree is the port, not the legacy tree).
set -u
H1='// SPDX-License-Identifier: GPL-3.0-or-later'
H2='// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,'
H3='// SAS.Planet development team; modified.'

CHECK=0
if [[ "${1:-}" == "--check" ]]; then CHECK=1; shift; fi
if [[ $# -eq 0 ]]; then
  cd "$(dirname "$0")/.." || exit 2
  mapfile -t FILES < <(find src cli tests -type f \( -name '*.cpp' -o -name '*.h' \) -not -path 'tests/fixtures/*' | sort)
else
  FILES=("$@")
fi

has_header(){ grep -q '^// SPDX-License-Identifier:' "$1"; }

# add_one <file>: 0 = header added, 1 = skipped (already there or empty), 2 = error
add_one(){
  local f=$1 eol="" skip=0 nxt sep=1 tmp
  [[ -s "$f" ]] || return 1
  has_header "$f" && return 1
  head -n1 "$f" | grep -q $'\r$' && eol=$'\r'
  head -n1 "$f" | grep -q '^#!' && skip=1
  nxt=$(sed -n "$((skip + 1))p" "$f"); nxt=${nxt%$'\r'}
  [[ -z "$nxt" ]] && sep=0
  tmp=$(mktemp) || return 2
  {
    if [[ $skip -eq 1 ]]; then head -n1 "$f"; fi
    printf '%s\n' "$H1$eol" "$H2$eol" "$H3$eol"
    if [[ $sep -eq 1 ]]; then printf '%s\n' "$eol"; fi
    tail -n +"$((skip + 1))" "$f"
  } >"$tmp" || { rm -f "$tmp"; return 2; }
  cat "$tmp" >"$f" || { rm -f "$tmp"; return 2; }   # cat keeps the file's inode and mode
  rm -f "$tmp"
  return 0
}

bad=0; added=0; kept=0
for f in "${FILES[@]}"; do
  if [[ $CHECK -eq 1 ]]; then
    [[ -s "$f" ]] || continue
    has_header "$f" || { echo "$f:1: SPDX: missing '$H1' header (run tools/add_spdx.sh)"; bad=1; }
    continue
  fi
  add_one "$f"
  case $? in
    0) added=$((added + 1)) ;;
    1) kept=$((kept + 1)) ;;
    *) echo "$f:1: SPDX: could not write the header" >&2; exit 2 ;;
  esac
done
if [[ $CHECK -eq 1 ]]; then exit $bad; fi
echo "add_spdx: ${added} file(s) got the header, ${kept} left unchanged"
exit 0
