# chart-grabber-cli: writing map sources

A map source is a small definition that tells `chart-grabber-cli` where the tiles of a map are on the internet. This
guide shows how to write one, test it and fix it. It does not list sources and does not cover the commands in general;
`cli-guide.md` does.

A step-by-step tutorial with working example files is in [sources-tutorial.md](sources-tutorial.md). This guide is the reference.

Contents: [1. An XYZ tile server](#1-how-to-add-a-source-for-an-xyz-tile-server) ·
[2. A WMS server](#2-how-to-add-a-wms-server) ·
[3. A script for the URL](#3-how-to-build-the-url-with-a-script) ·
[4. Test a source](#4-how-to-test-a-source) ·
[5. Pitfalls of Pascal scripts](#5-pitfalls-of-pascal-scripts) ·
[6. Terms of use and navigation warnings](#6-how-to-warn-about-terms-of-use-and-navigation) ·
[Reference: the folder](#reference-the-folder-of-a-source) ·
[Reference: `params.txt`](#reference-paramstxt) ·
[Reference: URL template](#reference-the-url-template) ·
[Reference: `*.source.json`](#reference-sourcejson-format-1) ·
[Reference: Pascal scripts](#reference-pascal-url-scripts) ·
[Reference: JavaScript](#reference-javascript) ·
[Troubleshooting](#troubleshooting)

## Before you start

A source is one of two things:

- a folder whose name ends in `.zmp`, holding a text file `params.txt` and, if needed, a script `GetUrlScript.txt`.
  This is the format SAS.Planet uses, so a `.zmp` folder written for SAS.Planet works here.
- a single file whose name ends in `.source.json`, with the same content written as JSON.

A `.zmp` folder from another person can carry a script that makes web requests. Before you use one, read
[what a script may not do](#what-a-script-may-not-do) and look at its `params.txt` and `GetUrlScript.txt`.

Both are found anywhere below the folder you pass with `--maps`, up to six levels deep. Folders whose name starts with
a dot are skipped. The examples keep their sources in the directory `my-sources`; run the commands from its parent:

```
$ mkdir -p my-sources
```

Every source needs a GUID, an identifier that no other source has. Make one with `uuidgen`; braces and capital letters
are the usual form:

```
$ echo "{$(uuidgen | tr a-f A-F)}"
{794FB3CC-B0D9-4A4E-8681-F3845DF8BCC5}
```

The examples carry fixed GUIDs. Replace them with your own before you add a source to a folder that holds others.

## 1. How to add a source for an XYZ tile server

Most tile servers answer a URL with the zoom level, the column and the row in it, such as
`https://tile.openstreetmap.org/12/2033/1375.png`. This is called an XYZ server. You need a folder with one file:

```
$ mkdir -p my-sources/demo-xyz.zmp
$ cat > my-sources/demo-xyz.zmp/params.txt <<'EOF'
[PARAMS]
GUID={6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F01}
name=Demo XYZ (OpenStreetMap)
ParentSubMenu=Demo
DefURLBase=https://tile.openstreetmap.org/{z}/{x}/{y}.png
ContentType=image/png
Ext=.png
NameInCache=Demo_XYZ_OSM
License=Map data (c) OpenStreetMap contributors, ODbL
EOF
```

The example uses the public OpenStreetMap server. Its usage policy allows a few tiles for testing and forbids bulk
download; do not run big areas against it.

What each line does:

- `GUID` identifies the source. Without a valid one the source is skipped.
- `name` is what `sources` shows and what you give to `--source`.
- `ParentSubMenu` is the group name, field 9 of `sources --format=tsv`. It may be left out.
- `DefURLBase` is the URL of one tile. `{z}`, `{x}` and `{y}` stand for the zoom level (counted from 0), the column
  and the row. The other tags are in the [URL template](#reference-the-url-template) reference.
- `ContentType` is the picture type the server must answer with. `Ext` is the file extension of a tile in the cache.
  PNG, JPEG and WebP tiles can be put into a chart.
- `NameInCache` is the name of the source's directory in the tile cache. Two sources with the same name share their
  tiles.
- `License` is the credit line. It goes into the chart file as its attribution.

Check that the program found it:

```
$ chart-grabber-cli sources --maps=my-sources
                                  GUID  kind                      name  content type  EPSG  cache  verdict
{6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F01}   map  Demo XYZ (OpenStreetMap)     image/png  3857      0       ok
```

Then ask the server for one tile. `--at` is the longitude and latitude of the tile and `--probe-zoom` its level:

```
$ chart-grabber-cli probe --maps=my-sources --at=-1.29,50.76 --probe-zoom=12
                                  GUID                      name             result
{6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F01}  Demo XYZ (OpenStreetMap)  ok 6987 image/png
```

The same source as a JSON file, `my-sources/demo-xyz.source.json`, uses other key names (the
[JSON reference](#reference-sourcejson-format-1) lists them). It needs its own GUID if the `.zmp` folder is in the same
tree:

```
$ cat > my-sources/demo-xyz.source.json <<'EOF'
{
  "format": 1,
  "guid": "{6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F02}",
  "name": "Demo XYZ (OpenStreetMap, JSON)",
  "group": "Demo",
  "url": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Demo_XYZ_OSM_JSON",
  "license": "Map data (c) OpenStreetMap contributors, ODbL"
}
EOF
```

A server with several host names for the same tiles uses `{s}` with `ServerNames=a,b,c`, or the inline form
`https://{a,b,c}.tile.example.org/{z}/{x}/{y}.png`. A server that wants request headers gets them in `RequestHead`;
two headers are separated by the four characters `\r\n`, written exactly so:

```
RequestHead=Referer: https://example.org/\r\nUser-Agent: my-charts
```

## 2. How to add a WMS server

A WMS server draws a picture of any rectangle you name. The URL carries the rectangle as `bbox`. The tag `{bbox}`
becomes `west,south,east,north` of the tile, in metres for the usual projection (EPSG:3857), which is what a chart
needs:

```
$ mkdir -p my-sources/demo-wms.zmp
$ cat > my-sources/demo-wms.zmp/params.txt <<'EOF'
[PARAMS]
GUID={2B8E5A17-3D94-4C60-A7F2-91C0E6B35D02}
name=Demo WMS (EOX Sentinel-2 Cloudless)
ParentSubMenu=Demo
DefURLBase=https://tiles.maps.eox.at/wms?service=wms&version=1.1.1&request=GetMap&layers=s2cloudless-2025_3857&styles=&srs=EPSG:3857&bbox={bbox}&width=256&height=256&format=image/jpeg
ContentType=image/jpeg
Ext=.jpg
NameInCache=Demo_WMS_EOX
License=Sentinel-2 cloudless - https://s2maps.eu by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2025)
EOF
```

The example uses the public demonstration service of EOX. Take only a few tiles from it. The 2025 mosaic is licensed
for non-commercial use; read the terms at https://s2maps.eu before you use more.

The URL must ask for a 256 × 256 picture in EPSG:3857 (`srs=EPSG:3857` for WMS 1.1.1; WMS 1.3.0 calls it
`crs=EPSG:3857`). `sources` lists a JPEG source as `image/jpg`; that is the same type.

```
$ chart-grabber-cli probe --maps=my-sources --filter=WMS --at=-1.29,50.76 --probe-zoom=12
                                  GUID                                 name              result
{2B8E5A17-3D94-4C60-A7F2-91C0E6B35D02}  Demo WMS (EOX Sentinel-2 Cloudless)  ok 8885 image/jpeg
```

## 3. How to build the URL with a script

Some servers want a URL that a template cannot write. A common case is a quadkey: the tile's column and row folded
into one string of digits, such as `0313131322230`. For this a source carries a Pascal script, `GetUrlScript.txt`,
next to `params.txt`. The script runs once for every tile and sets the variable `ResultURL`.

The example needs a server. To keep it harmless it uses a small server on your own machine that answers every request
with a grey picture and prints the path it was asked for. Save it as `tileserver.py` and run it in a second terminal
(it needs `python3`):

```
$ cat > tileserver.py <<'EOF'
import struct, zlib
from http.server import BaseHTTPRequestHandler, HTTPServer

def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

row = b"\x00" + b"\x80" * 256
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", 256, 256, 8, 0, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(row * 256))
       + chunk(b"IEND", b""))

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        print(self.path, flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(png)))
        self.end_headers()
        self.wfile.write(png)
    def log_message(self, *args):
        pass

HTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
EOF
$ python3 tileserver.py
```

Now the source. `params.txt` holds the start of the URL and the script adds the quadkey:

```
$ mkdir -p my-sources/demo-quadkey.zmp
$ cat > my-sources/demo-quadkey.zmp/params.txt <<'EOF'
[PARAMS]
GUID={8C4F9A63-1E05-4B7D-B2A8-47D3F60E9C03}
name=Demo quadkey (script)
ParentSubMenu=Demo
DefURLBase=http://127.0.0.1:8765/
ContentType=image/png
Ext=.png
NameInCache=Demo_Quadkey
EOF
$ cat > my-sources/demo-quadkey.zmp/GetUrlScript.txt <<'EOF'
var q: string;
    i, d: integer;
begin
  q := '';
  for i := GetZ - 1 downto 1 do begin
    d := ((GetX shr (i - 1)) and 1) + 2 * ((GetY shr (i - 1)) and 1);
    q := q + IntToStr(d);
  end;
  ResultURL := GetURLBase + q + '.png';
end.
EOF
```

`GetX`, `GetY` and `GetZ` are the tile's column, row and zoom level. `GetZ` counts from 1, not from 0, so the loop
runs `GetZ - 1` times. `GetURLBase` is the `DefURLBase` line. If the script leaves `ResultURL` empty, the server has no
tile for this place and the program does not ask. All variables and functions are in the
[script reference](#reference-pascal-url-scripts).

The tile at 1.29° W, 50.76° N and zoom level 12 is column 2033, row 1375. Its quadkey is `031313132223`:

```
$ chart-grabber-cli probe --maps=my-sources --filter=quadkey --at=-1.29,50.76 --probe-zoom=12
                                  GUID                   name            result
{8C4F9A63-1E05-4B7D-B2A8-47D3F60E9C03}  Demo quadkey (script)  ok 400 image/png
```

The second terminal shows what the server was asked for:

```
/031313132223.png
```

Looking at what the server received is the way to see the URL your script built. Section 4 uses it again.

## 4. How to test a source

Test in three steps, from the cheapest. Each tells you something the one before could not.

**Step 1: is it loaded?** `sources` lists every source it understood. A folder or file it could not use is reported
before the table, and the source is missing from the list. This folder holds a JSON file with a misspelt key
(`extension` for `ext`), a `.zmp` folder without a GUID, and two JSON files with one GUID:

```
$ chart-grabber-cli sources --maps=broken-sources
warning: There is an error "extension": unknown key in map bad.source.json
warning: Files dup2.source.json and …/broken-sources/dup.source.json have the same GUID
warning: There is an error Empty GUID in map nogui.zmp
                                  GUID  kind             name  content type  EPSG  cache  verdict
{6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F09}   map  Demo XYZ (JSON)     image/png  3857      0       ok
```

The `verdict` column says `ok` when a chart can be made from the source. Any other text is the reason it cannot.

**Step 2: does the server answer?** `probe` requests one tile and prints the result. `ok` is followed by the size of
the tile in bytes and its type. A failure prints the reason, often with the URL, and the exit code is 4. Here the test
server of section 3 is stopped:

```
$ chart-grabber-cli probe --maps=my-sources --filter=quadkey --at=-1.29,50.76 --probe-zoom=12
                                  GUID                   name                                                                                                    result
{8C4F9A63-1E05-4B7D-B2A8-47D3F60E9C03}  Demo quadkey (script)  unknown error: curl error #7 (Could not connect to server) on GET http://127.0.0.1:8765/031313132223.png
$ echo $?
4
```

When a failure does not name the URL, point `DefURLBase` at the test server for a moment. The server prints the path it
receives, whatever the real server would have said. `probe` does not read the cache: it asks the server every time.

**Step 3: does a download work?** Download a few tiles with `download` and a cache directory of its own, so that a
wrong tile does not stay in your real cache:

```
$ chart-grabber-cli download --maps=my-sources --cache=test-cache --source="Demo XYZ (OpenStreetMap)" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12-13
Demo XYZ (OpenStreetMap)
 zoom  SAS z  tiles  bbox  present  marked missing         to download
   12     13      1     1     0.0%            0.0%  ~20.0 KB (assumed)
   13     14      2     2     0.0%            0.0%  ~40.0 KB (assumed)
total             3     3                           ~60.0 KB (assumed)
Demo XYZ (OpenStreetMap)  z13  0/3 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Demo XYZ (OpenStreetMap)  z14  3/3 (100%)  downloaded 3 tiles 20.5 KB  left ~0.0 KB ~00:00:00
Demo XYZ (OpenStreetMap)
 zoom  tiles  present  marked missing  missing     size
   12      1        1               0        0   6.8 KB
   13      2        2               0        0  13.6 KB
total      3        3               0        0  20.5 KB
```

`present` is the number of tiles now in the cache and `missing` the number the server did not deliver. After a good
test `missing` is 0. When you change the URL or the script, delete the test cache (`rm -r test-cache`) or use another
`--cache`: `download` does not fetch a tile that is already in the cache, so the old tiles would hide your change.

If the download looks right, make a chart of the same area with `run` (`cli-guide.md`, section 1).

## 5. Pitfalls of Pascal scripts

A script copied from another program, or written in a hurry, goes wrong in the same few places. This small script puts
most of them into a URL so that you can see them on the test server:

```
var s: string;
begin
  s := 'abcdef';
  ResultURL := GetURLBase + IntToStr(-7 div 2) + ',' + IntToStr(-7 mod 2) + ','
    + IntToStr(Round(2.5)) + ',' + IntToStr(Round(3.5)) + ',' + IntToStr(Round(-2.5)) + ','
    + IntToStr(7 / 2) + ',' + Copy(s, 2, 3) + ',' + IntToStr(Pos('c', s)) + ',' + s[1]
    + ',' + IntToStr(GetZ) + ',' + IntToStr(GetX);
end.
```

For the tile of section 3 the server received `/-3,-1,2,4,-2,3,bcd,3,a,13,2033`. The rules behind the numbers:

- **Integer division cuts toward zero.** `-7 div 2` is `-3`, not `-4`. `mod` has the sign of the left number:
  `-7 mod 2` is `-1`. In this language `/` between two integers is the same as `div`: `7 / 2` is `3`. If either
  number is a real, the result is a real: `7 / 2.0` is `3.5`.
- **`Round` goes to the even number on a half.** `Round(2.5)` is `2`, `Round(3.5)` is `4`, `Round(-2.5)` is `-2`.
- **Strings start at 1.** `s[1]` is the first character. `Copy(s, start, count)` takes `count` characters beginning at
  `start`; a range past the end is cut to fit and a negative count gives an empty string. `Pos(needle, haystack)` is
  the place of the first match, or `0` when there is none. Reading just past the end gives the character with code 0;
  reading far outside the string is an error.
- **A string is bytes.** `Length` counts bytes, so a letter outside ASCII counts as two or more. `Uppercase` and
  `Lowercase` change ASCII letters only.
- **`GetZ` is the zoom level plus one.** The tag `{z}` and every other zoom in this program count from 0.
- **`and` and `or` bind tighter than a comparison.** `if a > 1 and b < 2` does not mean what it seems to, and the
  script does not compile. Write `if (a > 1) and (b < 2) then`. In the same way `$30 + x and 1` means `$30 + (x and 1)`. Both
  sides of `and` and `or` are always evaluated.
- **A number wraps at the size of its variable.** A calculation is done in the type of the leftmost variable in it: a
  `byte` wraps at 256, a `word` at 65536, an `integer` at about two billion. Declare variables that hold large
  products as `Int64`.
- **The argument order of the helper functions is not the same everywhere.** `Pos(needle, haystack)`,
  `PosEx(needle, haystack, from)`, `GetAfter(needle, haystack)` and `GetBefore(needle, haystack)` start with the
  needle; `SubStrPos(haystack, needle, from)` and `GetBetween(haystack, after, before)` start with the haystack.
- **`StringReplace` replaces only the first match** unless the last argument is `[rfReplaceAll]`.
- **Numbers do not turn into strings by themselves.** Use `IntToStr`. `StrToInt` stops the tile with an error on any
  stray character, a space included; `StrToIntDef(text, default)` does not.
- **Regular expressions are older than yours.** The pattern engine treats `.` as matching a line break, `$` as the very
  end of the text, and `\s`, `\w` and `\b` as ASCII only. A pattern is case-sensitive. In the replacement of
  `RegExprReplaceMatchSubStr` write `$1` for a group; `\1` is just `1`. A bad pattern stops the tile with an error.
- **`Random` is shared by all sources.** Do not count on a sequence.

Two more traps are not about the language:

- **A script that does not compile is ignored, and nothing says so.** The program then requests `DefURLBase` as it is
  written, and `probe` may still report `ok` if the server answers that address. A byte-order mark at the start of
  `GetUrlScript.txt` also makes it not compile; save the file without one. After every change check the URL your script
  produced (section 3).
- **A runtime error stops one tile, not the run.** `probe` shows it as the result, for example
  `error: Division by zero at line 9`, with exit code 4.

## 6. How to warn about terms of use and navigation

Two optional keys of `params.txt` make the program print a warning on standard error.

`TermsOfUseWarning=<address>` marks a source whose provider may not allow offline use. The program prints the address
whenever `sources` lists the source, and at the start of `download`, `run` and `resume`. The notices below come from a
folder `warn-sources` that holds one source:

```
$ mkdir -p warn-sources/depth.zmp
$ cat > warn-sources/depth.zmp/params.txt <<'EOF'
[PARAMS]
GUID={6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F0A}
name=Demo depth
DefURLBase=http://127.0.0.1:8765/{z}/{x}/{y}.png
ContentType=image/png
Ext=.png
NameInCache=Demo_Depth
NavigationWarning=1
TermsOfUseWarning=https://example.org/terms
EOF
$ chart-grabber-cli sources --maps=warn-sources
warning: "Demo depth": the provider's terms of use may not allow downloading tiles for offline use. Read https://example.org/terms before downloading; you are responsible for complying.
                                  GUID  kind        name  content type  EPSG  cache  verdict
{6F1D3C52-84A0-4E7B-9C21-5B0A7D2E4F0A}   map  Demo depth     image/png  3857      0       ok
```

`NavigationWarning=1` marks a source that shows depths or other chart data that is not an official nautical chart.
`download`, `run` and `resume` print a notice before the first tile is requested, once for each such source, and `-q`
does not hide it. `sources` and `export` do not print it. The notice follows the terms warning when a source has both:

```
$ chart-grabber-cli download --maps=warn-sources --cache=test-cache --source="Demo depth" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12
Demo depth
 zoom  SAS z  tiles  bbox  present  marked missing         to download
   12     13      1     1     0.0%            0.0%  ~20.0 KB (assumed)
total             1     1                           ~20.0 KB (assumed)
warning: "Demo depth": the provider's terms of use may not allow downloading tiles for offline use. Read https://example.org/terms before downloading; you are responsible for complying.
warning: "Demo depth": not for navigation. This source is not an official nautical chart; do not use it to navigate.
Demo depth  z13  0/1 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Demo depth  z13  1/1 (100%)  downloaded 1 tiles 0.4 KB  left ~0.0 KB ~00:00:00
Demo depth
 zoom  tiles  present  marked missing  missing    size
   12      1        1               0        0  0.4 KB
total      1        1               0        0  0.4 KB
```

The warnings change nothing else: the download goes on. In the JSON format the terms warning is the key
`termsOfUseWarning`; the navigation notice is the key `navigationWarning` (`true` or `false`).

Whether a provider allows bulk or offline download is for you to find out. Many do not. You are responsible for what
you download.

## Reference: the folder of a source

| Member | Needed | What it is |
|---|---|---|
| `params.txt` | yes | The settings: a `[PARAMS]` section of `key=value` lines. UTF-8, with or without a byte-order mark; a file that is not valid UTF-8 is read as Windows-1251. Key names are not case-sensitive. When a key appears twice, the first counts. |
| `GetUrlScript.txt` | no | The Pascal script (reference below). Read as bytes, without conversion. |
| `24.bmp`, `18.bmp` (or `.png`) | no | Large and small icon. They are read and not used. `BigIconName` and `SmallIconName` name other files. |
| `info.txt`, `index.html`, and the same with a language, such as `info_ru.txt` | no | A description page for other programs. Not used here. |
| `EmptyTiles/` | no | Pictures the server sends for "no tile here". A downloaded tile that is byte for byte equal to one of these files counts as missing and is not stored. Every file in the folder counts, except `*.ini`. |
| `BanTiles/` | no | Pictures the server sends when it has blocked you. A tile equal to one of them counts as a failure and is not stored. Same rules as `EmptyTiles/`. |

`EmptyTiles/` and `BanTiles/` work in a `.zmp` folder and in a `.zmp` that is a zip file.

Text that depends on the language can carry a language code: `name_ru=…`. The program runs in English, so it reads
`name` and `name_en` (`name_en` wins) and ignores the other codes. The same holds for `ParentSubMenu` and `License`.

## Reference: `params.txt`

All keys go into the `[PARAMS]` section. A yes/no value is `1` or `0`. "Ignored" means the key is read and changes
nothing in a download by this program.

### Identity

| Key | Type | Default | Meaning |
|---|---|---|---|
| `GUID` | GUID in braces | none, required | Identifies the source. Empty, malformed or all zeros: the source is skipped with `Empty GUID`. Two sources with one GUID in a folder: the second is skipped with a warning. In several `--maps` folders the later one wins. |
| `name` | text | `map#-1` | Name shown by `sources`, accepted by `--source`. `name_<code>` gives it for a language. |
| `ParentSubMenu` | text | empty | Group, field 9 of `sources --format=tsv`. `\` separates levels. |
| `asLayer` | yes/no | 0 | 1 makes the source a transparent layer, for `--overlay`. `sources` shows `layer` in the `kind` column. |
| `Version` | text | empty | The value of `{ver}` in the URL and of the script variable `Version`. |
| `License` | text | empty | The credit line. It goes into the chart as its attribution (`--attribution` replaces it) and is field 12 of the TSV listing. |
| `pnum` | integer | -1 | Position in a menu of another program. Ignored; `sources` sorts by name. |
| `LayerZOrder` | integer | 0 | Stacking order of layers in a viewer. Ignored. |
| `DefHotKey`, `HotKey`, `separator`, `Enabled`, `CanShowOnSmMap` | integer, integer, yes/no, yes/no, yes/no | 0, 0, 0, 1, 1 | Menu items and keyboard shortcut of another program. Ignored. |
| `BigIconName`, `SmallIconName` | file name | `24.bmp`, `18.bmp` | Icon files. Ignored. |
| `MapCenter` | `lon,lat,zoom` | empty | Where another program opens the map. Ignored. |

### URL and requests

| Key | Type | Default | Meaning |
|---|---|---|---|
| `DefURLBase` | text | empty | The URL template, or the start of the URL for a script. See [the URL template](#reference-the-url-template). |
| `URLBase` | text | `DefURLBase` | Wins over `DefURLBase` when present. |
| `ServerNames` | list | empty | The values of `{s}`, separated by commas. One of them is picked and kept for the run. |
| `RequestHead` | text | empty | Headers sent with every request. Separate two headers with `\r\n` (these four characters). A script can change them. |
| `IsUseDownloaderInScript` | yes/no | 0 | 1 lets the script make its own web requests with `Downloader`. |
| `Proj4Args` | text | empty | Arguments for a projection converter that a script can use as `DefProjConverter`. |
| `UsePreloadPage`, `PreloadPage` | integer, URL | 0, empty | When `UsePreloadPage` is above 0, the program requests the page `PreloadPage` before the first tile, to collect the cookies a server wants. |
| `Sleep` | milliseconds | 0 | Wait this long after each request of a connection. Use it to keep to a rate limit: 700 with one connection is about 85 requests a minute. |
| `MaxConnectToServerCount` | integer | 4 | Connections to the server at the same time, 1 to 64. `--threads` is lowered to this number for the source, with a warning. |
| `AllowUseCookie` | yes/no | 0 | 1 keeps the cookies a server sets and sends them back. |
| `IteratorSubRectSize` | `X,Y` | `1,1` | The size of the block of tiles taken together in one step. Leave it out. A value that is not two integers stops the source from loading. |

### What the server answers

| Key | Type | Default | Meaning |
|---|---|---|---|
| `ContentType` | list | `image/jpg` | The types the server may answer with, separated by commas or semicolons; write it without quotes. A tile of another type fails with `bad content type`. |
| `Ext` | text with a dot | `.jpg` | The extension of a tile in the cache. Converted to lower case. |
| `DefaultContentType` | text | `image/jpg` | The type to assume when the server names none. |
| `IgnoreContentType` | yes/no | 0 | 1 accepts any type and takes `DefaultContentType`. |
| `DetectContentType` | yes/no | 0 | 1 works out the type from the first bytes of the tile. Use it when a server names the type wrongly. |
| `MimeTypeSubst` | pairs | empty | Rewrites types a server gets wrong. Pairs `received=wanted` separated by `;`, the whole value in double quotes: `"image/png8bit=image/png"`. |
| `UseDwn` | yes/no | 1 | 0 switches downloading off. `sources` then says `downloads are disabled for this map`. |

### Projection

| Key | Type | Default | Meaning |
|---|---|---|---|
| `EPSG` | integer | from `projection` | 3857 (Web Mercator, the usual), 3785 (an old name of 3857), 53004 (Esri sphere), 3395 (Mercator on the WGS 84 ellipsoid), 4326 (latitude and longitude). A chart can be made only from 3857. |
| `projection` | integer | 1 | Used when `EPSG` is absent. 1 is the sphere Mercator, 2 the ellipsoid Mercator, 3 latitude and longitude. |
| `sradiusa`, `sradiusb` | metres | 6378137, same as `sradiusa` | Radii of the sphere or ellipsoid. |
| `EPSG` in a `[ViewInfo]` section | integer | none | A second projection for display. Ignored. |

### Cache

| Key | Type | Default | Meaning |
|---|---|---|---|
| `NameInCache` | text | empty | The source's directory in the tile cache. Use letters, digits, `_`, `-` and `.`. |
| `CacheType` | integer | 0 | The way tiles are stored. 0 is the program's default (SQLite files), 71 is SQLite. A weather source with `SnapshotIndex` needs 71. 9 (memory only) is treated as the default. Leave this out unless you know a reason. |
| `MainStorageContentType` | text | empty | The type of the stored tile when it differs from the one downloaded. Leave it out. |
| `IsReadOnly` | yes/no | 0 | 1 forbids adding tiles: a download is refused with `the cache of this map is read-only`. |
| `AllowRead`, `AllowScan`, `AllowAdd`, `AllowDelete`, `AllowReplace` | yes/no | 1, 1, not `IsReadOnly`, not `IsReadOnly`, not `IsReadOnly` | What may be done with the stored tiles. `AllowAdd=0` refuses a download, like `IsReadOnly`. |
| `UseMemCache`, `MemCacheCapacity`, `MemCacheTTL`, `MemCacheClearStrategy`, `RestartDownloadOnMemCacheTTL` | yes/no, integer, milliseconds, integer, yes/no | 1, 100, 60000, 1, 0 | A cache in memory in front of the files. Leave them out. |

### Cropping and cutting

SAS.Planet can crop or cut the tiles of a server that sends other sizes. This program does not. A source with a crop or
a cut switched on is refused with `has crop or cut after download switched on; this is not supported`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `TileRLeft`, `TileRTop`, `TileRRight`, `TileRBottom` | integer | 0 | The crop rectangle. All four 0 means no crop; so does a rectangle whose right edge is not beyond the left, whose bottom is not beyond the top, or that reaches 10000. |
| `CutCountX`, `CutCountY`, `CutSizeX`, `CutSizeY`, `CutTileX`, `CutTileY`, `CutToSkip` | integer, text | 0, empty | Cutting a tile into parts. |

### Vector maps

A source of vector data can set how its marks look, in the sections `[PARAMS_Vector_Point]`, `[PARAMS_Vector_Line]` and
`[PARAMS_Vector_Poly]`. The keys `IsForceTextColor`, `IsForceShadowColor`, `IsForceFontSize`, `IsForceIconSize`,
`IsForceIconName`, `IsForceLineColor`, `IsForceLineWidth` and `IsForceFillColor` are read and not used.

### Keys of this program

SAS.Planet does not know these keys and ignores them.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `TermsOfUseWarning` | URL | empty | Section 6. Printed as written. |
| `NavigationWarning` | yes/no | 0 | Section 6. `true` is accepted as `1`. |
| `MaxZoom` | integer 0 to 23 | none | The highest zoom level, counted from 0. For a higher level the program skips the source with a warning; if every chosen level is higher, the job is refused. |
| `SnapshotIndex` | URL | none | Makes a weather source: an `http` or `https` address of an index that names the newest picture, the frame. `download` and `run` ask it once; the frame becomes `{ver}` and the cache key, so a later run gets a newer picture. The next five keys are read only when this one is set. |
| `SnapshotIndexType` | `json-last`, `wms-default`, `wmts-default`, `wmts-domain` or `none-refetch` | none, required | How to read the index: the last entry of a list in JSON, the default time of a WMS layer, the default time of a WMTS layer, or the time domain of a WMTS layer. `none-refetch` is for a layer that has no time dimension (for example the NOAA nowCOAST tropical cyclones): nothing is asked of the index address, and the moment of the run is the frame, so every download takes fresh tiles instead of serving the picture in the cache; the older picture is removed like an older frame. Give the service address as `SnapshotIndex`; the URL needs no `{ver}`. Only `wmts-default` lets the user pick another frame with `--time` (`cli-guide.md`, section 13). |
| `SnapshotItem` | text | none | For `json-last` the path of the list in the JSON, such as `radar.past`; for `wms-default` the layer name; for `wmts-default` the layer identifier from the capabilities document; empty for `wmts-domain` and `none-refetch`. |
| `SnapshotPick` | `default` or `nearest-now` | `default` | `wms-default` and `wmts-default` only. `default` takes the default time of the layer (a forecast layer such as RTOFS may default to its last forecast hour). `nearest-now` takes the frame of the layer's time dimension nearest to the moment of the run (a tie goes to the earlier frame), but not later than now plus one step of the dimension; if the layer has no such frame, or its time dimension lists no readable times, the download stops with an error. `--time` still wins when given. |
| `SnapshotTimeField` | text | `time` | `json-last` only: the member of the last entry that holds the time. |
| `SnapshotVersionField` | text | the time field | `json-last` only: the member whose value becomes `{ver}`. |

A wrong combination of the snapshot keys refuses the source and says why. A snapshot source must use the cache type 71.
A `wmts-default` source passes the frame to the server in the tile URL, for example `…/{z}/{x}/{y}.png?TIME={ver}`: `{ver}` is the layer's default time, or the time given with `--time`.

### Keys that are ignored

The program does not read `BanIfLen`, `UseGenPrevious`, `Usedel`, `Usesave`, `Usestick`, `DelAfterShow`, `active` and
`UseAntiBan`. A key it does not know at all is also ignored, without a warning, so a misspelt key can go unnoticed.
Compare a surprising result with the spelling in this reference.

## Reference: the URL template

`DefURLBase` (or `url` in a JSON file) is a template. Each part in braces is replaced for each tile and the rest is
copied. Names in braces are not case-sensitive. A brace that does not belong to a known tag stays in the URL as it is.

| Tag | Becomes |
|---|---|
| `{x}`, `{y}` | The column and row of the tile, counted from the top left. |
| `{-y}` | The row counted from the bottom: `2^z - 1 - y`. For TMS servers. |
| `{z}` | The zoom level, counted from 0. |
| `{q}` | The quadkey of the tile (empty at level 0). |
| `{s}` | One of the names in `ServerNames`. |
| `{a,b,c}` | A tag with commas is a list of server names written in place; one is picked. |
| `{bbox}` | `west,south,east,north` of the tile in the source's projection: metres for 3857, degrees for 4326. Eight decimals. |
| `{ver}` | The `Version` of the source, or the frame of a weather source. |
| `{lang}` | The language code of the program. |
| `{timestamp}` | The time in milliseconds since 1970. |
| `{sas_path}` | `z<zoom+1>/<x div 1024>/x<x>/<y div 1024>/y<y>`, the path format of the SAS.Planet cache. |
| `{z+1}`, `{x*2}`, `{y/2}` | A calculation on `x`, `y` or `z` with whole numbers: `+`, `-`, `*`, `/`. `*` and `/` come first, otherwise left to right. No brackets, no spaces; `/` drops the fraction. A calculation that cannot be done gives an empty string. |

## Reference: `*.source.json` format 1

One JSON object in a file whose name ends in `.source.json`, in any letter case. UTF-8, a byte-order mark allowed, no
comments. A key that is not in the table, a value of the wrong type or a missing required key makes the source fail
to load, with a message that names the key (see Troubleshooting). Each key becomes the `params.txt` key in the table;
what that key does is explained above.

Text that depends on the language is a string, or an object with the default under the empty key:
`{ "": "Bing - Satellite", "ru": "Bing - Спутник" }`.

| Key | Type | Required | `params.txt` key | Rule |
|---|---|---|---|---|
| `format` | integer | yes | none | Must be `1`. |
| `guid` | string | yes | `GUID` | Braces and 8-4-4-4-12 hex digits, any case. |
| `name` | text | yes | `name` | |
| `group` | text | | `ParentSubMenu` | |
| `sortIndex` | integer | | `pnum` | |
| `layer` | true/false | | `asLayer` | |
| `layerZOrder` | integer | | `LayerZOrder` | |
| `url` | string | yes, unless there is a script | `DefURLBase` | The URL template. |
| `servers` | list of strings | | `ServerNames` | |
| `headers` | object of strings | | `RequestHead` | `{ "Referer": "https://example.org/" }`; the program joins them. |
| `version` | string | | `Version` | |
| `epsg` | integer | | `EPSG` | 3857, 3395, 4326 or 3785. |
| `contentType` | string | yes | `ContentType` | |
| `ext` | string | yes | `Ext` | With the dot. |
| `detectContentType` | true/false | | `DetectContentType` | |
| `defaultContentType` | string | | `DefaultContentType` | |
| `ignoreContentType` | true/false | | `IgnoreContentType` | `true`: the type the server sends is not checked; `defaultContentType` is used instead. |
| `contentTypeSubst` | object of strings | | `MimeTypeSubst` | `{ "image/png8bit": "image/png" }`: a received type is replaced by the given one. Not empty; no empty key. |
| `nameInCache` | string | yes | `NameInCache` | Letters, digits, `_`, `-` and `.` only. |
| `license` | text | | `License` | |
| `maxConnections` | integer | | `MaxConnectToServerCount` | |
| `sleepMs` | integer | | `Sleep` | |
| `crop` | object `left`, `top`, `right`, `bottom` | | `TileRLeft`, `TileRTop`, `TileRRight`, `TileRBottom` | Refused at download, as above. |
| `emptyTiles` | list of base64 strings | | the files of `EmptyTiles/` | Each string is the content of one sample picture. |
| `allowHttp` | true/false | | `IsUseDownloaderInScript` | |
| `script` | object | | | `{ "file": "name.js" }` or `{ "code": "…" }`. See the JavaScript section. |
| `termsOfUseWarning` | string | | `TermsOfUseWarning` | |
| `navigationWarning` | true/false | | `NavigationWarning` | `true`: the notice of section 6 is printed. |
| `maxZoom` | integer 0 to 23 | | `MaxZoom` | |
| `cacheType` | integer | | `CacheType` | Only `71`. |
| `snapshot` | object | | `SnapshotIndex`, `SnapshotIndexType`, `SnapshotItem`, `SnapshotTimeField`, `SnapshotVersionField`, `SnapshotPick` | Members `index`, `type`, `item`, `timeField`, `versionField`, `pick`; `index` and `type` are required. |

A JSON file has no keys for the other `params.txt` keys. A source that needs one of them is a `.zmp` folder.

## Reference: Pascal URL scripts

The script is a Pascal program without a name: an optional `var` section and optional functions and procedures, then
`begin … end.`. It runs once for each tile request and sets `ResultURL`. It is a subset of the language: no `record` or
`type` declarations, no classes, no units. A construct it does not know is a compile error, and a script that does
not compile is ignored (section 5). Comments are `// to the end of the line`, `(* … *)` and `{ … }`.

### Variables

These exist in every script. "Out" means the value is read back after the script ends.

| Variable | Type | Meaning |
|---|---|---|
| `GetX`, `GetY` | integer | Column and row of the tile. |
| `GetZ` | integer | Zoom level plus one. |
| `GetLlon`, `GetTLat`, `GetRLon`, `GetBLat` | real | West, north, east and south edge of the tile in degrees. |
| `GetLMetr`, `GetTMetr`, `GetRMetr`, `GetBMetr` | real | The same edges in metres of the source's projection. |
| `GetURLBase` | string | The `DefURLBase` text. A script may change its own copy; the next tile starts again from the original. |
| `Version`, `Lang` | string | The `Version` of the source or frame, and the language code. |
| `RequestHead` | string, out | The request headers. Change them with `SetHeaderValue`. |
| `ResponseHead` | string | The headers of the previous answer of this connection, empty at first. |
| `ScriptBuffer` | string, out | A text that is kept from one tile to the next, for this source. |
| `ResultURL` | string, out | The URL. Cleared before each tile. Empty means "no tile here": no request, and not an error. |
| `PostData` | string, out | When not empty, the request is a POST with this body. Cleared before each tile. |
| `Downloader` | object | Makes web requests; only with `IsUseDownloaderInScript=1`, otherwise `nil`. |
| `Global` | object | Values shared by all connections of the source. |
| `Logger` | object | Writes lines to a log file. |
| `TileCache` | object | Reads and writes tiles in the source's own cache. |
| `Converter`, `DefProjConverter`, `ProjFactory` | objects | Projection helpers for scripts that convert coordinates. |

`Downloader.DoHttpRequest(url, requestHeader, postData, var responseHeader, var responseBody)` returns the HTTP status as
a number, `0` if there was no answer. It makes a POST when `postData` is not empty.

`Global.SetVar(id, value)`, `Global.GetVar(id)` and `Global.Exists(id)` keep values under whole-number keys. They do not
lock by themselves: put them between `Global.Lock` and `Global.Unlock` when several connections can reach them. The lock
is not re-entrant. `SetVarTS`, `GetVarTS` and `ExistsTS` lock for one call.

`TileCache.Read(x, y, zoom, version, withData)` returns a record `TTileInfo` with `IsExists`, `IsExistsTne`,
`LoadDate`, `Size`, `Version`, `ContentType` and `Data`. `Write`, `WriteTne` and `Delete` change tiles in the same
cache.

### What a script may not do

These limits cannot be switched off:

- `SaveToLocalFile` writes nothing and returns `0`. `FileExists` always says `False`.
- `Downloader.DoHttpRequest` with a `file:///` address returns status `0`. `http` and `https` work.
- A script has no other access to files.
- A script that runs longer than 5 seconds, or for about fifty million steps, is stopped and the tile fails.
- `WriteLn` writes nowhere while tiles are downloaded.

This script, with `IsUseDownloaderInScript=1` and the test server of section 3, shows the first three:

```
var head, body, a: string;
    st1, st2, n: integer;
begin
  st1 := Downloader.DoHttpRequest('file:///etc/hostname', '', '', head, body);
  st2 := Downloader.DoHttpRequest('http://127.0.0.1:8765/probe', '', '', head, body);
  n := SaveToLocalFile('/tmp/should-not-exist.txt', 'x');
  a := 'no';
  if FileExists('/etc/hostname') then a := 'yes';
  ResultURL := GetURLBase + IntToStr(st1) + ',' + IntToStr(st2) + ',' + IntToStr(n) + ',' + a;
end.
```

The server received `/probe` and then `/0,200,0,no`. The `file:///` request returned 0, the `http` request 200, nothing
was written and `FileExists` said no.

### Functions

Built into the language: `IntToStr`, `StrToInt`, `StrToIntDef`, `StrToFloat`, `FloatToStr`, `Copy`, `Pos`, `Insert`,
`Delete`, `Length`, `SetLength`, `Chr`, `Ord`, `Round`, `Trunc`, `Int`, `Frac`, `Abs`, `Sqr`, `Sqrt`, `Sin`, `Cos`,
`ArcTan`, `Exp`, `Ln`, `Assigned`, `Uppercase`, `Lowercase`, `Trim`, `Inc`, `Dec`, `High`, `Low`, `VarType` and `Pi`.
`StrToInt` accepts a sign and digits, or `$` and hex digits, and fails on anything else. `Trim` removes every character
up to a space from both ends.

Added by the program:

| Function | Result |
|---|---|
| `Random(n)`, `RandomRange(from, to)` | A whole number from 0 to n-1, or from `from` up to but not including `to`. |
| `Power(base, exponent)`, `IntPower(base, exponent)` | A power. `IntPower` takes a whole exponent and is exact for whole bases. |
| `Ceil(x)`, `Floor(x)` | Rounded up and down to a whole number. |
| `Log2(x)` | The base-2 logarithm; an error for `x` of 0 or less. `Ln` is the same for the natural logarithm. |
| `Max(a, b)`, `Min(a, b)`, `MaxExt(a, b)`, `MinExt(a, b)` | Larger and smaller of two whole numbers, and of two reals. |
| `RegExprGetMatchSubStr(text, pattern, n)` | Part `n` of the first match (0 is the whole match), or an empty string. |
| `RegExprReplaceMatchSubStr(text, pattern, replacement)` | Replaces every match; the text is unchanged when nothing matches. `$1` in the replacement is a group. |
| `Base64Encode(s)`, `Base64UrlEncode(s)`, `Base64Decode(s)` | Base 64 with padding; the URL alphabet without padding; the decoding of the first (an empty string for bad input). |
| `IntToHex(n, digits)` | Capital hex digits, padded to `digits`. |
| `Format(format, [args])`, `FormatEx(format, [args], separator)` | Delphi `Format` with `%d %u %s %x %e %f %g`; `Format` uses a dot as the decimal separator. |
| `PosEx(needle, haystack, from)`, `SubStrPos(haystack, needle, from)` | The place of a match at or after `from`, or 0. |
| `StringReplace(s, old, new, flags)` | `flags` is `[]` or `[rfReplaceAll]`, optionally with `rfIgnoreCase`. |
| `MD5String(s)` | The MD5 of the bytes in capital hex. |
| `RoundEx(x, digits)` | `x` with exactly `digits` decimals and a dot, as a string. |
| `GetAfter(needle, haystack)`, `GetBefore(needle, haystack)`, `GetBetween(haystack, after, before)` | The part of the text after, before or between; an empty string when not found. |
| `GetNumberAfter(needle, text)` | The run of digits right after the first `needle`, as a string. |
| `GetDiv3Path(number)` | The digits in groups of three counted from the right, each followed by `\`; a short leading group is dropped. |
| `SetHeaderValue(headers, name, value)`, `GetHeaderValue(headers, name)`, `DeleteHeaderEntry(headers, name)` | Change, read or remove one header in a block of header lines. The name is matched as the start of a line, without regard to letter case. |
| `GetUnixTime` | The current time in seconds since 1970 (UTC). |
| `FileExists(name)`, `SaveToLocalFile(name, data)` | Switched off, see above. |
| `WriteLn(text)` | No effect while tiles are downloaded. |
| `TemplateToUrl(template)` | Fills in a [URL template](#reference-the-url-template) for this tile. |

Rules of the language that scripts rely on:

| Subject | Rule |
|---|---|
| Whole numbers | `div`, `mod`, `shl` and `shr` work as in Delphi. Division by zero stops the tile with `Division by zero`. A calculation takes the type of its leftmost operand and wraps at that type's size: `byte` 8 bits, `word` 16, `integer` 32 (signed), `cardinal` 32 (unsigned), `int64` 64. A shift count is taken modulo 32, and modulo 64 for `int64`. |
| Real numbers | Reals have the precision of the x87 `Extended` type. A whole number and a real compare as reals. |
| Strings | Bytes, counted from 1. Comparisons go byte by byte. A new string is empty. |
| Assigning | A whole number widens to a real. A real into a whole-number variable is an error. A number is never turned into text by itself. |
| Variables | Every variable starts as 0, empty, false or `nil`. A name in a function refers to the local first, then the global one, then the built-in. |
| Precedence | `not` and the unary signs bind tightest; then `* / div mod and shl shr`; then `+ - or xor`; then the comparisons. `and` and `or` are bitwise on numbers and logical on booleans, and never short-circuit. |
| Control | `if`, `case`, `for`, `while`, `repeat`, `break`, `continue`, `exit`, `try … finally` and `try … except`. |

## Reference: JavaScript

A `*.source.json` file can carry a JavaScript function that builds the URL of each tile. Put it in the `script` key as
`"script": { "code": "..." }` (see the table of keys above). The script defines one function:

```js
function getUrl(tile, ctx) {
  return ctx.urlBase.replace('{x}', tile.x).replace('{y}', tile.y).replace('{z}', tile.z);
}
```

`getUrl` returns one of:

- a string: the URL, fetched with the headers of the `headers` key;
- an object `{ url, headers, postData }`: `headers` replaces the request headers, a non-empty `postData` makes the
  request a POST;
- `null`, `undefined` or `""`, or an object whose `url` is `""`: there is no tile here. This is not an error. The
  program does not ask the server. `probe` prints `error: Tile does not exist` and exits with 4; `download` counts
  such a tile as `missing`.

`tile` describes the tile:

| Name | Value |
|---|---|
| `tile.x`, `tile.y` | the tile column and row, as `{x}` and `{y}` |
| `tile.z` | the zoom level, counted from 0, as `{z}` |
| `tile.zoom1` | the zoom level counted from 1, which is `tile.z + 1` |
| `tile.lonLeft`, `tile.latTop`, `tile.lonRight`, `tile.latBottom` | the tile edges in degrees |
| `tile.mLeft`, `tile.mTop`, `tile.mRight`, `tile.mBottom` | the same edges in metres of the EPSG:3857 projection |

`ctx` holds `urlBase`, `version`, `lang`, `requestHeaders`, `lastResponseHeaders` and `state`. `template(text)` fills
in a [URL template](#reference-the-url-template) for this tile. `http.get(url)` and `http.post(url, body)` exist only
when the source sets `allowHttp` to `true`. The other names are described below.

The examples below use the test server of section 3 with one change. It sends one more header, `X-Next`, that holds
the number of requests it has answered. In `tileserver.py` add `count = 0` before the class, add
`global count; count += 1` as the first line of `do_GET`, and add this line after the `Content-Length` header:

```
        self.send_header("X-Next", "t%d" % count)
```

**`ctx.lastResponseHeaders`** is the header text of the server's previous answer to this download worker, or `""`
before the first one. A worker is one connection, so set `"maxConnections": 1` when the order matters. This source
puts the `X-Next` value of the previous answer into the next URL, and has no tile in odd columns:

```
$ cat > my-sources/demo-token.source.json <<'EOF'
{
  "format": 1,
  "guid": "{8C4F9A63-1E05-4B7D-B2A8-47D3F60E9C11}",
  "name": "Demo token (script)",
  "group": "Demo",
  "url": "http://127.0.0.1:8765/{z}/{x}/{y}.png",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Demo_Token",
  "maxConnections": 1,
  "script": { "code": "function getUrl(tile, ctx) {\n  if (tile.x % 2 === 1) return '';\n  var prev = pascal.regExprGetMatchSubStr(ctx.lastResponseHeaders, 'X-Next: (\\\\w+)', 1);\n  log('previous token: ' + prev);\n  return template(ctx.urlBase) + '?after=' + prev;\n}" }
}
EOF
$ chart-grabber-cli probe --maps=my-sources --filter=token --at=-1.29,50.76 --probe-zoom=12
                                  GUID                 name                      result
{8C4F9A63-1E05-4B7D-B2A8-47D3F60E9C11}  Demo token (script)  error: Tile does not exist
$ chart-grabber-cli download --maps=my-sources --cache=test-cache --source="Demo token (script)" --bbox=-1.34,50.76,-1.24,50.77 --zooms=13
…
 zoom  tiles  present  marked missing  missing    size
   13      6        2               0        4  0.8 KB
total      6        2               0        4  0.8 KB
```

The test server saw two requests. The first has no earlier answer, so `prev` is empty:

```
/13/4066/2750.png?after=
/13/4066/2751.png?after=t1
```

**`ctx.state`** is a string that a worker keeps from one call to the next. It starts as `""`. To change it, assign
text to it inside `getUrl`: `ctx.state = String(n);`. The next call of the same worker reads that text. A value that
is not text is converted to text. If `getUrl` throws, the state stays as it was before the call.

**`shared` and `shared.locked`** hold text that all workers of one source can read and write while the program runs.
`shared.set(key, value)` stores `value` as text. `shared.get(key)` returns it, or `undefined` for a key never set.
A token that every tile needs, but that must be fetched once, goes inside `shared.locked(fn)`. It runs `fn` while no
other worker is inside `shared.locked`, and returns what `fn` returns. Do not call `shared.locked` inside `fn`.
This source asks `/key` once for all tiles and puts the answer into every URL. It also uses `pascal.div`, below:

```
$ cat > my-sources/demo-key.source.json <<'EOF'
{
  "format": 1,
  "guid": "{8C4F9A63-1E05-4B7D-B2A8-47D3F60E9C12}",
  "name": "Demo key (script)",
  "group": "Demo",
  "url": "http://127.0.0.1:8765/{z}/{x}/{y}.png",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Demo_Key",
  "allowHttp": true,
  "script": { "code": "function getUrl(tile, ctx) {\n  var key = shared.locked(function () {\n    var k = shared.get('key');\n    if (k === undefined) {\n      var r = http.get('http://127.0.0.1:8765/key');\n      k = pascal.regExprGetMatchSubStr(r.headers, 'X-Next: (\\\\w+)', 1);\n      shared.set('key', k);\n    }\n    return k;\n  });\n  return template(ctx.urlBase) + '?key=' + key + '&half=' + pascal.div(tile.x, 2);\n}" }
}
EOF
$ chart-grabber-cli download --maps=my-sources --cache=test-cache --source="Demo key (script)" --bbox=-1.34,50.76,-1.24,50.77 --zooms=13
…
 zoom  tiles  present  marked missing  missing    size
   13      6        6               0        0  2.3 KB
total      6        6               0        0  2.3 KB
```

The test server saw one request for `/key`, then six tiles with the same key:

```
/key
/13/4065/2750.png?key=t1&half=2032
/13/4066/2750.png?key=t1&half=2033
…
```

`http.get` returns `{ status, headers, body }`. `status` is the server's status code, or `0` when no answer came.
`headers` is the raw header text. `body` is text, and it is empty unless the answer was a success.

Both calls take an optional last argument: an object of strings that become request headers of that call, for example
`http.get(url, { 'X-Test': 'b' })` or `http.post(url, body, { 'X-Test': 'a' })`. `http.post` with an empty `body`
sends a GET request.

**`pascal`** holds helpers whose result differs from the obvious JavaScript. They give the same results as the same
functions in a Pascal script, so a script moved from `GetUrlScript.txt` keeps its URLs:

| Call | Result |
|---|---|
| `pascal.round(x)` | the nearest integer; a half goes to the even one: `pascal.round(2.5)` is `2`, `Math.round(2.5)` is `3` |
| `pascal.roundEx(x, digits)` | text with `digits` decimals |
| `pascal.div(a, b)`, `pascal.mod(a, b)` | integer quotient and remainder; the remainder has the sign of `a`; `b` = 0 throws |
| `pascal.regExprGetMatchSubStr(s, re, i)` | group `i` of the first match, `0` for the whole match; `""` if there is none |
| `pascal.regExprReplaceMatchSubStr(s, re, repl)` | `s` with every match replaced; `$1` in `repl` is group 1 |
| `pascal.random(n)`, `pascal.randomRange(a, b)` | a random integer from 0 up to `n`, or from the smaller of `a` and `b` up to the larger one. The upper bound is not included: `pascal.random(3)` gives 0, 1 or 2. `Math.random()` uses the same generator |

**`log(text)`** takes one text. The command line program does not show it, so `log` has no visible effect there.

A call that runs longer than 5 seconds is stopped and that tile fails with `script time limit exceeded`; time spent
inside `http` is not counted. A script that throws fails that tile with the error text. A script with a syntax error,
no `getUrl`, or an error in its top-level code switches downloads off for that map, and the reason is printed.
`sources` shows it in the last column instead of `ok`, `probe` prints `skipped: ` and the same text (for example
`skipped: the download subsystem of this map is disabled: function getUrl(tile, ctx) is not defined`), and `download`
refuses to start with it. The text of a syntax error or a throw in the top-level code ends with `at line N`.

## Troubleshooting

**`warning: There is an error Empty GUID in map nogui.zmp`** (the source is missing from `sources`)
The `GUID` line is missing, empty, malformed or all zeros. Make one with `uuidgen` (start of this guide).

**`warning: There is an error Not found "params.txt" in zmp in map demo.zmp`**
The folder `demo.zmp` has no `params.txt` directly inside it. A `.zmp` folder must hold the file itself, not a
sub-folder that holds it.

**`warning: There is an error "extension": unknown key in map bad.source.json`**
A JSON key is not one of the keys in the table, or is misspelt. The message names the key. A missing required key
gives `"ext": missing required key`; a wrong type gives `must be a string` or a similar text.

**`warning: Files dup2.source.json and …/dup.source.json have the same GUID`**
Two files in one folder carry the same GUID. Only one stays. Change one GUID. With two `--maps` folders the later
one replaces the earlier and the program says so.

**`bad content type: Unexpected content type image/png image/png`** (in `probe`, exit 4; in a download the tile is `missing`)
The server answered with a type that `ContentType` does not list. The message gives the type received. Fix
`ContentType` and `Ext`; when the server names the type wrongly, set `DetectContentType=1` or `IgnoreContentType=1`.

**`not exists: HTTP 404 Not Found`** (in `probe`, exit 4)
The server has no tile at that place. The URL may be wrong, or the point may lie outside the map. Check the URL on the
test server (section 3), then try a point the map covers.

**`error: Division by zero at line 9`** (in `probe`, exit 4)
The script failed while building the URL, here at line 9 of `GetUrlScript.txt`. Other texts name the function or the
type that failed.

**The source is not listed and there is no warning.**
It lies deeper than six folders below `--maps`, or inside a folder whose name starts with a dot, or inside a folder
named `*.zmp`. Check also that `--maps` points at the right folder.

**The URL looks wrong and the script seems right.**
The script did not compile and was ignored (section 5). Look for a typo, a construct the language subset does not
have, or a byte-order mark at the start of the file.

**Tiles do not change after a fix.**
`download` does not fetch tiles that are already in the cache. Give it a fresh `--cache`, or remove the source's
directory from the old one (`cli-guide.md`, section 9).
