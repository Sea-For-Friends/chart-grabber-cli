// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-011. Ports i_PredicateByTileInfo.pas, u_PredicateByTileInfoBase.pas (7 of its
// 9 subclasses), i_PredicateByTileRequest.pas; docs/specs/storage.md §2.21;
// docs/handoffs/T-0021A.md Rework 1 (T-0021A review findings 1, 2, 3)
#include "cg/storage/predicates.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QTime>

#include <gtest/gtest.h>

using cg::storage::TileDate;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;

namespace {

TileInfo makeTile(TileInfoType type, quint32 size = 0, TileDate loadDate = TileDate{}) {
    TileInfo t;
    t.type = type;
    t.size = size;
    t.loadDate = loadDate;
    return t;
}

constexpr TileInfoType kAllTypes[] = {TileInfoType::Unknown, TileInfoType::NotExists, TileInfoType::TneExists,
                                      TileInfoType::Exists};

// --- tileExists() : ports TPredicateByTileInfoExistsTile ---------------------------------------

TEST(Predicates, TileExistsMatchesOnlyExistsAcrossAllFourTypes) {
    const auto pred = cg::storage::tileExists();
    for (const TileInfoType type : kAllTypes) {
        EXPECT_EQ(pred(makeTile(type)), type == TileInfoType::Exists) << int(type);
    }
}

// --- tileNotExists() : ports TPredicateByTileInfoNotExistsTile ---------------------------------

TEST(Predicates, TileNotExistsMatchesEverythingButExistsAcrossAllFourTypes) {
    const auto pred = cg::storage::tileNotExists();
    for (const TileInfoType type : kAllTypes) {
        EXPECT_EQ(pred(makeTile(type)), type != TileInfoType::Exists) << int(type);
    }
}

// --- tileExistsTne() : ports TPredicateByTileInfoExistsTNE (fr_DeleteTiles.pas:150, "Delete tne")

TEST(Predicates, TileExistsTneMatchesOnlyTneExistsAcrossAllFourTypes) {
    const auto pred = cg::storage::tileExistsTne();
    for (const TileInfoType type : kAllTypes) {
        EXPECT_EQ(pred(makeTile(type)), type == TileInfoType::TneExists) << int(type);
    }
}

// --- tileExistsOrTne() : ports ExistsTileOrTNE (fr_DeleteTiles.pas:156, "Delete both") ---------

TEST(Predicates, TileExistsOrTneMatchesExistsAndTneExistsAcrossAllFourTypes) {
    const auto pred = cg::storage::tileExistsOrTne();
    for (const TileInfoType type : kAllTypes) {
        const bool expected = type == TileInfoType::Exists || type == TileInfoType::TneExists;
        EXPECT_EQ(pred(makeTile(type)), expected) << int(type);
    }
}

// --- tileExistsCheckData() : ports ExistsTileCheckData (fr_DeleteTiles.pas:164) ----------------

TEST(Predicates, TileExistsCheckDataDelegatesOnlyForExists) {
    const auto trueCheck = cg::storage::tileExistsCheckData([](const QByteArray&) { return true; });
    const auto falseCheck = cg::storage::tileExistsCheckData([](const QByteArray&) { return false; });
    for (const TileInfoType type : kAllTypes) {
        const TileInfo t = makeTile(type);
        if (type == TileInfoType::Exists) {
            EXPECT_TRUE(trueCheck(t));
            EXPECT_FALSE(falseCheck(t));
        } else {
            EXPECT_FALSE(trueCheck(t)) << int(type);
            EXPECT_FALSE(falseCheck(t)) << int(type);
        }
    }
}

// --- tileOlderThan() : ports NotExistOrBeforDate (AIgnoreTNE=False), storage.md §2.4.3 ---------

TEST(Predicates, TileOlderThanNotExistsIsAlwaysTrue) {
    const auto pred = cg::storage::tileOlderThan(TileDate(QDate(2000, 1, 1), QTime(0, 0, 0)));
    EXPECT_TRUE(pred(makeTile(TileInfoType::NotExists)));
}

TEST(Predicates, TileOlderThanTneAndUnknownAreAlwaysFalseWithDefaultIgnoreTne) {
    const auto pred = cg::storage::tileOlderThan(TileDate(QDate(2100, 1, 1), QTime(0, 0, 0)));
    EXPECT_FALSE(pred(makeTile(TileInfoType::TneExists)));
    EXPECT_FALSE(pred(makeTile(TileInfoType::Unknown)));
}

TEST(Predicates, TileOlderThanExistsUsesStrictLessThan) {
    const TileDate cutoff(QDate(2020, 1, 1), QTime(0, 0, 0));
    const auto pred = cg::storage::tileOlderThan(cutoff);
    EXPECT_TRUE(pred(makeTile(TileInfoType::Exists, 0, TileDate(QDate(2019, 1, 1), QTime(0, 0, 0)))));
    EXPECT_FALSE(pred(makeTile(TileInfoType::Exists, 0, cutoff))); // equal, not older -- strict <
    EXPECT_FALSE(pred(makeTile(TileInfoType::Exists, 0, TileDate(QDate(2021, 1, 1), QTime(0, 0, 0)))));
}

TEST(Predicates, TileOlderThanExistsWithInvalidLoadDateIsAlwaysTrue) {
    // storage.md §2.4.3: invalid loadDate is "infinitely old", checked via isValid() explicitly
    // (T-0021A review finding 3) rather than relying on QDateTime's own, Qt-version-dependent
    // ordering of invalid vs. valid values.
    const auto pred = cg::storage::tileOlderThan(TileDate(QDate(1970, 1, 1), QTime(0, 0, 0)));
    EXPECT_TRUE(pred(makeTile(TileInfoType::Exists, 0, TileDate{})));
}

// --- tileSizeEquals() : ports TPredicateByTileInfoEqualSize, exact legacy semantics ------------
// (T-0021A review finding 1: the shipped `tileSizeDiffers` inverted this and hard-coded
// ADeleteTNE=False; fixed here to `tileSizeEquals(size, matchTne)`.)

TEST(Predicates, TileSizeEqualsExistsComparesSizeExactlyNotDiffers) {
    const auto pred = cg::storage::tileSizeEquals(100);
    EXPECT_TRUE(pred(makeTile(TileInfoType::Exists, 100)));
    EXPECT_FALSE(pred(makeTile(TileInfoType::Exists, 99)));
    EXPECT_FALSE(pred(makeTile(TileInfoType::Exists, 101)));
}

TEST(Predicates, TileSizeEqualsTneUsesMatchTneParameterBothArms) {
    const auto ignoreTne = cg::storage::tileSizeEquals(0, /*matchTne=*/false);
    const auto matchTne = cg::storage::tileSizeEquals(0, /*matchTne=*/true);
    EXPECT_FALSE(ignoreTne(makeTile(TileInfoType::TneExists)));
    EXPECT_TRUE(matchTne(makeTile(TileInfoType::TneExists)));
}

TEST(Predicates, TileSizeEqualsNotExistsAndUnknownAreAlwaysFalse) {
    const auto pred = cg::storage::tileSizeEquals(0, /*matchTne=*/true);
    EXPECT_FALSE(pred(makeTile(TileInfoType::NotExists)));
    EXPECT_FALSE(pred(makeTile(TileInfoType::Unknown)));
}

// Named per T-0021A review finding 2: the legacy "delete empty tiles" fallback,
// fr_DeleteTiles.pas:166 -- TPredicateByTileInfoEqualSize.Create(False, 0).
TEST(Predicates, DeleteEmptyTilesLegacyCallSiteEqualSizeFalseZero) {
    const auto deleteEmptyTiles = cg::storage::tileSizeEquals(0, /*matchTne=*/false);
    EXPECT_TRUE(deleteEmptyTiles(makeTile(TileInfoType::Exists, 0)));   // empty tile -> selected
    EXPECT_FALSE(deleteEmptyTiles(makeTile(TileInfoType::Exists, 50))); // non-empty -> not selected
    EXPECT_FALSE(deleteEmptyTiles(makeTile(TileInfoType::TneExists)));  // ADeleteTNE=False
    EXPECT_FALSE(deleteEmptyTiles(makeTile(TileInfoType::NotExists)));
}

// fr_DeleteTiles.pas:154 -- TPredicateByTileInfoEqualSize.Create(True, seDelSize.Value): the
// "Delete both" + delete-by-size path, where ADeleteTNE is passed True.
TEST(Predicates, DeleteBySizeLegacyCallSiteEqualSizeTrue) {
    const auto pred = cg::storage::tileSizeEquals(1234, /*matchTne=*/true);
    EXPECT_TRUE(pred(makeTile(TileInfoType::Exists, 1234)));
    EXPECT_TRUE(pred(makeTile(TileInfoType::TneExists)));
}

} // namespace
