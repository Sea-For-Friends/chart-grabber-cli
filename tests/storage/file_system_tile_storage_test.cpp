// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-STO-002..013. Ports u_TileStorageFileSystem.pas (GetTileInfo/SaveTile/DeleteTile/
// GetTileFileName/GetTileInfoByPath); docs/specs/storage.md §2.13 (:952-1046).
//
// Test-count ruling (tests/conventions/tests-port-dont-multiply): legacy has no DUnit suite for
// this unit, so this is the ticket's own minimal list: one table-driven save/load/delete round
// trip over all nine folder layouts (exact relative path per tools/gen_cache_fixtures.py's
// tile (3,7,5)), TNE write/read/supersede (incl. the "TNE never overwrites a TNE" quirk), mtime
// == load date, read-only/no-access paths, the tileInfo order-of-operations case (tile + tne both
// present), and the bad-content-type Result.
#include "cg/core/content_type.h"
#include "cg/core/platform/file_times.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/storage/codecs/es_tile_name_codec.h"
#include "cg/storage/codecs/gm1_tile_name_codec.h"
#include "cg/storage/codecs/gm2_tile_name_codec.h"
#include "cg/storage/codecs/gm3_tile_name_codec.h"
#include "cg/storage/codecs/gmv_tile_name_codec.h"
#include "cg/storage/codecs/mobac_tile_name_codec.h"
#include "cg/storage/codecs/osmand_tile_name_codec.h"
#include "cg/storage/codecs/sas_tile_name_codec.h"
#include "cg/storage/codecs/tms_tile_name_codec.h"
#include "cg/storage/file_system_tile_storage.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"
#include "cg/storage/tile_pyramid_notifier.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "storage_state_changeable_check.h"

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
using cg::geo::RectI;
using cg::storage::FileSystemTileStorage;
using cg::storage::fromPosixSeconds;
using cg::storage::ITileNameCodecPtr;
using cg::storage::LoadMode;
using cg::storage::MapVersionFactory;
using cg::storage::MapVersionRequest;
using cg::storage::TileDate;
using cg::storage::TileInfo;
using cg::storage::TileInfoType;
using cg::storage::TilePyramidNotifier;
using cg::storage::TileStorageAbilities;
using cg::storage::TileStorageKind;
using cg::storage::tileStorageTypeAbilities;
using cg::storage::toPosixSeconds;

namespace {

ProjectionSetPtr makeProjectionSet() {
    static const auto datums = std::make_shared<const DatumFactory>(HashSeed{});
    static const ProjectionSetFactory factory{HashSeed{}, datums};
    return factory.byCode(kProjEpsgGoogle, kTileSplitQuadrate256x256);
}

std::unique_ptr<FileSystemTileStorage>
makeStorage(const QString& storagePath, ITileNameCodecPtr codec, TileStorageAbilities abilities,
            ContentTypeInfo mainContentType = ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap},
            std::shared_ptr<cg::storage::TileInfoMemCache> memCache = nullptr) {
    return std::make_unique<FileSystemTileStorage>(
        tileStorageTypeAbilities(TileStorageKind::FolderSimple), abilities,
        std::make_shared<MapVersionFactory>(), makeProjectionSet(), std::make_shared<TilePyramidNotifier>(),
        storagePath, std::move(mainContentType), std::move(codec), std::move(memCache));
}

void writeFile(const QString& path, const QByteArray& contents = QByteArray()) {
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(contents);
}

struct LayoutCase {
    const char* name;
    ITileNameCodecPtr codec;
    QString expectedRelPath; // for tile (x=3, y=7, z=5) -- tests/fixtures/cache/expected.json
};

// Src/TileStorage/FileSystem/u_TileFileName{ES,GM1,GM2,GM3,GMV,MOBAC,OsmAnd,SAS}.pas,
// u_TileFileNameTMS.pas -- the nine folder-cache formats landed by T-0021B/C.
std::vector<LayoutCase> nineLayouts() {
    return {
        {"es", std::make_shared<cg::storage::EsTileNameCodec>(), QStringLiteral("06/06-03-07.jpg")},
        {"gm1", std::make_shared<cg::storage::Gm1TileNameCodec>(), QStringLiteral("z5/7/3.jpg")},
        {"gm2", std::make_shared<cg::storage::Gm2TileNameCodec>(), QStringLiteral("z5/7_3.jpg")},
        {"gm3", std::make_shared<cg::storage::Gm3TileNameCodec>(), QStringLiteral("5/7/3.jpg")},
        {"gmv", std::make_shared<cg::storage::GmvTileNameCodec>(), QStringLiteral("06/tqqtss.jpg")},
        {"mobac", std::make_shared<cg::storage::MobacTileNameCodec>(), QStringLiteral("5/3/7.jpg")},
        {"osmand", std::make_shared<cg::storage::OsmandTileNameCodec>(), QStringLiteral("5/3/7.jpg.tile")},
        {"sas", std::make_shared<cg::storage::SasTileNameCodec>(), QStringLiteral("z6/0/x3/0/y7.jpg")},
        {"tms", std::make_shared<cg::storage::TmsTileNameCodec>(), QStringLiteral("5/3/24.jpg")},
    };
}

} // namespace

