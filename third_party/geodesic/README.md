# Vendored geodesic routines (GeographicLib C)

**Origin**: `geographiclib/geographiclib-c`, tag `v2.2` (`GEODESIC_VERSION_MAJOR=2`,
`GEODESIC_VERSION_MINOR=2`, `GEODESIC_VERSION_PATCH=0`), files `src/geodesic.c` and
`src/geodesic.h`, fetched verbatim, not modified.

**License**: MIT/X11, Copyright (c) Charles Karney (2012-2022). See `LICENSE.txt`
(the C package's `LICENSE.txt`, dated 2012-2025, applies to this code as vendored).

**Why vendored** (`docs/decisions/ADR-0006-projections.md` decision 7,
`docs/specs/geo.md` §2.3.1): legacy SAS.Planet's `u_DistanceCalculatorByGeographicLib.pas`
links `geodesic.dll`, itself built from this exact upstream C source. Vendoring the same
source (rather than depending on the system's PROJ-bundled copy, which renames every
`geod_*` symbol via `proj_symbol_rename.h` for internal PROJ use and is not a public API)
guarantees `geod_direct`/`geod_inverse` produce bit-identical results to the legacy
Windows build for the same inputs.

**Not used**: `geod_polygon_*` (`u_PolygonAreaCalculator.pas` uses a different algorithm,
the GRASS/QGIS series — see `docs/specs/geo.md` §2.3.2; substituting Karney's polygon area
would change every area the app reports).

**Build**: its own `OBJECT` library target `geodesic` in `third_party/CMakeLists.txt`,
**not** linked against `cg_warnings` — this upstream C source does not build clean under
`-Werror -Wpedantic -Wold-style-cast` and must not be modified to make it so (same rule
`core.md` §10.1 applies to the vendored CityHash sources).

**Calls used** (`cg::geo::Datum`, ports `u_DistanceCalculatorByGeographicLib.pas:78,94,106,123`):
`geod_init(&g, radiusA, (radiusA - radiusB) / radiusA)`, `geod_direct(...)`, `geod_inverse(...)`.
