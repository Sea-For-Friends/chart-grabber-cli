// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-029 (engine only). Ports u_ThreadCacheConverter.pas Process/
// OnSourceTileStorageScan; docs/specs/storage.md §2.20 (:1626-1646).
//
// Test-count ruling (tests/conventions/tests-port-dont-multiply, T-0027 dispatch "~5 tests"):
// legacy has no DUnit suite for this unit (its "Legacy units to port" section finds none under
// Test/), so this is the ticket's own minimal list: a plain copy (every tile + TNE, correct
// running progress totals), one table-driven test over ignoreTne / overwriteDestination / the
// "a destination TNE blocks a source TNE but never a source tile" quirk, removeSource deleting
// only after a successful copy, cancellation leaving a consistent partial state with correct
// counters, and an incompatible destination content type aborting the whole conversion.
#include "cg/core/content_type.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/cache_converter.h"
#include "cg/storage/codecs/gm3_tile_name_codec.h"
#include "cg/storage/file_system_tile_storage.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stop_token>
#include <vector>

using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::core::ErrorCode;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::kProjEpsgGoogle;
using cg::geo::kTileSplitQuadrate256x256;
using cg::geo::PointI;
using cg::geo::ProjectionSetFactory;
using cg::geo::ProjectionSetPtr;
using cg::storage::CacheConvertOptions;
using cg::storage::convertCache;
using cg::storage::EnumTileInfoPtr;
using cg::storage::FileSystemTileStorage;
using cg::storage::fromPosixSeconds;
using cg::storage::IEnumTileInfo;
using cg::storage::ITileStorage;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionInfoPtr;
using cg::storage::MapVersionList;
using cg::storage::MapVersionRequest;
using cg::storage::StorageState;
using cg::storage::StorageStatePtr;
using cg::storage::TileDate;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileRectInfoPtr;
using cg::storage::TileStorageAbilities;
using cg::storage::TileStorageKind;
using cg::storage::TileStorageTypeAbilities;
using cg::storage::tileStorageTypeAbilities;
using cg::storage::VersionSupport;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

std::unique_ptr<FileSystemTileStorage> makeStorage(const QString& storagePath,
                                                   ContentTypeInfo mainContentType = ContentTypeInfo{
                                                       "image/jpg", ".jpg", ContentKind::Bitmap}) {
    return std::make_unique<FileSystemTileStorage>(
        tileStorageTypeAbilities(TileStorageKind::FolderSimple), TileStorageAbilities::readWrite(),
        std::make_shared<MapVersionFactory>(), makeProjectionSet(),
        std::make_shared<cg::storage::TilePyramidNotifier>(), storagePath, std::move(mainContentType),
        std::make_shared<cg::storage::Gm3TileNameCodec>(), nullptr);
}

QString tempStoragePath(QTemporaryDir& dir) {
    return QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
}

void saveTileOk(FileSystemTileStorage& storage, PointI xy, quint8 zoom, TileDate date,
                const QByteArray& data) {
    auto r = storage.saveTile(xy, zoom, nullptr, date, QByteArrayView("image/jpg"), data, true);
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(*r);
}

void saveTneOk(FileSystemTileStorage& storage, PointI xy, quint8 zoom, TileDate date) {
    auto r = storage.saveTile(xy, zoom, nullptr, date, QByteArrayView(), QByteArray(), true);
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(*r);
}

bool tileOrTneExistsAt(FileSystemTileStorage& storage, PointI xy, quint8 zoom) {
    auto info = storage.tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
    return info.has_value() && (info->isExists() || info->isExistsTne());
}

// A minimal ITileStorage double that yields exactly one versioned real tile from scanTiles().
// Needed only to exercise the version-filter comparison end to end (T-0027 review finding 2,
// truth-table row V3): FileSystemTileStorage always forwards a null version
// (file_system_tile_storage.h:8-10, matching legacy's file-system enumerator), so it can never
// produce a scanned entry whose version differs from a filter only in letter case.
class SingleVersionedTileEnumerator final : public IEnumTileInfo {
public:
    SingleVersionedTileEnumerator(TileInfo info, PointI xy, quint8 zoom)
        : info_(std::move(info)), xy_(xy), zoom_(zoom) {}

    bool next(TileInfo& out, PointI& tile, quint8& zoom) override {
        if (done_) {
            return false;
        }
        done_ = true;
        out = info_;
        tile = xy_;
        zoom = zoom_;
        return true;
    }

private:
    TileInfo info_;
    PointI xy_;
    quint8 zoom_;
    bool done_ = false;
};

