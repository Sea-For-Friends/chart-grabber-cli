# chart-grabber-cli: make your own chart sources, a tutorial

A chart source is a small definition that tells `chart-grabber-cli` where the tiles of a map are on the internet. This
tutorial builds one step by step: a plain tile server, a source with a script, a WMS server, a weather source. Every
step is a command you can run. It does not list every key; the reference is [sources-guide.md](sources-guide.md), and
the commands in general are in [cli-guide.md](cli-guide.md).

Contents: [Set up](#set-up) · [1. Where sources live](#1-where-sources-live-and-which-format-to-use) ·
[2. A minimal XYZ source](#2-a-minimal-xyz-source) · [3. Test a source](#3-test-a-source) ·
[4. A Pascal script](#4-a-url-built-by-a-pascal-script) · [5. A JavaScript function](#5-a-url-built-by-javascript) ·
[6. WMS and weather](#6-a-wms-server-and-a-weather-source) · [7. Warnings](#7-warnings-for-the-user-of-your-source) ·
[8. Use it in OpenCPN](#8-use-a-source-in-opencpn) · [9. Common mistakes](#9-common-mistakes)

## Set up

The example files are in the folder `docs/user/examples/sources` of the project; in the Debian package it is `/usr/share/doc/chart-grabber-cli/examples/sources`, in the Windows zip `docs\examples\sources`. Copy it and work inside the copy:

```
$ cp -r docs/user/examples/sources ~/source-tutorial
$ cd ~/source-tutorial
$ ls
broken  lag  my-sources  tileserver.py  warn-sources
```

`my-sources` holds the sources that work. `broken` holds one small source for each mistake in section 9. `lag` and
`warn-sources` are used in sections 6 and 7.

The sources point at a test server on your own machine, so that nothing goes to the internet and no real server is
loaded. It answers every request with a grey picture and prints the path it was asked for. It needs `python3`. Start it
in a second terminal and leave it running:

```
$ python3 tileserver.py
frames: 2026-10-24T16:10:00Z and 2026-10-24T16:20:00Z
```

Its first line names two weather frames; section 6 uses them. To try a source on a real server, change `url` or
`DefURLBase` to the real address. A public server allows a few test tiles. It may forbid bulk download: read its terms
first. You are responsible for what you download.

## 1. Where sources live, and which format to use

A source is one of two things:

- a **folder** whose name ends in `.zmp`. It holds `params.txt` and, when the URL needs calculating, a Pascal script
  `GetUrlScript.txt`. This is the format SAS.Planet uses, so a source written for it works here.
- a **single file** whose name ends in `.source.json`. It holds the same settings as JSON, and, if needed, a JavaScript
  function.

Both are found anywhere below the folder you give to `--maps`, up to six folders deep. Folders whose name starts with
a dot are skipped. You can repeat `--maps`; for the same GUID the later folder wins.

In OpenCPN, with the Chart Grabber plugin, put your sources in the folder `Charts/Chart sources` in your home folder.
Another folder is chosen in the plugin's Preferences, row "Chart sources" (section 8).

Which format:

| You need | Use |
|---|---|
| A plain XYZ or WMS server | Either. JSON is one file and reads easily. |
| A URL that needs calculating | A `.zmp` with a Pascal script, or a `.source.json` with JavaScript. A source from SAS.Planet is already a `.zmp`. |
| Cookies (`AllowUseCookie`), a page to load first (`UsePreloadPage`) or other `params.txt` keys that JSON has no name for | A `.zmp`. The [JSON reference](sources-guide.md#reference-sourcejson-format-1) lists what JSON can say. |

Every source needs its own GUID, an identifier that no other source has. Make one with `uuidgen`:

```
$ echo "{$(uuidgen | tr a-f A-F)}"
{794FB3CC-B0D9-4A4E-8681-F3845DF8BCC5}
```

The example files carry fixed GUIDs of the form `{7E1D0A00-…}`. Replace them with your own before you add a file to a
folder that holds other sources.

## 2. A minimal XYZ source

Most tile servers answer a URL with the zoom level, the column and the row in it. This is called an XYZ server. A tile
is a 256 × 256 pixel picture; the zoom level counts from 0. Here is the source as a `.zmp` folder,
`my-sources/xyz.zmp/params.txt`:

```
[PARAMS]
GUID={7E1D0A00-3C5B-4F21-9A60-000000000001}
name=Tutorial XYZ
ParentSubMenu=Tutorial
DefURLBase=http://127.0.0.1:8765/xyz/{z}/{x}/{y}.png
ContentType=image/png
Ext=.png
NameInCache=Tutorial_XYZ
License=Test tiles, no real map data
```

And the same as one JSON file, `my-sources/xyz-json.source.json`, with other key names:

```json
{
  "format": 1,
  "guid": "{7E1D0A00-3C5B-4F21-9A60-000000000002}",
  "name": "Tutorial XYZ (JSON)",
  "group": "Tutorial",
  "url": "http://127.0.0.1:8765/xyz/{z}/{x}/{y}.png",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Tutorial_XYZ_JSON",
  "license": "Test tiles, no real map data"
}
```

What the lines do:

- `GUID` / `guid` identifies the source. Without a valid one the source is skipped.
- `name` is what `sources` shows and what you give to `--source`.
- `ParentSubMenu` / `group` is the group the plugin shows it under. It may be left out.
- `DefURLBase` / `url` is the URL of one tile. `{z}`, `{x}` and `{y}` stand for the zoom level, the column and the
  row. The other tags (`{s}`, `{bbox}`, `{ver}` …) are in the
  [URL template](sources-guide.md#reference-the-url-template) reference.
- `ContentType` is the picture type the server must answer with, `Ext` the file extension of a tile in the cache.
- `NameInCache` is the name of the source's folder in the tile cache. Two sources with one name share tiles.
- `License` / `license` is the credit line. It is written into the chart file as its attribution.

A server with several host names for the same tiles uses `{s}` with `ServerNames=a,b,c` (JSON: `"servers"`). A server
that wants a header gets it in `RequestHead` (JSON: `"headers"`).

## 3. Test a source

Test in this order, from the cheapest step. Each step tells you what the one before could not.

**Is it loaded?** `sources` lists every source it understood. A file it could not use is named in a warning above the
table and is missing from the list. The column `verdict` says `ok` when a chart can be made from the source.

```
$ chart-grabber-cli sources --maps=my-sources
                                  GUID  kind                                 name  content type  EPSG  cache  verdict
{7E1D0A00-3C5B-4F21-9A60-000000000007}   map         Tutorial frames (JSON index)     image/png  3857     71       ok
{7E1D0A00-3C5B-4F21-9A60-000000000004}   map                Tutorial quadkey (JS)     image/png  3857      0       ok
{7E1D0A00-3C5B-4F21-9A60-000000000003}   map            Tutorial quadkey (Pascal)     image/png  3857      0       ok
{7E1D0A00-3C5B-4F21-9A60-000000000006}   map  Tutorial sea temperature (WMS, now)     image/png  3857     71       ok
{7E1D0A00-3C5B-4F21-9A60-00000000000B}   map                  Tutorial token (JS)     image/png  3857      0       ok
{7E1D0A00-3C5B-4F21-9A60-000000000005}   map                         Tutorial WMS     image/png  3857      0       ok
{7E1D0A00-3C5B-4F21-9A60-000000000001}   map                         Tutorial XYZ     image/png  3857      0       ok
{7E1D0A00-3C5B-4F21-9A60-000000000002}   map                  Tutorial XYZ (JSON)     image/png  3857      0       ok
```

`--filter=TEXT` keeps the sources whose name contains the text. The next commands use it.

**Does the server answer?** `probe` asks the server for one tile. `--at` is the longitude and latitude of the tile and
`--probe-zoom` its zoom level. `ok` is followed by the size of the tile in bytes and its type. A failure prints the
reason and the exit code is 4.

```
$ chart-grabber-cli probe --maps=my-sources --filter=XYZ --at=-1.29,50.76 --probe-zoom=12
                                  GUID                 name            result
{7E1D0A00-3C5B-4F21-9A60-000000000001}         Tutorial XYZ  ok 400 image/png
{7E1D0A00-3C5B-4F21-9A60-000000000002}  Tutorial XYZ (JSON)  ok 400 image/png
```

The test server shows the path it received, `/xyz/12/2033/1375.png`. This is the surest way to see the URL your source
builds. `probe` does not read the cache: it asks the server every time.

**How big is the job?** `estimate` counts the tiles and the bytes without using the network. Use a small
area in the Solent:

```
$ chart-grabber-cli estimate --maps=my-sources --cache=test-cache --source="Tutorial XYZ" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12-14
Tutorial XYZ
 zoom  SAS z  tiles  bbox  present  marked missing          to download
   12     13      1     1     0.0%            0.0%   ~20.0 KB (assumed)
   13     14      2     2     0.0%            0.0%   ~40.0 KB (assumed)
   14     15      4     4     0.0%            0.0%   ~80.0 KB (assumed)
total             7     7                           ~140.0 KB (assumed)
```

Each zoom level has four times the tiles of the one before. The sizes are a guess ("assumed") until the cache holds
real tiles. Use a `--cache` directory of your own while you test, so that a wrong tile does not stay in your real cache.

**Does a download work?** `run` downloads the tiles and writes a chart file in the MBTiles format, which OpenCPN
reads. The directory given to `--out` must exist.

```
$ mkdir charts
$ chart-grabber-cli run --maps=my-sources --cache=test-cache --source="Tutorial XYZ" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12-14 --out=charts/
Tutorial XYZ
 zoom  SAS z  tiles  bbox  present  marked missing          to download
…
total             7     7                           ~140.0 KB (assumed)
Tutorial XYZ  z13  0/7 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Tutorial XYZ  z15  7/7 (100%)  downloaded 7 tiles 2.7 KB  left ~0.0 KB ~00:00:00
Tutorial XYZ
 zoom  tiles  present  marked missing  missing    size
   12      1        1               0        0  0.4 KB
   13      2        2               0        0  0.8 KB
   14      4        4               0        0  1.6 KB
total      7        7               0        0  2.7 KB
Tutorial XYZ  export  100%
written: …/charts/xyz.mbtiles  7 tiles  20.0 KB
```

`missing` is the number of tiles the server did not deliver. After a good test it is 0. The chart file is named after the
source's folder or file, here `xyz`.

When you change the URL or the script, remove the test cache (`rm -r test-cache`) or use another `--cache`. A tile that is
already in the cache is not fetched again, so the old tiles would hide your change.

## 4. A URL built by a Pascal script

Some servers want a URL that a template cannot write. A common case is a quadkey: the tile's column and row folded into
one string of digits, such as `031313132223`. For this the folder carries `GetUrlScript.txt` next to `params.txt`. The
script runs once for every tile and sets the variable `ResultURL`.

`my-sources/quadkey.zmp/params.txt` holds the start of the URL:

```
[PARAMS]
GUID={7E1D0A00-3C5B-4F21-9A60-000000000003}
name=Tutorial quadkey (Pascal)
ParentSubMenu=Tutorial
DefURLBase=http://127.0.0.1:8765/quad/
ContentType=image/png
Ext=.png
NameInCache=Tutorial_Quadkey
```

`my-sources/quadkey.zmp/GetUrlScript.txt` adds the quadkey:

```
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
```

What the script may use:

- **The variables.** `GetX`, `GetY` are the tile's column and row. `GetZ` is the zoom level plus one: the tag `{z}`
  counts from 0, `GetZ` from 1, so the loop runs `GetZ - 1` times. `GetURLBase` is the `DefURLBase` line. The full
  list is in the [script reference](sources-guide.md#reference-pascal-url-scripts).
- **What it must set.** `ResultURL`. If the script leaves it empty, the server has no tile for this place: the program
  does not ask and the tile counts as missing. To change the request headers, change `RequestHead`; to send a POST,
  fill `PostData`.
- **The language.** A Pascal program without a name: an optional `var` section, optional functions and procedures, then
  `begin … end.`. `if`, `case`, `for`, `while`, `repeat`, `try`, strings, whole numbers and reals work. There are no
  `record` or `type` declarations, no classes and no units. The script has no access to files, and an HTTP request
  needs `IsUseDownloaderInScript=1`. The functions are in the same reference, and
  [Pitfalls of Pascal scripts](sources-guide.md#5-pitfalls-of-pascal-scripts) lists where the language surprises.

Check it. The tile at 1.29° W, 50.76° N and zoom level 12 is column 2033, row 1375, and its quadkey is `031313132223`:

```
$ chart-grabber-cli probe --maps=my-sources --filter="quadkey (Pascal)" --at=-1.29,50.76 --probe-zoom=12
                                  GUID                       name            result
{7E1D0A00-3C5B-4F21-9A60-000000000003}  Tutorial quadkey (Pascal)  ok 400 image/png
```

The test server received `/quad/031313132223.png`. A script that does not compile is ignored without a message, and the
program then asks for `DefURLBase` as written (section 9). Always look at the path the server received.

## 5. A URL built by JavaScript

The same source as a `.source.json` with a JavaScript function. The function is in a file next to the JSON,
`my-sources/quadkey-js.js`:

```js
function getUrl(tile, ctx) {
  var q = "";
  for (var i = tile.z; i > 0; i--) {
    var mask = 1 << (i - 1);
    q += ((tile.x & mask) ? 1 : 0) + ((tile.y & mask) ? 2 : 0);
  }
  return ctx.urlBase + q + ".png";
}
```

`my-sources/quadkey-js.source.json` names it in the key `script`:

```json
{
  "format": 1,
  "guid": "{7E1D0A00-3C5B-4F21-9A60-000000000004}",
  "name": "Tutorial quadkey (JS)",
  "group": "Tutorial",
  "url": "http://127.0.0.1:8765/quad/",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Tutorial_Quadkey_JS",
  "script": { "file": "quadkey-js.js" }
}
```

`"script": { "code": "function getUrl(tile, ctx) { … }" }` puts the function inside the JSON instead; the file form is
easier to read. `tile.z` counts from 0 like `{z}` (`tile.zoom1` is the Pascal `GetZ`). `ctx.urlBase` is the `url` of the
source. `getUrl` returns the URL as a string, or `""` for "no tile here". The other names are in the
[JavaScript reference](sources-guide.md#reference-javascript).

```
$ chart-grabber-cli probe --maps=my-sources --filter="quadkey (JS)" --at=-1.29,50.76 --probe-zoom=12
                                  GUID                   name            result
{7E1D0A00-3C5B-4F21-9A60-000000000004}  Tutorial quadkey (JS)  ok 400 image/png
```

The test server received `/quad/031313132223.png`, the same path as the Pascal script built.

**Limits.** A script cannot read or write files. It cannot reach the network unless the source says `"allowHttp": true`;
then it has `http.get(url)` and `http.post(url, body)`. One call of `getUrl` may run for 5 seconds. The example
`my-sources/token-js.js` asks the server for a token once and puts it into every URL. `shared.locked` makes the first
tile fetch it while the other tiles wait:

```js
function getUrl(tile, ctx) {
  var token = shared.locked(function () {
    var t = shared.get("token");
    if (t === undefined) {
      t = http.get("http://127.0.0.1:8765/token").body;
      shared.set("token", t);
    }
    return t;
  });
  return ctx.urlBase + token + "/" + tile.z + "/" + tile.x + "/" + tile.y + ".png";
}
```

```
$ chart-grabber-cli probe --maps=my-sources --filter="token" --at=-1.29,50.76 --probe-zoom=12
                                  GUID                 name            result
{7E1D0A00-3C5B-4F21-9A60-00000000000B}  Tutorial token (JS)  ok 400 image/png
```

The server saw `/token` once and then `/token-tiles/abc123/12/2033/1375.png`. Without `allowHttp` the same script fails
(`broken/js-http-off`), and so does a script that runs too long (`broken/js-slow`, an endless loop):

```
$ chart-grabber-cli probe --maps=broken/js-http-off --at=-1.29,50.76 --probe-zoom=12
                                  GUID                 name                                result
{7E1D0A00-3C5B-4F21-9A60-00000000000D}  Token with http off  error: http is not defined at line 5
$ chart-grabber-cli probe --maps=broken/js-slow --at=-1.29,50.76 --probe-zoom=12
                                  GUID             name                             result
{7E1D0A00-3C5B-4F21-9A60-000000000027}  JS endless loop  error: script time limit exceeded
```

Both end with exit code 4. The Pascal script has the same limits (section 4).

## 6. A WMS server and a weather source

### WMS

A WMS server draws a picture of any rectangle you name. The URL carries the rectangle as `bbox`. The tag `{bbox}`
becomes `west,south,east,north` of the tile, in metres for the usual projection (EPSG:3857), which is what a chart needs.
`my-sources/wms.source.json`:

```json
{
  "format": 1,
  "guid": "{7E1D0A00-3C5B-4F21-9A60-000000000005}",
  "name": "Tutorial WMS",
  "group": "Tutorial",
  "url": "http://127.0.0.1:8765/wms?service=WMS&version=1.3.0&request=GetMap&layers=depth&styles=&crs=EPSG:3857&bbox={bbox}&width=256&height=256&format=image/png",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Tutorial_WMS"
}
```

The URL must ask for a 256 × 256 picture in EPSG:3857: `crs=EPSG:3857` for WMS 1.3.0, `srs=EPSG:3857` for 1.1.1.

```
$ chart-grabber-cli probe --maps=my-sources --filter="Tutorial WMS" --at=-1.29,50.76 --probe-zoom=12
                                  GUID          name            result
{7E1D0A00-3C5B-4F21-9A60-000000000005}  Tutorial WMS  ok 400 image/png
```

The server received `…&bbox=-146759.09430754,6574807.42497772,-136975.15468704,6584591.36459822&width=256&height=256…`.

### A weather source: one frame per download

Weather changes. A **snapshot** source takes the newest picture the provider has, one frame, and keeps it: a later run
gets a newer frame. The source names an **index**, a document at the provider that says which frame is newest. The
program reads it once per download; the frame becomes `{ver}` in the tile URL and part of the cache key. Three keys do the
work, and a fourth, `SnapshotItem` (JSON: `item`), says where in the index to look:

| `params.txt` | JSON `snapshot` member | Meaning |
|---|---|---|
| `SnapshotIndex` | `index` | The address of the index. Its presence makes the source a snapshot source. |
| `SnapshotIndexType` | `type` | How to read the index: `json-last`, `wms-default`, `wmts-default`, `wmts-domain` or `none-refetch`. |
| `SnapshotPick` | `pick` | `default` or `nearest-now` (for `wms-default` and `wmts-default`). |

A snapshot source must use the cache type 71 (`CacheType=71`, JSON `"cacheType": 71`). The other members and the exact
rules are in the [keys of this program](sources-guide.md#keys-of-this-program).

**An index in JSON (`json-last`).** The provider lists its frames in a JSON document; the last entry is the newest. The
test server serves one:

```
$ curl -s http://127.0.0.1:8765/frames.json
{"frames": [{"time": "2026-10-24T16:10:00Z"}, {"time": "2026-10-24T16:20:00Z"}]}
```

`my-sources/frames.source.json` says: read the list `frames`, take its last entry, use its `time`:

```json
{
  "format": 1,
  "guid": "{7E1D0A00-3C5B-4F21-9A60-000000000007}",
  "name": "Tutorial frames (JSON index)",
  "group": "Tutorial",
  "url": "http://127.0.0.1:8765/frames/{ver}/{z}/{x}/{y}.png",
  "contentType": "image/png",
  "ext": ".png",
  "nameInCache": "Tutorial_Frames",
  "cacheType": 71,
  "snapshot": {
    "index": "http://127.0.0.1:8765/frames.json",
    "type": "json-last",
    "item": "frames"
  }
}
```

`sources --format=tsv` shows `snapshot` in the field `refresh` (eleventh) instead of `static`. `estimate` does not ask the
provider; `download` does, and prints the frame after the source name:

```
$ chart-grabber-cli sources --maps=my-sources --filter=frames --format=tsv
source	{7E1D0A00-3C5B-4F21-9A60-000000000007}	map	Tutorial frames (JSON index)	image/png	3857	71	ok	frames.source.json	Tutorial	user	snapshot			0
$ chart-grabber-cli estimate --maps=my-sources --cache=snap-cache --source="Tutorial frames (JSON index)" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12
Tutorial frames (JSON index)
snapshot: taken when downloading
 zoom  SAS z  tiles  bbox  present  marked missing         to download
   12     13      1     1     0.0%            0.0%  ~20.0 KB (assumed)
total             1     1                           ~20.0 KB (assumed)
$ chart-grabber-cli download --maps=my-sources --cache=snap-cache --source="Tutorial frames (JSON index)" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12
Tutorial frames (JSON index)
snapshot 2026-10-24 16:20 UTC
 zoom  SAS z  tiles  bbox  present  marked missing         to download
…
Tutorial frames (JSON index)
 zoom  tiles  present  marked missing  missing    size
   12      1        1               0        0  0.4 KB
total      1        1               0        0  0.4 KB
```

The test server received `/frames.json` and then `/frames/2026-10-24T16:20:00Z/12/2033/1375.png`: the newest frame is
in the tile path.

**An index in WMS (`wms-default`).** A WMS 1.3.0 server lists the times of a layer in its capabilities document, with
one default. The test server's layer `sea-temp` has hourly frames from two days ago to a day ahead, as a forecast layer
has, and a default at the far end:

```
<Dimension name="time" units="ISO8601" default="2026-10-25T16:00:00Z">2026-10-22T16:00:00Z/2026-10-25T16:00:00Z/PT1H</Dimension>
```

`SnapshotPick=nearest-now` takes the frame nearest to the moment of the run instead of the default. In a `.zmp` folder,
`my-sources/sea-temp.zmp/params.txt`:

```
[PARAMS]
GUID={7E1D0A00-3C5B-4F21-9A60-000000000006}
name=Tutorial sea temperature (WMS, now)
ParentSubMenu=Tutorial
DefURLBase=http://127.0.0.1:8765/wms?service=WMS&version=1.3.0&request=GetMap&layers=sea-temp&styles=&crs=EPSG:3857&bbox={bbox}&width=256&height=256&format=image/png&time={ver}
ContentType=image/png
Ext=.png
NameInCache=Tutorial_SeaTemp
CacheType=71
SnapshotIndex=http://127.0.0.1:8765/caps.xml
SnapshotIndexType=wms-default
SnapshotItem=sea-temp
SnapshotPick=nearest-now
```

`SnapshotItem` is the layer name. `{ver}` is the frame, passed to the server as `time=`.

```
$ chart-grabber-cli download --maps=my-sources --cache=snap-cache --source="Tutorial sea temperature (WMS, now)" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12
Tutorial sea temperature (WMS, now)
snapshot 2026-10-24 16:00 UTC
…
```

With `SnapshotPick=default` the same download takes `snapshot 2026-10-25 16:00 UTC`, a day ahead.

**What the user sees when the newest frame is not ready.** A provider often lists a new frame before it serves its
tiles. If the newest frame is less than 60 minutes old and none of its tiles is served, the program takes the frame
before it and says so on standard error. In `--format=tsv` there are then two `snapshot` records; the last one names the
frame used. The source in `lag/` points its tiles at a path where the newest frame answers 404:

```
$ chart-grabber-cli download --maps=lag --cache=lag-cache --source="Tutorial lagging frames" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12 --format=tsv
snapshot	{7E1D0A00-3C5B-4F21-9A60-000000000008}	2026-10-24T16:20:00Z	2026-10-24T16:20:00Z
estimate	{7E1D0A00-3C5B-4F21-9A60-000000000008}	12	1	1	0.0000	0.0000	20480	1	20480
Tutorial lagging frames  z13  0/1 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Tutorial lagging frames  z13  1/1 (100%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~00:00:00
warning: the newest frame 2026-10-24 16:20 UTC of "Tutorial lagging frames" is not served yet; using the previous frame 2026-10-24 16:10 UTC
snapshot	{7E1D0A00-3C5B-4F21-9A60-000000000008}	2026-10-24T16:10:00Z	2026-10-24T16:10:00Z
Tutorial lagging frames  z13  1/1 (100%)  downloaded 1 tiles 0.4 KB  left ~0.0 KB ~00:00:00
fill	{7E1D0A00-3C5B-4F21-9A60-000000000008}	12	1	1	0	0	400
```

The frame times are those of your server's start. The step back happens only while the newest frame is under an hour
old; if no warning appears, stop the test server and start it again.

The index types `wmts-default`, `wmts-domain` and `none-refetch`, and the option `--time` for forecast layers, are in
the reference and in [cli-guide.md](cli-guide.md#13-weather-the-current-picture).

## 7. Warnings for the user of your source

Two optional keys make the program print a warning on standard error. They change nothing else: the download goes on.

`TermsOfUseWarning=<address>` (JSON: `termsOfUseWarning`) marks a source whose provider may not allow offline use. The
program prints the address whenever `sources` lists the source, and at the start of `download`, `run` and `resume`.

`NavigationWarning=1` (JSON: `navigationWarning`, `true` or `false`) marks a source that shows depths or other chart data
that is not an official nautical chart. `download`, `run` and `resume` print a notice before the first tile, and `-q`
does not hide it. Both keys exist in `.zmp` and in `.source.json`. `warn-sources/depth.zmp` has both:

```
[PARAMS]
GUID={7E1D0A00-3C5B-4F21-9A60-000000000009}
name=Tutorial depth
ParentSubMenu=Tutorial
DefURLBase=http://127.0.0.1:8765/depth/{z}/{x}/{y}.png
ContentType=image/png
Ext=.png
NameInCache=Tutorial_Depth
NavigationWarning=1
TermsOfUseWarning=https://example.org/terms
```

```
$ chart-grabber-cli download --maps=warn-sources --cache=test-cache --source="Tutorial depth" --bbox=-1.30,50.76,-1.28,50.77 --zooms=12
Tutorial depth
 zoom  SAS z  tiles  bbox  present  marked missing         to download
   12     13      1     1     0.0%            0.0%  ~20.0 KB (assumed)
total             1     1                           ~20.0 KB (assumed)
warning: "Tutorial depth": the provider's terms of use may not allow downloading tiles for offline use. Read https://example.org/terms before downloading; you are responsible for complying.
warning: "Tutorial depth": not for navigation. This source is not an official nautical chart; do not use it to navigate.
Tutorial depth  z13  0/1 (0%)  downloaded 0 tiles 0.0 KB  left ~~ Kb ~~
Tutorial depth  z13  1/1 (100%)  downloaded 1 tiles 0.4 KB  left ~0.0 KB ~00:00:00
…
```

`warn-sources/depth-json.source.json` is the same source in JSON, with `"navigationWarning": true` and
`"termsOfUseWarning": "https://example.org/terms"`. The OpenCPN plugin shows a "Not for navigation" note for such a source. Whether a provider allows offline download is for you to find out.

## 8. Use a source in OpenCPN

With the Chart Grabber plugin:

1. Test the source with the commands of section 3.
2. Copy the `.zmp` folder or the `.source.json` file into `Charts/Chart sources` in your home folder. To keep your
   sources elsewhere, choose that folder in the plugin's Preferences, row "Chart sources". It is read after the sources
   that come with the plugin.
3. Open "New chart". The source is in the menu named by `ParentSubMenu` / `group`.
4. Pick a small area and a low detail first, and download.
5. The chart appears in OpenCPN's chart list. OpenCPN 5.10.2 may hide a new chart until you click it in the chart
   bar. MBTiles charts need OpenGL.

A source that needs a key of your own has the word `KEY` in its file where the key goes. Put your key there before you
use it (section 9).

## 9. Common mistakes

Each mistake has a folder in `broken/`. Run the command and compare. A `…` stands for the folder you are working in.

### A duplicate GUID

Two files carry one GUID. One source stays in the list, the other is left out, and a warning names both files. The
program is not stopped. Give each file its own GUID (section 1).

```
$ chart-grabber-cli sources --maps=broken/dup-guid
warning: Files first.source.json and …/broken/dup-guid/second.source.json have the same GUID
                                  GUID  kind   name  content type  EPSG  cache  verdict
{7E1D0A00-3C5B-4F21-9A60-000000000011}   map  First     image/png  3857      0       ok
```

### A wrong file encoding

`params.txt` should be UTF-8. A file that is not valid UTF-8 is read as Windows-1251: the program prints no error, and
a letter outside ASCII comes out as another letter. The name `Café` saved in Latin-1 becomes `Cafй`. A UTF-16 file with a
byte-order mark was also read correctly, but save as UTF-8 anyway.

```
$ chart-grabber-cli sources --maps=broken/encoding
                                  GUID  kind         name  content type  EPSG  cache  verdict
{7E1D0A00-3C5B-4F21-9A60-000000000013}   map  Cafй Latin1     image/png  3857      0       ok
{7E1D0A00-3C5B-4F21-9A60-000000000012}   map   Utf16 file     image/png  3857      0       ok
```

A `.source.json` file must be UTF-8 and is refused otherwise. When it is the only file, nothing loads:

```
$ chart-grabber-cli sources --maps=broken/encoding-json
warning: There is an error invalid UTF8 string in map latin1.source.json
error: no map source loaded from …/broken/encoding-json: Unable to load map(s)! Aborting...
```

The exit code is 3. Re-save the file as UTF-8. `GetUrlScript.txt` is read as bytes: save it as UTF-8 without a
byte-order mark.

### A source in the wrong folder

A source more than six folders below `--maps`, or inside a folder whose name starts with a dot, is not found. Nothing is
printed; the source is just not in the list. The folder `broken/deep` has a source at six folders, one at seven and one in
`.hidden`:

```
$ chart-grabber-cli sources --maps=broken/deep
                                  GUID  kind        name  content type  EPSG  cache  verdict
{7E1D0A00-3C5B-4F21-9A60-000000000015}   map  Six levels     image/png  3857      0       ok
```

If a source is missing and there is no warning, look for these two causes first. Check also that `--maps` points at the
right folder.

### A URL without {z}, {x} and {y}

A URL that lacks the tags is not an error. Every tile is fetched from the same address, so the whole chart repeats one
picture. `probe` says `ok`:

```
$ chart-grabber-cli probe --maps=broken/no-tags --at=-1.29,50.76 --probe-zoom=12
                                  GUID     name            result
{7E1D0A00-3C5B-4F21-9A60-000000000018}  No tags  ok 400 image/png
```

The sign is on the test server: after `probe` and a download of three tiles it has been asked for `/xyz/tile.png` four
times. Add `{z}`, `{x}` and `{y}` to the URL, or a script that builds them.

### A script error

Four scripts in `broken/script-error`, one probe:

```
$ chart-grabber-cli probe --maps=broken/script-error --at=-1.29,50.76 --probe-zoom=12
                                  GUID                     name                                                                                 result
{7E1D0A00-3C5B-4F21-9A60-000000000022}          JS syntax error  skipped: the download subsystem of this map is disabled: Expected token `;' at line 1
{7E1D0A00-3C5B-4F21-9A60-000000000021}                JS throws                            error: Cannot call method 'toString' of undefined at line 1
{7E1D0A00-3C5B-4F21-9A60-000000000019}  Pascal division by zero                                                      error: Division by zero at line 3
{7E1D0A00-3C5B-4F21-9A60-000000000020}      Pascal syntax error                                                                       ok 400 image/png
```

The exit code is 4.

- A runtime error stops one tile, not the run, and is named with the line: `Division by zero at line 3`, or the
  JavaScript error with its line. In `download` such a tile counts as `missing` and the exit code stays 0; add
  `--strict` to make it 4. Use `probe` to see the reason.
- A JavaScript file with a syntax error, without `getUrl`, or with an error in its top-level code turns the source
  off, and the first row shows why. `sources` shows the same text in the last column instead of `ok`, `download`
  refuses to start with it (exit 3), and a `skipped:` row does not change the exit code of `probe`. Without `getUrl`
  the text is `function getUrl(tile, ctx) is not defined`. A `getUrl` that throws is different: that is one tile, as
  in the first item.
- A Pascal script that does not compile is ignored without a message. Here `probe` says `ok` because the server
  answers the plain `DefURLBase`; the test server received `/quad/`. Look at the path the server received.

### HTTP 403 without a Referer or User-Agent

Some servers refuse a request that has no `Referer` or a browser's `User-Agent`. `probe` shows the status. `download`
only counts the tile as `missing`:

```
$ chart-grabber-cli probe --maps=broken/forbidden --at=-1.29,50.76 --probe-zoom=12
                                  GUID                     name                               result
{7E1D0A00-3C5B-4F21-9A60-000000000023}          Needs a Referer  load error: Unknown status code 403
{7E1D0A00-3C5B-4F21-9A60-000000000024}  Needs a Referer (fixed)                     ok 400 image/png
```

The second source is the first with two lines added to its JSON, `"headers": { "Referer": "https://example.org/",
"User-Agent": "my-charts" }` (in a `.zmp`: `RequestHead=Referer: https://example.org/\r\nUser-Agent: my-charts`, with
the four characters `\r\n`). Use a `Referer` the provider expects and a `User-Agent` that names your program.

### A key that is not accepted

A JSON key the program does not know, or a misspelt one, makes that file fail to load. The message names the key:

```
$ chart-grabber-cli sources --maps=broken/unknown-key
warning: There is an error "extension": unknown key in map misspelt.source.json
error: no map source loaded from …/broken/unknown-key: Unable to load map(s)! Aborting...
```

The key is `ext`. A missing required key says `"ext": missing required key`; a value of the wrong type says
`must be a string` or the like. In `params.txt` the opposite holds: a key that is not known is ignored without a
warning, so check the spelling against the [reference](sources-guide.md#reference-paramstxt).

### KEY left as a placeholder

A source that needs a key has the word `KEY` where yours goes. The program cannot tell that it is a placeholder. It asks
the server, the server refuses, and `probe` prints the status. Which status depends on the server: 401, 403 or 400.

```
$ chart-grabber-cli probe --maps=broken/key-placeholder --at=-1.29,50.76 --probe-zoom=12
                                  GUID          name                               result
{7E1D0A00-3C5B-4F21-9A60-000000000026}  Keyed server  load error: Unknown status code 401
```

Replace `KEY` in the file with your own key. The key is yours: do not share the file with it in.

More messages, with causes, are in the [troubleshooting list](sources-guide.md#troubleshooting) of the reference.
