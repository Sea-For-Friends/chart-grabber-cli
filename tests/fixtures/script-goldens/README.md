# Script golden URLs (T-0015D)

21 maps × up to 4 tile points, per `docs/specs/script.md` §11.5.3 and
`docs/backlog/T-0015D-script-golden-urls-21-maps.md`. There is no Delphi compiler or oracle for
this port. Every value in every `.golden` file below was produced by **reading**
`GetUrlScript.txt` and `params.txt` and evaluating the script by hand, following Pascal semantics
(integer `div`/`mod` truncating toward zero, `Round` banker's, `IntToStr`/`IntToHex`,
`RoundEx`'s 15-significant-digit `ffFixed` cap, `Copy`/`Pos` 1-based clamped semantics, `shl`,
static-array bounds and zero-initialisation) — **never** by running the port and pasting its
output. `golden_url_test.cpp` runs the real `UrlScript` afterward only to check the derivation,
not to produce it (ticket rule: if the two disagree, the hand derivation wins and the test case is
skipped with a defect id, not "corrected" to match the port).

A calculator (Python, exact `Decimal`/`Fraction` arithmetic) was used to carry out the by-hand
arithmetic precisely for the longer chains (base-35/base-2/base-1000 digit extraction, `RoundEx`)
— the *logic* (which operations, in what order, with what truncation/rounding rule) was
transcribed from the Pascal source by reading it, which is the derivation the ticket asks for;
the calculator only avoids arithmetic slips a human would also want to avoid in a 17-digit binary
string or a 15-significant-digit decimal rounding. Every fixture's INI comments show the
intermediate values so a reviewer can redo the same steps independently.

## Format

One INI file per map (`core::IniFile`, core §2.12), path identical to the map's path under
`tests/fixtures/zmp/sas.maps/`, plus `.golden`. One section per tile point (`[P1]`..`[P4]`, plus
one extra section for `Maps.by.zmp`, see below). `Version=''` and `Lang=en` throughout, per the
ticket (no script among the 21 reads either bound variable, so no crossing was needed). The eight
lon/lat/metre bound variables are stored as `0x` + 16 hex digits: the IEEE-754 double's raw bit
pattern, big-endian nibble order — exact, and decoupled from `sas::geo` entirely (no projection is
computed by the test). `RequestHead` doubles as both the params.txt pre-fill (input) and the
expected output, since none of the 21 scripts assign `RequestHead`.

## The four tile points

P1 `(0,0,z=1)`, P2 `(1,1,z=2)`, P3 `(1234,5678,z=14)`, P4 `(131071,87654,z=18)` — spec §11.5.2.

## Expected-error sections

A golden section may declare `ExpectError=1` (with an optional `ErrorText=<substring>` key)
instead of `ResultURL`: the test then asserts `buildRequest()` fails, rather than comparing a URL.
Three points that were previously left out as "not derivable" turned out to be derivable to a
*defined script error*, not an undefined one (docs/handoffs/T-0015D-review.md finding 2): the
scripts run in the PascalScript VM, not compiled Delphi code, and the VM range-checks every
static-array index at run time (`uPSRuntime.pas:7127-7135`/`:7289-7297`) — a negative index is
first reinterpreted as a (huge) `Cardinal` before the bounds compare, so it too ends in a defined
"Static array index out of bounds" error rather than an undefined memory read:

- **`_broken_maps/Local/Belarus/belgeodesy.zmp` P1, P2** (`GetZ-6` = -5, -4): `dx`/`dy: array[1..50]`
  indexed below the declared low bound 1. T-0032 CORRECTION: the reported index is not the raw
  script value - for a non-constant index the compiler itself subtracts the declared low bound
  into a Cardinal temp before the bounds check runs (uPSCompiler.pas ~6965-6988,
  uPSRuntime.pas:7158-7233), so it is `dx[Cardinal(-5-1)]` / `dx[Cardinal(-4-1)]` =
  `dx[Cardinal(-6)]` / `dx[Cardinal(-5)]`, both `>= Size (50)`, both a defined range-check error.
  (P3/P4 land *inside* the declared `[1..50]` range, past the filled `1..9` — that case is
  derivable to a value, §4.4 I1 zero-initialisation — and are kept as value goldens.)
- **`megafonpro.zmp` P4**: the base-35 digit extraction on the P4 corner (`GetX=131071` is the
  zoom-17 edge, `dx3` comes out to 65535) computes `ppx3 = 53`, an index into
  `a: array[0..35] of char` (`Size` 36) — `53 >= 36`, a defined range-check error. This is a
  genuine bug in the *legacy map script itself*, not the port (see
  `docs/parity/map-corpus-defects.md`). P1-P3 stay well inside `a`'s bounds and are kept as value
  goldens.

All 84 (map, point) cells of the 21×4 grid are now present — 3 of them as expected-error sections
rather than value goldens. PD-1 (static-array indexing) is fixed by T-0032; every cell is now
asserted, none skipped.

## `Maps.by.zmp`'s extra section

