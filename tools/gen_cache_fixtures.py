#!/usr/bin/env python3
"""gen_cache_fixtures.py -- writes a miniature multi-format tile cache and its manifest.

Generates tests/fixtures/cache/{cache,cache_old,cache_es}/ (one folder-cache layout each) plus
tests/fixtures/cache/expected.json, the cross-check manifest tests/storage/tile_name_codec_test.cpp
reads. Protocol rule 10 (docs/conventions/agent-protocol.md): for a formula-defined layout,
generated fixtures are legal provided the generator is an INDEPENDENT implementation, written
directly from the legacy Pascal by a different author than the C++ -- so a test failure means the
two disagree, not that both copied one bug. Each function below carries its legacy unit and line
range in a comment, the same way docs/specs/storage.md does; nothing here imports or shells out to
anything the C++ build touches.

Scope (T-0021B): the three folder layouts that ticket ported to C++ -- SAS.Planet (cache/),
GoogleMV (cache_old/) and EarthSlicer (cache_es/).

Scope (T-0021C): the six regex-family folder layouts -- GlobalMapper Tiles/Aux/Bing (cache_gmt_*),
MOBAC (cache_ma/), OsmAnd+ (cache_osmand/) and TMS (cache_tms/). The native-SQLite/BerkeleyDB
region layout is still out of scope: its formula maps a 256x256 tile BLOCK, not one tile per
fixture entry like the other nine, so it doesn't fit this script's per-tile TILES table the same
way; T-0021C's region codec tests are hand-derived from the Pascal directly instead (matching the
repo's real `cache_sqlite/` fixture, docs/backlog/T-0021C's own acceptance criteria). T-0021D adds
a native-SQLite layout here if it ends up needing one.

usage: gen_cache_fixtures.py --out DIR [--layout NAME ...] [--check]
  --check   re-generate into a temp dir and diff against --out; non-zero on any difference
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sqlite3
import struct
import sys
import tempfile
import zlib
from pathlib import Path

# The tile set: chosen to hit every branch of every in-scope formula. Every entry except the
# truncation demo uses X,Y within [0, 2^zoom) -- the real constraint any legitimate tile satisfies
# (a zoom-Z pyramid level has exactly 2^Z columns/rows) -- so codecs whose format is bit/digit-
# width-limited (GMV's quad string, ES's zero-padded shards) round-trip exactly; the fifth column
# marks that eligibility for the C++ round-trip test.
#   (0,0) z0            : GMV zoom-0 degenerate case ("01/t"); ES Z<6 branch minimum
#   (3,7) z5             : ES Z<6 branch, upper edge; zoomStr "06"
#   (12345,67) z6, NOT   : deliberately OUT OF RANGE (2^6=64): demonstrates the ES RightStr
#   round-trip-eligible    TRUNCATION quirk (X=12345 -> "45", chr(60+6)='B') and GMV's analogous
#                          silent high-bit drop. A generate-only regression, not a real tile.
#   (200,130) z8         : zoomStr boundary just below the "10" flip ("09"); ES Z<10 branch, no
#                          truncation (both values fit width 3)
#   (300,450) z9         : zoomStr flips to "10" here (Z+1=10); ES Z<10 branch upper edge
#   (700,900) z10        : ES Z>=10 top-tier "10-" directory, shr(Z-9)=shr(1), width 4, no truncation
#   (2,5) z3, TNE        : a tile-not-exists marker at a low zoom (ES Z<6, no padding branch)
TILES: list[tuple[int, int, int, str, bool]] = [
    (0, 0, 0, "tile", True),
    (5, 3, 7, "tile", True),
    (6, 12345, 67, "tile", False),
    (8, 200, 130, "tile", True),
    (9, 300, 450, "tile", True),
    (10, 700, 900, "tile", True),
    (3, 2, 5, "tne", True),
]

# T-0321: the native-SQLite region file (cache_sqlite/map/z1/0/0/0.0.sqlitedb). The container layout
# (table t / index t_v_idx DDL, 4096-byte pages, WAL journal mode, c NULL, d = load date) is the one
# the legacy SQLite cache writes; the former real captured Google tile was replaced by a synthetic,
# formula-defined 256x256 8-bit RGB gradient PNG (public release: no third-party imagery).
SQLITE_REGION_REL = "cache_sqlite/map/z1/0/0/0.0.sqlitedb"
SQLITE_TILE_DATE = 1789643597  # unchanged from the replaced capture (tests pin 2026-09-17 11:13:17)
SQLITE_DDL = (
    "CREATE TABLE t (x INTEGER NOT NULL,y INTEGER NOT NULL,v INTEGER DEFAULT 0 NOT NULL,c TEXT,"
    "s INTEGER DEFAULT 0 NOT NULL,h INTEGER DEFAULT 0 NOT NULL,d INTEGER NOT NULL,b BLOB,"
    "constraint PK_TB primary key (x,y,v))"
)


def synthetic_png() -> bytes:
    """256x256 RGB gradient PNG, written with zlib/struct only (deterministic)."""
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    def row(y: int) -> bytes:  # filter type 1 (Sub) keeps the gradient small
        px = [(x, y, (x + y) // 2) for x in range(256)]
        out = bytearray(b"\x01")
        for i, p in enumerate(px):
            prev = px[i - 1] if i else (0, 0, 0)
            out += bytes((p[c] - prev[c]) & 0xFF for c in range(3))
        return bytes(out)

    raw = b"".join(row(y) for y in range(256))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 256, 256, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_sqlite_region(out_dir: Path) -> None:
    path = out_dir / SQLITE_REGION_REL
    path.parent.mkdir(parents=True, exist_ok=True)
    for suffix in ("", "-wal", "-shm"):
        Path(str(path) + suffix).unlink(missing_ok=True)
    png = synthetic_png()
    con = sqlite3.connect(path)
    con.execute("PRAGMA page_size=4096")
    con.execute("PRAGMA journal_mode=wal")
    con.execute(SQLITE_DDL)
    con.execute("INSERT INTO t VALUES(0,0,0,NULL,?,?,?,?)",
                (len(png), zlib.crc32(png) & 0xFFFFFFFF, SQLITE_TILE_DATE, png))
    con.execute("CREATE INDEX t_v_idx on t (v)")
    con.commit()
    con.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    con.close()


BASE_MTIME = 1700000000  # arbitrary fixed epoch second; deterministic so --check is reproducible


def full_int(value: int, width_zoom: int) -> str:
    """Src/TileStorage/FileSystem/u_TileFileNameES.pas:47-72 FullInt.

    Zero-pad to width `w`, then RightStr -- i.e. truncate from the LEFT down to `w` characters
    when the number is wider than the pad.
    """
    s = str(value)
    if width_zoom < 4:
        return s  # w == 1: no padding, no truncation
    w = 2 if width_zoom < 7 else 3 if width_zoom < 10 else 4 if width_zoom < 14 else 5 if width_zoom < 17 else 6 if width_zoom < 20 else 7
    padded = ("0" * (w - 1)) + s
    return padded[-w:]


def _zoom_str(z: int) -> str:
    """Shared by SAS/GMV/ES (each ports it from its own unit, transcribed once here): zoomStr =
    Z+1, zero-padded to 2 digits while Z+1 < 10."""
    n = z + 1
    return str(n) if z >= 9 else "0" + str(n)


def sas_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameSAS.pas:57-67 GetTileFileName."""
    return f"z{z + 1}/{x >> 10}/x{x}/{y >> 10}/y{y}"