TEST(FileSystemTileStorage, SaveLoadDeleteRoundTripAcrossAllNineLayouts) {
    for (const LayoutCase& layout : nineLayouts()) {
        SCOPED_TRACE(layout.name);
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto storage = makeStorage(storagePath, layout.codec, TileStorageAbilities::readWrite());

        const PointI xy{3, 7};
        constexpr quint8 zoom = 5;
        const QByteArray data("jpegbytes");
        const TileDate loadDate = fromPosixSeconds(1700000000);

        auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/jpg"), data, false);
        ASSERT_TRUE(saved.has_value());
        EXPECT_TRUE(*saved);

        const QString expectedAbsPath = storagePath + layout.expectedRelPath;
        EXPECT_TRUE(QFile::exists(expectedAbsPath)) << expectedAbsPath.toStdString();
        EXPECT_EQ(storage->tileFileName(xy, zoom, nullptr), expectedAbsPath);

        auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithData);
        ASSERT_TRUE(info.has_value());
        EXPECT_TRUE(info->isExists());
        EXPECT_EQ(info->data, data);
        EXPECT_EQ(info->contentType, QByteArray("image/jpg"));

        auto deleted = storage->deleteTile(xy, zoom, nullptr);
        ASSERT_TRUE(deleted.has_value());
        EXPECT_TRUE(*deleted);
        EXPECT_FALSE(QFile::exists(expectedAbsPath));

        auto infoAfterDelete = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
        ASSERT_TRUE(infoAfterDelete.has_value());
        EXPECT_EQ(infoAfterDelete->type, TileInfoType::NotExists);
    }
}

TEST(FileSystemTileStorage, TneWriteReadAndSupersedeQuirk) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                               TileStorageAbilities::readWrite());

    const PointI xy{3, 7};
    constexpr quint8 zoom = 5;
    const TileDate firstDate = fromPosixSeconds(1700000000);

    // Empty content type / null data -> a .tne marker (tile_storage.h::saveTile contract).
    auto savedTne = storage->saveTile(xy, zoom, nullptr, firstDate, QByteArrayView(), QByteArray(), false);
    ASSERT_TRUE(savedTne.has_value());
    EXPECT_TRUE(*savedTne);

    auto tneInfo = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(tneInfo.has_value());
    EXPECT_TRUE(tneInfo->isExistsTne());
    EXPECT_EQ(toPosixSeconds(tneInfo->loadDate), toPosixSeconds(firstDate));

    // u_TileStorageFileSystem.pas:SaveTile -- writing a TNE NEVER overwrites an existing TNE, even
    // with AIsOverwrite=true; AIsOverwrite is not consulted in that branch at all.
    const TileDate secondDate = fromPosixSeconds(1700000999);
    auto savedTneAgain =
        storage->saveTile(xy, zoom, nullptr, secondDate, QByteArrayView(), QByteArray(), true);
    ASSERT_TRUE(savedTneAgain.has_value());
    EXPECT_FALSE(*savedTneAgain);
    auto stillFirstDate = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(stillFirstDate.has_value());
    EXPECT_EQ(toPosixSeconds(stillFirstDate->loadDate), toPosixSeconds(firstDate));

    // A real tile supersedes an existing TNE: the .tne file is removed, the tile wins.
    const QByteArray data("jpegbytes");
    auto savedTile =
        storage->saveTile(xy, zoom, nullptr, secondDate, QByteArrayView("image/jpg"), data, true);
    ASSERT_TRUE(savedTile.has_value());
    EXPECT_TRUE(*savedTile);

    auto tileInfoResult = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithData);
    ASSERT_TRUE(tileInfoResult.has_value());
    EXPECT_TRUE(tileInfoResult->isExists());
    EXPECT_EQ(tileInfoResult->data, data);
    EXPECT_EQ(toPosixSeconds(tileInfoResult->loadDate), toPosixSeconds(secondDate));
}