Spec table row 19 asks for two cases: the `Assigned(DefProjConverter)` branch (`[P1]`..`[P4]`,
using the test's identity `IProjConverter` per the ticket, so `RoundEx` sees the lon/lat inputs
unchanged) and the `else` branch (`DefProjConverter = nil`, asserting `ResultURL = 'invalid_url'`
regardless of tile coordinates) — the latter is the fifth section, `[NilConverter]`, reusing P1's
coordinates arbitrarily since the `else` branch never reads them.

## Port defects found (kept as hand-derived golden)

- **PD-1** (renamed from PD-101/PD-102, same code sites, docs/handoffs/T-0015D-review.md finding
  3) (`belgeodesy.zmp`, all 4 points; `megafonpro.zmp`, all 4 points): the interpreter
  (`src/sas/script/interpreter.cpp`, `evalIndex`/the `IndexAssign` case of `execStmt`) supported
  element indexing (`s[i]`, `s[i] := c`) only on `ValueKind::Str`/`Empty`. `ValueKind::Array`
  (a static array, `arr[i]`/`arr[i] := v`) returned `ErrorCode::Unsupported`. Both scripts write an
  array element as their very first statement (`dx[1] := 36;` / `a[0] := '0';`), so every section
  of both maps failed through `UrlScript::buildRequest` — value goldens and expected-error goldens
  alike — before any interesting arithmetic, or the engine's own range check, ever ran.
  **FIXED by T-0032** — see `docs/handoffs/T-0032.md` (also corrects belgeodesy P1/P2's ErrorText,
  see the `.golden` file's own comment: the reported index subtracts the array's declared low
  bound, which T-0015D's derivation missed).
- **PD-103** (`_historic/Other/zoomify_usage_example.zmp`, all 4 points): found while proving the
  hand derivation against the port. `parser.cpp`'s `parseConstSection()` parsed a `const`
  declaration's value expression but never folded or stored it, so the interpreter bound the
  const's name to an ordinary zero-initialised local slot instead of its literal value. Confirmed
  with a minimal repro (`const X = 42; var A: Integer; begin A := X; end.` left `A = 0`, not `42`)
  before concluding it wasn't a mistake in this derivation. `zoomify_usage_example.zmp`'s
  `const m_nWidth = 9910; m_nHeight = 7476;` therefore read as `0` everywhere they're used. This is
  the only one of the 21 maps that declares a top-level `const`. Corpus scope
  (docs/handoffs/T-0015D-review.md): 8 of 350 `GetUrlScript.txt` declare `const` — Navionics ×4,
  `Google/GoogleSat`, `Yandex/YaTraf`, `_broken_maps/_low_quality_/kartasbp.ru`, this map.
  **FIXED by T-0032** (`Parser::foldConstExpr` + `CompiledScript::constValue`) — see
  `docs/handoffs/T-0032.md`.
- **PD-104** (docs/handoffs/T-0015D-review.md finding 1, fixed by T-0031 — see
  docs/handoffs/T-0031.md) (`zoomify_usage_example.zmp`, P4 only): integer binary-op intermediates
  are computed in the operand's static 32-bit type by the engine (PascalScript `Trunc` returns
  `LongInt`, `GetY` is an S32 script variable, an S32×S32 product is computed in an S32 temp —
  `uPSCompiler.pas:13571`, `u_TileDownloadRequestBuilderPascalScriptVars.pas:143`,
  `uPSRuntime.pas:6052`) and every such intermediate wraps mod 2^32 (overflow checks are off in the
  legacy build). The port used to keep integers as `qint64` and wrap only on assignment, so it
  never wrapped these intermediates and got a different result once the value exceeded ±2^31; T-0031
  corrected spec `script.md` §N1 and the interpreter to compute at the left operand's static width.
  P4's `nTileGroup` computation overflows partway through (`GetY*Trunc(...)` alone is ~6.9 billion),
  so this was the only one of the 21 maps' goldens affected. Now matches (PD-103 above no longer
  blocks it either).

## Which 5 goldens to re-derive first (most complex / highest payoff for a reviewer)

1. `_broken_maps/Rosreestr/rosreestr_cadastr.zmp` (`RoundEx` precision 10 on 8-digit metre
   values) — the 15-significant-digit cap is "the single most likely `RoundEx` bug" per spec
   table row 18; P3's trailing-zero padding is the sharpest test of the algorithm.
2. `megafonpro.zmp` (base-35 three-level digit extraction + the P4 out-of-bounds finding above) —
   the longest arithmetic chain in the set, and the one most likely to hide a hand-transcription
   error.
3. `_broken_maps/Others/Geoportal/RoscosmGeo.zmp` (`repeat/until`, `shr`, `Pos`/`Copy` 3 times,
   the `#0`-past-end string read) — the most control-flow-heavy script with no arrays to fall back
   on if the derivation is wrong.
4. `_historic/Other/zoomify_usage_example.zmp` (user `Floor`/`Ceil` shadowing, recursive-looking
   `GetTilesCount`, negative `IntPower` exponent at P3/P4 since the requested zoom is far past the
   image's own 6 zoomify levels) — the only map combining shadowing with real (Extended) division.
5. `Bing/Bing.map.zmp` (quadkey `for` loop, 17 digits at P4) — mechanical but long; easiest to
   verify independently by cross-checking a couple of digits against the standard Bing quadkey
   algorithm.
