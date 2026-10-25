# Download session (.sls) fixtures — NONE AVAILABLE

**Searched**: the entire stock SAS.Planet install (full recursive
`find -iname "*.sls"`) and the entire `Test/` tree of the legacy source
repo. **No `.sls` file exists in either source.**

**Why**: the live install is a fresh/lightly-used copy — it has cached exactly one map tile and
created a default marks user (see `../cache/README.md`, `../marks/README.md`) but no region
download job has ever been started or saved, so no `.sls` session file was ever written.

**Per protocol rule 10 ("fixtures are real ... never synthetic approximations of a format")**:
no fixture file is provided here rather than hand-crafting a fake `.sls`. This is a real gap —
flagged as an open question for the Tech Lead: to fixture the download-session format, someone
needs to start (and cancel/pause) a region download in the live SAS.Planet install so it persists
a real `.sls` file, or locate one elsewhere. Until then, the download-session-format module has
no regression fixture and should not be marked done in the parity matrix.