TEST(FileSystemTileStorage, MtimeEqualsLoadDate) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                               TileStorageAbilities::readWrite());

    const PointI xy{1, 1};
    constexpr quint8 zoom = 2;
    const TileDate loadDate = fromPosixSeconds(1700000123); // deliberately not a 2s-grid multiple
    const QByteArray data("x");

    auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/jpg"), data, false);
    ASSERT_TRUE(saved.has_value());
    ASSERT_TRUE(*saved);

    const QString path = storage->tileFileName(xy, zoom, nullptr);
    const auto onDiskMtime = cg::core::platform::fileMTime(path);
    ASSERT_TRUE(onDiskMtime.has_value());
    EXPECT_EQ(*onDiskMtime, toPosixSeconds(loadDate));

    auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(toPosixSeconds(info->loadDate), toPosixSeconds(loadDate));
}

TEST(FileSystemTileStorage, ReadOnlyAndNoAccessPathsDoNotTouchDisk) {
    const PointI xy{2, 2};
    constexpr quint8 zoom = 3;
    const TileDate loadDate = fromPosixSeconds(1700000000);
    const QByteArray data("x");

    {
        // allowAdd/allowDelete false: saveTile/deleteTile are refused (Result<bool>{false}, not an
        // error), and nothing is written.
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                                   TileStorageAbilities::readOnly());

        auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/jpg"), data, false);
        ASSERT_TRUE(saved.has_value());
        EXPECT_FALSE(*saved);
        EXPECT_FALSE(QFile::exists(storage->tileFileName(xy, zoom, nullptr)));

        auto deleted = storage->deleteTile(xy, zoom, nullptr);
        ASSERT_TRUE(deleted.has_value());
        EXPECT_FALSE(*deleted);
    }
    {
        // allowRead false: tileInfo answers Unknown (legacy's nil, u_TileStorageFileSystem.pas:409, :416)
        // WITHOUT looking at the filesystem, even though a real tile file exists on disk, and adds
        // nothing to the mem-cache (the add stands inside the access branch, :423-425).
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto codec = std::make_shared<cg::storage::SasTileNameCodec>();
        writeFile(storagePath + codec->addExt(codec->tilePath(xy, zoom), QStringLiteral(".jpg")), data);

        auto memCache = std::make_shared<cg::storage::TileInfoMemCache>(16, std::chrono::minutes(5),
                                                                        cg::storage::ClearStrategy::ByOldest);
        auto storage = makeStorage(storagePath, codec, TileStorageAbilities::none(),
                                   ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}, memCache);
        auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->type, TileInfoType::Unknown);
        EXPECT_FALSE(memCache->get(xy, zoom, nullptr, LoadMode::AsIs, false).has_value());
        auto infoEx = storage->tileInfoEx(xy, zoom, MapVersionRequest{}, LoadMode::WithoutData);
        ASSERT_TRUE(infoEx.has_value());
        EXPECT_EQ(infoEx->type, TileInfoType::Unknown);
    }
    {
        // allowRead false, but the mem-cache holds the tile (a save fills it without read access,
        // :461-465, :499): the mem-cache is asked BEFORE the access test (:410-415), so the cached
        // Exists info is the answer.
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto memCache = std::make_shared<cg::storage::TileInfoMemCache>(16, std::chrono::minutes(5),
                                                                        cg::storage::ClearStrategy::ByOldest);
        auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                                   TileStorageAbilities{false, false, true, false, false},
                                   ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}, memCache);
        auto saved = storage->saveTile(xy, zoom, nullptr, loadDate, QByteArrayView("image/jpg"), data, false);
        ASSERT_TRUE(saved.has_value());
        ASSERT_TRUE(*saved);
        auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->type, TileInfoType::Exists);
        EXPECT_EQ(info->size, static_cast<quint32>(data.size()));
    }
}

TEST(FileSystemTileStorage, TileInfoOrderOfOperationsTileWinsWhenBothTileAndTneExist) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto codec = std::make_shared<cg::storage::SasTileNameCodec>();

    const PointI xy{3, 7};
    constexpr quint8 zoom = 5;
    const QString base = codec->tilePath(xy, zoom);
    writeFile(storagePath + codec->addExt(base, QStringLiteral(".jpg")), QByteArray("tiledata"));
    writeFile(storagePath + codec->addExt(base, QStringLiteral(".tne")));

    auto storage = makeStorage(storagePath, codec, TileStorageAbilities::readWrite());
    auto info = storage->tileInfo(xy, zoom, nullptr, LoadMode::WithoutData);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, TileInfoType::Exists); // the tile file is checked BEFORE the .tne

    // T-0021F1 review finding 2: `Result := DeleteFile(VFileName) or DeleteFile(VTneName)`
    // (u_TileStorageFileSystem.pas:204) short-circuits under Delphi's default {$B-} -- once the
    // tile file is deleted, the .tne sibling is left untouched.
    const QString tilePath = storagePath + codec->addExt(base, QStringLiteral(".jpg"));
    const QString tnePath = storagePath + codec->addExt(base, QStringLiteral(".tne"));
    auto deleted = storage->deleteTile(xy, zoom, nullptr);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_TRUE(*deleted);
    EXPECT_FALSE(QFile::exists(tilePath));
    EXPECT_TRUE(QFile::exists(tnePath)); // left alone -- short-circuit, not "delete both"
}

