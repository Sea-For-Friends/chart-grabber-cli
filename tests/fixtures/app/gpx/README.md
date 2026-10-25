# GPX fixtures (T-0187)

- `opencpn_foo.gpx` - a real file written by OpenCPN (creator="OpenCPN", GPX 1.1 with `opencpn:` and
  `gpxx:` extensions): one route "foo", 13 route points. Origin: OpenCPN source repository, `test/testdata/foo.gpx`
  (https://github.com/OpenCPN/OpenCPN, checkout of 2026-10-13). Licence: GPL-2.0-or-later (OpenCPN `LICENSING`);
  copied unchanged, used only as a test input.

The other inputs of `tests/app/gpx_route_reader_test.cpp` are small GPX texts written inline in the test. Protocol
rule 10 allows hand-written inputs here because the format reader is new code and no legacy output exists.