class SingleVersionedTileStorage final : public ITileStorage {
public:
    SingleVersionedTileStorage(PointI xy, quint8 zoom, MapVersionInfoPtr version, QByteArray data)
        : xy_(xy), zoom_(zoom), version_(std::move(version)), data_(std::move(data)),
          projectionSet_(makeProjectionSet()) {}

    TileStorageTypeAbilities typeAbilities() const override {
        return {TileStorageAbilities::readWrite(), VersionSupport::MultiVersions, false,
                cg::storage::StorageClass::Other};
    }
    StorageStatePtr state() const override {
        return std::make_shared<const StorageState>(StorageState{true, true, true, true, true});
    }
    cg::core::Changeable& stateChangeable() override { return stateHolder_; }
    const cg::geo::IProjectionSet& projectionSet() const override { return *projectionSet_; }
    TilePyramidNotifier& tileNotifier() override { return notifier_; }
    QString tileFileName(PointI, quint8, const MapVersionInfoPtr&) const override { return QString(); }
    cg::core::Result<TileInfo> tileInfo(PointI xy, quint8 zoom, const MapVersionInfoPtr&,
                                        LoadMode) const override {
        if (xy == xy_ && zoom == zoom_) {
            return info();
        }
        return TileInfo{};
    }
    cg::core::Result<TileInfo> tileInfoEx(PointI xy, quint8 zoom, const MapVersionRequest&,
                                          LoadMode mode) const override {
        return tileInfo(xy, zoom, nullptr, mode);
    }
    cg::core::Result<bool> deleteTile(PointI, quint8, const MapVersionInfoPtr&) override {
        deleted_ = true;
        return true;
    }
    cg::core::Result<bool> saveTile(PointI, quint8, const MapVersionInfoPtr&, TileDate, QByteArrayView,
                                    const QByteArray&, bool) override {
        return cg::core::fail(ErrorCode::Unsupported,
                              QStringLiteral("SingleVersionedTileStorage is read-only"));
    }
    cg::core::Result<MapVersionList> listVersions(PointI, quint8, const MapVersionRequest&) const override {
        return MapVersionList{};
    }
    cg::core::Result<TileRectInfoPtr> tileRectInfo(cg::geo::RectI, quint8, const MapVersionRequest&,
                                                   std::stop_token) const override {
        return cg::core::fail(ErrorCode::Unsupported, QStringLiteral("not implemented"));
    }
    cg::core::Result<EnumTileInfoPtr> scanTiles(bool, bool) override {
        EnumTileInfoPtr result = std::make_unique<SingleVersionedTileEnumerator>(info(), xy_, zoom_);
        return result;
    }

    bool deleted() const { return deleted_; }

private:
    TileInfo info() const {
        TileInfo t;
        t.type = TileInfoType::Exists;
        t.loadDate = fromPosixSeconds(1700000000);
        t.size = static_cast<quint32>(data_.size());
        t.data = data_;
        t.version = version_;
        t.contentType = QByteArray("image/jpg");
        return t;
    }

    PointI xy_;
    quint8 zoom_;
    MapVersionInfoPtr version_;
    QByteArray data_;
    ProjectionSetPtr projectionSet_;
    cg::storage::StorageStateHolder stateHolder_{TileStorageAbilities::readWrite()}; // never mutated
    TilePyramidNotifier notifier_;
    bool deleted_ = false;
};

struct ProgressTotals {
    qint64 processed = 0;
    qint64 skipped = 0;
    qint64 bytes = 0;
};

constexpr quint8 kZoom = 5;
const PointI kTileA{1, 1};   // real tile "AAA" (3 bytes)
const PointI kTileB{2, 2};   // real tile "BBBBB" (5 bytes)
const PointI kTileTne{3, 3}; // TNE

} // namespace