TEST(FileSystemTileStorage, SaveTileRejectsMismatchedContentTypeAsAResult) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                               TileStorageAbilities::readWrite());

    const PointI xy{0, 0};
    constexpr quint8 zoom = 0;
    auto result = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                    QByteArrayView("image/png"), QByteArray("x"), false);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::Unsupported);
    EXPECT_FALSE(QFile::exists(storage->tileFileName(xy, zoom, nullptr)));
}

// T-0021F1 review finding 1: the check is legacy's CheckOtherForSaveCompatible (same KIND, same
// DEFAULT EXTENSION), not MIME string equality. The four vectors the review lists as required to
// pass, plus one that must still be rejected (a bitmap into a vector storage).
TEST(FileSystemTileStorage, SaveTileContentTypeCompatibilityMatchesLegacyCheckOtherForSaveCompatible) {
    struct Case {
        const char* name;
        QByteArray mainType; // resolved via the registry, so its kind/defaultExt come from there
        QByteArray incomingType;
        bool expectAccepted;
    };
    const std::vector<Case> cases = {
        {"JpegAliasIntoJpgStorage", "image/jpg", "image/jpeg", true},
        {"PjpegAliasIntoJpgStorage", "image/jpg", "image/pjpeg", true},
        {"XPngAliasIntoPngStorage", "image/png", "image/x-png", true},
        {"DifferentKmlTypesShareDefaultExt", "application/vnd.sas.wikimapia.kml+xml",
         "application/vnd.google-earth.kml+xml", true},
        {"BitmapIntoVectorStorageRejected", "application/vnd.sas.wikimapia.kml+xml", "image/jpg", false},
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        const cg::core::ContentTypeInfo* mainInfo =
            cg::core::ContentTypeRegistry::standard().infoByType(c.mainType);
        ASSERT_NE(mainInfo, nullptr);

        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                                   TileStorageAbilities::readWrite(), *mainInfo);

        const PointI xy{0, 0};
        constexpr quint8 zoom = 0;
        auto result = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                        QByteArrayView(c.incomingType), QByteArray("x"), false);
        if (c.expectAccepted) {
            ASSERT_TRUE(result.has_value());
            EXPECT_TRUE(*result);
        } else {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code(), ErrorCode::Unsupported);
        }
    }
}

namespace {

// Test-only codec whose paths are fixed regardless of xy/zoom/`path`: with storagePath == "/",
// the tile path resolves to exactly "/dev/null" (a root-owned, mode-0666 device node -- writable
// by any user but not owned by this process) and the TNE path resolves to a throwaway file inside
// a caller-supplied temp directory this process does own. Used only by
// SaveTileIgnoresFailedSetFileMTime below; the save branch it exercises never unlinks the tile
// path itself, only the TNE sibling, so /dev/null is never touched beyond the write + failed
// utimensat this test is about.
class FixedPathCodec final : public cg::storage::ITileNameCodec {
public:
    explicit FixedPathCodec(QString safeTneRelPath) : safeTneRelPath_(std::move(safeTneRelPath)) {}

    QString tilePath(cg::geo::PointI, quint8) const override { return QStringLiteral("tile"); }
    bool parse(QStringView, cg::geo::PointI&, quint8&) const override { return false; }
    QString addExt(const QString&, QStringView ext) const override {
        if (ext == QStringLiteral(".jpg")) {
            return QStringLiteral("dev/null");
        }
        return safeTneRelPath_;
    }

private:
    QString safeTneRelPath_;
};

} // namespace

