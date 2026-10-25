Fixtures of T-0280 (URL differential, `tests/maptype/url_differential_test.cpp`). Test data only, not shipped, never
downloaded from: the tests build URLs offline; the Navionics token service is a scripted in-process downloader.

## base-zmp/ and base-json/ (24 sources of the base chart set)
- `base-zmp/<group>/<name>.zmp/params.txt`: byte-for-byte copies of `data/sources/<group>/<name>.zmp/params.txt` of the
  the plugin repository's `data/sources/` (read only; the 24 files are identical at commit acdf739 and
  at 9f39e0c, except line 12 of `satellite/Esri-World-Imagery`, where the terms URL is shortened to `https://www.arcgis.com/…` in both twins, T-0354). None has a `GetUrlScript.txt` (plain URL templates).
- Group folders are hyphen-case with no spaces (the landing scope check splits paths on whitespace). Mapping to the
  plugin's original group names (`data/sources/<group>`):

  | folder | plugin group |
  |---|---|
  | `coast-and-land` | Coast and land |
  | `depth` | Depth |
  | `nautical-charts` | Nautical charts |
  | `ocean` | Ocean |
  | `satellite` | Satellite |
  | `seamarks-and-traffic` | Seamarks and traffic |
  | `weather-now` | Weather now |
- `base-json/<group>/<name>.source.json`: the same source written as a JSON source (map-source-json.md section 3 key
  table). No legacy sample exists for it (the legacy tree has no JSON sources), so it was produced by a one-off script
  (kept outside the repository) that maps each `params.txt` key by the spec table and asserts that no key is left over;
  `EPSG=3857` is the default and is not written.
- `NavigationWarning=1` (7 sources) is the JSON key `"navigationWarning": true` (T-0346); the differential compares
  the key on both sides. It has no effect on a URL.

## navionics-zmp/ and navionics-json/ (the 4 Navionics maps)
- `navionics-zmp/<map>.zmp/{params.txt,GetUrlScript.txt}`: copies from the SAS maps archive (`maps/Navionics/<map>.zmp/`; the Pascal originals; the icons and `EmptyTiles` are not needed and not copied).
- `navionics-json/<map>.source.json` and `<map>.js`: copies of the parked Navionics sources (`sources/Navionics/`; byte-identical to sas-maps-latest-json `Marine/Navionics/`), with ONE exception: `navionics.js` has three
  lines changed (T-0280, found by the differential):
  - `var buf = getNavToken().split('\u0000')[0];` (the Pascal subject ends at the first NUL, script.md R15);
  - the two patterns use `([\s\S]*)` instead of `(.*)` (TRegExpr's `.` matches a line break, script.md R1; JavaScript's
    does not). The other three `.js` files already had both.
  The parked repository is read only for this ticket: the same three lines have to be applied there and in
  sas-maps-latest-json by their owner.
- The Navionics terms of use are not known; the token service is never contacted.
