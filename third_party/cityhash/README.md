# Vendored Google CityHash

- **Upstream**: https://github.com/google/cityhash
- **Version**: 1.1.1 (NEWS entry "CityHash v1.1.1, June 17, 2013")
- **Commit**: `f5dc54147fcce12cefd16548c8e760d68ac04226` (2022-07-19, compiler-warning fixes only;
  no functional change since 1.1.1 — see upstream `NEWS`)
- **Licence**: MIT (`COPYING`, copied verbatim)
- **Files**: `city.h`, `city.cc` (upstream `src/`), `COPYING`.

## Why vendored

The legacy Delphi app links `CityHash.dll` through `Includes/CityHash.pas` and persists the
resulting 64-bit content hashes (survey 03), so `cg::core::cityHash64` must be byte-identical to
that DLL. Vendoring the upstream C++ source is the only way to guarantee that; there is no
packaged CityHash on the target distro.

## Which upstream version the legacy DLL is

`docs/specs/core.md` §2.9 / §10.1 says "CityHash 1.0.x / 1.0.3". That is **wrong**, and CityHash64
values differ between 1.0.3 and 1.1 (upstream `NEWS`, v1.1: "Change existing functions to improve
their hash quality and/or speed"; concretely `HashLen17to32` was rewritten). Evidence that the
legacy DLL is >= 1.1:

1. `CityHash.dll` of a stock SAS.Planet install exports `CityHash32`, which upstream added
   in v1.1 (`strings -a CityHash.dll`).
2. `Includes/CityHash.pas:101` declares `CityHash32`.
3. The DLL contains no occurrence of the constant `0xc949d7c7509e6557` (`k3`), which exists only in
   CityHash <= 1.0.3 and was removed in 1.1.

So 1.1.1 is vendored. Recorded as a deviation from the spec in `docs/handoffs/T-0002A.md`.

## Local modifications

One line, so the sources compile without autoconf:

- `city.cc:31`: `#include <city.h>` -> `#include "city.h"` (upstream expects `src/` on the include
  path via its own build system).

`config.h` is **not** upstream: it is a 13-line stand-in for the autoconf-generated header that
`city.cc:30` includes, defining `HAVE_BUILTIN_EXPECT` and deriving `WORDS_BIGENDIAN` from
`__BYTE_ORDER__`. Nothing else was touched.

## Verification

Upstream's own `src/city-test.cc` (300 cases over a 1 MiB pseudo-random buffer) was compiled and
run against this exact copy of `city.cc` and reported no errors. The golden vectors pinned in
`tests/core/hash_test.cpp` were generated from that verified build; regenerate them (never invent
them) if this directory is updated.

## Build

`third_party/CMakeLists.txt` builds it as the static target `cityhash`, deliberately **not** linked
against `cg_warnings`: upstream does not compile under `-Werror -Wold-style-cast` and must not be
modified. `tools/nocmake_build.sh` compiles everything under `third_party/` with `-w` for the same
reason.

## Local changes

- T-0335: `city.cc` has an `#elif defined(__MINGW32__)` branch that defines `bswap_32`/`bswap_64` with the GCC
  builtins (MinGW has no `<byteswap.h>`). The branch is not compiled elsewhere; the hash values do not change.