TEST(FileSystemTileStorage, SaveTileIgnoresFailedSetFileMTime) {
    // T-0021F1 review finding 3: legacy discards FileSetDate's result (u_TileStorageFileSystem.pas
    // :483 -> u_FileSystemTools.pas:165-176), so a failed setFileMTime must not fail the save.
    // /dev/null lets the write succeed while an explicit-timestamp utimensat(2) fails with EPERM
    // (root-owned, this process is not the owner) -- the "shared cache file owned by another user"
    // trigger the review describes, reproduced without touching any real cache file.
    const auto probe = cg::core::platform::setFileMTime(QStringLiteral("/dev/null"), 1700000000);
    if (probe.has_value()) {
        GTEST_SKIP() << "this environment allows an explicit utimensat on /dev/null (e.g. running "
                        "as root); the EPERM this test relies on cannot be reproduced here";
    }

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString safeTnePath =
        dir.path().mid(1) + QStringLiteral("/tne_marker"); // storagePath="/" supplies the leading '/'
    auto memCache = std::make_shared<cg::storage::TileInfoMemCache>(16, std::chrono::minutes(5),
                                                                    cg::storage::ClearStrategy::ByOldest);
    auto storage = makeStorage(QStringLiteral("/"), std::make_shared<FixedPathCodec>(safeTnePath),
                               TileStorageAbilities::readWrite(),
                               ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}, memCache);

    const PointI xy{0, 0};
    constexpr quint8 zoom = 0;
    const QByteArray data("x");
    auto saved = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                   QByteArrayView("image/jpg"), data, false);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(*saved); // must still succeed despite the failed setFileMTime

    auto cached = memCache->get(xy, zoom, nullptr, LoadMode::AsIs, false);
    ASSERT_TRUE(cached.has_value()); // mem cache still updated, as on a clean success
    EXPECT_TRUE(cached->isExists());
    EXPECT_EQ(cached->data, data);
}

TEST(FileSystemTileStorage, SaveTileWithoutOverwriteProbeMatchesLegacyReentrantGetTileInfo) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto codec = std::make_shared<cg::storage::SasTileNameCodec>();

    const PointI xy{1, 2};
    constexpr quint8 zoom = 3;
    const QString base = codec->tilePath(xy, zoom);
    writeFile(storagePath + codec->addExt(base, QStringLiteral(".jpg")), QByteArray("old"));

    // Finding 4a: with AddAccess but not ReadAccess, legacy's reentrant GetTileInfo returns nil
    // (u_TileStorageFileSystem.pas:461 -> :407-426), so the "already exists" check never fires and
    // the save proceeds as an overwrite, even though a tile is physically on disk.
    {
        auto storage =
            makeStorage(storagePath, codec, TileStorageAbilities{false, false, true, false, false});
        auto saved = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                       QByteArrayView("image/jpg"), QByteArray("new"), false);
        ASSERT_TRUE(saved.has_value());
        EXPECT_TRUE(*saved);
    }

    // Finding 4b: with ReadAccess, legacy's GetTileInfo caches the probed result (:422-424), not
    // just the save's own outcome.
    {
        writeFile(storagePath + codec->addExt(base, QStringLiteral(".jpg")), QByteArray("old-again"));
        auto memCache = std::make_shared<cg::storage::TileInfoMemCache>(16, std::chrono::minutes(5),
                                                                        cg::storage::ClearStrategy::ByOldest);
        auto storage = makeStorage(storagePath, codec, TileStorageAbilities::readWrite(),
                                   ContentTypeInfo{"image/jpg", ".jpg", ContentKind::Bitmap}, memCache);
        auto saved = storage->saveTile(xy, zoom, nullptr, fromPosixSeconds(1700000000),
                                       QByteArrayView("image/jpg"), QByteArray("new"), false);
        ASSERT_TRUE(saved.has_value());
        EXPECT_FALSE(*saved); // refused: overwrite=false and a tile already exists on disk

        auto cached = memCache->get(xy, zoom, nullptr, LoadMode::AsIs, false);
        ASSERT_TRUE(cached.has_value()); // the probe's result was cached, not discarded
        EXPECT_TRUE(cached->isExists());
    }
}

// ---------------------------------------------------------------------------------------------
// T-0021F2: tileRectInfo (storage.md §2.13.2/§2.5, u_TileStorageFileSystem.pas:GetTileRectInfo)
// and scanTiles (storage.md §2.13.4, FEAT-STO-035, u_TileStorageFileSystem.pas:ScanTiles +
// u_EnumTileInfoByFileIterator.pas). Test-count ruling: no legacy DUnit suite for either method
// (verified: `grep -ril "TileRectInfo\|EnumTileInfoByFileIterator" Test/` in the legacy tree has
// no hits), so this is the ticket's own minimal list.
// ---------------------------------------------------------------------------------------------

namespace {

struct ScannedEntry {
    PointI tile;
    quint8 zoom = 0;
    TileInfoType type = TileInfoType::Unknown;
    QByteArray data;
    QByteArray contentType;
};

std::vector<ScannedEntry> drain(cg::storage::EnumTileInfoPtr enumerator) {
    std::vector<ScannedEntry> out;
    if (!enumerator) {
        return out;
    }
    TileInfo info;
    PointI tile;
    quint8 zoom = 0;
    while (enumerator->next(info, tile, zoom)) {
        out.push_back({tile, zoom, info.type, info.data, info.contentType});
    }
    return out;
}

} // namespace

