# maptype fixtures

`Maps_stock_extract.ini` — the first three sections (bytes verbatim: UTF-8 BOM, CRLF, blank line after
each section) of `Maps/Maps.ini` of a stock SAS.Planet install, written by legacy SAS.Planet
(`Lang=en`) on 2026-10-12. Used by `map_type_gui_config_test.cpp` as the oracle for the written shape
of an untouched map: `MapName_`, `pnum`, `ShowOtherVersions`. The full 514-section file is copied to
`tests/fixtures/zmp/Maps.ini` by the corpus ticket (spec maptype.md section 7.4).
The first section belongs to `tests/fixtures/zmp/sas.maps/_Space/sky_chandracomp.zmp`.
