// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Tests of pruneOtherSnapshots (docs/specs/weather-sources.md 9, T-0246); no FEAT
// tag (Features none). Inherits Src/RegionProcess/Delete/u_ThreadDeleteTiles.pas ProcessRegion: stop checked
// before each tile (:121-123), DeleteTile with the found version, the `true` answers counted (:127-129). Rig:
// tile_estimate_rig.h (scripted storage; the current version is the map's base version "base-ver"; the area
// is the z6 rectangle of 80 tiles and its z3 form of 2 tiles).
#include "cg/app/snapshot_prune.h"
#include "cg/core/hash.h"

#include <gtest/gtest.h>

#include "tile_estimate_rig.h"

using namespace cg;
using namespace cg::app;
using namespace cg::app::test;
using namespace Qt::StringLiterals;

namespace {

class SnapshotPruneTest : public EstimateRigTest {
protected:
    storage::MapVersionInfoPtr version(const QString& text) { return factory_.byStoreString(text); }
    storage::MapVersionInfoPtr current() const { return map_->versionRequestValue->getStatic()->baseVersion; }

    core::Result<qint64> run(const std::vector<quint8>& zooms, std::stop_token stop = {}) {
        const auto area = rect(kLonLeft, kLatTop, kLonRight, kLatBottom);
        return pruneOtherSnapshots(*map_, *area, zooms, app(), stop);
    }

    // T-0352: the record of a refused save of "bytes" (size and CRC-32, as the facade stores it)
    static storage::ITileStorage::SkippedSave bytes() { return {5, core::crc32(0, "bytes", 5)}; }

    storage::MapVersionFactory factory_;
};

using Deletion = std::tuple<int, int, int, QString>;

std::vector<Deletion> deletions(const ScriptedStorage& s) {
    std::vector<Deletion> result;
    for (const auto& [zoom, x, y, v] : s.deletions) {
        result.emplace_back(zoom, x, y, v ? v->storeString() : QStringLiteral("<null>"));
    }
    return result;
}

} // namespace

// Every other version of a tile of the area is deleted with its own version, the current one stays, a tile
// outside the area and a tile of another zoom are never touched, the count is the number of deletions. The
// versions are given with the current one in the middle and not sorted.
TEST_F(SnapshotPruneTest, DeletesEveryOtherVersionInTheAreaAndNeverTheCurrentOne) {
    storage_->versions[{3, 2, 3}] = {version(u"/zz"_s), current(), version(u"/Ä1"_s)};
    storage_->versions[{3, 1, 3}] = {current()};
    storage_->versions[{6, 8, 26}] = {version(u"/B"_s), current()};
    storage_->versions[{6, 24, 26}] = {version(u"/outside"_s)}; // just right of the z6 rectangle
    storage_->versions[{4, 2, 3}] = {version(u"/other zoom"_s)};
    const auto result = run({6, 3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 3);
    EXPECT_EQ(deletions(*storage_),
              (std::vector<Deletion>{{6, 8, 26, u"/B"_s}, {3, 2, 3, u"/zz"_s}, {3, 2, 3, u"/Ä1"_s}}));
    EXPECT_EQ(storage_->listCalls.size(), 82U); // 80 + 2 tiles: one lookup per tile of the area
}

// The lookup asks for all versions: no base version, showOtherVersions.
TEST_F(SnapshotPruneTest, ListsTheVersionsOfEveryTileWithShowOtherVersions) {
    ASSERT_TRUE(run({3}).has_value());
    ASSERT_EQ(storage_->listCalls.size(), 2U);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(storage_->listCalls[i].baseVersion, nullptr);
        EXPECT_TRUE(storage_->listShowOther[i]);
    }
    EXPECT_EQ(storage_->deletions.size(), 0U); // null lists: nothing to delete
}

// A `false` answer of deleteTile is not counted (the tile was gone already).
TEST_F(SnapshotPruneTest, OnlyTrueAnswersAreCounted) {
    storage_->versions[{3, 1, 3}] = {version(u"/a"_s), version(u"/b"_s)};
    storage_->deleteAnswer = false;
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(storage_->deletions.size(), 2U);
}

// A storage error of the lookup is returned at once: no further lookup, the deletions made stay made.
TEST_F(SnapshotPruneTest, ALookupErrorStopsTheWalk) {
    storage_->versions[{3, 1, 3}] = {version(u"/a"_s)};
    storage_->failListAtCall = 2;
    const auto result = run({3});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), core::ErrorCode::IoError);
    EXPECT_EQ(result.error().what(), u"scripted list failure"_s);
    EXPECT_EQ(storage_->listCalls.size(), 2U);
    EXPECT_EQ(deletions(*storage_), (std::vector<Deletion>{{3, 1, 3, u"/a"_s}}));
}