TEST(FileSystemTileStorage, TileRectInfoPopulatedRectAcrossFoldersIncludingAMissingOne) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto codec = std::make_shared<cg::storage::SasTileNameCodec>();
    constexpr quint8 zoom = 5;

    // x=1's folder ("z6/0/x1/0/") gets a real tile at y=6 -- (1,7) shares that folder but has no
    // file of its own (folder-exists, tile-missing path).
    writeFile(storagePath + codec->addExt(codec->tilePath(PointI{1, 6}, zoom), QStringLiteral(".jpg")),
              QByteArray("tile16"));
    // x=2's folder gets only a .tne at y=7 -- (2,6) shares that folder but has no file either.
    writeFile(storagePath + codec->addExt(codec->tilePath(PointI{2, 7}, zoom), QStringLiteral(".tne")));
    // x=3's folder is never created at all -- exercises the missing-directory shortcut for both
    // (3,6) and (3,7) (storage.md §2.13.2's VPrevFolderName/VPrevFolderExist optimisation).

    auto storage = makeStorage(storagePath, codec, TileStorageAbilities::readWrite());
    const RectI rect{1, 6, 4, 8}; // x in [1,4), y in [6,8) -- 3x2 grid
    auto result = storage->tileRectInfo(rect, zoom, MapVersionRequest{}, std::stop_token{});
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(*result, nullptr);
    const auto& info = **result;
    EXPECT_EQ(info.rect(), rect);
    EXPECT_EQ(info.zoom(), zoom);

    struct Expectation {
        PointI tile;
        TileInfoType type;
    };
    const std::vector<Expectation> expectations = {
        {{1, 6}, TileInfoType::Exists},    {{2, 6}, TileInfoType::NotExists},
        {{3, 6}, TileInfoType::NotExists}, {{1, 7}, TileInfoType::NotExists},
        {{2, 7}, TileInfoType::TneExists}, {{3, 7}, TileInfoType::NotExists},
    };
    for (const Expectation& e : expectations) {
        SCOPED_TRACE(QString("(%1,%2)").arg(e.tile.x()).arg(e.tile.y()).toStdString());
        const auto* entry = info.at(e.tile);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->type, e.type);
    }
}

TEST(FileSystemTileStorage, TileRectInfoCapAndReadAccessGating) {
    // 2048x2048 cap (storage.md §2.5, u_TileStorageFileSystem.pas:GetTileRectInfo:325): a rect
    // wider than 2048 tiles after ValidateTileRect clamping gives a null rect info (legacy nil), no error.
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                                   TileStorageAbilities::readWrite());
        constexpr quint8 zoom = 12; // 2^12 = 4096 tiles wide, room for a 2049-wide rect
        const RectI rect{0, 0, 2049, 1};
        auto result = storage->tileRectInfo(rect, zoom, MapVersionRequest{}, std::stop_token{});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, nullptr);
    }
    // ReadAccess false: legacy leaves Result = nil (storage.md §2.13.2 "gated on ReadAccess" note,
    // u_TileStorageFileSystem.pas:319) -- a successful null pointer, not an error.
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                                   TileStorageAbilities::none());
        auto result = storage->tileRectInfo(RectI{0, 0, 4, 4}, 5, MapVersionRequest{}, std::stop_token{});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, nullptr);
    }
}

