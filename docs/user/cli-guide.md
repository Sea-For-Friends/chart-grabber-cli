# chart-grabber-cli: user guide

`chart-grabber-cli` downloads map tiles of an area from an online map source and writes them into an MBTiles file
that OpenCPN opens as a chart. It does not display maps, plan routes, or convert vector charts (S-57, S-63).

Contents: [Before the first chart](#before-the-first-chart-the-map-sources) ·
[1. Make a chart of an area](#1-how-to-make-an-offline-chart-of-an-area-for-opencpn) ·
[2. Choose a source](#2-how-to-choose-a-map-source) ·
[3. Check the size first](#3-how-to-check-how-big-a-download-will-be) ·
[4. Continue an interrupted download](#4-how-to-continue-an-interrupted-download) ·
[5. Update a chart](#5-how-to-update-a-chart-you-already-made) ·
[6. Chart along a route](#6-how-to-make-a-chart-along-a-route) ·
[7. Job files](#7-how-to-use-a-job-file-instead-of-long-command-lines) ·
[8. Proxy, offline machine](#8-how-to-work-behind-a-proxy-or-on-a-machine-without-internet) ·
[9. Cache and disk space](#9-how-to-see-and-clear-the-tile-cache) ·
[10. Check a source](#10-how-to-check-that-a-source-works) ·
[11. Scripts](#11-how-to-use-the-program-from-a-script) ·
[12. More map sources](#12-more-map-sources-the-full-catalogue-and-several-folders) ·
[13. Weather](#13-weather-the-current-picture) ·
[Reference](#reference) · [Troubleshooting](#troubleshooting)

## Before the first chart: the map sources

The program runs on Linux, and on 64-bit Windows 10 or later from the zip `chart-grabber-cli-<version>-win64.zip`
(unzip it into any folder and run `chart-grabber-cli.exe` from a terminal; the examples in this guide are Linux shell lines).
`windows-guide.md` next to this guide installs and uses the program step by step on Windows, with PowerShell and `cmd` lines.
The Windows zip decodes PNG, BMP, JPEG, WebP, GIF, TIFF, ICO and ICNS images.
The file `NOTICE` in the zip lists the third-party libraries that come with it and their licences; their texts are in `licenses\`.
`--version` shows which version you have:

```
$ chart-grabber-cli --version
chart-grabber-cli 2026.10.11 (5242f5140bcf)
```

A map source is a folder ending in `.zmp` (or a file ending in `.source.json`) that tells the program where a map's
tiles are on the internet. The program brings none. You need a folder of map-source definitions, for example from the
separate sources project, and you pass it to every command with `--maps`. The folder is searched including its
subfolders. SAS.Planet's own catalogue (section 12) is another source of such a folder. The examples keep the folder
name in a shell variable:

```
$ MAPS=~/map-sources
```

To write a source of your own, see `sources-guide.md` next to this guide. The step-by-step `sources-tutorial.md` comes with it.

Without `--maps` the program refuses to start (exit code 3):

```
$ chart-grabber-cli sources
error: no map sources given: pass --maps DIR, a folder of map-source definitions (repeat --maps for more folders)
```

`sources` lists what the program found in the folder. A source whose provider's terms may not allow downloading tiles
for offline use makes the program print a warning with the terms address each time you list or download from it; read
the terms before you use it. The warning comes from the source's definition. The listing below is from a folder that
holds the sources used in this guide:

```
$ chart-grabber-cli sources --maps="$MAPS"
warning: "Esri World Imagery": the provider's terms of use may not allow downloading tiles for offline use. Read https://www.arcgis.com/… before downloading; you are responsible for complying.
warning: "OpenTopoMap": the provider's terms of use may not allow downloading tiles for offline use. Read https://opentopomap.org/about before downloading; you are responsible for complying.
                                  GUID   kind                                                                     name  content type  EPSG  cache  verdict
{25E4232E-08A5-47C5-8D97-E38B5F418A2D}  layer         DWD weather radar (Germany, North Sea and western Baltic coasts)     image/png  3857     71       ok
{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}    map                        EMODnet Bathymetry (European seas, depth shading)     image/png  3857      0       ok
{C44877A0-D455-4238-B5E2-C8BFF25FC0D4}    map                                               EMODnet Bathymetry (world)     image/png  3857      0       ok
{7B743985-BC5F-4AB6-8915-AC5DBBB8F552}    map                                                       Esri World Imagery     image/jpg  3857      0       ok
{BC2EA862-0F3D-4933-8F71-00AFA0BAD114}  layer        EUMETSAT H SAF precipitation rate H60B (Europe, Africa, Atlantic)     image/png  3857     71       ok
{FE84A175-2C0F-420F-86DF-5900DE16A33C}    map  EUMETSAT MTG GeoColour (Europe, Africa, Atlantic; clouds day and night)     image/jpg  3857     71       ok
{92E40853-04A2-46B7-8A5B-3665FCDDFDBE}  layer                        NASA IMERG precipitation rate (30 min, worldwide)     image/png  3857     71       ok
{8B9C244E-E978-4267-8FE4-D703534C0789}    map                           NASA VIIRS NOAA-20 true colour (daily, clouds)     image/jpg  3857     71       ok
{076D1F9A-DC94-4184-B785-77362CD01B5B}  layer                              NOAA nowCOAST weather radar (contiguous US)     image/png  3857     71       ok
{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}  layer                                                      OpenSeaMap Seamarks     image/png  3857      0       ok
{51C1079B-B55D-4DF7-A410-ADC118F53AB1}    map                                                              OpenTopoMap     image/png  3857      0       ok
{1A696D82-F258-41A4-BCD6-7648D9E08DE2}  layer                                    RainViewer radar (worldwide, current)     image/png  3857     71       ok
{92122135-6B15-4800-AA9E-3E48C556DA17}    map                                          Sentinel-2 Cloudless 2025 (EOX)     image/jpg  3857      0       ok
{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}    map                                                       USGS Imagery (USA)     image/jpg  3857      0       ok
```

The examples use Sentinel-2 Cloudless for the picture and OpenSeaMap Seamarks for the sea marks. If your folder holds
other sources, use the names `sources` shows for it.

## 1. How to make an offline chart of an area for OpenCPN

Give the area, the zoom levels and the source; `run` downloads the tiles and writes the chart file. The examples
in this guide are run from your home directory, so the charts go to `~/charts`.

```
$ cd ~
$ mkdir -p charts
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --out=charts/
Sentinel-2 Cloudless 2025 (EOX)
 zoom  SAS z  tiles  bbox  present  marked missing          to download
   10     11      1     1     0.0%            0.0%   ~20.0 KB (assumed)
   11     12      1     1     0.0%            0.0%   ~20.0 KB (assumed)
   12     13      2     2     0.0%            0.0%   ~40.0 KB (assumed)
   13     14      6     6     0.0%            0.0%  ~120.0 KB (assumed)
   14     15     20    20     0.0%            0.0%  ~400.0 KB (assumed)
   15     16     70    70     0.0%            0.0%    ~1.4 MB (assumed)
total           100   100                             ~2.0 MB (assumed)
Sentinel-2 Cloudless 2025 (EOX)  z11  0/100 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Sentinel-2 Cloudless 2025 (EOX)  z16  100/100 (100%)  downloaded 100 tiles 479.2 KB  left ~0.0 KB ~00:00:00
Sentinel-2 Cloudless 2025 (EOX)
 zoom  tiles  present  marked missing  missing      size
   10      1        1               0        0   15.1 KB
   11      1        1               0        0   10.9 KB
   12      2        2               0        0   15.1 KB
   13      6        6               0        0   40.4 KB
   14     20       20               0        0  101.5 KB
   15     70       70               0        0  296.2 KB
total    100      100               0        0  479.2 KB
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/Sentinel2-Cloudless-2025.mbtiles  100 tiles  564.0 KB
```

- `--bbox` is the area as west, south, east, north in decimal degrees, with `.` as the decimal point. Longitude comes
  first: W,S,E,N. West and south are negative. The program cannot tell a swapped pair from a real area; check the
  tile count in the estimate. The example is the Cowes end of the Solent.
- To find the numbers for your own area, point at its south-west and north-east corners in OpenCPN and read the
  cursor position in the status bar, or read them off any web map. Convert degrees and minutes to decimal degrees:
  50° 44.4' N is 50.74, 1° 20.4' W is -1.34.
- A tile is a 256 × 256 pixel picture. A zoom level is a scale step: level 0 shows the whole world in one tile, and
  each next level has twice the detail and four times as many tiles. Levels are counted from 0, as OpenCPN counts
  them; the `SAS z` column and the `z11` in the progress line count the same levels from 1.
- At 50° N one pixel is about 97 m at level 10, 6 m at level 14, 1.5 m at level 16 and 0.75 m at level 17. Levels
  10–14 suit a passage, 15–17 an anchorage or a harbour.
- `--zooms` takes a range (`10-15`), single levels (`12,14`) or both (`8-12,14`).
- `--out` is a file name ending in `.mbtiles`, or a directory. With a directory the file is named after the source's
  definition: `Sentinel2-Cloudless-2025.mbtiles` for the file `Sentinel2-Cloudless-2025.source.json` or for a folder
  `Sentinel2-Cloudless-2025.zmp`. The directory must exist.
- The first table is the estimate (section 3). The second is the fill report: per level, how many tiles are now in
  the cache, how many the server marked as not existing, and how many are still missing. `missing` tiles are ones
  the server did not deliver, for example where it has no picture; `-v` shows the server's answer for each tile.
- The progress line reads: source, level being worked (counted from 1), tiles checked of all tiles (percent), tiles
  and bytes downloaded by this run, and the estimated bytes and time left. In a terminal it is rewritten once a
  second. When the output is not a terminal, as here, a line is printed every 30 seconds and once at the end, so a
  short run shows the first and the last. At the start it shows `left ~~ Kb ~~` until there is something to measure.
  The `export` line is the progress of writing the chart file.
- Speed depends on the server. In the runs for this guide it was 3 to 12 tiles a second; at 5 tiles a second, 10,000
  tiles take about half an hour.
- `written:` gives the full path, the number of tiles and the file size. The file only appears when it is complete.

To add sea marks on top of the picture, give a transparent layer with `--overlay`. It goes into a second file next to
the first, with `-overlay` added to its name:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --overlay="OpenSeaMap Seamarks" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --out=charts/solent.mbtiles
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/solent.mbtiles  100 tiles  564.0 KB
OpenSeaMap Seamarks  export  100%
written: …/charts/solent-overlay.mbtiles  100 tiles  320.0 KB
```

To load the files in OpenCPN: Options > Charts > Chart Files > Add Directory, choose the `charts` directory, tick
"Scan Charts and Update Database", then OK. The chart's name in OpenCPN is the source name, or the text given with
`--name="…"`. The file also carries the source's credit line (its licence text) as attribution; `--attribution="…"`
replaces it.

## 2. How to choose a map source

For sailing, put the sea marks over a picture of the surface: Sentinel-2 Cloudless shows anchorages, reefs and
harbour detail but no depths, and OpenSeaMap Seamarks adds buoys, lights and harbour symbols. The EMODnet maps show
sea depth. The weather sources are in section 13.

List the sources whose name contains a word; the search ignores case.

```
$ chart-grabber-cli sources --maps="$MAPS" --filter=Sea
                                  GUID   kind                                                              name  content type  EPSG  cache  verdict
{25E4232E-08A5-47C5-8D97-E38B5F418A2D}  layer  DWD weather radar (Germany, North Sea and western Baltic coasts)     image/png  3857     71       ok
{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}    map                 EMODnet Bathymetry (European seas, depth shading)     image/png  3857      0       ok
{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}  layer                                               OpenSeaMap Seamarks     image/png  3857      0       ok
```

- `kind` is `map` for a base chart (`--source`) and `layer` for a transparent overlay (`--overlay`).
- `content type` is the picture format, `EPSG` the projection code and `cache` the way tiles are stored; they matter
  only through the verdict.
- Pass a source by its full name or by its GUID (`--source=92122135-6B15-4800-AA9E-3E48C556DA17`). If two sources
  have the same name, the name is refused and you must use the GUID.
- `verdict` is `ok` when the program can make a chart from the source. A source from a larger folder (section 12)
  can say why it cannot be used.

Some sources' terms of use forbid bulk download or offline use. You are responsible for checking the terms of the
source you use.

A few sources show sea depth or chart data that is not an official nautical chart. For those, `download`, `run` and
`resume` print a "not for navigation" notice once per source, before the first tile is requested; `-q` does not hide
it. `sources` and `export` do not print it. In `sources --format=tsv` its field 14 is `1` for such a source (section
11). The sources project marks its depth sources (EMODnet, GEBCO, NOAA BlueTopo) and its Finnish, Norwegian and NOAA
sea charts this way.

```
$ chart-grabber-cli download --maps="$MAPS" --source="EMODnet Bathymetry (world)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=6-7
EMODnet Bathymetry (world)
 zoom  SAS z  tiles  bbox  present  marked missing         to download
    6      7      1     1     0.0%            0.0%  ~20.0 KB (assumed)
    7      8      1     1     0.0%            0.0%  ~20.0 KB (assumed)
total             2     2                           ~40.0 KB (assumed)
warning: "EMODnet Bathymetry (world)": not for navigation. This source is not an official nautical chart; do not use it to navigate.
EMODnet Bathymetry (world)  z7  0/2 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
…
```


## 3. How to check how big a download will be

`estimate` counts the tiles and the bytes before you download anything. It does not use the network.

```
$ chart-grabber-cli estimate --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.60,50.60,-0.90,50.90 --zooms=10-17
Sentinel-2 Cloudless 2025 (EOX)
 zoom  SAS z  tiles   bbox  present  marked missing  to download
   10     11      6      6    16.7%            0.0%     ~75.7 KB
   11     12     20     20     5.0%            0.0%    ~207.1 KB
   12     13     54     54     3.7%            0.0%    ~393.6 KB
   13     14    187    187     3.2%            0.0%      ~1.2 MB
   14     15    726    726     2.8%            0.0%      ~3.4 MB
   15     16   2860   2860     2.3%            0.0%     ~10.3 MB
   16     17  11352  11352     0.0%            0.0%     ~61.7 MB
   17     18  44544  44544     0.0%            0.0%    ~242.3 MB
total         59749  59749                             ~319.6 MB
```

- This is the whole Solent from the Needles to Selsey. Level 17 alone is three quarters of the job; dropping one
  level divides the download by about four.
- `present` is the share of tiles already in the cache (section 9); here, the tiles of section 1. Only the rest
  is downloaded.
- `to download` is the expected size. `(assumed)` after it means the cache holds no tile of this source yet, so the
  program assumes 20 KB per tile; change the guess with `--assume-tile-kb=N`. Once some tiles are cached, it uses
  their real mean size.
- `bbox` counts the tiles of the rectangle around the area. It differs from `tiles` only for a route (section 6).

## 4. How to continue an interrupted download

Run the same command again: the program continues where it stopped.

Pressing Ctrl-C saves the download state and leaves no half-written chart file:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-17 --out=charts/ --overwrite
…
Sentinel-2 Cloudless 2025 (EOX)  z11  0/1216 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Sentinel-2 Cloudless 2025 (EOX)  z17  225/1216 (19%)  downloaded 125 tiles 270.1 KB  left ~2.1 MB ~00:01:28
warning: session saved; run the same command again to continue
warning: to continue from this file: resume …/AutoSave/c490d7b70273/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
```

The same command, run again:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-17 --out=charts/ --overwrite
…
   16     17    228   228    54.8%            0.0%    ~222.6 KB
   17     18    888   888     0.0%            0.0%      ~2.9 MB
total          1216  1216                               ~3.1 MB
warning: continuing the session of 2026-10-18 16:24:11
Sentinel-2 Cloudless 2025 (EOX)  z17  225/1216 (19%)  downloaded 125 tiles 270.1 KB  left ~2.1 MB ~00:01:28
…
Sentinel-2 Cloudless 2025 (EOX)  z18  1216/1216 (100%)  downloaded 1116 tiles 2.7 MB  left ~0.0 KB ~00:00:00
Sentinel-2 Cloudless 2025 (EOX)
 zoom  tiles  present  marked missing  missing      size
…
   16    228      228               0        0  744.5 KB
   17    888      888               0        0    2.0 MB
total   1216     1216               0        0    3.2 MB
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/Sentinel2-Cloudless-2025.mbtiles  1216 tiles  3.9 MB
```

- "The same command" means the same source, zoom levels, area, `--threads` and replace options. Change one of them
  and the program starts a new download; tiles already in the cache are still not downloaded again.
- `warning: continuing the session of …` shows that the saved state was found and when it was saved.
- A continued download fetches the unfinished part again, including the tiles that arrived before Ctrl-C. Here 54.8%
  of level 16 was in the cache before the run, yet at `225/1216` the run had skipped the 100 tiles of levels 10–15
  and downloaded the other 125, all of level 16.
- To throw the saved state away and scan the area from the start, add `--restart`. Tiles already in the cache are
  checked, not downloaded again.

`resume` continues from the saved state file that the warning names, without repeating the options. It downloads
only; make the chart afterwards with `export` or `run`. A `download` of the area east of Cowes, stopped with Ctrl-C:

```
$ chart-grabber-cli download --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.24,50.75,-1.20,50.775 --zooms=17
…
Sentinel-2 Cloudless 2025 (EOX)  z18  1/256 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~00:00:00
Sentinel-2 Cloudless 2025 (EOX)  z18  70/256 (27%)  downloaded 65 tiles 63.3 KB  left ~181.1 KB ~00:00:26
warning: session saved; run the same command again to continue
warning: to continue from this file: resume …/AutoSave/b0ceb7d4240b/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
$ chart-grabber-cli resume --maps="$MAPS" ~/.local/share/chart-grabber/AutoSave/b0ceb7d4240b/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
warning: continuing the session of 2026-10-18 16:27:06
Sentinel-2 Cloudless 2025 (EOX)  z18  70/256 (27%)  downloaded 65 tiles 63.3 KB  left ~181.1 KB ~00:00:26
Sentinel-2 Cloudless 2025 (EOX)  z18  256/256 (100%)  downloaded 240 tiles 231.8 KB  left ~0.0 KB ~00:00:00
Sentinel-2 Cloudless 2025 (EOX)
 zoom  tiles  present  marked missing  missing      size
   17    256      256               0        0  248.5 KB
total    256      256               0        0  248.5 KB
```

A download that finishes, with `run`, `download` or `resume`, deletes its saved state. Running the original command
afterwards starts a new download and fetches nothing that is already in the cache.

## 5. How to update a chart you already made

Run the same command again with `--overwrite`. Tiles already in the cache are not downloaded again, so this only
rebuilds the file:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --out=charts/ --overwrite
…
total           100   100                               ~0.0 KB
…
written: …/charts/Sentinel2-Cloudless-2025.mbtiles  100 tiles  564.0 KB
```

To fetch fresh pictures from the server, add `--replace`; every tile of the area is downloaded again:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --replace --out=charts/ --overwrite
…
total           100   100                             ~479.2 KB
…
written: …/charts/Sentinel2-Cloudless-2025.mbtiles  100 tiles  564.0 KB
```

- With `--replace` the estimate counts the tiles already in the cache as to download, here 479.2 KB.
- `--replace --replace-older-than=2026-10-01` replaces only tiles cached before that date. Without `--replace`, the
  date option does nothing.
- `--replace --replace-if-size-differs` replaces a tile only when the server's copy has a different size.
- Without `--overwrite` an existing chart file is never touched (see Troubleshooting).

## 6. How to make a chart along a route

Give a GPX file and the width of the corridor; only the tiles along the route are downloaded.

```
$ cat yarmouth-cowes.gpx
<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="OpenCPN" xmlns="http://www.topografix.com/GPX/1/1">
  <rte>
    <name>Yarmouth to Cowes</name>
    <rtept lat="50.7080" lon="-1.4990"><name>Yarmouth</name></rtept>
    <rtept lat="50.7330" lon="-1.4300"><name>Hurst Road</name></rtept>
    <rtept lat="50.7680" lon="-1.3300"><name>Gurnard</name></rtept>
    <rtept lat="50.7660" lon="-1.2970"><name>Cowes</name></rtept>
  </rte>
</gpx>
$ chart-grabber-cli estimate --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --route=yarmouth-cowes.gpx --corridor-nm=1 --zooms=10-15
Sentinel-2 Cloudless 2025 (EOX)
 zoom  SAS z  tiles  bbox  present  marked missing  to download
   10     11      4     4    25.0%            0.0%     ~45.4 KB
   11     12      4     4    25.0%            0.0%     ~32.7 KB
   12     13      6     8    33.3%            0.0%     ~30.3 KB
   13     14     13    24    30.8%            0.0%     ~68.0 KB
   14     15     30    72    20.0%            0.0%    ~153.2 KB
   15     16     90   276    23.3%            0.0%    ~297.9 KB
total           147   388                             ~627.5 KB
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --route=yarmouth-cowes.gpx --corridor-nm=1 --zooms=10-15 --out=charts/yarmouth-cowes.mbtiles
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/yarmouth-cowes.mbtiles  147 tiles  716.0 KB
```

- `--corridor-nm` is the total width in nautical miles: `1` means half a mile on each side of the route.
- Export the route from OpenCPN's Route Manager as GPX. A track works too. The program takes the first route of the
  file, or the first track if there is no route; choose another one by its name with `--route-name="…"`.
- An area can also be a polygon drawn in SAS.Planet and saved as a `.hlg` file: `--polygon=FILE.hlg`. Give only one of
  `--bbox`, `--route` and `--polygon`.

## 7. How to use a job file instead of long command lines

Put the options into an INI-format file, called `solent.conf` here, and pass it with `--job`. Any file name works; `.ini` files from older versions work unchanged.

```
$ cat solent.conf
[Job]
Source=Sentinel-2 Cloudless 2025 (EOX)
Overlay=OpenSeaMap Seamarks
BBox=-1.34,50.74,-1.24,50.78
Zooms=10-15
Out=charts/solent.mbtiles
Name=Solent, Cowes
Overwrite=1

[Download]
Threads=2
$ chart-grabber-cli run --maps="$MAPS" --job=solent.conf
…
warning: --threads 2 lowered to 1: the source allows 1 connections
…
Solent, Cowes  export  100%
written: …/charts/solent.mbtiles  100 tiles  564.0 KB
OpenSeaMap Seamarks  export  100%
written: …/charts/solent-overlay.mbtiles  100 tiles  320.0 KB
```

- A key is the option name without the dashes, written in CamelCase: `--corridor-nm` becomes `CorridorNm`,
  `--bbox` becomes `BBox`. Area, source and output keys go under `[Job]`, download options under `[Download]`.
  `--help` shows which option belongs where.
- A relative path in the file is relative to the file's directory, not to where you run the command.
- A map source can limit the connections it accepts. The program then lowers `--threads` for that source and says so,
  as above; the other source keeps 2.
- An option on the command line wins over the file:

```
$ chart-grabber-cli estimate --maps="$MAPS" --job=solent.conf --zooms=16
Sentinel-2 Cloudless 2025 (EOX)
 zoom  SAS z  tiles  bbox  present  marked missing  to download
   16     17    228   228   100.0%            0.0%      ~0.0 KB
total           228   228                               ~0.0 KB
OpenSeaMap Seamarks
 zoom  SAS z  tiles  bbox  present  marked missing        to download
   16     17    228   228     0.0%            0.0%  ~4.5 MB (assumed)
total           228   228                           ~4.5 MB (assumed)
```

- A misspelt key stops the program (exit code 2):

```
$ cat bad.conf
[Job]
Area=1
$ chart-grabber-cli estimate --maps="$MAPS" --job=bad.conf
error: unknown key "Area" in [Job] of bad.conf
Usage: chart-grabber-cli COMMAND [options]; chart-grabber-cli --help
```

## 8. How to work behind a proxy or on a machine without internet

**Proxy.** The program uses the `https_proxy` and `http_proxy` environment variables; `--proxy=URL` overrides them.
On Windows, when no such variable is set, the system proxy of the current user (Internet Settings, HKCU) is used too.
The form is `[http|https|socks4|socks4a|socks5|socks5h://][user:pass@]host[:port]`. The program never asks for a
password. When the proxy does not answer, nothing moves and the download stops after 300 seconds; set a shorter wait
with `--stall-timeout=SEC`. When the network goes away or the server answers errors for 60 tiles in a row, the
download stops with exit 4 as well (`--max-connection-errors=N` changes the count, 0 = never). Each failed tile
waits the 30-second pause before its second try: on a refused port a failed tile takes about 45 seconds, and a server
that does not answer at all costs up to two timeouts (40 seconds each) plus 30 seconds, so with one connection the
default 60 takes about 45 minutes. A lower count stops sooner. Run the
same command again to continue. A proxy that does not answer:

```
$ chart-grabber-cli download --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.40,50.70,-1.38,50.71 --zooms=17 --proxy=http://127.0.0.1:3128 --stall-timeout=20
…
Sentinel-2 Cloudless 2025 (EOX)  z18  0/48 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
error: no progress for 20 s: Downloading...
warning: session saved; run the same command again to continue
warning: to continue from this file: resume …/AutoSave/043be990885f/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
Sentinel-2 Cloudless 2025 (EOX)
 zoom  tiles  present  marked missing  missing    size
   17     48        0               0       48  0.0 KB
total     48        0               0       48  0.0 KB
```

When the server is gone (here with `--max-connection-errors=2`):

```
$ chart-grabber-cli download --maps="$MAPS" --cache=c11 --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.33,50.745 --zooms=16 --max-connection-errors=2 --no-progress
…
error: no connection to the server for 2 tiles in a row: Error connecting to server
Wait 5 seconds...
warning: session saved; run the same command again to continue
warning: to continue from this file: resume …/AutoSave/5e7e5d341dd2/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
```

**Machine without internet.** Download into a cache on a USB stick on a connected machine with `download`, then make
the chart on the boat's machine with `export`, which needs no network. Give both the same `--cache`:

```
$ chart-grabber-cli download --maps="$MAPS" --cache=usb/chart-cache --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-14
…
total     30       30               0        0  183.0 KB
```

On the offline machine, with the stick mounted at the same relative path:

```
$ chart-grabber-cli export --maps="$MAPS" --cache=usb/chart-cache --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-14 --out=charts/from-usb.mbtiles
…
total            30    30                               ~0.0 KB
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/from-usb.mbtiles  30 tiles  220.0 KB
```

The offline machine needs the same map folders, given with `--maps`.

## 9. How to see and clear the tile cache

Every downloaded tile is kept in a cache, `~/.local/share/chart-grabber/cache_sqlite/`, one directory per source, besides
the copy in the chart file. Later charts of the same source reuse it, so nothing is downloaded twice; it also takes
disk space. See how much:

```
$ (cd ~/.local/share/chart-grabber/cache_sqlite && du -sh *)
288K	EMODnet_Bathymetry_World
392K	Osm_OpenSeaMap_Layer_Seamark
4.8M	Sentinel2_Cloudless_2025
```

Deleting it is safe: charts already written are separate files and keep working. Delete one source's directory, or
the whole `cache_sqlite` directory:

```
$ rm -r ~/.local/share/chart-grabber/cache_sqlite/Osm_OpenSeaMap_Layer_Seamark
```

The next chart of that source downloads its tiles again. To keep the cache somewhere else, for example on a larger
disk, give `--cache=DIR` to every command (section 8).

## 10. How to check that a source works

`probe` downloads one tile from each source whose name contains the filter text, and prints whether it worked:

```
$ chart-grabber-cli probe --maps="$MAPS" --filter=Sea --at=-1.29,50.76 --probe-zoom=12
                                  GUID                                                              name              result
{25E4232E-08A5-47C5-8D97-E38B5F418A2D}  DWD weather radar (Germany, North Sea and western Baltic coasts)   ok 1784 image/png
{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}                 EMODnet Bathymetry (European seas, depth shading)  ok 52529 image/png
{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}                                               OpenSeaMap Seamarks  ok 12535 image/png
```

- `--at` is the point of the test tile (longitude, latitude; default `0,0`) and `--probe-zoom` its level (default 10).
  Pick a point inside your area: some sources have no tiles over open sea or outside their region.
- `ok` is followed by the tile's size in bytes and its picture format. Otherwise the result is the reason of the
  failure. USGS Imagery covers the United States only, so a point in the Solent fails and a point in Washington works:

```
$ chart-grabber-cli probe --maps="$MAPS" --filter=USGS --at=-1.29,50.76 --probe-zoom=12
                                  GUID                name                          result
{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}  USGS Imagery (USA)  not exists: HTTP 404 Not Found
$ echo $?
4
$ chart-grabber-cli probe --maps="$MAPS" --filter=USGS --at=-77.04,38.89 --probe-zoom=12
                                  GUID                name               result
{E05EDD5E-C6C3-483C-8BA7-8DE1D9892ED8}  USGS Imagery (USA)  ok 24000 image/jpeg
```

- The exit code is 0 when every probed source worked, 4 otherwise (4 in the first USGS example).
- Sources that cannot be downloaded at all are skipped.
- The test tile is stored in the cache like any downloaded tile.

## 11. How to use the program from a script

Results go to standard output, progress and messages to standard error, so the output can be piped:

```
$ chart-grabber-cli run --maps="$MAPS" --job=solent.conf --no-progress 2>/dev/null | grep "^written:"
written: …/charts/solent.mbtiles  100 tiles  564.0 KB
written: …/charts/solent-overlay.mbtiles  100 tiles  320.0 KB
```

- Check the exit code (table in the Reference). 0 means the chart was written.
- A finished download may still miss tiles the server does not have. `--strict` turns that into exit code 4. This
  source has no tiles at level 20:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.2601,50.7601,-1.2600,50.7602 --zooms=20 --out=charts/z20.mbtiles --strict -q
…
total      2        0               0        2  0.0 KB
error: tiles are missing in the cache (--strict)
```

- `-q` prints only errors and results; `--no-progress` drops the progress line; `-v` adds every line of the download
  log (each tile requested and the server's answer).

**Machine-readable results.** With `--format=tsv` (the default is `--format=text`, the output shown in this guide)
standard output carries only records: one per line, fields separated by a TAB, UTF-8, no header. The first field names the record. Numbers have no thousands separator; shares have 4 decimals with `.`; yes/no
fields are `1`/`0`; zoom levels count from 0. A TAB or line break inside a value becomes a space. New fields are only
ever added at the end of a record, so read fields by position and ignore extra ones and unknown record types.

| Record | Fields after the record name | Printed by |
|---|---|---|
| `source` | GUID, kind (`map`/`layer`), name, content type, EPSG code, cache type code, verdict (`ok` or the reason), map source file, group (may be empty), origin (always `user`; kept for older scripts), refresh (`snapshot` for a weather source, section 13, `static` otherwise), licence text (may be empty), highest zoom (empty when the source sets none), navigationWarning (`1` when the source prints the "not for navigation" notice, section 2, else `0`) | `sources` |
| `estimate` | GUID, zoom, tiles, bbox tiles, share present, share marked missing, bytes to download, assumed (`1` when the size is a guess), expected size when complete (every tile of the zoom that is not marked missing, at the mean tile size; it is not what is on disk now, and equals the bytes to download on an empty cache) | `estimate`, `download`, `export`, `run`; one per zoom |
| `fill` | GUID, zoom, tiles, present, marked missing, missing, bytes | `download`, `run`, `resume`; one per zoom |
| `session` | GUID, time of the saved state (`yyyy-MM-dd HH:mm:ss`), full path of the `.sls` file | `download`, `run`, `resume` when a download continues |
| `snapshot` | GUID, time of the weather frame in UTC (`yyyy-MM-ddTHH:mm:ssZ`, only the date for a daily picture, empty when unknown), the provider's name of the frame | `download`, `run`, `export` (before the `estimate` records of a weather source), `resume`; a second record follows when the program steps back to an earlier frame (see the weather section): the last `snapshot` record of a source names the frame used |
| `written` | GUID, full path of the `.mbtiles` file, tiles, file size in bytes, time of the weather frame (empty for other sources) | `export`, `run` |

In the examples below `→` stands for a TAB.

```
$ chart-grabber-cli sources --maps="$MAPS" --format=tsv --filter=Sea
source→{25E4232E-08A5-47C5-8D97-E38B5F418A2D}→layer→DWD weather radar (Germany, North Sea and western Baltic coasts)→image/png→3857→71→ok→Weather now/DWD-Radar-Germany.source.json→Weather now→user→snapshot→Source: Deutscher Wetterdienst (DWD), CC BY 4.0→9→0
source→{91D4F1B4-A7CD-4FC8-95F1-637609068FB2}→map→EMODnet Bathymetry (European seas, depth shading)→image/png→3857→0→ok→Depth/EMODnet-Bathymetry-Europe.source.json→Depth→user→static→EMODnet Bathymetry Consortium→→1
source→{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}→layer→OpenSeaMap Seamarks→image/png→3857→0→ok→Seamarks and traffic/OpenSeaMap-Seamarks.source.json→Seamarks and traffic→user→static→Seamarks (c) OpenSeaMap contributors, CC-BY-SA→→0
$ chart-grabber-cli run --maps="$MAPS" --job=solent.conf --format=tsv 2>/dev/null
estimate→{92122135-6B15-4800-AA9E-3E48C556DA17}→10→1→1→1.0000→0.0000→0→0→15495
…
estimate→{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}→15→70→70→1.0000→0.0000→0→0→146546
fill→{92122135-6B15-4800-AA9E-3E48C556DA17}→10→1→1→0→0→15495
…
fill→{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}→15→70→70→0→0→146546
written→{92122135-6B15-4800-AA9E-3E48C556DA17}→…/charts/solent.mbtiles→100→577536→
written→{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}→…/charts/solent-overlay.mbtiles→100→327680→
```

**Progress records.** `--progress=lines` replaces the progress line on standard error by records in the same
format; `--progress=off` equals `--no-progress`, and `--progress=auto` is the default. Warnings and errors still appear as their own lines;
they never start with `phase` or `progress` followed by a TAB.

| Record | Fields | When |
|---|---|---|
| `phase` | step (`estimate`, `download`, `fill`, `export`), GUID | when that step starts for a source |
| `progress` | `download`, GUID, zoom, tiles processed, tiles in total, tiles downloaded, bytes downloaded, seconds elapsed | about once a second during a download, and once at its end |
| `progress` | `export`, GUID, per mille done (0–1000) | during the export, and `1000` once the file is written |

A download into an empty cache, with standard output discarded:

```
$ chart-grabber-cli download --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --cache=fresh-cache --format=tsv --progress=lines >/dev/null
phase→estimate→{92122135-6B15-4800-AA9E-3E48C556DA17}
phase→download→{92122135-6B15-4800-AA9E-3E48C556DA17}
progress→download→{92122135-6B15-4800-AA9E-3E48C556DA17}→10→0→100→0→0→0
progress→download→{92122135-6B15-4800-AA9E-3E48C556DA17}→12→3→100→3→33275→1
progress→download→{92122135-6B15-4800-AA9E-3E48C556DA17}→14→10→100→10→83541→2
…
progress→download→{92122135-6B15-4800-AA9E-3E48C556DA17}→15→100→100→100→490722→15
progress→download→{92122135-6B15-4800-AA9E-3E48C556DA17}→15→100→100→100→490722→15
phase→fill→{92122135-6B15-4800-AA9E-3E48C556DA17}
```

## 12. More map sources: the full catalogue and several folders

The SAS.Planet map catalogue has about 500 sources. You can also keep sources of your own, in more than one folder.
The terms of use of those sources are your responsibility: some forbid bulk download or offline use, and the program
does not check them.

**The full catalogue.** The catalogue is a download from the SAS.Planet website. Open
https://www.sasgis.org/download1/archive.pl in a browser. The page is in Russian. Under the heading "Наборы карт"
(map sets), in the line "полный набор карт" (full map set), click "СКАЧАТЬ" (download). The link points to
`http://parasite.kicks-ass.org:3000/sasgis/maps/archive/master.zip` (plain http). The browser saves `maps-master.zip`
(about 2.7 MB) into `~/Downloads`. Then unpack it and point `--maps` at the folder:

```
unzip -q ~/Downloads/maps-master.zip -d ~/Downloads
```

```
$ chart-grabber-cli sources --maps ~/Downloads/maps | wc -l
515
```

The first line is the header, the other 514 are sources.

**Several folders.** Give `--maps` once for each folder. The program reads them in the order given. If a source with
the same GUID is in more than one folder, the later folder wins and the program prints one warning for that GUID. Put
your own folder last. Inside one folder, a second source with a GUID that is already there is an error for the later
file.

```
$ chart-grabber-cli sources --maps ~/Downloads/maps --maps ~/map-sources | wc -l
warning: map source {AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}: …/map-sources/Seamarks and traffic/OpenSeaMap-Seamarks.source.json replaces …/Downloads/maps/__anygis_filegenerator/Osm_OpenSeaMap_Layer_Seamark.zmp
warning: map source {51C1079B-B55D-4DF7-A410-ADC118F53AB1}: …/map-sources/Coast and land/OpenTopoMap.source.json replaces …/Downloads/maps/__anygis_filegenerator/Osm_Topo_Map.zmp
warning: map source {7B743985-BC5F-4AB6-8915-AC5DBBB8F552}: …/map-sources/Satellite/Esri-World-Imagery.source.json replaces …/Downloads/maps/ESRI/ArcGIS.Imagery.zmp
warning: "Esri World Imagery": the provider's terms of use may not allow downloading tiles for offline use. Read https://www.arcgis.com/… before downloading; you are responsible for complying.
warning: "OpenTopoMap": the provider's terms of use may not allow downloading tiles for offline use. Read https://opentopomap.org/about before downloading; you are responsible for complying.
526
```

- The folder of the sources project comes last, so its OpenSeaMap Seamarks, OpenTopoMap and Esri World Imagery
  replace the catalogue's: three warnings, one per GUID. The two terms-of-use warnings are a different matter (first
  section).
- The count: 514 sources from the catalogue and 14 from the other folder, minus the 3 replaced ones, is 525, and the
  header line makes 526.

`resume` needs the same `--maps` folders as the download it continues. In a job file `--maps` is not a key; give it on
the command line.

US waters: NOAA publishes ready MBTiles, updated weekly, https://distribution.charts.noaa.gov/ncds/index.html ; open
them in OpenCPN directly (no download by this program).

**Sources the program cannot use.** In the catalogue some sources are listed with a `verdict` that says why a chart
cannot be made from them:

```
$ chart-grabber-cli sources --maps ~/Downloads/maps --filter="Yandex - Map"
                                  GUID  kind                    name  content type  EPSG  cache                                                                                                 verdict
{8238C84A-D37E-45E1-A735-FBCFBCD4168C}   map            Yandex - Map     image/png  3395      0            the projection of "Yandex - Map" is EPSG:3395; only EPSG:3857 (Web Mercator) can be exported
{38E29DB4-0051-4D08-836C-936325F0B63B}   map  Yandex - Map (English)     image/png  3395      0  the projection of "Yandex - Map (English)" is EPSG:3395; only EPSG:3857 (Web Mercator) can be exported
```

The reasons you will see in the full list:

| Verdict says | Why the source is refused |
|---|---|
| `the projection of "…" is EPSG:…; only EPSG:3857 (Web Mercator) can be exported` | OpenCPN reads MBTiles only in the Web Mercator projection; the tiles of this source would land in the wrong place. |
| `the tiles of "…" are stored as "image/gif"; only PNG, JPEG and WebP can be exported` | The picture format is not one an MBTiles chart can carry. |
| `"…" has crop or cut after download switched on; this is not supported` | The source changes its tiles after download; the program does not do that. |
| `"…" does not hold bitmap tiles` | The source is not a picture map. |

When a map and a layer, or two maps, share a name, as the two `Meta.ua` maps do, the name is refused and you must use
the GUID: `--source=4D31E660-5D1B-48DE-B413-43E85B8DBB37`.

## 13. Weather: the current picture

Weather sources show the situation now. A map source of this kind is a snapshot source: each `run` or `download`
takes the newest picture the provider has (one frame) and keeps it. Run the command again an hour later and you get a
newer frame. The frame's time is the end of the `written:` line.

The README of the sources project lists its weather sources with their credits and highest zoom. The examples below
use RainViewer and NASA VIIRS.

A credit may contain `{year}`; it becomes the year of the frame you downloaded. The zoom counts from 0, like
`--zooms`. A zoom above the maximum is skipped with a warning. The radar and rain sources are transparent layers: put
them over a picture with `--overlay`. VIIRS and GeoColour are pictures of their own.

Rain radar over the Solent, on top of the satellite picture:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --overlay="RainViewer radar (worldwide, current)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-9 --out=charts/solent-weather.mbtiles
warning: "RainViewer radar (worldwide, current)": zooms 8,9 skipped, the source stops at zoom 7
…
RainViewer radar (worldwide, current)
snapshot 2026-10-20 16:50 UTC
 zoom  SAS z  tiles  bbox  present  marked missing         to download
    5      6      1     1     0.0%            0.0%  ~20.0 KB (assumed)
    6      7      1     1     0.0%            0.0%  ~20.0 KB (assumed)
    7      8      1     1     0.0%            0.0%  ~20.0 KB (assumed)
total             3     3                           ~60.0 KB (assumed)
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/solent-weather.mbtiles  5 tiles  96.0 KB
RainViewer radar (worldwide, current)  export  100%
written: …/charts/solent-weather-overlay.mbtiles  3 tiles  72.0 KB  snapshot 2026-10-20 16:50 UTC
```

The `snapshot` line, before the tables, gives the time of the frame. `estimate` does not ask the provider and says
`snapshot: taken when downloading` instead. Ten minutes later the same command with `--overwrite` takes the next one:

```
$ chart-grabber-cli run --maps="$MAPS" --source="Sentinel-2 Cloudless 2025 (EOX)" --overlay="RainViewer radar (worldwide, current)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-9 --out=charts/solent-weather.mbtiles --overwrite
…
total             5     5                               ~0.0 KB
RainViewer radar (worldwide, current)
snapshot 2026-10-20 17:00 UTC
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: …/charts/solent-weather.mbtiles  5 tiles  96.0 KB
RainViewer radar (worldwide, current)  export  100%
written: …/charts/solent-weather-overlay.mbtiles  3 tiles  68.0 KB  snapshot 2026-10-20 17:00 UTC
```

The MBTiles `name` of a base map from a weather source carries the frame time (`NASA VIIRS NOAA-20 true colour (daily, clouds) 2026-10-20`, with the hour and minute for a picture taken several times a day), so repeated exports have different chart names; `--name` is used as given for the base map. A weather layer exported as an overlay carries the frame time in its name the same way (`--name` does not name an overlay); an overlay of a static source keeps the plain source name. The MBTiles `description` of both names the frame: `RainViewer radar (worldwide, current), snapshot 2026-10-20 16:50 UTC`. A chart from a static source has the description `Created by chart-grabber-cli <version>`; `--description` replaces either text.

The satellite picture does not change, so it is not downloaded again (section 5). When a weather download is
complete, the program removes the older frames of the same area from the cache, so the cache holds one frame.

A provider often lists a new frame before it serves its tiles. When the newest frame the provider lists is less than
60 minutes old and none of its tiles is served, the program goes one frame back. "Not served" means the answers are
404, the provider's empty or ban tile, or an HTTP 5xx error such as 502; a connection that gives no answer does not
count. An HTTP 5xx or ban answer repeats the same tile, so after three of them in a row the download stops and the
frame before it is taken; for 404 and the empty tile the program waits until the download of the frame has finished with
nothing served. It then downloads the earlier frame and says so on standard error:
`warning: the newest frame 2026-10-23 08:50 UTC of "Clouds" is not served yet; using the previous frame 2026-10-23 08:40 UTC`.
Only one step back is made. In `--format=tsv` a second `snapshot` record follows and the last one names the frame used;
the `written:` line and the MBTiles name carry that frame's time. If any tile of the newest frame is served, the newest
frame is used and is never replaced by an older one, also when the newest frame is older than 60 minutes. If the
provider lists no earlier frame, the newest is downloaded again and the run ends as before.

- RainViewer is free for personal and educational use only; the credit "Weather data by RainViewer
  (https://www.rainviewer.com)" must be visible wherever you show the picture. It allows 100 requests a minute from one address. The program keeps to 85: one connection, 0.7 seconds
  between requests. Do not run two RainViewer downloads at the same time.
- RainViewer keeps a frame about 2 hours. After that its tiles are gone, so `resume` of an old interrupted download
  fails for them. Run the whole command again instead (section 4); it takes the current frame.
- NASA VIIRS has one picture a day, and the newest day may not cover your area yet. The tiles then come out black. In
  the fill report every tile then has the same small size, about 1.6 KB, which is the sign. Run it again later. On a
  good day:

```
$ chart-grabber-cli run --maps="$MAPS" --source="NASA VIIRS NOAA-20 true colour (daily, clouds)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-9 --out=charts/
…
 zoom  tiles  present  marked missing  missing     size
    5      1        1               0        0  15.3 KB
    6      1        1               0        0  13.6 KB
    7      1        1               0        0   7.9 KB
    8      1        1               0        0   5.7 KB
    9      1        1               0        0   4.0 KB
total      5        5               0        0  46.4 KB
NASA VIIRS NOAA-20 true colour (daily, clouds)  export  100%
written: …/charts/NASA-GIBS-VIIRS-TrueColor.mbtiles  5 tiles  72.0 KB  snapshot 2026-10-20
```

- `--time=VALUE` picks another frame of a source that offers a forecast (for example the Copernicus Marine waves
  and currents). `+24h` is the frame 24 hours from now, rounded down to the frames the provider has (every 3 or 6
  hours). A time written out, such as `2026-10-21T12:00:00Z`, must be one of the frames. Without `--time` you get the
  latest frame. A different time is a different snapshot, and a complete download removes the older ones. The value
  applies to every source of the job, so a job that also names a source without selectable times is refused. `Demo waves`
  below is a test source with the index type `wmts-default` (`sources-guide.md`).
  A source whose definition says `SnapshotPick=nearest-now` takes, without `--time`, the frame nearest to the time of
  the run instead of the layer's default; a source of type `none-refetch` (a layer without times) fetches all tiles
  afresh on every download (`sources-guide.md`).

```
$ chart-grabber-cli download --maps="$MAPS" --source="Demo waves" --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-6 --time=+24h
…
Demo waves
snapshot 2026-10-21 15:00 UTC
…
```

  Refusals, exit 3 (a value that is not a time at all, such as `--time=tomorrow`, is a wrong command line, exit 2):

```
$ chart-grabber-cli download --maps="$MAPS" --source="Demo waves" --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-6 --time=2026-10-20T13:00:00Z
error: --time 2026-10-20T13:00:00Z is not a time of "Demo waves" (range 2022-11-01T03:00:00Z..2026-10-29T00:00:00Z, step 3 h)
$ chart-grabber-cli run --maps="$MAPS" --source="RainViewer radar (worldwide, current)" --time=+24h --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-6 --out=charts/x.mbtiles
error: --time: "RainViewer radar (worldwide, current)" has no selectable time
$ chart-grabber-cli export --maps="$MAPS" --source="Demo waves" --bbox=-1.34,50.74,-1.24,50.78 --zooms=5-6 --time=+24h --out=charts/w.mbtiles
error: --time is not accepted by export: export uses the frame stored by the last download
```

- The NOAA radar covers US waters only, and the DWD radar only Germany and its coasts; outside them there is nothing to show.

### Stopping from a controlling program: `--stop-on-stdin`

A program that starts `chart-grabber-cli` (a plugin, a script) and cannot send a signal, as on Windows, adds `--stop-on-stdin`
to `download`, `run`, `resume`, `export` or `estimate` and keeps the CLI's standard input as a pipe. The exact line `stop`
(a trailing carriage return is allowed) or the end of the pipe (the controlling program ended or closed it) stops the
command like `kill` does: exit code 143, the session is saved up to the last finished tile and the same command continues
it. Any other line is ignored. A stop that arrives before the first tile also ends with 143. When standard input is not a
pipe (a file, `< /dev/null`, a terminal) the command is refused with exit code 2, so that an empty input cannot stop it at
once. Ctrl-C and `kill` keep working. On stop one line `chart-grabber-cli: stop requested on stdin; exit 143` goes to
standard error. Without the option standard input is not read.

## Reference

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Done. The fill report may still show missing tiles, unless `--strict` is given. |
| 1 | Internal error. |
| 2 | Wrong command line: unknown option, bad value, unreadable job file. |
| 3 | Job refused before any download: source, area, zoom or output file not usable. |
| 4 | Download not completed: no progress until the stall timeout, the network or the server is gone (`--max-connection-errors`), tiles could not be saved, or `--strict` with missing tiles. The state is saved; the same command continues. |
| 5 | The chart file could not be written, or the cache holds no tile for it. |
| 130 / 143 | Stopped by Ctrl-C / by `kill` (143 also by `--stop-on-stdin`). The state is saved. |

### Options

The output of `chart-grabber-cli --help`:

```
Usage:
    chart-grabber-cli sources   [--filter TEXT] [common]
    chart-grabber-cli estimate  JOB [common]
    chart-grabber-cli download  JOB [download options] [common]
    chart-grabber-cli export    JOB [common]
    chart-grabber-cli run       JOB [download options] [common]
    chart-grabber-cli resume    FILE.sls [FILE.sls ...] [download options] [common]
    chart-grabber-cli probe     [--filter TEXT] [--at LON,LAT] [--probe-zoom Z] [common]
    chart-grabber-cli -h | --help | --version

Job options (flags or --job FILE, section [Job]):
    --job=FILE                      read the options below from an INI job file
    --source=ID                     base map: GUID (with or without braces) or name
    --overlay=ID                    overlay layer: GUID or name
    --bbox=W,S,E,N                  area: rectangle in decimal degrees, '.' as decimal point
    --polygon=FILE                  area: .hlg selection file
    --route=FILE                    area: GPX file, the route to follow
    --route-name=NAME               which route or track of the GPX file, by its name
    --corridor-nm=N                 total width of the corridor in nautical miles; required with --route
    --zooms=LIST                    zoom levels, 0-based, such as 8-12,14
    --time=VALUE                    frame of a snapshot source: 2026-10-18T12:00:00Z or +24h; default latest
    --out=PATH                      output file of the base map, or a directory
    --overlay-out=FILE              output file of the overlay
    --name=TEXT                     MBTiles name of the base file
    --description=TEXT              MBTiles description of the base file
    --attribution=TEXT              MBTiles attribution of the base file
    --overwrite                     replace an existing output file

Download options (flags or section [Download]):
    --threads=N                     workers per source, 1 to 64
    --restart                       ignore saved sessions, scan from the first tile
    --replace                       download tiles that already exist
    --replace-if-size-differs       replace an existing tile when its size differs
    --replace-older-than=DATE       replace an existing tile older than DATE (YYYY-MM-DD or YYYY-MM-DDThh:mm:ss)
    --retry-missing                 request tiles marked "not on the server" again
    --retry-missing-older-than=DATE  only markers older than DATE
    --autosave-min=N                session autosave interval in minutes; 0 = only at the end
    --stall-timeout=SEC             stall timeout in seconds; 0 = wait for ever
    --max-connection-errors=N       stop after N tiles in a row failed with a connection error; 0 = never
    --save-retries=N                retries of a failed tile save
    --strict                        exit 4 when a tile is neither present nor marked missing
    --allow-webp                    accept WebP tiles
    --proxy=URL                     proxy: [http|https|socks4|socks4a|socks5|socks5h://][user:pass@]host[:port]
    --assume-tile-kb=N              mean tile size in KB when the cache gives no sample

Common options:
    --maps=DIR                      folder of map-source definitions (*.zmp, *.source.json); required; repeat for more folders, a later folder wins on the same GUID
    --cache=DIR                     cache directory
    --filter=TEXT                   sources, probe: only maps whose name contains TEXT
    --at=LON,LAT                    probe: point of the probe tile (default 0,0)
    --probe-zoom=Z                  probe: zoom of the probe tile (default 10)
    -v                              print every worker log line
    -q                              errors and results only
    --format=FORMAT                 text (default) or tsv: machine-readable records on stdout
    --progress=MODE                 auto (default), lines (machine-readable records on stderr) or off
    --no-progress                   no progress line
    --stop-on-stdin                 stop gracefully (exit 143) on a "stop" line or EOF on stdin, which must be a pipe
    -h, --help                      print this help and exit
    --version                       print the version and exit
    --datadir=DIR, --portable       data directory, portable mode

Zoom levels are 0-based (OpenCPN, MBTiles); SAS.Planet shows them 1-based.
--corridor-nm is the total width of the corridor.
Map sources are not built in: every command except -h and --version needs at least one --maps folder.
probe requests one tile per source (the tile under --at at --probe-zoom) and prints GUID, name and result; exit 0 when every probed source passed, 4 otherwise. Sources that cannot be downloaded are skipped. probe writes the probed tile into the cache.

Examples:
    chart-grabber-cli estimate --maps=./map-sources --source="OpenStreetMap" --bbox=24.5,59.3,24.9,59.5 --zooms=8-12
    chart-grabber-cli run --job=passage.conf --maps=./map-sources --threads=4 --out=passage.mbtiles
```

Write every option as `--name=value`.

### Files and directories

| What | Where | Change with |
|---|---|---|
| Map sources | only the folders given with `--maps` (required, repeatable) | `--maps=DIR` |
| Settings (read only, optional) | `~/.config/chart-grabber/chart-grabber.conf` | `--datadir=DIR` |
| Tile cache | `~/.local/share/chart-grabber/cache_sqlite/<source>/` | `--cache=DIR` (the cache goes into `DIR/cache_sqlite/`) |
| Saved download state | `~/.local/share/chart-grabber/AutoSave/` | — |
| Frame of each weather source (section 13) | `~/.local/share/chart-grabber/Snapshots/<GUID>.conf` | — |
| Chart files | where `--out` says | `--out`, `--overlay-out` |

Earlier versions named the weather frame files `<GUID>.ini`. The first command that reads such a file renames it and
says so on stderr:

```
note: renamed …/Snapshots/{1A696D82-F258-41A4-BCD6-7648D9E08DE2}.ini to …/Snapshots/{1A696D82-F258-41A4-BCD6-7648D9E08DE2}.conf
```

If both files exist, the `.conf` file is used, the `.ini` file is left alone, and a `note:` line says so at every
command that reads it. Delete the `.ini` file to stop it.

On Windows the settings file, the cache and the saved state are in `%LOCALAPPDATA%\chart-grabber` (`windows-guide.md`, section 6).

Earlier versions used the directories `~/.local/share/SAS.Planet/` and `~/.config/SAS.Planet/` and the settings file
`SASPlanet.ini`. At the first start the program moves such a directory to the new name and renames the settings file
inside it. One line on stderr says so:

```
note: moved …/.config/SAS.Planet to …/.config/chart-grabber; moved …/.local/share/SAS.Planet to …/.local/share/chart-grabber
```

If the new directory already exists, nothing is moved and nothing is printed. If the move fails, the line says why and
the old directory is used as it is.

With `--datadir=DIR` (or `--datadir DIR`) all of the above except the map sources and the chart files move into `DIR`: `DIR/chart-grabber.conf`,
`DIR/cache_sqlite/`, `DIR/AutoSave/`, `DIR/Snapshots/`. `DIR/Maps/` is not read. `--portable`, or a file `chart-grabber.conf` or
`portable.txt` next to the program, does the same with the program's own directory. A settings file `SASPlanet.ini` in such a directory is no longer read:
rename it to `chart-grabber.conf`.

## Troubleshooting

**`error: no map sources given: pass --maps DIR, a folder of map-source definitions (repeat --maps for more folders)`** (exit 3)
The command has no `--maps`. Pass the folder of map sources (first section); `resume` needs it too.

**`error: no map source loaded from …/emptymaps: Unable to load map(s)! Aborting...`** (exit 3)
The folder given with `--maps` holds no map source. Point `--maps` at a folder with `.zmp` folders or `.source.json`
files, at any depth.

**`error: no map source matches "Sentinel-2 Clodless"; run "chart-grabber-cli sources"`** (exit 3)
The name is misspelt or the source is not in the folders given with `--maps`. Find the exact name with `sources --filter=` and a part of it.

**`error: "Meta.ua" matches more than one map source:`** (exit 3)
```
  {F031E660-5D1B-48DE-B413-43E85B8DBB26}  Meta.ua  Meta.ua/betamap.meta.ua.zmp
  {4D31E660-5D1B-48DE-B413-43E85B8DBB37}  Meta.ua  Meta.ua/sat_meta_ua.zmp
```
Two sources have this name (this happens with the full catalogue, section 12). Use the GUID of the one you want.

**`error: Please select at least one zoom`** (exit 3)
`--zooms` is missing.

**`error: bounding box: west must be below east and south below north (an area across the 180° meridian is not supported)`** (exit 3)
The four numbers are in the wrong order; it is west, south, east, north. If you swap longitude and latitude
(`--bbox=50.74,-1.34,50.78,-1.24`) the program accepts it as a different place in the world; check the tile count
in the estimate. An area that crosses the 180° meridian has to be split into two charts.

**`error: the area is too large at zoom 21 (0-based): more than 2147483647 tiles; choose a smaller area or lower zooms`** (exit 3)
The area at that zoom level holds more tiles than the program can count. Lower the zoom or shrink the area.

**`error: "…/charts/Sentinel2-Cloudless-2025.mbtiles" exists; pass --overwrite to replace it`** (exit 3)
Add `--overwrite`, or choose another `--out`. The old file is replaced only after the new one is complete.

**`error: no progress for 20 s: Downloading...`** (exit 4)
No tile arrived for the stall timeout. The network is down, the proxy does not answer, or the server stopped
answering. The state is saved; fix the connection and run the same command again. Add `-v` to see every request and
the server's answer.

**`error: no connection to the server for 60 tiles in a row: Error connecting to server`** (exit 4)
The network or the server is gone. The state is saved; run the same command again when it is back (section 8).

**`error: "Sentinel-2 Cloudless 2025 (EOX)": cannot write the cache folder …/cache_sqlite/Sentinel2_Cloudless_2025/`** (exit 3)
The cache directory (`~/.local/share/chart-grabber`, or the one given with `--cache`) is not writable or is a file.
The program says so at the start, before a tile is requested. Fix the permissions or give another `--cache`.

**`error: no tiles of Sentinel-2 Cloudless 2025 (EOX) in the cache for this area`** (exit 5)
There was nothing to put into the chart: `export` was run before `download`, the cache given with `--cache` is not
the one the tiles went to, or the server has no tiles there (`-v` shows `Tile is not found!` for each). No file is
written.
