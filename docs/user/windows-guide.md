# chart-grabber-cli on Windows: user guide

`chart-grabber-cli` downloads map tiles of an area from an online map source and writes them into an MBTiles file
that OpenCPN opens as a chart. It runs in a terminal window; it has no windows or buttons of its own, and it does not
display maps, plan routes, or convert vector charts (S-57, S-63).

This guide is for Windows 10 or later, 64-bit, and for people who have never typed a command. The commands and the
output below are from version 2026.10.11, Windows 10 22H2, Windows PowerShell 5.1 and `cmd`. Everything the program does
that is not specific to Windows is in `cli-guide.md`, in the same folder as this file.

Contents: [1. Install](#1-how-to-install-the-program) ·
[2. Open a terminal and run it](#2-how-to-open-a-terminal-and-run-the-program) ·
[3. Make a chart of an area](#3-how-to-make-an-offline-chart-of-an-area-for-opencpn) ·
[4. Stop a download and continue it](#4-how-to-stop-a-download-and-continue-it) ·
[5. Job files](#5-how-to-use-a-job-file) ·
[6. Where the files are](#6-where-the-program-keeps-its-files) ·
[7. Windows details](#7-windows-details) ·
[Reference](#reference) · [Troubleshooting](#troubleshooting)

## 1. How to install the program

The program comes as a zip file, `chart-grabber-cli-<version>-win64.zip`. The OpenCPN plugin looks for the program in
`C:\Program Files\Chart Grabber CLI`. Put it there if you use the plugin. For use from a terminal only, any folder
works, and you do not need administrator rights for a folder of your own.

The program is not code-signed. Windows treats a file from the internet with suspicion. Remove that mark from the zip
before you unzip it: in File Explorer right-click the zip, choose Properties, tick Unblock at the bottom, click OK.
In PowerShell the same is one command (the zip is in your Downloads folder here):

```
PS> cd $HOME\Downloads
PS> Unblock-File .\chart-grabber-cli-*-win64.zip
```

Unzip it. The zip holds one folder, `chart-grabber-cli-<version>-win64`, and the program is inside it. The "Extract
All" command of File Explorer adds a second folder of the same name around it; use the inner one. In PowerShell:

```
PS> Expand-Archive -Path .\chart-grabber-cli-*-win64.zip -DestinationPath .
```

To install into `C:\Program Files`, you need a PowerShell window started as administrator: type `PowerShell` in the
Start menu, right-click "Windows PowerShell" and choose "Run as administrator". Then copy the folder:

```
PS> Copy-Item .\chart-grabber-cli-*-win64 "C:\Program Files\Chart Grabber CLI" -Recurse
PS> cd "C:\Program Files\Chart Grabber CLI"
PS> Get-ChildItem -Directory -Name
docs
imageformats
licenses
platforms
tls
PS> Get-ChildItem -File -Name chart-grabber-cli.exe, LICENSE, NOTICE, qt.conf, proj.db
chart-grabber-cli.exe
LICENSE
NOTICE
qt.conf
proj.db
```

- `chart-grabber-cli.exe` is the program. The 50 `.dll` files next to it are the libraries it needs; keep them in the
  same folder. `imageformats`, `platforms`, `tls`, `qt.conf` and `proj.db` are needed as well.
- `NOTICE` lists the third-party libraries in the zip and their licences. The licence texts are in `licenses\`.
- `docs\` holds this guide, `cli-guide.md`, `sources-guide.md` (how to write your own map sources) and the change
  list `CHANGELOG.md`. They are plain text; Notepad opens them.

## 2. How to open a terminal and run the program

A terminal is a window where you type a command and press Enter. Use Windows PowerShell: type `PowerShell` in the
Start menu and press Enter. Windows Terminal, if you have it, runs the same PowerShell. `cmd` works too (see the end
of this section). The prompt shows the folder you are in, here `PS C:\Users\builder>`; the examples write only `PS>`.

Go to the program's folder and ask for its version:

```
PS> cd "C:\Program Files\Chart Grabber CLI"
PS> .\chart-grabber-cli.exe --version
chart-grabber-cli 2026.10.11 (5242f5140bcf)
```

The `.\` in front means "the program in this folder". PowerShell does not run a program from the current folder
without it. Do not double-click `chart-grabber-cli.exe` in File Explorer: it needs a command, so a window opens and
closes at once.

To run the program from any folder, add its folder to the search path of this window. The change lasts until you close
the window:

```
PS> $env:Path += ";C:\Program Files\Chart Grabber CLI"
PS> cd $HOME
PS> chart-grabber-cli --version
chart-grabber-cli 2026.10.11 (5242f5140bcf)
```

The rest of this guide uses that form. Every new window needs the first line again. To make it permanent, open the
Start menu, type "environment variables", choose "Edit environment variables for your account", select `Path`,
click Edit, New, and add the folder.

A command without a command word, or without map sources, says what is missing. `$LASTEXITCODE` shows how the last
program ended; 0 is success:

```
PS> chart-grabber-cli
error: no command given. Usage: chart-grabber-cli COMMAND [options]; chart-grabber-cli --help
PS> $LASTEXITCODE
2
PS> chart-grabber-cli sources
error: no map sources given: pass --maps DIR, a folder of map-source definitions (repeat --maps for more folders)
PS> $LASTEXITCODE
3
```

**Typing the options in PowerShell.**
- Put a value in double quotes when it holds a space or a bracket: `--source="Sentinel-2 Cloudless 2025 (EOX)"`.
- Put a GUID in double quotes too. PowerShell reads braces as code, so `--source={9212…}` without quotes does not
  start the program (Troubleshooting).
- A path with a variable goes in quotes: `--maps="$HOME\map-sources"`. `$HOME` is your user folder, such as
  `C:\Users\builder`.
- Do not end a quoted path with a backslash: `--out="$HOME\My Charts\"` is read wrongly (Troubleshooting). Write
  `--out="$HOME\My Charts"`.
- The area needs no quotes: `--bbox=-1.34,50.74,-1.24,50.78`.
- A long command can go over several lines if each line but the last ends with a backtick (`` ` ``).
- Write every option as `--name=value`.

**The same in `cmd`.** Open it from the Start menu (type `cmd`). A variable is `%NAME%`, a line continues with `^`,
and the GUID needs no quotes. Put the folder on the search path for this window and run:

```
C:\Users\builder>set "PATH=%PATH%;C:\Program Files\Chart Grabber CLI"
C:\Users\builder>set "MAPS=%USERPROFILE%\map-sources"
C:\Users\builder>chart-grabber-cli sources --maps="%MAPS%" --filter=sea
                                  GUID   kind                 name  content type  EPSG  cache  verdict
{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}  layer  OpenSeaMap Seamarks     image/png  3857      0       ok
C:\Users\builder>echo %ERRORLEVEL%
0
```

## 3. How to make an offline chart of an area for OpenCPN

You need three things: a folder of map sources, an area, and zoom levels.

**The map sources.** The program brings none. A map source is a folder ending in `.zmp`, or a file ending in
`.source.json`, that tells the program where a map's tiles are on the internet. Put the folder you were given, with
its subfolders, somewhere in your user folder, for example `C:\Users\builder\map-sources`, and keep its name in a
variable. Then list what the program finds:

```
PS> $maps = "$HOME\map-sources"
PS> chart-grabber-cli sources --maps="$maps"
                                  GUID   kind                             name  content type  EPSG  cache  verdict
{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}  layer              OpenSeaMap Seamarks     image/png  3857      0       ok
{92122135-6B15-4800-AA9E-3E48C556DA17}    map  Sentinel-2 Cloudless 2025 (EOX)     image/jpg  3857      0       ok
```

`map` is a base chart, `layer` is a transparent overlay (sea marks, weather). `verdict` is `ok` when the program can
make a chart from the source. `--filter=sea` lists only the sources with `sea` in their name.

The sources come from the provider's web server. Some providers' terms forbid bulk download or offline use. The
program prints a warning with the terms address for such a source. Read the terms; you are responsible for complying.

**The area and the zoom levels.** A tile is a 256 × 256 pixel picture. A zoom level is a scale step: level 0 shows
the whole world in one tile, and each next level has twice the detail and four times as many tiles. At 50° N one pixel
is about 97 m at level 10, 6 m at level 14 and 1.5 m at level 16. Levels 10–14 suit a passage, 15–17 an anchorage.
`--bbox=W,S,E,N` is the area in decimal degrees with a `.` as the decimal point. West and south are negative. The
example is the Cowes end of the Solent.

**Check the size first.** `estimate` counts the tiles and the bytes without using the network:

```
PS> chart-grabber-cli estimate --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15
Sentinel-2 Cloudless 2025 (EOX)
 zoom  SAS z  tiles  bbox  present  marked missing          to download
   10     11      1     1     0.0%            0.0%   ~20.0 KB (assumed)
   11     12      1     1     0.0%            0.0%   ~20.0 KB (assumed)
   12     13      2     2     0.0%            0.0%   ~40.0 KB (assumed)
   13     14      6     6     0.0%            0.0%  ~120.0 KB (assumed)
   14     15     20    20     0.0%            0.0%  ~400.0 KB (assumed)
   15     16     70    70     0.0%            0.0%    ~1.4 MB (assumed)
total           100   100                             ~2.0 MB (assumed)
```

Look at the `total` row before a big download. One zoom level more is about four times the tiles. `(assumed)` means
nothing of this source is stored on your computer yet, so the program guesses 20 KB per tile. The `SAS z` column is
the same zoom level counted from 1; OpenCPN and this guide count from 0.

**Make the chart.** `run` downloads the tiles and writes the chart file. The folder for `--out` must exist:

```
PS> mkdir charts
PS> chart-grabber-cli run --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --out=charts\
Sentinel-2 Cloudless 2025 (EOX)
 zoom  SAS z  tiles  bbox  present  marked missing          to download
…
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
written: C:/Users/builder/charts/Sentinel2-Cloudless-2025.mbtiles  100 tiles  564.0 KB
```

- The progress line shows the source, the level being worked on (counted from 1), the tiles done of all tiles, the
  bytes downloaded and an estimate of the time left. In a console window it is rewritten in place. When the output goes
  to a file or a pipe, it is printed every 30 seconds and once at the end.
- The second table is the fill report. `missing` counts tiles the server did not deliver; `-v` shows the server's
  answer for each.
- With a folder for `--out`, the file is named after the definition of the source: the folder
  `Sentinel2-Cloudless-2025.zmp` gives `Sentinel2-Cloudless-2025.mbtiles`, and so does a file
  `Sentinel2-Cloudless-2025.source.json`. With a file name ending in `.mbtiles` the file is named as you say. The file
  appears only when it is complete.
- `written:` shows the full path of the file. The program prints paths with `/` on Windows; both forms work when you
  type a path.

To put sea marks over the picture, add `--overlay`. It goes into a second file with `-overlay` added to its name:

```
PS> chart-grabber-cli run --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --overlay="OpenSeaMap Seamarks" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-15 --out=charts\solent.mbtiles
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: C:/Users/builder/charts/solent.mbtiles  100 tiles  564.0 KB
OpenSeaMap Seamarks  export  100%
written: C:/Users/builder/charts/solent-overlay.mbtiles  100 tiles  320.0 KB
PS> dir charts


    Directory: C:\Users\builder\charts


Mode                 LastWriteTime         Length Name
----                 -------------         ------ ----
-a----        10/23/2026   9:18 PM         577536 Sentinel2-Cloudless-2025.mbtiles
-a----        10/23/2026   9:18 PM         327680 solent-overlay.mbtiles
-a----        10/23/2026   9:18 PM         577536 solent.mbtiles
```

**Open the charts in OpenCPN.** In OpenCPN choose Options, then Charts, then Chart Files, then Add Directory. Pick the
`charts` folder (here `C:\Users\builder\charts`), tick "Scan Charts and Update Database" and click OK. The chart is
named after the source, or after the text of `--name="…"`.

**Check that a source answers.** `probe` downloads one tile and says whether it worked. Give a point inside your area
(`--at=longitude,latitude`) and a zoom level. Exit code 0 means every probed source worked, 4 means one did not:

```
PS> chart-grabber-cli probe --maps="$maps" --filter=Sea --at=-1.29,50.76 --probe-zoom=12
                                  GUID                 name            result
{AB8A8685-AEC6-4E16-80DA-DC4AC37E4777}  OpenSeaMap Seamarks  ok 12535 image/png
```

More tasks, which work the same on Windows, are in `cli-guide.md`: choosing a source (section 2), a chart along a
GPX route (6), updating a chart (5), a proxy or a machine without internet (8), weather (13). Write the paths in
those examples as Windows paths.

## 4. How to stop a download and continue it

Press Ctrl-C in the terminal window. The program saves how far it got, writes no half-finished chart file, and ends
with exit code 130. Ctrl-Break does the same. A big area takes a long time, and the stop is safe at any moment:

```
PS> chart-grabber-cli run --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-17 --out=charts\ --overwrite
…
Sentinel-2 Cloudless 2025 (EOX)  z11  0/1216 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Sentinel-2 Cloudless 2025 (EOX)  z17  317/1216 (26%)  downloaded 317 tiles 1.1 MB  left ~3.3 MB ~00:01:25
warning: session saved; run the same command again to continue
warning: to continue from this file: resume C:/Users/builder/AppData/Local/chart-grabber/AutoSave/c490d7b70273/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
```

To continue, run the same command again (press the Up arrow to bring it back):

```
PS> chart-grabber-cli run --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-17 --out=charts\ --overwrite
…
warning: continuing the session of 2026-10-23 21:19:54
Sentinel-2 Cloudless 2025 (EOX)  z17  317/1216 (26%)  downloaded 317 tiles 1.1 MB  left ~3.3 MB ~00:01:25
…
Sentinel-2 Cloudless 2025 (EOX)  z18  1216/1216 (100%)  downloaded 1216 tiles 3.2 MB  left ~0.0 KB ~00:00:00
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: C:/Users/builder/charts/Sentinel2-Cloudless-2025.mbtiles  1216 tiles  3.9 MB
```

- "The same command" means the same source, zoom levels, area and `--threads`. Change one of them and the program
  starts a new download. Tiles already stored are not downloaded again either way.
- Add `--restart` to throw the saved state away and scan the area from the start.
- Closing the console window with the X button saves the session as well and ends with exit code 143.
- A finished download deletes its saved state.

`resume` continues from the saved file that the warning names. It downloads only; make the chart afterwards with
`export` or `run`. Here a `download` of the area east of Cowes is stopped with Ctrl-C. The path contains braces, so
put it in quotes:

```
PS> chart-grabber-cli download --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.24,50.75,-1.20,50.775 --zooms=17
…
Sentinel-2 Cloudless 2025 (EOX)  z18  173/256 (68%)  downloaded 162 tiles 157.9 KB  left ~80.9 KB ~00:00:07
warning: session saved; run the same command again to continue
warning: to continue from this file: resume C:/Users/builder/AppData/Local/chart-grabber/AutoSave/b0ceb7d4240b/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
PS> Get-ChildItem "$env:LOCALAPPDATA\chart-grabber\AutoSave" -Recurse -Filter final.sls | Select-Object -ExpandProperty FullName
C:\Users\builder\AppData\Local\chart-grabber\AutoSave\b0ceb7d4240b\{92122135-6B15-4800-AA9E-3E48C556DA17}\final.sls
PS> chart-grabber-cli resume --maps="$maps" "$env:LOCALAPPDATA\chart-grabber\AutoSave\b0ceb7d4240b\{92122135-6B15-4800-AA9E-3E48C556DA17}\final.sls"
warning: continuing the session of 2026-10-23 21:21:30
Sentinel-2 Cloudless 2025 (EOX)  z18  173/256 (68%)  downloaded 162 tiles 157.9 KB  left ~80.9 KB ~00:00:07
Sentinel-2 Cloudless 2025 (EOX)  z18  256/256 (100%)  downloaded 240 tiles 231.8 KB  left ~0.0 KB ~00:00:00
Sentinel-2 Cloudless 2025 (EOX)
 zoom  tiles  present  marked missing  missing      size
   17    256      256               0        0  248.5 KB
total    256      256               0        0  248.5 KB
```

The `Get-ChildItem` line lists the saved files. Each path is one saved download.

## 5. How to use a job file

A job file holds the options of a command, so you do not retype them. It is a text file with sections in square
brackets and one `Key=Value` per line. A key is the option name without the dashes, in CamelCase: `--bbox` is `BBox`.
Make it in Notepad and save it as `solent.conf`, or write it from PowerShell:

```
PS> Set-Content -Path solent.conf -Encoding ascii -Value @('[Job]','Source=Sentinel-2 Cloudless 2025 (EOX)','BBox=-1.34,50.74,-1.24,50.78','Zooms=10-15','Out=charts/solent-job.mbtiles','','[Download]','Threads=2')
PS> Get-Content solent.conf
[Job]
Source=Sentinel-2 Cloudless 2025 (EOX)
BBox=-1.34,50.74,-1.24,50.78
Zooms=10-15
Out=charts/solent-job.mbtiles

[Download]
Threads=2
PS> chart-grabber-cli run --job=solent.conf --maps="$maps"
warning: --threads 2 lowered to 1: the source allows 1 connections
…
Sentinel-2 Cloudless 2025 (EOX)  export  100%
written: C:/Users/builder/charts/solent-job.mbtiles  100 tiles  564.0 KB
```

- Area, source and output keys go under `[Job]`, download options under `[Download]`. `--help` shows which option
  belongs where.
- In a job file you may write paths with `\` or with `/`. Do not put quotes around a value.
- A relative path in the file is relative to the folder of the file, not to where you run the command.
- An option on the command line wins over the file.
- Notepad may save the file as `solent.conf.txt` if you choose "Text documents". Choose "All files" in the Save dialog,
  or turn on File name extensions in the File Explorer View menu to see the real name. Older job files named
  `.ini` work too.
- More about job files, and the error for a misspelt key, is in `cli-guide.md`, section 7.

## 6. Where the program keeps its files

| What | Where | Change with |
|---|---|---|
| Map sources | only the folders given with `--maps` | `--maps=DIR` |
| Tile cache | `%LOCALAPPDATA%\chart-grabber\cache_sqlite\<source>\` | `--cache=DIR` (the cache goes into `DIR\cache_sqlite`) |
| Saved download state | `%LOCALAPPDATA%\chart-grabber\AutoSave\` | `--datadir=DIR` |
| Settings file (optional) | `%LOCALAPPDATA%\chart-grabber\chart-grabber.conf` | `--datadir=DIR` |
| Chart files | where `--out` says | `--out`, `--overlay-out` |

`%APPDATA%` is `C:\Users\<you>\AppData\Roaming` and `%LOCALAPPDATA%` is `C:\Users\<you>\AppData\Local`. In
PowerShell they are `$env:APPDATA` and `$env:LOCALAPPDATA`. The `AppData` folder is hidden in File Explorer; paste
the path into the address bar.

The cache keeps every tile you downloaded, one folder per source. Later charts of the same source reuse it, so nothing
is downloaded twice. It takes disk space. See how much, then delete the folder of one source:

```
PS> $cache = "$env:LOCALAPPDATA\chart-grabber\cache_sqlite"
PS> Get-ChildItem $cache | ForEach-Object { "{0,6:N1} MB  {1}" -f ((Get-ChildItem $_.FullName -Recurse | Measure-Object Length -Sum).Sum / 1MB), $_.Name }
   0.4 MB  Osm_OpenSeaMap_Layer_Seamark
   4.3 MB  Sentinel2_Cloudless_2025
PS> Remove-Item "$cache\Osm_OpenSeaMap_Layer_Seamark" -Recurse
PS> Get-ChildItem $cache -Name
Sentinel2_Cloudless_2025
```

Deleting is safe: charts already written are separate files. The next chart of that source downloads its tiles again.
Delete the whole `cache_sqlite` folder to clear everything.

Earlier versions kept the cache and the saved state in the roaming folder `%APPDATA%\chart-grabber`, which a company
network may copy to a server at every logon. They now live in the local folder `%LOCALAPPDATA%\chart-grabber`. At the
first start the program moves an old `%APPDATA%\chart-grabber` there and prints one line on stderr:

```
note: moved C:/Users/builder/AppData/Roaming/chart-grabber to C:/Users/builder/AppData/Local/chart-grabber
```

If the new folder already holds a cache or saved state, nothing is merged: the new folder is used, the old one is left
as it is, and the line says so:

```
note: not moving C:/Users/builder/AppData/Roaming/chart-grabber to C:/Users/builder/AppData/Local/chart-grabber: the target is in use; C:/Users/builder/AppData/Local/chart-grabber is used, C:/Users/builder/AppData/Roaming/chart-grabber is left as it is
```

The program leaves no marker, so this line is printed again at every start while both folders exist. A folder in `%LOCALAPPDATA%` that holds only the settings file does not count as in use. Delete the old folder yourself when you do not need it.

**Portable use.** With `--portable`, or with a file `portable.txt` or `chart-grabber.conf` next to
`chart-grabber-cli.exe`, the program keeps the cache, saved state and settings in its own folder, so you can carry it
on a USB stick. `--datadir=DIR` (also written `--datadir DIR`) does the same with the folder you name. Both create `AutoSave` and `cache_sqlite`
there:

```
PS> chart-grabber-cli download --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.34,50.74,-1.24,50.78 --zooms=10-12 --datadir=C:\cg\data-test
…
Sentinel-2 Cloudless 2025 (EOX)  z13  4/4 (100%)  downloaded 4 tiles 41.2 KB  left ~0.0 KB ~00:00:00
…
PS> Get-ChildItem C:\cg\data-test -Name
AutoSave
cache_sqlite
```

A folder under `C:\Program Files` is not writable without administrator rights, so `--portable` suits a folder of
your own.

## 7. Windows details

- **Paths.** The program prints paths with `/` (`C:/Users/builder/charts/…`), in `written:` and in the messages. You
  may type them with `\` or with `/`. Copy a printed path into a command as it is.
- **Spaces.** A folder name with spaces, such as `C:\Users\builder\My Charts`, works if the whole value is in quotes.
- **Path length.** Windows 10 refuses paths over 260 characters unless "long paths" are switched on, which they are
  not by default. A chart in such a folder fails at the end of the run (Troubleshooting). Keep the `--out` path short.
- **Closing the window.** The X button of the console window saves the session and ends the program with exit code 143.
  Run the same command again to continue.
- **A program that starts the CLI**, such as the OpenCPN plugin, cannot press Ctrl-C. It adds `--stop-on-stdin` and
  writes the line `stop` to the program's input; the program then ends with exit code 143. See `cli-guide.md`.
- **Proxy.** The program uses the proxy set in Windows (Settings, Network & Internet, Proxy), the `http_proxy` and
  `https_proxy` variables, and `--proxy=URL`. In PowerShell set a variable for the window with
  `$env:https_proxy = "http://proxy.example:3128"`. A proxy that is down stops the download with exit code 4, and the
  same command continues it later (Troubleshooting).
- **Firewall.** The program only makes outgoing connections to the map servers. A firewall that blocks them shows
  up as "no connection to the server" (Troubleshooting).

## Reference

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Done. The fill report may still show missing tiles, unless `--strict` is given. |
| 1 | Internal error. |
| 2 | Wrong command line: unknown option, bad value, unreadable job file. |
| 3 | Job refused before any download: source, area, zoom or output file not usable. |
| 4 | Download not completed: no progress, the network or the server is gone, tiles could not be saved. The state is saved; the same command continues. |
| 5 | The chart file could not be written, or the cache holds no tile for it. |
| 130 | Stopped by Ctrl-C or Ctrl-Break. The state is saved. |
| 143 | Stopped by closing the console window, by `--stop-on-stdin`, or by `kill`. The state is saved. |
| -1073741515 | Windows could not start the program: a `.dll` is missing (Troubleshooting). |

PowerShell shows the code in `$LASTEXITCODE`, `cmd` in `%ERRORLEVEL%`. The full table is in `cli-guide.md`.

### Options

`chart-grabber-cli --help` prints the full list of commands and options; `cli-guide.md` has the same text.

## Troubleshooting

**`The term 'chart-grabber-cli' is not recognized as the name of a cmdlet, function, script file, or operable program.`**
PowerShell does not know where the program is. Either add its folder to the search path of this window (section 2),
or `cd` into the folder and start it as `.\chart-grabber-cli.exe`. The same message appears for `chart-grabber-cli.exe`
without `.\` when you are in its folder.

**The program does not start and prints nothing; `$LASTEXITCODE` is -1073741515.** A window may open and say that a
`.dll` file was not found. The program was copied without the `.dll` files next to it. Copy the whole unzipped
folder, not only `chart-grabber-cli.exe`:

```
PS> .\chart-grabber-cli.exe --version
PS> $LASTEXITCODE
-1073741515
```

**`You must provide a value expression following the '-' operator.`** PowerShell tried to read a GUID in braces as
code, as in `--source={92122135-6B15-4800-AA9E-3E48C556DA17}`. Put it in quotes:
`--source="{92122135-6B15-4800-AA9E-3E48C556DA17}"`, or leave the braces out.

**`error: the output file name "C:/Users/builder/My Charts"" does not end with .mbtiles`** (exit 3)
The value of `--out` ended with a backslash before the closing quote (`--out="$HOME\My Charts\"`), and Windows reads
`\"` as a quote character. Remove the last backslash, or give a file name.

**`error: the directory of "C:/Users/builder/nodir/z.mbtiles" does not exist`** (exit 3)
The folder for the chart does not exist. Create it with `mkdir` first.

**`error: no map source loaded from …: Unable to load map(s)! Aborting...`** (exit 3)
The folder given with `--maps` does not exist or holds no map source. Check the spelling, and that the folder has
`.zmp` folders or `.source.json` files.

**`error: unable to open database file ( error code: 14)`** (exit 5)
The chart file's path is longer than 260 characters. The tiles are already in the cache; give a shorter `--out` and
run the same command again, which takes them from the cache.

**`error: no connection to the server for 2 tiles in a row: Error connecting to server`** (exit 4)
The server or the proxy cannot be reached. Check the internet connection, the proxy setting of Windows, and the
`http_proxy` and `https_proxy` variables (`Get-ChildItem Env:*proxy*` lists them). The saved state is kept:

```
PS> chart-grabber-cli download --maps="$maps" --source="Sentinel-2 Cloudless 2025 (EOX)" --bbox=-1.40,50.70,-1.38,50.71 --zooms=17 --proxy=http://127.0.0.1:3128 --max-connection-errors=2 --no-progress
…
error: no connection to the server for 2 tiles in a row: Error connecting to server
Wait 5 seconds...
warning: session saved; run the same command again to continue
warning: to continue from this file: resume C:/Users/builder/AppData/Local/chart-grabber/AutoSave/043be990885f/{92122135-6B15-4800-AA9E-3E48C556DA17}/final.sls
```

Without `--max-connection-errors` the program tries 60 tiles in a row first. Fix the connection, then run the same
command again.

**`error: "…" exists; pass --overwrite to replace it`** (exit 3), **`error: no map source matches …`** (exit 3) and the
other messages not listed here are explained in the Troubleshooting section of `cli-guide.md`.