TEST(FileSystemTileStorage, ScanTilesYieldsExactSequenceOrderedByFolderThenMaskThenName) {
    // Table-driven over ignoreTne on/off (storage.md §2.13.4): within one folder, tile-extension
    // files are yielded before .tne files (legacy's mask list order,
    // u_TileStorageFileSystem.pas:574-577), both name-sorted; folders are walked breadth-first
    // (x=1's folder, discovered first alphabetically under their shared parent, entirely before
    // x=3's folder -- u_FoldersIteratorRecursiveByLevels' FIFO queue). A same-extension file that
    // does not parse as a tile name ("bogus.jpg") is skipped silently.
    //
    // Review findings F-1/F-2/F-3 (T-0021F2-review.md): four more top-level wrapper folders are
    // added below, each holding exactly one further-nested valid tile. Their file-bearing leaf
    // folders sit one level deeper than "z6/0/x1/0" and "z6/0/x3/0", so -- being a strict
    // breadth-first walk -- the four original entries above are always yielded first, unaffected;
    // these wrappers only ever affect the tail of the sequence, appended identically for both
    // ignoreTne cases (none of them carry a .tne sibling).
    struct Case {
        const char* name;
        bool ignoreTne;
        std::vector<PointI> expectedOrder; // (x,y) pairs, in the exact order expected
        std::vector<TileInfoType> expectedTypes;
    };
    const std::vector<Case> cases = {
        {"IgnoreTneFalseYieldsTileThenTneInFolderOrder",
         false,
         {{1, 7}, {1, 8}, {1, 7}, {3, 2}, {22, 22}, {21, 21}, {20, 20}},
         {TileInfoType::Exists, TileInfoType::Exists, TileInfoType::TneExists, TileInfoType::Exists,
          TileInfoType::Exists, TileInfoType::Exists, TileInfoType::Exists}},
        {"IgnoreTneTrueSkipsTneMaskEntirely",
         true,
         {{1, 7}, {1, 8}, {3, 2}, {22, 22}, {21, 21}, {20, 20}},
         {TileInfoType::Exists, TileInfoType::Exists, TileInfoType::Exists, TileInfoType::Exists,
          TileInfoType::Exists, TileInfoType::Exists}},
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
        auto codec = std::make_shared<cg::storage::SasTileNameCodec>();
        constexpr quint8 zoom = 5;

        writeFile(storagePath + codec->addExt(codec->tilePath(PointI{1, 7}, zoom), QStringLiteral(".jpg")),
                  QByteArray("t17"));
        writeFile(storagePath + codec->addExt(codec->tilePath(PointI{1, 8}, zoom), QStringLiteral(".jpg")),
                  QByteArray("t18"));
        writeFile(storagePath + codec->addExt(codec->tilePath(PointI{1, 7}, zoom), QStringLiteral(".tne")));
        writeFile(storagePath + codec->addExt(codec->tilePath(PointI{3, 2}, zoom), QStringLiteral(".jpg")),
                  QByteArray("t32"));
        // Foreign file: same extension mask, but the name does not parse as a tile -- must be
        // skipped silently, without disturbing the order of the real entries around it.
        writeFile(storagePath + QStringLiteral("z6/0/x1/0/bogus.jpg"), QByteArray("not a tile"));
        // A non-matching extension is filtered at the mask level, never reaches the codec.
        writeFile(storagePath + QStringLiteral("z6/0/x1/0/readme.txt"));

        // F-1: sibling top-level folders "Z3" and "z1" differ only by case. Case-insensitive sort
        // (the fix) yields "z1" before "Z3" (NTFS order); the pre-fix case-sensitive QDir::Name
        // would yield "Z3" (uppercase) before "z1".
        writeFile(storagePath + QStringLiteral("Z3/") +
                      codec->addExt(codec->tilePath(PointI{20, 20}, zoom), QStringLiteral(".jpg")),
                  QByteArray("z3"));
        writeFile(storagePath + QStringLiteral("z1/") +
                      codec->addExt(codec->tilePath(PointI{21, 21}, zoom), QStringLiteral(".jpg")),
                  QByteArray("z1"));
        // F-2: a dot-prefixed folder is scanned once QDir::Hidden is set (legacy's
        // FindFirstFileEx lists hidden/dot-prefixed entries regardless of attributes).
        writeFile(storagePath + QStringLiteral(".hidden/") +
                      codec->addExt(codec->tilePath(PointI{22, 22}, zoom), QStringLiteral(".jpg")),
                  QByteArray("hidden"));
        // F-3: a candidate whose relative path runs through a non-ASCII wrapper folder is skipped
        // before it reaches the parser (u_EnumTileInfoByFileIterator.pas:99's IsAscii gate) -- this
        // tile must never appear in `entries` below, so it is deliberately absent from
        // c.expectedOrder.
        writeFile(storagePath + QStringLiteral("Копия/") +
                      codec->addExt(codec->tilePath(PointI{23, 23}, zoom), QStringLiteral(".jpg")),
                  QByteArray("nonascii"));

        auto storage = makeStorage(storagePath, codec, TileStorageAbilities::readWrite());
        auto result = storage->scanTiles(c.ignoreTne, false);
        ASSERT_TRUE(result.has_value());
        ASSERT_NE(*result, nullptr);
        const std::vector<ScannedEntry> entries = drain(std::move(*result));

        ASSERT_EQ(entries.size(), c.expectedOrder.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            SCOPED_TRACE(i);
            EXPECT_EQ(entries[i].tile, c.expectedOrder[i]);
            EXPECT_EQ(entries[i].zoom, zoom);
            EXPECT_EQ(entries[i].type, c.expectedTypes[i]);
        }
    }
}

