// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_TileFileNameSQLite.pas, u_TileFileNameBerkeleyDB.pas (byte-identical, one codec class).
// docs/specs/storage.md §2.10.2. Hand-derived from the Pascal directly (not fixture-driven, per
// docs/backlog/T-0021C's own scope note): the region formula maps a 256x256 tile BLOCK, so a
// generated tilePath() does not round-trip back to the original (x, y) the way the other nine
// folder codecs do -- reusing tools/gen_cache_fixtures.py's per-tile TILES table would not
// exercise anything the formula doesn't already make obvious by hand.
#include "cg/storage/codecs/region_tile_name_codec.h"

#include <QFile>
#include <QString>

#include <gtest/gtest.h>

#include "fixture_path.h"

using cg::geo::PointI;
using cg::storage::RegionTileNameCodec;

// --- the four formula components: z{Z+1} / {X>>10} / {Y>>10} / {X>>8}.{Y>>8} -----------------------

TEST(RegionTileNameCodec, ZoomComponentMatchesRealFixture) {
    // The repo's generated fixture (tools/gen_cache_fixtures.py) (tests/fixtures/cache/README.md) is exactly
    // this formula at Z = 0: z(0+1)=z1, 0>>10=0, 0>>10=0, 0>>8.0>>8 = "0.0".
    const RegionTileNameCodec codec;
    const QString base = codec.tilePath(PointI(0, 0), 0);
    EXPECT_EQ(base, QStringLiteral("z1/0/0/0.0"));
    const QString relpath = base + QStringLiteral(".sqlitedb"); // concatenated, see below
    EXPECT_EQ(relpath, QStringLiteral("z1/0/0/0.0.sqlitedb"));
    ASSERT_TRUE(QFile::exists(cgStorageFixture(QStringLiteral("cache/cache_sqlite/map/") + relpath)))
        << "expected the generated fixture (tools/gen_cache_fixtures.py) at "
           "tests/fixtures/cache/cache_sqlite/map/" +
               relpath.toStdString();
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(relpath, xy, zoom));
    EXPECT_EQ(zoom, 0);
    EXPECT_EQ(xy.x(), 0);
    EXPECT_EQ(xy.y(), 0);
}

TEST(RegionTileNameCodec, XShr10DirectoryComponent) {
    // X = 2048 -> X>>10 = 2 (the second directory level); Y stays 0 to isolate the X component.
    const RegionTileNameCodec codec;
    EXPECT_EQ(codec.tilePath(PointI(2048, 0), 0), QStringLiteral("z1/2/0/8.0"));
}

TEST(RegionTileNameCodec, YShr10DirectoryComponent) {
    // Y = 2048 -> Y>>10 = 2 (the third directory level); X stays 0 to isolate the Y component.
    const RegionTileNameCodec codec;
    EXPECT_EQ(codec.tilePath(PointI(0, 2048), 0), QStringLiteral("z1/0/2/0.8"));
}

TEST(RegionTileNameCodec, FileNameShr8ComponentRoundTripsToBlockCorner) {
    // X = 300, Y = 600 both share directory shard 0 (X>>10 = Y>>10 = 0), so this isolates the
    // FILE NAME's {X>>8}.{Y>>8} = "1.2". parse() only ever reads the file name (the two directory
    // shard numbers are matched but not captured, like ES's write-only directory structure) --
    // it recovers the BLOCK's top-left tile (256, 512), not the original (300, 600).
    const RegionTileNameCodec codec;
    const QString base = codec.tilePath(PointI(300, 600), 0);
    EXPECT_EQ(base, QStringLiteral("z1/0/0/1.2"));
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(base + QStringLiteral(".sqlitedb"), xy, zoom));
    EXPECT_EQ(zoom, 0);
    EXPECT_EQ(xy.x(), 256);
    EXPECT_EQ(xy.y(), 512);
}

// --- the shard-boundary pair: catches an off-by-one in the X>>10 shift ------------------------------

TEST(RegionTileNameCodec, ShardBoundaryBelow1024) {
    const RegionTileNameCodec codec;
    EXPECT_EQ(codec.tilePath(PointI(1023, 0), 0), QStringLiteral("z1/0/0/3.0"));
}

TEST(RegionTileNameCodec, ShardBoundaryAt1024) {
    const RegionTileNameCodec codec;
    EXPECT_EQ(codec.tilePath(PointI(1024, 0), 0), QStringLiteral("z1/1/0/4.0"));
}

// --- parse rejecting addExt-mangled input -----------------------------------------------------------

TEST(RegionTileNameCodec, ParseRejectsAddExtMangledInput) {
    // If a caller mistakenly ran the base name through ChangeFileExt-style addExt() instead of
    // concatenating, "0.0" + ext=".sqlitedb" would become "0.sqlitedb" (the LAST dot -- the one
    // between "0" and "0" -- gets treated as the base name's own extension and truncated away).
    // parse()'s regex requires a second all-digit group after the dot, so the mangled form is
    // correctly rejected rather than silently mis-parsed.
    const RegionTileNameCodec codec;
    PointI xy;
    quint8 zoom = 0;
    EXPECT_FALSE(codec.parse(u"z1/0/0/0.sqlitedb", xy, zoom));
}

// --- the extension-concatenation assertion ------------------------------------------------------

TEST(RegionTileNameCodec, ExtensionMustBeConcatenatedNotAddExted) {
    // storage.md §2.10.2: the base name already contains a '.' (the {X>>8}.{Y>>8} separator), so
    // real callers (u_TileStorageSQLiteHelper.pas:238-240, u_TileStorageBerkeleyDB.pas:634)
    // concatenate the extension directly and never call addExt()/ChangeFileExt on this codec.
    const RegionTileNameCodec codec;
    const QString base = codec.tilePath(PointI(0, 0), 0); // "z1/0/0/0.0"
    EXPECT_EQ(base + QStringLiteral(".sqlitedb"), QStringLiteral("z1/0/0/0.0.sqlitedb"));
    // Demonstrate why: the inherited ChangeFileExt-style addExt() (RegionTileNameCodec does not
    // override it, matching legacy) finds the dot between the two numbers and eats the second one.
    EXPECT_EQ(codec.addExt(base, u".sqlitedb"), QStringLiteral("z1/0/0/0.sqlitedb"));
}
