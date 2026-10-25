#!/usr/bin/env python3
"""Zips one stock ZMP folder, unchanged, into two .zmp files (T-0061, maptype spec OQ-5).

  sas.maps/_osm/Gps_tiles.zmp/  ->  zipped/Gps_tiles_deflated.zmp  (every member deflated)
                                    zipped/Gps_tiles_stored.zmp    (every member stored)

Layout as `zip -r` writes it: a directory entry "EmptyTiles/" followed by its files, no leading
folder, '/' separators. Timestamps are fixed so the output is reproducible.
Run from anywhere: python3 tests/fixtures/zmp/make_zipped_zmp.py
"""
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "sas.maps", "_osm", "Gps_tiles.zmp")
OUT = os.path.join(HERE, "zipped")
STAMP = (2020, 1, 1, 0, 0, 0)


def members():
    for root, dirs, files in os.walk(SRC):
        dirs.sort()
        rel = os.path.relpath(root, SRC)
        if rel != ".":
            yield rel.replace(os.sep, "/") + "/", None
        for name in sorted(files):
            yield (name if rel == "." else rel.replace(os.sep, "/") + "/" + name), os.path.join(root, name)


def build(path, method, extra_params=b""):
    with zipfile.ZipFile(path, "w") as z:
        for name, src in members():
            info = zipfile.ZipInfo(name, STAMP)
            if src is None:
                info.external_attr = (0o40755 << 16) | 0x10
                z.writestr(info, b"")
            else:
                info.external_attr = 0o100644 << 16
                info.compress_type = method
                with open(src, "rb") as f:
                    data = f.read()
                if name == "params.txt":
                    data += extra_params
                z.writestr(info, data)


os.makedirs(OUT, exist_ok=True)
build(os.path.join(OUT, "Gps_tiles_deflated.zmp"), zipfile.ZIP_DEFLATED)
build(os.path.join(OUT, "Gps_tiles_stored.zmp"), zipfile.ZIP_STORED)

# T-0072: the same stored zip with `UseDwn=0` appended to params.txt (the only change): the set must not read the script.
os.makedirs(os.path.join(HERE, "zipped_nodwn"), exist_ok=True)
build(os.path.join(HERE, "zipped_nodwn", "Gps_tiles_nodwn.zmp"), zipfile.ZIP_STORED, b"UseDwn=0\r\n")
