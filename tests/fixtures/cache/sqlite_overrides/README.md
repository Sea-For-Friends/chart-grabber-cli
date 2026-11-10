# `sqlite_overrides` fixture

Hand-made (not from a real cache) -- unlike every other fixture under `tests/fixtures/cache/`, the
three files here have no real-world source to copy: `s.sql`, `t.sql` and `e.sql` are optional
override files a *user* places next to a native SQLite region cache, and nobody has ever
contributed a customised set. This is the one fixture that has to be synthetic.

Content (one SQL statement per line, verbatim, no trailing blank line):

- `s.sql` -- three `PRAGMA` statements, deliberately DIFFERENT from `SqliteRegionSchema`'s built-in
  defaults (`cache_size = 500` not `2000`, `journal_mode = DELETE` not `WAL`), so a test can tell
  the override file was actually read instead of silently falling back to the default.
- `t.sql` -- a 5-column `t` table (`x, y, d, s, b`; no `v`, `c` or `h`), so `SqliteRegionHandler`'s
  column discovery is exercised against an older/hand-edited schema instead of the default 8-column
  one.
- `e.sql` -- `PRAGMA optimize`, run after the structure check on every connection, so a test can
  confirm `e.sql` actually ran (e.g. by checking it did not raise, or via a stmt/query count).

Used by `tests/storage/sqlite_region_handler_test.cpp`.