// A storage error of a deletion is returned at once: the next version of the tile is not deleted.
TEST_F(SnapshotPruneTest, ADeleteErrorStopsTheWalk) {
    storage_->versions[{3, 1, 3}] = {version(u"/a"_s), version(u"/b"_s)};
    storage_->versions[{3, 2, 3}] = {version(u"/c"_s)};
    storage_->failDeleteAtCall = 1;
    const auto result = run({3});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().what(), u"scripted delete failure"_s);
    EXPECT_EQ(storage_->deletions.size(), 1U);
}

// The stop is checked before each tile (legacy :121-123): a stop requested after the first deletion ends the
// walk with Cancelled before the second tile is looked up.
TEST_F(SnapshotPruneTest, StopEndsTheWalkWithCancelledBeforeTheNextTile) {
    storage_->versions[{3, 1, 3}] = {version(u"/a"_s)};
    storage_->versions[{3, 2, 3}] = {version(u"/c"_s)};
    std::stop_source source;
    storage_->onDelete = [&](int) { source.request_stop(); };
    const auto result = run({3}, source.get_token());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), core::ErrorCode::Cancelled);
    EXPECT_EQ(storage_->listCalls.size(), 1U);
    EXPECT_EQ(deletions(*storage_), (std::vector<Deletion>{{3, 1, 3, u"/a"_s}}));
}

// T-0351: the download recorded the tile as skipped (byte-identical to a stored version) and the current
// version has no row: the newest stored version (the list is newest first) is read, every stored version is
// deleted and the tile is saved under the current version. The moved tile is not counted as removed. A tile
// that has the current version keeps the old behaviour (all others deleted, no save), a tile the new frame
// lacks (not recorded: 404) is deleted with its old frames and not saved.
TEST_F(SnapshotPruneTest, ASkippedTileMovesItsNewestVersionToTheCurrentOneAndTheOlderOnesGo) {
    storage::TileInfo info;
    info.type = storage::TileInfoType::Exists;
    info.data = "bytes";
    info.contentType = "image/png";
    info.size = 5;
    storage_->put(3, 1, 3, info);
    storage_->put(3, 2, 3, info);
    storage_->skippedSaves = {{{3, 1, 3}, bytes()}, {{3, 2, 3}, bytes()}};
    storage_->versions[{3, 1, 3}] = {version(u"/new"_s), version(u"/old"_s)};
    storage_->versions[{3, 2, 3}] = {version(u"/x"_s), current()};
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2); // /old of the first tile, /x of the second
    EXPECT_EQ(deletions(*storage_),
              (std::vector<Deletion>{{3, 1, 3, u"/new"_s}, {3, 1, 3, u"/old"_s}, {3, 2, 3, u"/x"_s}}));
    ASSERT_EQ(storage_->saves.size(), 1U); // the second tile has its row: nothing to move
    const auto& [zoom, x, y, saved, data] = storage_->saves[0];
    EXPECT_EQ(std::make_tuple(zoom, x, y, data), std::make_tuple(3, 1, 3, QByteArray("bytes")));
    EXPECT_TRUE(saved->isSame(*current()));
    ASSERT_EQ(storage_->calls.size(), 1U); // the data of the newest version was asked once, with its data
    EXPECT_EQ(storage_->calls[0].mode, storage::LoadMode::WithData);
    EXPECT_EQ(storage_->calls[0].baseVersion->storeString(), u"/new"_s);
    EXPECT_EQ(storage_->takenFor.size(), 1U); // the record is asked once, for the current version
    EXPECT_TRUE(storage_->takenFor[0]->isSame(*current()));
}

// A tile the new frame lacks (no row, not recorded as skipped) is deleted with its old frame: an old
// picture is never shown as current.
TEST_F(SnapshotPruneTest, ATileOfTheNewFrameThatIsMissingIsDeletedNotCarriedOver) {
    storage::TileInfo info;
    info.type = storage::TileInfoType::Exists;
    info.data = "bytes";
    storage_->put(3, 1, 3, info);
    storage_->put(3, 2, 3, info);
    storage_->skippedSaves = {{{3, 2, 3}, bytes()}}; // only the other tile was skipped
    storage_->versions[{3, 1, 3}] = {version(u"/new"_s), version(u"/old"_s)};
    storage_->versions[{3, 2, 3}] = {version(u"/new"_s)};
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2);
    EXPECT_EQ(deletions(*storage_),
              (std::vector<Deletion>{{3, 1, 3, u"/new"_s}, {3, 1, 3, u"/old"_s}, {3, 2, 3, u"/new"_s}}));
    ASSERT_EQ(storage_->saves.size(), 1U);
    EXPECT_EQ(std::get<2>(storage_->saves[0]), 3);
    EXPECT_EQ(std::get<1>(storage_->saves[0]), 2); // only (3, 2, 3) was moved
}

