# GPS track-log fixtures — NONE AVAILABLE

**Searched**: the entire stock SAS.Planet install (recursive search
for `*track*`, `*.plt`, `*.gpx`) and the entire legacy repo's `Test/` tree. Two search results
came back but both are stock **map-style** definitions named `*tracks*.zmp`
(`Maps/sas.maps/Yandex/yandex.tracks.zmp`, `Maps/sas.maps/__anygis_filegenerator/Tracks_Strava_*.zmp`)
— these are ZMP overlay map sources for displaying Strava heatmap tiles, already captured under
`../zmp/`, and are **not** GPS track-log files (no `.plt`/`.gpx`/`.kml` recorded track exists in
either source).

**Why**: the live install has no GPS device/track history — it has never recorded or imported a
track (see `../sessions/README.md` for the same pattern with download sessions: this appears to
be a lightly-used install with almost no user-generated state).

**Per protocol rule 10**: no fixture file is provided here rather than fabricating a track log.
Flagged as an open question for the Tech Lead: obtain a real recorded/imported track (`.plt` is
the legacy native format per `vsagps`; `.gpx`/`.kml` are import formats) from another source
before the GPS-track module's fixtures/tests are considered complete.