def gmv_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameGMV.pas:56-77 GetTileFileName."""
    path = _zoom_str(z) + "/t"
    if z > 0:
        mask = 1 << (z - 1)
        for _ in range(z):
            x_bit = (x & mask) != 0
            y_bit = (y & mask) != 0
            c = ("s" if y_bit else "r") if x_bit else ("t" if y_bit else "q")
            path += c
            mask >>= 1
    return path


def es_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameES.pas:74-91 GetTileFileName."""
    zoom_str = _zoom_str(z)
    file_name = f"{zoom_str}-{full_int(x, z)}-{full_int(y, z)}"
    if z < 6:
        directory = f"{zoom_str}/"
    elif z < 10:
        directory = f"{zoom_str}/{chr(60 + z)}{full_int(x >> 5, z - 5)}{full_int(y >> 5, z - 5)}/"
    else:
        directory = (
            f"10-{full_int(x >> (z - 9), 9)}-{full_int(y >> (z - 9), 9)}/"
            f"{zoom_str}/{chr(60 + z)}{full_int(x >> 5, z - 5)}{full_int(y >> 5, z - 5)}/"
        )
    return directory + file_name


def add_ext(base_path: str, ext: str) -> str:
    """u_TileFileNameBase.pas:60-63 AddExt -> SysUtils.ChangeFileExt."""
    slash = base_path.rfind("/")
    dot = base_path.rfind(".")
    if dot > slash:
        return base_path[:dot] + ext
    return base_path + ext


def gm1_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameGM1.pas:54,60-68 GetTileFileName. (!) Y first, X last."""
    return f"z{z}/{y}/{x}"


def gm2_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameGM2.pas:54,60-66 GetTileFileName. (!) Y first, X last."""
    return f"z{z}/{y}_{x}"


def gm3_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameGM3.pas:54,60-66 GetTileFileName. No leading 'z'. (!) Y first, X last."""
    return f"{z}/{y}/{x}"


def mobac_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameMOBAC.pas:54,60-66 GetTileFileName. (!) X first, Y last."""
    return f"{z}/{x}/{y}"