TEST(CacheConverter, PlainCopyMovesEveryTileAndTneWithCorrectProgress) {
    QTemporaryDir srcDir, dstDir;
    ASSERT_TRUE(srcDir.isValid());
    ASSERT_TRUE(dstDir.isValid());
    auto src = makeStorage(tempStoragePath(srcDir));
    auto dst = makeStorage(tempStoragePath(dstDir));

    const TileDate loadDate = fromPosixSeconds(1700000000);
    saveTileOk(*src, kTileA, kZoom, loadDate, QByteArray("AAA"));
    saveTileOk(*src, kTileB, kZoom, loadDate, QByteArray("BBBBB"));
    saveTneOk(*src, kTileTne, kZoom, loadDate);

    std::vector<ProgressTotals> calls;
    CacheConvertOptions opts;
    opts.progress = [&](qint64 p, qint64 s, qint64 b) { calls.push_back({p, s, b}); };

    auto result = convertCache(*src, *dst, opts, std::stop_token{});
    ASSERT_TRUE(result.has_value());

    ASSERT_EQ(calls.size(), 3u);
    EXPECT_EQ(calls.back().processed, 3);
    EXPECT_EQ(calls.back().skipped, 0);
    EXPECT_EQ(calls.back().bytes, 8); // "AAA" (3) + "BBBBB" (5) + TNE (0)

    auto tA = dst->tileInfo(kTileA, kZoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(tA.has_value());
    EXPECT_TRUE(tA->isExists());
    EXPECT_EQ(tA->data, QByteArray("AAA"));

    auto tB = dst->tileInfo(kTileB, kZoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(tB.has_value());
    EXPECT_EQ(tB->data, QByteArray("BBBBB"));

    auto tTne = dst->tileInfo(kTileTne, kZoom, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(tTne.has_value());
    EXPECT_TRUE(tTne->isExistsTne());
}

TEST(CacheConverter, IgnoreTneOverwriteAndDestTneQuirk) {
    struct Row {
        const char* name;
        bool ignoreTne;
        bool overwriteDestination;
        std::function<void(FileSystemTileStorage&, TileDate)> prepareDest;
        qint64 expectedProcessed;
        qint64 expectedSkipped;
        bool expectTileAExists;   // dest kTileA ends up Exists (a real tile)
        bool expectTneCoordIsTne; // dest kTileTne ends up an existing TNE
        QByteArray expectTileAData;
    };

    const TileDate loadDate = fromPosixSeconds(1700000000);
    std::vector<Row> rows = {
        {"ignoreTne_dropsTneKeepsReal", true, false, [](FileSystemTileStorage&, TileDate) {}, 2, 0, true,
         false, "AAA"},
        {"overwriteFalse_skipsPreexisting", false, false,
         [loadDate](FileSystemTileStorage& d, TileDate) {
             saveTileOk(d, kTileA, kZoom, loadDate, QByteArray("OLD"));
             saveTneOk(d, kTileTne, kZoom, loadDate);
         },
         1, 2, true, true, "OLD"}, // skipped: dest keeps its own preexisting bytes
        {"overwriteTrue_ignoreTne_replacesPreexistingTile", true, true,
         [loadDate](FileSystemTileStorage& d, TileDate) {
             saveTileOk(d, kTileA, kZoom, loadDate, QByteArray("OLD"));
         },
         2, 0, true, false, "AAA"}, // overwritten: dest now holds the source's bytes
        {"destTneNeverBlocksSourceRealTile", false, false,
         [loadDate](FileSystemTileStorage& d, TileDate) { saveTneOk(d, kTileA, kZoom, loadDate); }, 3, 0,
         true, true, "AAA"},
    };

    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        QTemporaryDir srcDir, dstDir;
        ASSERT_TRUE(srcDir.isValid());
        ASSERT_TRUE(dstDir.isValid());
        auto src = makeStorage(tempStoragePath(srcDir));
        auto dst = makeStorage(tempStoragePath(dstDir));

        saveTileOk(*src, kTileA, kZoom, loadDate, QByteArray("AAA"));
        saveTileOk(*src, kTileB, kZoom, loadDate, QByteArray("BBBBB"));
        saveTneOk(*src, kTileTne, kZoom, loadDate);
        row.prepareDest(*dst, loadDate);

        CacheConvertOptions opts;
        opts.ignoreTne = row.ignoreTne;
        opts.overwriteDestination = row.overwriteDestination;
        qint64 lastProcessed = -1, lastSkipped = -1;
        opts.progress = [&](qint64 p, qint64 s, qint64) {
            lastProcessed = p;
            lastSkipped = s;
        };

        auto result = convertCache(*src, *dst, opts, std::stop_token{});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(lastProcessed, row.expectedProcessed);
        EXPECT_EQ(lastSkipped, row.expectedSkipped);

        auto tA = dst->tileInfo(kTileA, kZoom, nullptr, LoadMode::WithData);
        ASSERT_TRUE(tA.has_value());
        EXPECT_EQ(tA->isExists(), row.expectTileAExists);
        if (row.expectTileAExists) {
            EXPECT_EQ(tA->data, row.expectTileAData);
        }

        if (row.ignoreTne) {
            EXPECT_FALSE(tileOrTneExistsAt(*dst, kTileTne, kZoom));
        } else {
            auto tTne = dst->tileInfo(kTileTne, kZoom, nullptr, LoadMode::WithoutData);
            ASSERT_TRUE(tTne.has_value());
            EXPECT_EQ(tTne->isExistsTne(), row.expectTneCoordIsTne);
        }
    }
}

// Pins rows of the T-0027 review's removeSource truth table (docs/handoffs/T-0027-review.md):
// deletion from the source is never conditioned on whether a copy happened (legacy
// u_ThreadCacheConverter.pas:174-178 is a sibling of the whole version/scan branch, not nested
// inside it; legacy-defects.md L-1/L-2). Renamed from RemoveSourceDeletesOnlyAfterSuccessfulCopy,
// which claimed the opposite of legacy's actual behaviour (T-0027 review finding 3).
TEST(CacheConverter, RemoveSourceDeletesEveryScannedEntryRegardlessOfSkip) {
    const TileDate loadDate = fromPosixSeconds(1700000000);

    // Row A1: every entry is copied, and removeSource deletes every one of them from the source.
    {
        QTemporaryDir srcDir, dstDir;
        ASSERT_TRUE(srcDir.isValid());
        ASSERT_TRUE(dstDir.isValid());
        auto src = makeStorage(tempStoragePath(srcDir));
        auto dst = makeStorage(tempStoragePath(dstDir));

        saveTileOk(*src, kTileA, kZoom, loadDate, QByteArray("AAA"));
        saveTileOk(*src, kTileB, kZoom, loadDate, QByteArray("BBBBB"));
        saveTneOk(*src, kTileTne, kZoom, loadDate);

        CacheConvertOptions opts;
        opts.removeSource = true;

        auto result = convertCache(*src, *dst, opts, std::stop_token{});
        ASSERT_TRUE(result.has_value());

        EXPECT_FALSE(tileOrTneExistsAt(*src, kTileA, kZoom));
        EXPECT_FALSE(tileOrTneExistsAt(*src, kTileB, kZoom));
        EXPECT_FALSE(tileOrTneExistsAt(*src, kTileTne, kZoom));

        EXPECT_TRUE(tileOrTneExistsAt(*dst, kTileA, kZoom));
        EXPECT_TRUE(tileOrTneExistsAt(*dst, kTileB, kZoom));
        EXPECT_TRUE(tileOrTneExistsAt(*dst, kTileTne, kZoom));
    }

    // Row V2 (T-0027 review finding 1; legacy-defects.md L-1/L-2): a `sourceVersionFilter` is
    // set, and the scanned entry has NO version at all -- a plain file-system source never
    // carries one (file_system_tile_storage.h:8-10). Legacy's `Assigned(...) and SameText(...)`
    // takes the `else` branch: the entry is SKIPPED, not copied. removeSource still deletes it
    // from the source: a skipped tile IS deleted, the copy is simply lost.
    {
        QTemporaryDir srcDir, dstDir;
        ASSERT_TRUE(srcDir.isValid());
        ASSERT_TRUE(dstDir.isValid());
        auto src = makeStorage(tempStoragePath(srcDir));
        auto dst = makeStorage(tempStoragePath(dstDir));

        saveTileOk(*src, kTileA, kZoom, loadDate, QByteArray("AAA"));

        MapVersionFactory factory;
        CacheConvertOptions opts;
        opts.removeSource = true;
        opts.sourceVersionFilter = factory.byStoreString(QStringLiteral("rel-1"));

        qint64 lastProcessed = -1, lastSkipped = -1;
        opts.progress = [&](qint64 p, qint64 s, qint64) {
            lastProcessed = p;
            lastSkipped = s;
        };

        auto result = convertCache(*src, *dst, opts, std::stop_token{});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(lastProcessed, 0);
        EXPECT_EQ(lastSkipped, 1);

        EXPECT_FALSE(tileOrTneExistsAt(*dst, kTileA, kZoom)); // skipped, never copied
        EXPECT_FALSE(tileOrTneExistsAt(*src, kTileA, kZoom)); // yet still deleted from the source
    }

    // Row V3 (T-0027 review finding 2; T-0041): a versioned entry whose version differs from the filter
    // only in ASCII letter case IS a match (legacy SameText) and is copied; a non-ASCII case difference is
    // not (Delphi folding is ASCII only here, not Qt's Unicode folding). Exercised through a minimal
    // versioned ITileStorage double, since FileSystemTileStorage never carries a per-tile version.
    struct Row {
        const char16_t* tileVersion;
        const char16_t* filter;
        bool copied;
        qint64 processed;
        qint64 skipped;
    };
    for (const Row& row : {Row{u"REL-1", u"rel-1", true, 1, 0}, Row{u"\u00C4-1", u"\u00E4-1", false, 0, 1}}) {
        QTemporaryDir dstDir;
        ASSERT_TRUE(dstDir.isValid());
        auto dst = makeStorage(tempStoragePath(dstDir));

        MapVersionFactory factory;
        SingleVersionedTileStorage src(
            kTileA, kZoom, factory.byStoreString(QString::fromUtf16(row.tileVersion)), QByteArray("ZZZ"));

        CacheConvertOptions opts;
        opts.sourceVersionFilter = factory.byStoreString(QString::fromUtf16(row.filter));

        qint64 lastProcessed = -1, lastSkipped = -1;
        opts.progress = [&](qint64 p, qint64 s, qint64) {
            lastProcessed = p;
            lastSkipped = s;
        };

        auto result = convertCache(src, *dst, opts, std::stop_token{});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(lastProcessed, row.processed);
        EXPECT_EQ(lastSkipped, row.skipped);

        auto tA = dst->tileInfo(kTileA, kZoom, nullptr, LoadMode::WithData);
        ASSERT_TRUE(tA.has_value());
        EXPECT_EQ(tA->isExists(), row.copied);
        if (row.copied) {
            EXPECT_EQ(tA->data, QByteArray("ZZZ"));
        }
    }
}

TEST(CacheConverter, CancellationMidScanStopsAndLeavesConsistentCounters) {
    QTemporaryDir srcDir, dstDir;
    ASSERT_TRUE(srcDir.isValid());
    ASSERT_TRUE(dstDir.isValid());
    auto src = makeStorage(tempStoragePath(srcDir));
    auto dst = makeStorage(tempStoragePath(dstDir));

    const TileDate loadDate = fromPosixSeconds(1700000000);
    saveTileOk(*src, kTileA, kZoom, loadDate, QByteArray("AAA"));
    saveTileOk(*src, kTileB, kZoom, loadDate, QByteArray("BBBBB"));
    saveTneOk(*src, kTileTne, kZoom, loadDate);

    std::stop_source stopSource;
    int progressCalls = 0;
    ProgressTotals lastTotals;
    CacheConvertOptions opts;
    opts.progress = [&](qint64 p, qint64 s, qint64 b) {
        ++progressCalls;
        lastTotals = {p, s, b};
        stopSource.request_stop(); // cancel after the very first tile
    };

    auto result = convertCache(*src, *dst, opts, stopSource.get_token());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Cancelled);

    // Exactly one tile was processed before the next iteration observed the cancellation
    // (stop_token is polled at the top of the loop, per storage.md §3.1 rule 4).
    EXPECT_EQ(progressCalls, 1);
    EXPECT_EQ(lastTotals.processed, 1);
    EXPECT_EQ(lastTotals.skipped, 0);

    const int destCount = static_cast<int>(tileOrTneExistsAt(*dst, kTileA, kZoom)) +
                          static_cast<int>(tileOrTneExistsAt(*dst, kTileB, kZoom)) +
                          static_cast<int>(tileOrTneExistsAt(*dst, kTileTne, kZoom));
    EXPECT_EQ(destCount, 1);
}

TEST(CacheConverter, IncompatibleDestinationContentTypeAbortsConversion) {
    QTemporaryDir srcDir, dstDir;
    ASSERT_TRUE(srcDir.isValid());
    ASSERT_TRUE(dstDir.isValid());
    auto src =
        makeStorage(tempStoragePath(srcDir), ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap});
    auto dst =
        makeStorage(tempStoragePath(dstDir), ContentTypeInfo{"image/png", ".png", ContentKind::Bitmap});

    const TileDate loadDate = fromPosixSeconds(1700000000);
    saveTileOk(*src, kTileA, kZoom, loadDate, QByteArray("AAA"));
    saveTileOk(*src, kTileB, kZoom, loadDate, QByteArray("BBBBB"));

    int progressCalls = 0;
    CacheConvertOptions opts;
    opts.progress = [&](qint64, qint64, qint64) { ++progressCalls; };

    auto result = convertCache(*src, *dst, opts, std::stop_token{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Unsupported);

    // Every source tile is jpg against a png-only destination, so the very first tile the scan
    // yields aborts the loop before any progress callback -- nothing was written.
    EXPECT_EQ(progressCalls, 0);
    EXPECT_FALSE(tileOrTneExistsAt(*dst, kTileA, kZoom));
    EXPECT_FALSE(tileOrTneExistsAt(*dst, kTileB, kZoom));
}