TEST(FileSystemTileStorage, ScanTilesEnforcesFolderDepthCap) {
    // cMaxFolderDepth = 10 (u_TileStorageFileSystem.pas:561): a folder at depth 10 still yields
    // its files; a folder at depth 11 is never discovered. Six wrapper directories plus the SAS
    // codec's own four folder levels (z/num/x/num) put the file at depth 10 (included); seven
    // wrapper directories put it at depth 11 (excluded). The codec's parse() accepts any number of
    // leading directories, so the wrapper prefix does not affect parsing, only discovery.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto codec = std::make_shared<cg::storage::SasTileNameCodec>();
    constexpr quint8 zoom = 5;
    const QString tail = codec->addExt(codec->tilePath(PointI{1, 7}, zoom), QStringLiteral(".jpg"));
    const QString tailExcluded = codec->addExt(codec->tilePath(PointI{1, 8}, zoom), QStringLiteral(".jpg"));

    writeFile(storagePath + QStringLiteral("inc1/inc2/inc3/inc4/inc5/inc6/") + tail, QByteArray("included"));
    writeFile(storagePath + QStringLiteral("exc1/exc2/exc3/exc4/exc5/exc6/exc7/") + tailExcluded,
              QByteArray("excluded"));

    auto storage = makeStorage(storagePath, codec, TileStorageAbilities::readWrite());
    auto result = storage->scanTiles(true, false);
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(*result, nullptr);
    const std::vector<ScannedEntry> entries = drain(std::move(*result));

    bool foundIncluded = false;
    bool foundExcluded = false;
    for (const ScannedEntry& e : entries) {
        if (e.tile == PointI{1, 7}) {
            foundIncluded = true;
        }
        if (e.tile == PointI{1, 8}) {
            foundExcluded = true;
        }
    }
    EXPECT_TRUE(foundIncluded);
    EXPECT_FALSE(foundExcluded);
}

TEST(FileSystemTileStorage, ScanTilesReturnsNullWhenScanAccessDenied) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto storage = makeStorage(storagePath, std::make_shared<cg::storage::SasTileNameCodec>(),
                               TileStorageAbilities{true, false, true, true, true}); // allowScan=false
    auto result = storage->scanTiles(false, false);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, nullptr);
}

TEST(FileSystemTileStorage, ScanTilesLoadsTileDataButNotTneData) {
    // u_EnumTileInfoByFileIterator.pas:105-114: UpdateTileInfoByFile is called with
    // AIsLoadData=True unconditionally, so every scanned tile file's bytes are loaded into the
    // yielded TileInfo (unlike tileInfo()/probeLocked, which only loads on request) -- .tne
    // entries never carry data. ContentType is set unconditionally on both (:114).
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString storagePath = QDir(dir.path()).filePath(QStringLiteral("cache")) + QStringLiteral("/");
    auto codec = std::make_shared<cg::storage::SasTileNameCodec>();
    constexpr quint8 zoom = 5;
    const QByteArray tileBytes("jpegbytes-for-scan");
    writeFile(storagePath + codec->addExt(codec->tilePath(PointI{4, 4}, zoom), QStringLiteral(".jpg")),
              tileBytes);
    writeFile(storagePath + codec->addExt(codec->tilePath(PointI{4, 5}, zoom), QStringLiteral(".tne")));

    auto storage = makeStorage(storagePath, codec, TileStorageAbilities::readWrite());
    auto result = storage->scanTiles(false, false);
    ASSERT_TRUE(result.has_value());
    const std::vector<ScannedEntry> entries = drain(std::move(*result));
    ASSERT_EQ(entries.size(), 2u);

    const ScannedEntry* tileEntry = entries[0].type == TileInfoType::Exists ? &entries[0] : &entries[1];
    const ScannedEntry* tneEntry = entries[0].type == TileInfoType::TneExists ? &entries[0] : &entries[1];
    ASSERT_NE(tileEntry, tneEntry);

    EXPECT_EQ(tileEntry->data, tileBytes);
    EXPECT_EQ(tileEntry->contentType, QByteArray("image/jpg"));
    EXPECT_TRUE(tneEntry->data.isEmpty());
    EXPECT_EQ(tneEntry->contentType, QByteArray("image/jpg")); // set unconditionally, both branches
}

// T-0117: stateChangeable() is the folder storage's own state holder.
TEST(FileSystemTileStorage, StateChangeableIsOwnHolder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto storage = makeStorage(dir.path(), std::make_shared<cg::storage::SasTileNameCodec>(),
                               TileStorageAbilities::readWrite());
    cg::storage::test::expectStateChangeableIsOwn(*storage, true);
}
