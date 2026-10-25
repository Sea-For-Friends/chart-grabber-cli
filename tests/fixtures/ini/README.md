# INI fixtures

**Origin**: a stock SAS.Planet install, copied 2026-09-17.

**How sliced**: copied verbatim, no editing needed (see "Scrubbing" below) — each file is a
default/template shipped with the app or an empty state file, none contain user data.

**Files**:
- `FormsState.ini` — 3 bytes (UTF-8 BOM only, no key/value content). The live install has
  apparently never had its window layout saved; kept as a fixture anyway so the parser has to
  handle an "empty but BOM-present" ini file.
- `TileStorage_DBMS.Default.ini` — shipped default template for the optional DBMS tile-storage
  backend (ODBC DSN name only, `DSN=SASGIS_DBMS`). Not a real credential — this is the stock
  template that ships with every install, renamed by the user to `TileStorage_DBMS.ini` to
  activate it. No user DSN/username/password present.
- `TerrainData/SASTerrain.Default.ini` — shipped default template listing built-in DEM sources
  (ASTER, SRTM1/3, viewfinderpanoramas, ALOS, FABDEM) with GUIDs, folder/prefix/suffix naming
  patterns, and raster geometry (LinesCount/SamplesCount/VoidValue/ByteOrder). Purely static
  metadata, no user data.

**Scrubbing**: none of the three files contained user paths, credentials, or proxy passwords
(checked with `grep -riE "password|proxy|login|token|secret|apikey"` — no hits). Nothing was
scrubbed; files are copied byte-for-byte.

**Missing (important gap for the Tech Lead)**: `SASPlanet.ini` — the main user settings file
(expected to hold window state, last-used map, download/proxy settings, and per docs/CHARTER
risk table, proxy password) — **does not exist anywhere on this live install**. The install
appears to have been run at least once (it did generate `Marks.db3`, `FormsState.ini`, and one
cached tile — see `../marks/README.md` and `../cache/README.md`), but no `SASPlanet.ini` was
ever written to the install directory or found elsewhere on the filesystem
(`find / -iname "SASPlanet.ini"` returned nothing). No sample was available in the legacy repo's
`Test/data` either. **This fixture folder cannot exercise the main settings-file format** —
flag as an open question / follow-up ticket to obtain a real `SASPlanet.ini` (e.g. by running the
legacy app once and letting it save settings) before the settings-parser module is implemented.

**What it exercises**: ini key/value parsing (UTF-8 BOM handling, empty file, `[SECTION]` headers,
quoted string values, repeated keys with `_<lang>` suffix pattern), and default-template loading
for the DBMS and terrain-DEM subsystems.

---

## `quirks.ini` (added by T-0002A — synthetic, deliberately)

**Origin**: hand-written for T-0002A/T-0002, not a real user file. Written with a UTF-8 BOM and
CRLF line endings, i.e. exactly what Delphi's `TMemIniFile` + `TEncoding.UTF8` produce on Windows.

**Purpose**: exercise the Delphi `TMemIniFile` parsing quirks Q1–Q24 enumerated in
`docs/specs/core.md` §2.12.2 — whole-line trimming, `;`-only comments (`#` is a key!), section
header shape, split-at-first-`=`, quotes kept verbatim, duplicate keys/sections, case-insensitive
lookup, the integer/bool/float/date/binary conversion rules, and append-on-write placement.
No real file in this repo happens to contain all of those constructs.

**Why a synthetic file is acceptable here**: the "fixtures are real" rule (agent-protocol §10) is
about *format regression*, and the real-format regression for this module is the 514-file ZMP
`params.txt` byte-identical round trip (`tests/core/zmp_roundtrip_test.cpp`, spec §7.19) plus the
three real ini files above. `quirks.ini` only adds edge cases on top.

**To be supplemented, not replaced**: when T-0004 delivers a real `SASPlanet.ini`, add it as an
extra round-trip fixture; `quirks.ini` stays for the edge cases.

**Used by**: `tests/core/ini_file_test.cpp` (30 cases), `tests/core/text_codec_test.cpp`.
