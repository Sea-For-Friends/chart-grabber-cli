# CLI captures of a second snapshot run (T-0356)

For the plugin's fixtures: the output order of T-0351 on the SECOND `run` of a weather (snapshot) source with `-v
--overwrite`, the index having moved on to a newer frame. The first run took frame `/A2`, the second took `/B`
(RainViewer-like `json-last` source "Radar", nine tiles, zooms 1-3). The run removes the nine tiles of the older frame
BEFORE the fill report, so the stderr line `removed 9 tiles of older snapshots of "Radar"` (the last line of stderr
before the report; with a storage error it is `warning: cannot remove older snapshots of "Radar": <text>` instead)
precedes the `fill` records / the fill table on stdout, and the tiles moved to the new frame count as present in it.

Produced by `runCli` (the code of `chart-grabber-cli run`) in `tests/app/cli_snapshot_test.cpp` against the local HTTP
test server of `CliSnapshotPrune.TheSecondRunRemovesTheOlderFrameAndVerboseCountsIt` (a one-off dump of `Captured`; the
test itself asserts the `removed` line; that the removal runs before the report is pinned by
`CliSnapshotPrune.IdenticalTilesOfTheNewFrameKeepTheOlderFrameAndTheExportHasEveryTile`, whose `--strict` run passes only
when the moved tiles are present in the report).
The two streams are separate files (stdout / stderr), as the process writes them; the temporary directory is `<tmp>` and
the session file `<session>`. Not a network capture: the frame times are the fixed ones of the test index.

- `second-run-text.stdout` / `.stderr`: text output (`written:` line with the snapshot time).
- `second-run-tsv.stdout` / `.stderr`: `--format=tsv`.
