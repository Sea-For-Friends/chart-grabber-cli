# Changelog

All notable changes to `chart-grabber-cli` are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

The text the program prints is a contract: a change to the columns of the `sources` table, the `written:` line, the
progress line or `--version` always gets an entry here.

## [Unreleased]

### Added
- A `.deb` for Debian 13 on 32-bit ARM (armhf) and Raspberry Pi OS 32-bit (trixie), `chart-grabber-cli_<version>_debian13_armhf.deb`,
  next to the armhf bundle.

## [2026.10.14] — 2026-11-02

### Added
- A Linux bundle for 32-bit ARM, `chart-grabber-cli-<version>-linux-armhf.tar.gz`, for Debian 12 and 13 armhf and
  Raspberry Pi OS 32-bit on ARMv7 and newer boards (Raspberry Pi 2, 3, 4, 5 and Zero 2 W; not the ARMv6 Pi Zero and
  Pi 1). It carries its own libraries and needs no `LD_LIBRARY_PATH`.
- A macOS universal bundle (Apple Silicon and Intel), `chart-grabber-cli-<version>-macos-universal.tar.gz`.

### Changed
- The Linux bundles for 64-bit PC and ARM 64-bit run on glibc 2.35 and newer (before: 2.36), and they work inside
  OpenCPN's Flatpak with the 24.08 and 25.08 runtimes.
- On 32-bit ARM Linux and on Apple Silicon, the script functions documented as Extended precision (exp, ln, sin, cos,
  arctan, non-integral powers) are computed in double precision. Results can differ from x86 in
  the last digits.

### Fixed
- On 32-bit ARM Linux, a tile date in an archive-format cache no longer breaks the writer. Dates after 2038 are stored
  as the latest date a 32-bit system can hold, and the "no date" mark written by 64-bit systems reads back as the same
  date. 64-bit systems write and read exactly what they did before.

## [2026.10.13] — 2026-10-30

### Added
- Self-contained Linux bundles `chart-grabber-cli-<version>-linux-x86_64.tar.gz` and
  `chart-grabber-cli-<version>-linux-arm64.tar.gz`. They carry their own libraries, need no `.deb` and no
  `LD_LIBRARY_PATH`, and replace the single-file Linux x86-64 binary of 2026.10.11.

### Changed
- The Linux bundles use their own font configuration and the bundled DejaVu Sans font instead of the system's, so a
  system with a newer fontconfig (Arch, CachyOS) no longer prints "Fontconfig warning" lines on stderr. The font setup
  goes to `<datadir>/fontroot`; without `--datadir` it goes to `$XDG_CACHE_HOME/chart-grabber/fontroot`, with
  `$XDG_RUNTIME_DIR/chart-grabber-fontroot` as a fallback. Pass `--datadir` on every call, `--version` included, to
  keep it in one place.
- The Linux bundles do not carry OpenGL libraries. On Debian and Ubuntu the system needs the packages
  `libopengl0 libglx0 libegl1`; installing `opencpn` already pulls them in.
- NOTICE and README state that Chart Grabber CLI is an independent port of SAS.Planet, not an official SAS.Planet
  project, and credit the SAS.Planet development team.

### Fixed
- Several copies of the program starting at once on a fresh data directory no longer print
  "Fontconfig error: No writable cache directories".
- A Linux program moved to another directory finds `proj.db` next to the executable.

## [2026.10.11] — 2026-10-25

The first public release.

### Added
- `.deb` packages for Debian 13 and 12 and Ubuntu 24.04 and 26.04 on amd64, and for Debian 13 and Ubuntu 24.04 and
  26.04 on arm64. A Windows x64 zip and a single-file Linux x86-64 binary.
- The Debian package and the Windows zip carry the sources tutorial and its example sources (`examples/sources/`).

### Changed
- A `*.source.json` source whose script has a syntax error, has no `getUrl`, or fails in its top-level code now shows
  the reason (the script engine's message with the line) in the verdict column of `sources`, in `probe` as
  `skipped: ...`, and when `download` refuses to start. Before, `sources` said `ok` and `probe` said
  `error: Tile does not exist`. `probe` no longer exits 4 for such a source: it is a `skipped:` row and does not count
  toward the exit code. A `getUrl` that throws is still a failure of its tile.
- On Windows, when moving `%APPDATA%\chart-grabber` to `%LOCALAPPDATA%\chart-grabber` fails partway, the note on stderr
  is `note: cannot move <old> to <new> (<reason>); moved: <names>; left in <old>: <names>` (before:
  `the cache starts empty`). A name present in both folders is not moved, and the note ends
  `; not moved, the name is in both folders: <names>; <old> is left with it, resolve by hand`.

### Fixed
- With a directory as `--out`, a `*.source.json` source is written as `<name>.mbtiles`, like a `.zmp` source (before:
  `<name>.source.json.mbtiles`).

[Unreleased]: https://github.com/Sea-For-Friends/chart-grabber-cli/compare/v2026.10.14...HEAD
[2026.10.14]: https://github.com/Sea-For-Friends/chart-grabber-cli/compare/v2026.10.13...v2026.10.14
[2026.10.13]: https://github.com/Sea-For-Friends/chart-grabber-cli/compare/v2026.10.11...v2026.10.13
[2026.10.11]: https://github.com/Sea-For-Friends/chart-grabber-cli/releases/tag/v2026.10.11
