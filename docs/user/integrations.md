# Using the charts with other navigation software

Since 2026.11.1.

The charts are MBTiles files with TMS rows (`scheme=tms` in the file). Signal K and AvNav serve them as they are; both
hand out tiles with XYZ rows and flip the row themselves. Checked with Signal K server 2.36.0, charts-plugin 3.8.1 and
AvNav 20261102 on Debian 13 (arm64); Freeboard-SK was not checked.

**Signal K.** Run the server on Node 24, the version that worked. With Debian 13's own Node 20 the charts-plugin does not start, and Node 22.23.4 failed too.
Put the charts in a folder of your own and list both folders in the plugin's `chartPaths`, in
`~/.signalk/plugin-config-data/charts.json`. `chartPaths` replaces the default folder: list only yours and the charts you
already had in `~/.signalk/charts` disappear.

```
{"enabled":true,"configuration":{"chartPaths":["/home/you/.signalk/charts","/home/you/Charts/ChartGrabber"]}}
```

The server lists each file under its file name, with the chart's name from the file.

**AvNav.** Copy the file into `~/avnav/charts` and change nothing. AvNav lists the chart and serves it right. It flags the
scheme of the file Chart Grabber writes as unconfirmed (the file does not say it was chosen), and what the screen shows for that flag was not
checked. If you are asked, TMS is the right choice. A running Signal K server on the same machine also shows its charts in
AvNav, so a chart that is in both folders appears twice.

**Refresh on a schedule.** Write the new chart straight into the server's folder with `--replace --overwrite`. The
program writes the new file next to the old one and renames it, so the server gets a whole file. Both
servers showed the new file about 10 seconds later (checked after 12 s), without a restart.

`--replace` downloads every tile of the area again on each run. Keep the area and the zooms small and the interval long:
a provider may block an address that downloads too much, and some forbid it in their terms (see the sources guide).

A source that serves WebP tiles is refused unless you add `--allow-webp`. Whether Signal K and AvNav show WebP was not tried.
