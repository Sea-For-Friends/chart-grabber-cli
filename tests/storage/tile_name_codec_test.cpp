// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-002..010. Ports i_TileFileNameGenerator.pas, i_TileFileNameParser.pas,
// u_TileFileNameBase.pas, u_TileFileName{SAS,GMV,ES,GM1,GM2,GM3,MOBAC,OsmAnd,TMS}.pas;
// docs/specs/storage.md §2.10/§2.10.1; fixtures from tools/gen_cache_fixtures.py
// (tests/fixtures/cache/expected.json).
//
// T-0021C adds the six regex-family codecs (GM1/GM2/GM3/MOBAC/OsmAnd+/TMS): 6 * 7 = 42 cases
// below, split 3 generate + 3 parse + 1 round-trip per codec, mirroring T-0021B's SAS/GMV/ES
// section above it. GM1/GM2/GM3/TMS's ParseZoom5 case doubles as the axis-transposition guard
// (§7.3): the fixture tile at that index has X != Y, so a transposed parse would already fail
// the plain EXPECT_EQ checks -- named/commented explicitly per the ticket's acceptance criteria.
#include "cg/storage/codecs/es_tile_name_codec.h"
#include "cg/storage/codecs/gm1_tile_name_codec.h"
#include "cg/storage/codecs/gm2_tile_name_codec.h"
#include "cg/storage/codecs/gm3_tile_name_codec.h"
#include "cg/storage/codecs/gmv_tile_name_codec.h"
#include "cg/storage/codecs/mobac_tile_name_codec.h"
#include "cg/storage/codecs/osmand_tile_name_codec.h"
#include "cg/storage/codecs/sas_tile_name_codec.h"
#include "cg/storage/codecs/tms_tile_name_codec.h"
#include "cg/storage/tile_name_codec.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

#include "fixture_path.h"

using cg::geo::PointI;
using cg::storage::EsTileNameCodec;
using cg::storage::Gm1TileNameCodec;
using cg::storage::Gm2TileNameCodec;
using cg::storage::Gm3TileNameCodec;
using cg::storage::GmvTileNameCodec;
using cg::storage::ITileNameCodec;
using cg::storage::MobacTileNameCodec;
using cg::storage::OsmandTileNameCodec;
using cg::storage::SasTileNameCodec;
using cg::storage::TmsTileNameCodec;