def osmand_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameOsmAnd.pas:54,60-66 GetTileFileName. Same shape as MOBAC."""
    return f"{z}/{x}/{y}"


def tms_path(x: int, y: int, z: int) -> str:
    """u_TileFileNameTMS.pas:54,60-78 GetTileFileName. (!) X first, Y last (flipped -- TMS
    numbers tiles from the bottom). The deliberately out-of-range demo tile (idx 2, roundtrip
    False) flips to a NEGATIVE number here, exactly as legacy's Format('%d', ...) would print
    one -- byte-exact fidelity, not a bug in this generator."""
    return f"{z}/{x}/{(1 << z) - y - 1}"


def add_ext_osmand(base_path: str, ext: str) -> str:
    """u_TileFileNameOsmAnd.pas:98-110 AddExt override: a TNE marker keeps a plain '.tne'; any
    other extension gets '.tile' appended first (SameFileName is a case-insensitive compare)."""
    real_ext = ext if ext.lower() == ".tne" else ext + ".tile"
    return add_ext(base_path, real_ext)


# name -> (directory under --out, path-building function, addExt function)
LAYOUTS = {
    "sas": ("cache", sas_path, add_ext),
    "gmv": ("cache_old", gmv_path, add_ext),
    "es": ("cache_es", es_path, add_ext),
    "gm1": ("cache_gmt_tiles", gm1_path, add_ext),
    "gm2": ("cache_gmt_aux", gm2_path, add_ext),
    "gm3": ("cache_gmt_bing", gm3_path, add_ext),
    "mobac": ("cache_ma", mobac_path, add_ext),
    "osmand": ("cache_osmand", osmand_path, add_ext_osmand),
    "tms": ("cache_tms", tms_path, add_ext),
}


def build_manifest(layout_names: list[str]) -> dict:
    manifest: dict = {"layouts": {}}
    for name in layout_names:
        directory, fn, addext_fn = LAYOUTS[name]
        entries = []
        for idx, (z, x, y, kind, roundtrip) in enumerate(TILES):
            base = fn(x, y, z)
            ext = ".tne" if kind == "tne" else ".jpg"
            relpath = addext_fn(base, ext)
            entries.append(
                {
                    "z": z,
                    "x": x,
                    "y": y,
                    "kind": kind,
                    "relpath": relpath,
                    "mtime": BASE_MTIME + idx,
                    "roundtrip": roundtrip,
                }
            )
        manifest["layouts"][name] = {"dir": directory, "tiles": entries}
    return manifest


def write_fixtures(out_dir: Path, layout_names: list[str]) -> dict:
    manifest = build_manifest(layout_names)
    for name in layout_names:
        directory = manifest["layouts"][name]["dir"]
        layout_dir = out_dir / directory
        if layout_dir.exists():
            shutil.rmtree(layout_dir)
        for entry in manifest["layouts"][name]["tiles"]:
            p = layout_dir / entry["relpath"]
            p.parent.mkdir(parents=True, exist_ok=True)
            # Content is not exercised by T-0021B (path-only codec tests); a minimal non-empty
            # payload for "tile" entries keeps the fixture visually distinguishable from a TNE.
            p.write_bytes(b"" if entry["kind"] == "tne" else b"\xff\xd8\xff\xd9")
            os.utime(p, (entry["mtime"], entry["mtime"]))
    write_sqlite_region(out_dir)
    manifest_path = out_dir / "expected.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def _all_files(root: Path) -> set[str]:
    return {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}


def _compare_recursive(a: Path, b: Path, layout_names: list[str]) -> bool:
    # Only compare the paths this run owns (its layout directories + expected.json) -- --out may
    # also hold fixtures other tickets manage by hand (e.g. tests/fixtures/cache/cache_sqlite/,
    # T-0021A's real captured sqlite region file) that this script never writes and must not flag.
    owned_dirs = {LAYOUTS[n][0] for n in layout_names}
    def owned(rel: str) -> bool:
        return rel in ("expected.json", SQLITE_REGION_REL) or rel.split("/", 1)[0] in owned_dirs

    a_files = {f for f in _all_files(a) if owned(f)}
    b_files = {f for f in _all_files(b) if owned(f)}
    if a_files != b_files:
        missing, extra = sorted(a_files - b_files), sorted(b_files - a_files)
        if missing:
            print(f"missing in --out: {missing}", file=sys.stderr)
        if extra:
            print(f"extra in --out: {extra}", file=sys.stderr)
        return False
    ok = True
    for rel in sorted(a_files):
        if (a / rel).read_bytes() != (b / rel).read_bytes():
            print(f"content differs: {rel}", file=sys.stderr)
            ok = False
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="output directory (e.g. tests/fixtures/cache)")
    ap.add_argument("--layout", action="append", default=None, choices=sorted(LAYOUTS), help="repeatable; default: all")
    ap.add_argument("--check", action="store_true", help="regenerate into a temp dir and diff against --out")
    args = ap.parse_args()

    layout_names = args.layout or sorted(LAYOUTS)
    out_dir = Path(args.out)

    if args.check:
        with tempfile.TemporaryDirectory() as td:
            tmp_dir = Path(td)
            write_fixtures(tmp_dir, layout_names)
            if not _compare_recursive(tmp_dir, out_dir, layout_names):
                print("MISMATCH between regenerated fixtures and --out", file=sys.stderr)
                return 1
        print("OK: fixtures match")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    write_fixtures(out_dir, layout_names)
    print(f"wrote fixtures to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
