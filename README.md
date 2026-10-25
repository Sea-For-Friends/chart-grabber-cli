# Chart Grabber core

`chart-grabber-cli` downloads map tiles of an area from an online map source and writes them into an MBTiles file that
OpenCPN opens as a chart. It runs on Linux without a display. It does not display maps, plan routes, or convert vector
charts (S-57, S-63).

It is an independent Qt 6 command-line port of the tile downloader and chart tools of
[SAS.Planet](https://github.com/sasgis/sas.planet.src), and it is the engine behind the Chart Grabber plugin for
OpenCPN.

It is not an official SAS.Planet project, and it is not affiliated with or endorsed by SAS.Planet, OpenCPN or OpenCPN's Chart
Downloader plugin (`chartdldr_pi`). It is derived from SAS.Planet and is released under the GPL-3.0-or-later, like the original.
Thanks to the SAS.Planet authors.

## Install

Download the `.deb` for your system from the Releases page of this repository:

- Debian 13: amd64 and arm64
- Debian 12: amd64
- Ubuntu 24.04 and 26.04: amd64 and arm64

Then install it:

```
$ sudo apt install ./chart-grabber-cli_*.deb
$ chart-grabber-cli --version
chart-grabber-cli 2026.10.4 (cca1543da939)
```

The version number is the year, the month and the number of the release in that month.

## Make a chart

The program brings no map sources. You pass it a folder of map-source definitions with `--maps`. Without it the
program stops with exit code 3:

```
$ chart-grabber-cli sources
error: no map sources given: pass --maps DIR, a folder of map-source definitions (repeat --maps for more folders)
```

How to get a folder of sources, list them, download an area and write the chart is in the
[user guide](docs/user/cli-guide.md); on Windows start with the [Windows guide](docs/user/windows-guide.md). How to
write a source of your own is in the [sources tutorial](docs/user/sources-tutorial.md) and the [sources guide](docs/user/sources-guide.md).

Some map providers forbid bulk download in their terms of use. Read the terms of a source before you use it. You are
responsible for what you download.

## Use it with OpenCPN

The Chart Grabber plugin for OpenCPN calls `chart-grabber-cli` for you. Plugin repository:
`https://github.com/Sea-For-Friends/chartgrabber_pi`

## Build from source

You need Debian 12 or 13 (or a system with the same libraries), Qt 6.4 or newer, CMake and a C++23 compiler. The
packages are listed in [tools/packages-debian1x.txt](tools/packages-debian1x.txt).

```
$ git clone https://github.com/Sea-For-Friends/chart-grabber-cli.git
$ cd chart-grabber-cli
$ cmake --preset release
$ cmake --build --preset release
```

The program is `build/release/cli/chart-grabber-cli`. To change the code, read [CONTRIBUTING.md](CONTRIBUTING.md).

## Documents

- [User guide](docs/user/cli-guide.md)
- [Windows guide](docs/user/windows-guide.md)
- [Sources tutorial](docs/user/sources-tutorial.md)
- [Sources guide](docs/user/sources-guide.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)
- [Licence](LICENSE)