// A recorded tile whose rows are gone between the list and the read has no matching candidate: it is deleted
// like a 404 (nothing to move, nothing saved).
TEST_F(SnapshotPruneTest, AVanishedSkippedTileIsDeletedAndNotSaved) {
    storage_->skippedSaves = {{{3, 1, 3}, bytes()}};
    storage_->versions[{3, 1, 3}] = {version(u"/new"_s)}; // no info scripted: Unknown
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
    EXPECT_EQ(deletions(*storage_), (std::vector<Deletion>{{3, 1, 3, u"/new"_s}}));
    EXPECT_TRUE(storage_->saves.empty());
}

// A failed or refused save puts the tile back under its own version and reports.
TEST_F(SnapshotPruneTest, AFailedSaveRestoresTheTileUnderItsOwnVersion) {
    storage::TileInfo info;
    info.type = storage::TileInfoType::Exists;
    info.data = "bytes";
    storage_->put(3, 1, 3, info);
    storage_->skippedSaves = {{{3, 1, 3}, bytes()}};
    storage_->versions[{3, 1, 3}] = {version(u"/new"_s)};
    storage_->failSaveAtCall = 1;
    const auto failed = run({3});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().what(), u"scripted save failure"_s);
    ASSERT_EQ(storage_->saves.size(), 2U);
    EXPECT_EQ(std::get<3>(storage_->saves[1])->storeString(), u"/new"_s);

    storage_->saves.clear();
    storage_->failSaveAtCall = 0;
    storage_->refuseSaveAtCall = 1;
    storage_->skippedSaves = {{{3, 1, 3}, bytes()}};
    const auto refused = run({3});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code(), core::ErrorCode::IoError);
    EXPECT_EQ(storage_->saves.size(), 2U);
}

// T-0352: the list drops the rows of the empty version, so a recorded tile whose only stored row is a
// pre-T-0246 one under the empty version has a null list: that row is read (null version), deleted and saved
// under the current version; it is no loss (not counted). A tile with a null list that was not recorded stays
// untouched.
TEST_F(SnapshotPruneTest, ARecordedTileWithOnlyAnEmptyVersionRowMovesToTheCurrentVersion) {
    storage::TileInfo info;
    info.type = storage::TileInfoType::Exists;
    info.data = "bytes";
    storage_->put(3, 1, 3, info);
    storage_->put(3, 2, 3, info);
    storage_->skippedSaves = {{{3, 1, 3}, bytes()}}; // no list for either tile
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(deletions(*storage_), (std::vector<Deletion>{{3, 1, 3, u"<null>"_s}}));
    ASSERT_EQ(storage_->saves.size(), 1U);
    const auto& [zoom, x, y, saved, data] = storage_->saves[0];
    EXPECT_EQ(std::make_tuple(zoom, x, y, data), std::make_tuple(3, 1, 3, QByteArray("bytes")));
    EXPECT_TRUE(saved->isSame(*current()));
    ASSERT_EQ(storage_->calls.size(), 1U);
    EXPECT_EQ(storage_->calls[0].baseVersion, nullptr);
    EXPECT_EQ(storage_->calls[0].mode, storage::LoadMode::WithData);
}

// The empty-version row of a recorded tile that vanished is left alone.
TEST_F(SnapshotPruneTest, AVanishedEmptyVersionRowIsNotTouched) {
    storage_->skippedSaves = {{{3, 1, 3}, bytes()}};
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_TRUE(storage_->deletions.empty());
    EXPECT_TRUE(storage_->saves.empty());
}

// T-0352: a recorded tile whose candidates (the newest listed version and the empty-version row) both differ
// in size or CRC from the refused data is not the new frame's: it goes with its old frame like a 404, nothing
// is saved, the empty-version row stays. Both candidates were read, the newest first.
TEST_F(SnapshotPruneTest, ARecordedTileNoCandidateMatchesIsDeletedLikeA404) {
    storage::TileInfo info;
    info.type = storage::TileInfoType::Exists;
    info.data = "bytes";
    storage_->put(3, 1, 3, info);
    storage_->put(3, 2, 3, info);
    // tile 1: the CRC differs; tile 2: the size differs
    storage_->skippedSaves = {{{3, 1, 3}, {5, 1}}, {{3, 2, 3}, {6, core::crc32(0, "bytes", 5)}}};
    storage_->versions[{3, 1, 3}] = {version(u"/new"_s), version(u"/old"_s)};
    storage_->versions[{3, 2, 3}] = {version(u"/new"_s)};
    const auto result = run({3});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 3);
    EXPECT_EQ(deletions(*storage_),
              (std::vector<Deletion>{{3, 1, 3, u"/new"_s}, {3, 1, 3, u"/old"_s}, {3, 2, 3, u"/new"_s}}));
    EXPECT_TRUE(storage_->saves.empty());
    ASSERT_EQ(storage_->calls.size(), 4U);
    EXPECT_EQ(storage_->calls[0].baseVersion->storeString(), u"/new"_s);
    EXPECT_EQ(storage_->calls[1].baseVersion, nullptr);
}