namespace {

struct FixtureTile {
    quint8 z = 0;
    int x = 0;
    int y = 0;
    QString kind;
    QString relpath;
    bool roundtrip = true; // false for the deliberately out-of-range RightStr-truncation demo tile
};

// Reads tests/fixtures/cache/expected.json's layouts.<name>.tiles array, written independently by
// tools/gen_cache_fixtures.py from the same legacy formulas the codecs below port.
QList<FixtureTile> loadLayout(const QString& name) {
    QFile f(cgStorageFixture(QStringLiteral("cache/expected.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray tiles = doc.object()
                                 .value(QStringLiteral("layouts"))
                                 .toObject()
                                 .value(name)
                                 .toObject()
                                 .value(QStringLiteral("tiles"))
                                 .toArray();
    QList<FixtureTile> out;
    out.reserve(tiles.size());
    for (const QJsonValue& v : tiles) {
        const QJsonObject o = v.toObject();
        FixtureTile t;
        t.z = static_cast<quint8>(o.value(QStringLiteral("z")).toInt());
        t.x = o.value(QStringLiteral("x")).toInt();
        t.y = o.value(QStringLiteral("y")).toInt();
        t.kind = o.value(QStringLiteral("kind")).toString();
        t.relpath = o.value(QStringLiteral("relpath")).toString();
        t.roundtrip = o.value(QStringLiteral("roundtrip")).toBool(true);
        out.push_back(t);
    }
    return out;
}

void expectGenerate(const ITileNameCodec& codec, const FixtureTile& t) {
    ASSERT_FALSE(t.relpath.isEmpty()) << "fixture missing/empty -- run tools/gen_cache_fixtures.py";
    const QString base = codec.tilePath(PointI(t.x, t.y), t.z);
    const QString ext = t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg");
    EXPECT_EQ(codec.addExt(base, ext), t.relpath);
}

void expectParse(const ITileNameCodec& codec, const FixtureTile& t) {
    ASSERT_FALSE(t.relpath.isEmpty()) << "fixture missing/empty -- run tools/gen_cache_fixtures.py";
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(t.relpath, xy, zoom));
    EXPECT_EQ(zoom, t.z);
    EXPECT_EQ(xy.x(), t.x);
    EXPECT_EQ(xy.y(), t.y);
}

} // namespace

// --- SAS.Planet (FEAT-STO-002) ------------------------------------------------------------------

TEST(TileNameCodecSas, GenerateZoom0Degenerate) {
    expectGenerate(SasTileNameCodec(), loadLayout(QStringLiteral("sas")).value(0));
}
TEST(TileNameCodecSas, GenerateZoom6RightStrTruncation) {
    expectGenerate(SasTileNameCodec(), loadLayout(QStringLiteral("sas")).value(2));
}
TEST(TileNameCodecSas, GenerateZoom10TopTier) {
    expectGenerate(SasTileNameCodec(), loadLayout(QStringLiteral("sas")).value(5));
}
TEST(TileNameCodecSas, ParseZoom5) {
    expectParse(SasTileNameCodec(), loadLayout(QStringLiteral("sas")).value(1));
}
TEST(TileNameCodecSas, ParseZoom8ZoomStrBoundary) {
    expectParse(SasTileNameCodec(), loadLayout(QStringLiteral("sas")).value(3));
}
TEST(TileNameCodecSas, ParseZoom9ZoomStrFlip) {
    expectParse(SasTileNameCodec(), loadLayout(QStringLiteral("sas")).value(4));
}
TEST(TileNameCodecSas, RoundTripAllFixtureTiles) {
    const SasTileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("sas"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- GoogleMV (FEAT-STO-003) ---------------------------------------------------------------------

TEST(TileNameCodecGmv, GenerateZoom0Degenerate) {
    expectGenerate(GmvTileNameCodec(), loadLayout(QStringLiteral("gmv")).value(0));
}
TEST(TileNameCodecGmv, GenerateZoom6RightStrTruncationSiblingTile) {
    expectGenerate(GmvTileNameCodec(), loadLayout(QStringLiteral("gmv")).value(2));
}
TEST(TileNameCodecGmv, GenerateZoom10TopTier) {
    expectGenerate(GmvTileNameCodec(), loadLayout(QStringLiteral("gmv")).value(5));
}
TEST(TileNameCodecGmv, ParseZoom5) {
    expectParse(GmvTileNameCodec(), loadLayout(QStringLiteral("gmv")).value(1));
}
TEST(TileNameCodecGmv, ParseZoom8ZoomStrBoundary) {
    expectParse(GmvTileNameCodec(), loadLayout(QStringLiteral("gmv")).value(3));
}
TEST(TileNameCodecGmv, ParseZoom9ZoomStrFlip) {
    expectParse(GmvTileNameCodec(), loadLayout(QStringLiteral("gmv")).value(4));
}
TEST(TileNameCodecGmv, RoundTripAllFixtureTiles) {
    const GmvTileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("gmv"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- EarthSlicer (FEAT-STO-004) -------------------------------------------------------------------

TEST(TileNameCodecEs, GenerateZoom0Degenerate) {
    expectGenerate(EsTileNameCodec(), loadLayout(QStringLiteral("es")).value(0));
}
TEST(TileNameCodecEs, GenerateZoom6RightStrTruncation) {
    expectGenerate(EsTileNameCodec(), loadLayout(QStringLiteral("es")).value(2));
}
TEST(TileNameCodecEs, GenerateZoom10TopTierDirectory) {
    expectGenerate(EsTileNameCodec(), loadLayout(QStringLiteral("es")).value(5));
}
TEST(TileNameCodecEs, ParseZoom5) {
    expectParse(EsTileNameCodec(), loadLayout(QStringLiteral("es")).value(1));
}
TEST(TileNameCodecEs, ParseZoom8ZoomStrBoundary) {
    expectParse(EsTileNameCodec(), loadLayout(QStringLiteral("es")).value(3));
}
TEST(TileNameCodecEs, ParseZoom9ZoomStrFlip) {
    expectParse(EsTileNameCodec(), loadLayout(QStringLiteral("es")).value(4));
}
TEST(TileNameCodecEs, RoundTripAllFixtureTiles) {
    const EsTileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("es"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- GlobalMapper Tiles (FEAT-STO-005) ------------------------------------------------------------

TEST(TileNameCodecGm1, GenerateZoom0) {
    expectGenerate(Gm1TileNameCodec(), loadLayout(QStringLiteral("gm1")).value(0));
}
TEST(TileNameCodecGm1, GenerateZoom6LargeX) {
    expectGenerate(Gm1TileNameCodec(), loadLayout(QStringLiteral("gm1")).value(2));
}
TEST(TileNameCodecGm1, GenerateZoom10) {
    expectGenerate(Gm1TileNameCodec(), loadLayout(QStringLiteral("gm1")).value(5));
}
TEST(TileNameCodecGm1, ParseZoom5AxisGuard) {
    // (!) u_TileFileNameGM1.pas:70-91 GetTilePoint: "ATileXY.Y := ...; // (!) Y - first, X - last".
    // The fixture tile here has X=3, Y=7 (unequal), so a transposed parse would already fail the
    // plain EXPECT_EQ below; named explicitly per the ticket's axis-transposition guard.
    expectParse(Gm1TileNameCodec(), loadLayout(QStringLiteral("gm1")).value(1));
}
TEST(TileNameCodecGm1, ParseZoom8) {
    expectParse(Gm1TileNameCodec(), loadLayout(QStringLiteral("gm1")).value(3));
}
TEST(TileNameCodecGm1, ParseZoom9) {
    expectParse(Gm1TileNameCodec(), loadLayout(QStringLiteral("gm1")).value(4));
}
TEST(TileNameCodecGm1, RoundTripAllFixtureTiles) {
    const Gm1TileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("gm1"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- GlobalMapper Aux (FEAT-STO-006) --------------------------------------------------------------

TEST(TileNameCodecGm2, GenerateZoom0) {
    expectGenerate(Gm2TileNameCodec(), loadLayout(QStringLiteral("gm2")).value(0));
}
TEST(TileNameCodecGm2, GenerateZoom6LargeX) {
    expectGenerate(Gm2TileNameCodec(), loadLayout(QStringLiteral("gm2")).value(2));
}
TEST(TileNameCodecGm2, GenerateZoom10) {
    expectGenerate(Gm2TileNameCodec(), loadLayout(QStringLiteral("gm2")).value(5));
}
TEST(TileNameCodecGm2, ParseZoom5AxisGuard) {
    // (!) u_TileFileNameGM2.pas:70-91 GetTilePoint: same Y-first/X-last trap as GM1.
    expectParse(Gm2TileNameCodec(), loadLayout(QStringLiteral("gm2")).value(1));
}
TEST(TileNameCodecGm2, ParseZoom8) {
    expectParse(Gm2TileNameCodec(), loadLayout(QStringLiteral("gm2")).value(3));
}
TEST(TileNameCodecGm2, ParseZoom9) {
    expectParse(Gm2TileNameCodec(), loadLayout(QStringLiteral("gm2")).value(4));
}
TEST(TileNameCodecGm2, RoundTripAllFixtureTiles) {
    const Gm2TileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("gm2"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- GlobalMapper Bing (FEAT-STO-007) -------------------------------------------------------------

TEST(TileNameCodecGm3, GenerateZoom0) {
    expectGenerate(Gm3TileNameCodec(), loadLayout(QStringLiteral("gm3")).value(0));
}
TEST(TileNameCodecGm3, GenerateZoom6LargeX) {
    expectGenerate(Gm3TileNameCodec(), loadLayout(QStringLiteral("gm3")).value(2));
}
TEST(TileNameCodecGm3, GenerateZoom10) {
    expectGenerate(Gm3TileNameCodec(), loadLayout(QStringLiteral("gm3")).value(5));
}
TEST(TileNameCodecGm3, ParseZoom5AxisGuard) {
    // (!) u_TileFileNameGM3.pas:70-91 GetTilePoint: same Y-first/X-last trap as GM1/GM2.
    expectParse(Gm3TileNameCodec(), loadLayout(QStringLiteral("gm3")).value(1));
}
TEST(TileNameCodecGm3, ParseZoom8) {
    expectParse(Gm3TileNameCodec(), loadLayout(QStringLiteral("gm3")).value(3));
}
TEST(TileNameCodecGm3, ParseZoom9) {
    expectParse(Gm3TileNameCodec(), loadLayout(QStringLiteral("gm3")).value(4));
}
TEST(TileNameCodecGm3, RoundTripAllFixtureTiles) {
    const Gm3TileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("gm3"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- MOBAC (FEAT-STO-008) --------------------------------------------------------------------------

TEST(TileNameCodecMobac, GenerateZoom0) {
    expectGenerate(MobacTileNameCodec(), loadLayout(QStringLiteral("mobac")).value(0));
}
TEST(TileNameCodecMobac, GenerateZoom6LargeX) {
    expectGenerate(MobacTileNameCodec(), loadLayout(QStringLiteral("mobac")).value(2));
}
TEST(TileNameCodecMobac, GenerateZoom10) {
    expectGenerate(MobacTileNameCodec(), loadLayout(QStringLiteral("mobac")).value(5));
}
TEST(TileNameCodecMobac, ParseZoom5) {
    expectParse(MobacTileNameCodec(), loadLayout(QStringLiteral("mobac")).value(1));
}
TEST(TileNameCodecMobac, ParseZoom8) {
    expectParse(MobacTileNameCodec(), loadLayout(QStringLiteral("mobac")).value(3));
}
TEST(TileNameCodecMobac, ParseZoom9) {
    expectParse(MobacTileNameCodec(), loadLayout(QStringLiteral("mobac")).value(4));
}
TEST(TileNameCodecMobac, RoundTripAllFixtureTiles) {
    const MobacTileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("mobac"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- OsmAnd+ (FEAT-STO-009) --------------------------------------------------------------------------

TEST(TileNameCodecOsmand, GenerateZoom0) {
    expectGenerate(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(0));
}
TEST(TileNameCodecOsmand, GenerateZoom6LargeX) {
    expectGenerate(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(2));
}
TEST(TileNameCodecOsmand, GenerateZoom10) {
    // Also exercises the addExt() override's ".jpg.tile" suffix (expectGenerate always calls
    // codec.addExt()); the TNE fixture (idx 3) below exercises the ".tne" branch.
    expectGenerate(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(5));
}
TEST(TileNameCodecOsmand, ParseZoom5) {
    expectParse(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(1));
}
TEST(TileNameCodecOsmand, ParseZoom8) {
    expectParse(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(3));
}
TEST(TileNameCodecOsmand, GenerateAndParseTneMarker) {
    // Fixture idx 6 is the one TNE marker in the tile set: a plain ".tne", not ".jpg.tne" --
    // confirms the addExt() override's case-insensitive ".tne" branch leaves it untouched.
    expectGenerate(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(6));
    expectParse(OsmandTileNameCodec(), loadLayout(QStringLiteral("osmand")).value(6));
}
TEST(TileNameCodecOsmand, RoundTripAllFixtureTiles) {
    const OsmandTileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("osmand"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- TMS (FEAT-STO-010) -----------------------------------------------------------------------------

TEST(TileNameCodecTms, GenerateZoom0) {
    expectGenerate(TmsTileNameCodec(), loadLayout(QStringLiteral("tms")).value(0));
}
TEST(TileNameCodecTms, GenerateZoom6LargeXNegativeFlip) {
    // The out-of-range demo tile (Y=67 at zoom 6, valid range is [0, 64)) flips to a NEGATIVE
    // number: (1<<6)-67-1 = -4. Byte-exact with legacy's Format('%d', ...), which would print the
    // same "-4" -- not reproduced as a round-trip case (fixture roundtrip=False).
    expectGenerate(TmsTileNameCodec(), loadLayout(QStringLiteral("tms")).value(2));
}
TEST(TileNameCodecTms, GenerateZoom10) {
    expectGenerate(TmsTileNameCodec(), loadLayout(QStringLiteral("tms")).value(5));
}
TEST(TileNameCodecTms, ParseZoom5AxisGuard) {
    // (!) u_TileFileNameTMS.pas:65-98: X is captured first, but Y is then FLIPPED via
    // GetFlippedY(zoom, m4) -- getting either the axis order or the flip wrong changes the
    // decoded Y. The fixture tile's X=3, Y=7 are unequal, guarding both mistakes at once.
    expectParse(TmsTileNameCodec(), loadLayout(QStringLiteral("tms")).value(1));
}
TEST(TileNameCodecTms, ParseZoom8) {
    expectParse(TmsTileNameCodec(), loadLayout(QStringLiteral("tms")).value(3));
}
TEST(TileNameCodecTms, ParseZoom9) {
    expectParse(TmsTileNameCodec(), loadLayout(QStringLiteral("tms")).value(4));
}
TEST(TileNameCodecTms, RoundTripAllFixtureTiles) {
    const TmsTileNameCodec codec;
    for (const FixtureTile& t : loadLayout(QStringLiteral("tms"))) {
        if (!t.roundtrip) {
            continue;
        }
        const QString path =
            codec.addExt(codec.tilePath(PointI(t.x, t.y), t.z),
                         t.kind == QStringLiteral("tne") ? QStringLiteral(".tne") : QStringLiteral(".jpg"));
        PointI xy;
        quint8 zoom = 0;
        ASSERT_TRUE(codec.parse(path, xy, zoom)) << path.toStdString();
        EXPECT_EQ(zoom, t.z);
        EXPECT_EQ(xy.x(), t.x);
        EXPECT_EQ(xy.y(), t.y);
    }
}

// --- 5 hand-written cases (T-0021B, SAS/GMV/ES only) ------------------------------------------------

TEST(TileNameCodec, UnparseablePathReturnsFalse) {
    PointI xy;
    quint8 zoom = 0;
    EXPECT_FALSE(SasTileNameCodec().parse(u"not/a/tile/path.jpg", xy, zoom));
    EXPECT_FALSE(GmvTileNameCodec().parse(u"not/a/tile/path.jpg", xy, zoom));
    EXPECT_FALSE(EsTileNameCodec().parse(u"not/a/tile/path.jpg", xy, zoom));
}

TEST(TileNameCodec, ExtraLeadingDirectoriesAreAccepted) {
    // c_SAS_Expr's leading group is `(.+/)?`, matching any prefix (a mounted cache root, for
    // instance), not just zero or one directory.
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(SasTileNameCodec().parse(u"some/deeply/nested/mount/z6/0/x3/0/y7.jpg", xy, zoom));
    EXPECT_EQ(zoom, 5);
    EXPECT_EQ(xy.x(), 3);
    EXPECT_EQ(xy.y(), 7);
}

TEST(TileNameCodecGmv, Zoom0IsTheSingleFile01T) {
    const GmvTileNameCodec codec;
    EXPECT_EQ(codec.tilePath(PointI(0, 0), 0), QStringLiteral("01/t"));
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(u"01/t", xy, zoom));
    EXPECT_EQ(zoom, 0);
    EXPECT_EQ(xy.x(), 0);
    EXPECT_EQ(xy.y(), 0);
}

TEST(TileNameCodecGmv, MisNamedDirectoryIsSilentlyAccepted) {
    // GetTilePoint derives the zoom from the quad string's LENGTH, never from the leading
    // directory -- "99" here does not match the "tqrs" quad string's own zoom (3) but the parse
    // still succeeds, exactly as legacy's TSQR2XY does.
    const GmvTileNameCodec codec;
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(u"99/tqrs", xy, zoom));
    EXPECT_EQ(zoom, 3);
}

TEST(TileNameCodec, AddExtRoundTrip) {
    // Ports TTileFileNameBase.AddExt (Delphi ChangeFileExt): a dot in a DIRECTORY component must
    // not be mistaken for the file's own extension, and a second call replaces (not appends to)
    // the first extension -- the true round trip.
    const SasTileNameCodec codec;
    const QString withExt = codec.addExt(QStringLiteral("dir.with.dots/z1/0/x0/0/y0"), u".jpg");
    EXPECT_EQ(withExt, QStringLiteral("dir.with.dots/z1/0/x0/0/y0.jpg"));
    EXPECT_EQ(codec.addExt(withExt, u".png"), QStringLiteral("dir.with.dots/z1/0/x0/0/y0.png"));
}

// --- Test/u_RegularExpressions_Test.pas TestTileFileNameTMS / TestTileFileNameSAS (T-0040) -----------
// Legacy checks the TRegExpr capture groups of two literal names (backslash separators there, '/'
// in the port). Not covered by the fixture-driven cases above (no z10 / 603|604 / 680|342 tile), so
// added: parse of the legacy name (prefix + .jpg ext, groups 2..4 -> zoom/x/y) and generation of its tail.
// Zoom is stored 0-based for SAS (group "11" -> 10); TMS flips Y: (1<<10)-680-1 = 343.

TEST(TileNameCodecTms, LegacyRegexExampleCacheTmsSat) {
    const TmsTileNameCodec codec;
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(u"cache_tms/sat/10/603/680.jpg", xy, zoom));
    EXPECT_EQ(zoom, 10);
    EXPECT_EQ(xy.x(), 603);
    EXPECT_EQ(xy.y(), 343);
    EXPECT_EQ(codec.addExt(codec.tilePath(xy, zoom), u".jpg"), QStringLiteral("10/603/680.jpg"));
}

TEST(TileNameCodecSas, LegacyRegexExampleCacheSat) {
    const SasTileNameCodec codec;
    PointI xy;
    quint8 zoom = 0;
    ASSERT_TRUE(codec.parse(u"cache/sat/z11/0/x604/0/y342.jpg", xy, zoom));
    EXPECT_EQ(zoom, 10);
    EXPECT_EQ(xy.x(), 604);
    EXPECT_EQ(xy.y(), 342);
    EXPECT_EQ(codec.addExt(codec.tilePath(xy, zoom), u".jpg"), QStringLiteral("z11/0/x604/0/y342.jpg"));
}
