# Chart Grabber CLI

Chart Grabber downloads map tiles of the area you choose and saves them in one file. You copy the file to the boat and
open it in OpenCPN as a chart, with no internet at sea. The program is called `chart-grabber-cli`. You type commands
into it; it has no windows. The Chart Grabber plugin for OpenCPN can run it for you:
[plugin page](https://github.com/Sea-For-Friends/chartgrabber_pi).

It does not show maps, plan routes or convert official vector charts.

## Systems it runs on

- Debian 12: 64-bit PC
- Debian 13: 64-bit PC and ARM 64-bit
- Raspberry Pi OS 64-bit (the "trixie" version, which is Debian 13): ARM 64-bit, uses the Debian 13 ARM package
- Ubuntu 24.04 and 26.04: 64-bit PC and ARM 64-bit
- Windows 10 and 11: 64-bit PC

## Download

| Your system | File |
|---|---|
| Debian 12, 64-bit PC | [chart-grabber-cli_2026.10.11_debian12_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_debian12_amd64.deb) |
| Debian 13, 64-bit PC | [chart-grabber-cli_2026.10.11_debian13_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_debian13_amd64.deb) |
| Debian 13, ARM 64-bit, and Raspberry Pi OS 64-bit | [chart-grabber-cli_2026.10.11_debian13_arm64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_debian13_arm64.deb) |
| Ubuntu 24.04, 64-bit PC | [chart-grabber-cli_2026.10.11_ubuntu2404_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2404_amd64.deb) |
| Ubuntu 24.04, ARM 64-bit | [chart-grabber-cli_2026.10.11_ubuntu2404_arm64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2404_arm64.deb) |
| Ubuntu 26.04, 64-bit PC | [chart-grabber-cli_2026.10.11_ubuntu2604_amd64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2604_amd64.deb) |
| Ubuntu 26.04, ARM 64-bit | [chart-grabber-cli_2026.10.11_ubuntu2604_arm64.deb](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli_2026.10.11_ubuntu2604_arm64.deb) |
| Windows 10 and 11, 64-bit PC | [chart-grabber-cli-2026.10.11-win64.zip](https://github.com/Sea-For-Friends/chart-grabber-cli/releases/download/v2026.10.11/chart-grabber-cli-2026.10.11-win64.zip) |

All files, and the checksums to verify them, are on the [releases page](https://github.com/Sea-For-Friends/chart-grabber-cli/releases).

## Install

1. Download the file for your system from the table.
2. Install it.
   - Debian, Ubuntu, Raspberry Pi OS: open a terminal in the folder with the file and run
     `sudo apt install ./chart-grabber-cli_*.deb`.
   - Windows: unzip the file. The [Windows guide](docs/user/windows-guide.md) shows each click.
3. Check that it runs. On Debian, Ubuntu and Raspberry Pi OS type `chart-grabber-cli --help`. On Windows run
   `chart-grabber-cli.exe --help` in the unzipped folder.

You then see a list of commands.

## Make your first chart

The program brings no map sources. You need a folder of map-source definitions and you give it to every command.
The [user guide](docs/user/cli-guide.md) explains how to list the sources, check the size of a download, download an
area and write the chart. Windows users start with the [Windows guide](docs/user/windows-guide.md).

Some map providers forbid bulk download in their terms of use. Read the terms of a source before you use it. You are
responsible for what you download.

## More documents

- [User guide](docs/user/cli-guide.md)
- [Windows guide](docs/user/windows-guide.md)
- [Write your own map source: tutorial](docs/user/sources-tutorial.md)
- [Write your own map source: reference](docs/user/sources-guide.md)
- [Changelog](CHANGELOG.md)
- [Building from source](BUILDING.md)
- [Security](SECURITY.md)

## About

Chart Grabber is an independent port of the tile downloader and chart tools of
[SAS.Planet](https://github.com/sasgis/sas.planet.src). It is not an official SAS.Planet project, and it is not
affiliated with or endorsed by SAS.Planet, OpenCPN or OpenCPN's Chart Downloader plugin. Thanks to the SAS.Planet
authors.

It is free software under the GNU General Public License, version 3 or later, like the original. See [LICENSE](LICENSE).
