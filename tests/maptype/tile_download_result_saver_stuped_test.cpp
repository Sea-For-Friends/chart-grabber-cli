// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_TileDownloadResultSaverStuped.pas (docs/specs/maptype.md 2.13.5, OQ-14, OQ-16), the non-bitmap
// path. Expectations are read from the temp directory of a real FileSystemTileStorage (layout gm1: tile (3,7)
// zoom 5 is z5/7/3.<ext>, the TNE marker z5/7/3.tne) as literals; line numbers are those of the Pascal unit.
#include "cg/core/content_type.h"
#include "cg/core/content_type_subst.h"
#include "cg/core/notifier.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/content_converters.h"
#include "cg/maptype/tile_download_result_saver_stuped.h"
#include "cg/maptype/tile_request.h"
#include "cg/net/download_result_factory.h"
#include "cg/net/global_download_config.h"
#include "cg/storage/codecs/gm1_tile_name_codec.h"
#include "cg/storage/file_system_tile_storage.h"
#include "cg/storage/storage_state.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using namespace cg;
using cg::core::ContentKind;
using cg::core::ContentTypeInfo;
using cg::maptype::SaveTileDownloadError;
using cg::maptype::TileDownloadResultSaverStuped;
using cg::net::ResultKind;
using cg::storage::TileStorageAbilities;

namespace {

const core::ContentTypeRegistry& registry() {
    return core::ContentTypeRegistry::standard();
}

std::shared_ptr<const maptype::ContentConverters> converters() {
    return maptype::test::testContentConverters();
}

const ContentTypeInfo kJpg{"image/jpg", ".jpg", ContentKind::Bitmap};
const ContentTypeInfo kPng{"image/png", ".png", ContentKind::Bitmap};
const ContentTypeInfo kGeoJson{"application/geo+json", ".geojson", ContentKind::Vector};
const ContentTypeInfo kWikiKml{"application/vnd.sas.wikimapia.kml+xml", ".kml", ContentKind::Vector};
const ContentTypeInfo kKml{"application/vnd.google-earth.kml+xml", ".kml", ContentKind::Vector};

geo::ProjectionSetPtr projectionSet() {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
}

std::shared_ptr<storage::FileSystemTileStorage> makeStorage(const QString& path, const ContentTypeInfo& type,
                                                            TileStorageAbilities abilities) {
    return std::make_shared<storage::FileSystemTileStorage>(
        storage::tileStorageTypeAbilities(storage::TileStorageKind::FolderSimple), abilities,
        std::make_shared<storage::MapVersionFactory>(), projectionSet(),
        std::make_shared<storage::TilePyramidNotifier>(), path, type,
        std::make_shared<storage::Gm1TileNameCodec>(), nullptr);
}

// Forwards to a real storage, but its state is a holder the test drives.
class HolderStorage : public storage::ITileStorage {
public:
    HolderStorage(std::shared_ptr<storage::FileSystemTileStorage> inner, TileStorageAbilities abilities)
        : inner_(std::move(inner)), holder(abilities) {}
    [[nodiscard]] storage::TileStorageTypeAbilities typeAbilities() const override {
        return inner_->typeAbilities();
    }
    [[nodiscard]] storage::StorageStatePtr state() const override { return holder.get(); }
    [[nodiscard]] core::Changeable& stateChangeable() override { return holder; }
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override {
        return inner_->projectionSet();
    }
    [[nodiscard]] storage::TilePyramidNotifier& tileNotifier() override { return inner_->tileNotifier(); }
    [[nodiscard]] QString tileFileName(geo::PointI xy, quint8 zoom,
                                       const storage::MapVersionInfoPtr& v) const override {
        return inner_->tileFileName(xy, zoom, v);
    }
    [[nodiscard]] core::Result<storage::TileInfo> tileInfo(geo::PointI xy, quint8 zoom,
                                                           const storage::MapVersionInfoPtr& v,
                                                           storage::LoadMode m) const override {
        return inner_->tileInfo(xy, zoom, v, m);
    }
    [[nodiscard]] core::Result<storage::TileInfo> tileInfoEx(geo::PointI xy, quint8 zoom,
                                                             const storage::MapVersionRequest& r,
                                                             storage::LoadMode m) const override {
        return inner_->tileInfoEx(xy, zoom, r, m);
    }
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI xy, quint8 zoom,
                                                const storage::MapVersionInfoPtr& v) override {
        return inner_->deleteTile(xy, zoom, v);
    }
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI xy, quint8 zoom,
                                              const storage::MapVersionInfoPtr& v, storage::TileDate d,
                                              QByteArrayView ct, const QByteArray& data,
                                              bool overwrite) override {
        return inner_->saveTile(xy, zoom, v, d, ct, data, overwrite);
    }
    [[nodiscard]] core::Result<storage::MapVersionList>
    listVersions(geo::PointI xy, quint8 zoom, const storage::MapVersionRequest& r) const override {
        return inner_->listVersions(xy, zoom, r);
    }
    [[nodiscard]] core::Result<storage::TileRectInfoPtr> tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                      const storage::MapVersionRequest& r,
                                                                      std::stop_token s) const override {
        return inner_->tileRectInfo(rect, zoom, r, s);
    }
    [[nodiscard]] core::Result<storage::EnumTileInfoPtr> scanTiles(bool a, bool b) override {
        return inner_->scanTiles(a, b);
    }

    std::shared_ptr<storage::FileSystemTileStorage> inner_;
    storage::StorageStateHolder holder;
};

std::optional<QByteArray> readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return f.readAll();
}

// One cache folder plus a saver for it. `config` is the global download config the saver asks at every save.
struct Rig {
    QTemporaryDir dir;
    std::shared_ptr<storage::FileSystemTileStorage> storage;
    std::shared_ptr<net::GlobalDownloadConfig> config = std::make_shared<net::GlobalDownloadConfig>();
    std::unique_ptr<TileDownloadResultSaverStuped> saver;

    explicit Rig(const ContentTypeInfo& type,
                 TileStorageAbilities abilities = TileStorageAbilities::readWrite(),
                 const QString& subst = QString(), maptype::PredicateByBinaryData empty = nullptr,
                 maptype::TilePostDownloadCropConfigStaticPtr crop = noCrop())
        : storage(makeStorage(cache(), type, abilities)) {
        EXPECT_TRUE(dir.isValid());
        saver = std::make_unique<TileDownloadResultSaverStuped>(
            config, std::move(empty), registry(), converters(), core::ContentTypeSubst::fromString(subst),
            type, std::move(crop), storage);
    }

    QString cache() const { return QDir(dir.path()).filePath(u"cache"_s) + u"/"_s; }
    std::optional<QByteArray> file(const QString& relative) const { return readFile(cache() + relative); }
    // Every file under the cache folder, relative, sorted.
    QStringList files() const {
        QStringList out;
        QDirIterator it(cache(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            out.append(QDir(cache()).relativeFilePath(it.next()));
        }
        out.sort();
        return out;
    }

    static maptype::TilePostDownloadCropConfigStaticPtr noCrop() {
        return std::make_shared<maptype::TilePostDownloadCropConfigStatic>(
            geo::RectI{0, 0, 0, 0}, geo::PointI{0, 0}, geo::PointI{0, 0}, geo::PointI{0, 0}, QString());
    }
};

net::DownloadRequest tileRequest() {
    net::DownloadRequest r;
    r.url = "http://h/t";
    r.source = std::make_shared<const maptype::TileRequest>(geo::PointI{3, 7}, quint8{5}, nullptr);
    return r;
}

net::DownloadRequest plainRequest() {
    net::DownloadRequest r;
    r.url = "http://h/t";
    return r;
}

net::DownloadResult okResult(const QByteArray& type, const QByteArray& body) {
    return net::result::ok(200, "HTTP/1.1 200 OK\r\n", type, body);
}

const maptype::ITileRequestResultError* errorOf(const maptype::ITileRequestResultPtr& r) {
    return dynamic_cast<const maptype::ITileRequestResultError*>(r.get());
}
bool isOk(const maptype::ITileRequestResultPtr& r) {
    return dynamic_cast<const maptype::ITileRequestResultOk*>(r.get()) != nullptr;
}

} // namespace

// :258-261, :487-510: an OK result is converted (simple copies only) and stored with the map's content type.
// :426-427: the per-map substitution runs first. The BadMIME text is u_ResStrings.pas:102; a pair legacy
// converts with a bitmap codec / an archive converter has no converter here (OQ-14) and ends in that text.
TEST(TileDownloadResultSaverStuped, OkResultsAreStoredWithTheMapsContentTypeOrRejected) {
    struct Row {
        const char* name;
        ContentTypeInfo save;
        QString subst;
        QByteArray received;
        QString storedAs; // empty: nothing stored, the result is an error with `errorText`
        QString errorText;
    };
    const QString bad = u"Server returned type \"%1\", this is not bitmap image"_s;
    const std::vector<Row> rows = {
        {"same type", kJpg, {}, "image/jpg", u"z5/7/3.jpg"_s, {}},
        {"synonym", kJpg, {}, "image/jpeg", u"z5/7/3.jpg"_s, {}},
        {"case-insensitive key", kPng, {}, "IMAGE/X-PNG", u"z5/7/3.png"_s, {}},
        {"substituted", kPng, u"image/x-bad=image/png"_s, "image/x-bad", u"z5/7/3.png"_s, {}},
        {"substitution target decides",
         kJpg,
         u"image/jpeg=application/json"_s,
         "image/jpeg",
         {},
         bad.arg(u"application/json"_s)},
        {"not a content type", kJpg, {}, "text/html", {}, bad.arg(u"text/html"_s)},
        {"empty content type", kJpg, {}, "", {}, bad.arg(QString())},
        {"json to geojson", kGeoJson, {}, "application/json", u"z5/7/3.geojson"_s, {}},
        {"google kml to wikimapia kml",
         kWikiKml,
         {},
         "application/vnd.google-earth.kml+xml",
         u"z5/7/3.kml"_s,
         {}},
        {"wikimapia kml to google kml (one way)",
         kKml,
         {},
         "application/vnd.sas.wikimapia.kml+xml",
         {},
         bad.arg(u"application/vnd.sas.wikimapia.kml+xml"_s)},
        {"kmz to kml needs the archive converter",
         kKml,
         {},
         "application/vnd.google-earth.kmz",
         {},
         bad.arg(u"application/vnd.google-earth.kmz"_s)},
    };
    const QByteArray body("\x01\x02payload\xff", 10);
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        Rig rig(row.save, TileStorageAbilities::readWrite(), row.subst);
        const net::DownloadRequest request = tileRequest();
        const net::DownloadResult result = okResult(row.received, body);
        const auto out = rig.saver->saveDownloadResult(request, result);
        ASSERT_TRUE(out);
        if (row.storedAs.isEmpty()) {
            ASSERT_NE(errorOf(out), nullptr);
            EXPECT_EQ(errorOf(out)->errorText(), row.errorText);
            EXPECT_FALSE(isOk(out));
            EXPECT_TRUE(rig.files().isEmpty());
        } else {
            EXPECT_TRUE(isOk(out));
            EXPECT_EQ(errorOf(out), nullptr);
            EXPECT_EQ(rig.files(), QStringList{row.storedAs});
            EXPECT_EQ(rig.file(row.storedAs), body);
            const auto* withResult =
                dynamic_cast<const maptype::ITileRequestResultWithDownloadResult*>(out.get());
            ASSERT_NE(withResult, nullptr);
            EXPECT_EQ(withResult->downloadResult().contentType, row.received); // the original result, :261
        }
    }
}

// :491-495, :506: the empty-tile predicate sees the converted data; a hit means "no data" (a TNE only when
// the global option says so); the request result stays Ok(original) (:261). :314-325: the option is asked per
// save.
TEST(TileDownloadResultSaverStuped, EmptyTilePredicateAndTheSaveTileNotExistsOption) {
    const QByteArray empty("EMPTY-TILE-SAMPLE");
    Rig rig(kJpg, TileStorageAbilities::readWrite(), {}, [&](const QByteArray& d) { return d == empty; });

    const auto first = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpeg", empty));
    EXPECT_TRUE(isOk(first));
    EXPECT_TRUE(rig.files().isEmpty()); // option off: no marker

    rig.config->setSaveTileNotExists(true); // read again at the next save
    const auto second = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpeg", empty));
    EXPECT_TRUE(isOk(second));
    EXPECT_EQ(rig.files(), QStringList{u"z5/7/3.tne"_s});

    const auto third = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpeg", empty + "x"));
    EXPECT_TRUE(isOk(third)); // another body is a tile again and supersedes the marker
    EXPECT_EQ(rig.files(), QStringList{u"z5/7/3.jpg"_s});
    EXPECT_EQ(rig.file(u"z5/7/3.jpg"_s), empty + "x");
}

// :273-285: a data-not-exists result is a TNE only with the option; the request result is Ok in both cases.
TEST(TileDownloadResultSaverStuped, DataNotExistsWritesAMarkerOnlyWithTheOption) {
    Rig rig(kPng);
    const net::DownloadResult notExists = net::result::dataNotExistsByStatusCode(404, "HTTP/1.1 404\r\n");

    const auto off = rig.saver->saveDownloadResult(tileRequest(), notExists);
    ASSERT_TRUE(off);
    EXPECT_TRUE(isOk(off));
    const auto* withResult = dynamic_cast<const maptype::ITileRequestResultWithDownloadResult*>(off.get());
    ASSERT_NE(withResult, nullptr);
    EXPECT_EQ(withResult->downloadResult().kind, ResultKind::DataNotExists);
    EXPECT_EQ(withResult->downloadResult().statusCode, 404u);
    EXPECT_TRUE(rig.files().isEmpty());

    rig.config->setSaveTileNotExists(true);
    EXPECT_TRUE(isOk(rig.saver->saveDownloadResult(tileRequest(), notExists)));
    EXPECT_EQ(rig.files(), QStringList{u"z5/7/3.tne"_s});
}

// :245-289: every other kind is left to the caller (null), nothing is stored even with the option on.
TEST(TileDownloadResultSaverStuped, OtherResultKindsAreNotHandled) {
    Rig rig(kJpg);
    rig.config->setSaveTileNotExists(true);
    const std::vector<net::DownloadResult> others = {
        net::result::notNecessary(u"same"_s, 200, "h"),
        net::result::canceled(),
        net::result::badProxyAuth(),
        net::result::banned(200, "h"),
        net::result::badContentType("text/html", 200, "h"),
        net::result::badContentLength(1, 2, 200, "h"),
        net::result::badContentEncoding(200, "h", "br", "d", u"t"_s),
        net::result::loadErrorByStatusCode(500),
        net::result::noConnectToServer(7),
        net::result::loadErrorByUnknownReason(u"x"_s),
    };
    for (const net::DownloadResult& other : others) {
        EXPECT_EQ(rig.saver->saveDownloadResult(tileRequest(), other), nullptr)
            << static_cast<int>(other.kind);
    }
    EXPECT_TRUE(rig.files().isEmpty());
}

// :428-432 / :283. For Ok the handler of :262 builds ErrorAfterDownloadRequest, whose constructor casts the
// request again (u_TileRequestResult.pas:224) and raises: that exception, not "This was not tile
// request", leaves the saver. The data-not-exists path has no handler: its own text leaves.
TEST(TileDownloadResultSaverStuped, ARequestWithoutATileThrows) {
    Rig rig(kJpg);
    rig.config->setSaveTileNotExists(true);
    try {
        (void)rig.saver->saveDownloadResult(plainRequest(), okResult("image/jpg", "x"));
        ADD_FAILURE() << "no exception";
    } catch (const std::logic_error& e) {
        EXPECT_STREQ(e.what(), "Interface not supported");
    }
    try {
        (void)rig.saver->saveDownloadResult(plainRequest(), net::result::dataNotExistsByStatusCode(404, "h"));
        ADD_FAILURE() << "no exception";
    } catch (const std::logic_error& e) {
        EXPECT_STREQ(e.what(), "This was not tile request");
    }
    EXPECT_TRUE(rig.files().isEmpty());
}

// :238-242, :301-303: no add access -> the state is disabled with rsNoWriteAccess; a save raises it: caught
// for Ok (:262), through for data-not-exists (:276 has no handler).
TEST(TileDownloadResultSaverStuped, ReadOnlyStorageDisablesTheSaverAndRejectsEverySave) {
    Rig rig(kJpg, TileStorageAbilities::readOnly());
    rig.config->setSaveTileNotExists(true);
    const auto state = rig.saver->state()->getStatic();
    EXPECT_FALSE(state->enabled);
    EXPECT_EQ(state->disableReason, u"No write access to tile storage"_s);

    const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpg", "x"));
    ASSERT_NE(errorOf(out), nullptr);
    EXPECT_EQ(errorOf(out)->errorText(), u"No write access to tile storage"_s);
    try {
        (void)rig.saver->saveDownloadResult(tileRequest(), net::result::dataNotExistsByStatusCode(404, "h"));
        ADD_FAILURE() << "no exception";
    } catch (const SaveTileDownloadError& e) {
        EXPECT_STREQ(e.what(), "No write access to tile storage");
    }
    EXPECT_TRUE(rig.files().isEmpty());
}

// :181-184, :233-243: the state is evaluated at construction and follows every change of the storage state;
// the subscription ends with the saver.
TEST(TileDownloadResultSaverStuped, StateFollowsTheStorageState) {
    QTemporaryDir dir;
    auto holderStorage = std::make_shared<HolderStorage>(
        makeStorage(QDir(dir.path()).filePath(u"cache"_s) + u"/"_s, kJpg, TileStorageAbilities::readWrite()),
        TileStorageAbilities::readWrite());
    int stateChanges = 0;
    core::Subscription watch;
    {
        TileDownloadResultSaverStuped saver(std::make_shared<net::GlobalDownloadConfig>(), nullptr,
                                            registry(), converters(), {}, kJpg, Rig::noCrop(), holderStorage);
        watch = saver.state()->changed().subscribe([&stateChanges] { ++stateChanges; });
        EXPECT_EQ(holderStorage->holder.changed().subscriberCount(), 1u);
        EXPECT_TRUE(saver.state()->getStatic()->enabled);

        holderStorage->holder.setAddAccess(false);
        EXPECT_FALSE(saver.state()->getStatic()->enabled);
        EXPECT_EQ(saver.state()->getStatic()->disableReason, u"No write access to tile storage"_s);
        EXPECT_EQ(stateChanges, 1);

        holderStorage->holder.setReplaceAccess(false); // still no add access: nothing changes
        EXPECT_EQ(stateChanges, 1);

        holderStorage->holder.setAddAccess(true);
        EXPECT_TRUE(saver.state()->getStatic()->enabled);
        EXPECT_EQ(stateChanges, 2);
    }
    EXPECT_EQ(holderStorage->holder.changed().subscriberCount(), 0u);
}

// :312: replaceAccess of the storage state is the overwrite flag of SaveTile.
TEST(TileDownloadResultSaverStuped, OverwriteFollowsTheReplaceAccess) {
    for (const bool replace : {false, true}) {
        SCOPED_TRACE(replace);
        TileStorageAbilities abilities = TileStorageAbilities::readWrite();
        abilities.allowReplace = replace;
        Rig rig(kJpg, abilities);
        EXPECT_TRUE(isOk(rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpg", "first"))));
        EXPECT_TRUE(isOk(rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpg", "second"))));
        EXPECT_EQ(rig.file(u"z5/7/3.jpg"_s), replace ? QByteArray("second") : QByteArray("first"));
    }
}

// :262-269: an error of the storage is the text of the error result.
TEST(TileDownloadResultSaverStuped, AStorageErrorIsTheTextOfTheErrorResult) {
    Rig rig(kJpg);
    QFile blocker(rig.cache() + u"z5"_s); // a file where the zoom folder must be created
    ASSERT_TRUE(QDir().mkpath(rig.cache()));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpg", "x"));
    ASSERT_TRUE(out);
    ASSERT_NE(errorOf(out), nullptr);
    EXPECT_EQ(errorOf(out)->errorText(), u"cannot create directory "_s + rig.cache() + u"z5/7"_s);
    EXPECT_FALSE(isOk(out));
}

// :434-435, :436-486 (OQ-16, spec bitmap.md §1.7 M-1): crop / cut on a bitmap map: the received-type checks
// of legacy run, then the refusal (the port has no crop / cut, R-13) is a readable SaveTileDownloadError. A
// vector map ignores the config.
TEST(TileDownloadResultSaverStuped, CropOrCutOnABitmapMapIsRefusedWithAReadableError) {
    const auto cropOn = std::make_shared<maptype::TilePostDownloadCropConfigStatic>(
        geo::RectI{0, 0, 100, 100}, geo::PointI{0, 0}, geo::PointI{0, 0}, geo::PointI{0, 0}, QString());
    const auto cutOn = std::make_shared<maptype::TilePostDownloadCropConfigStatic>(
        geo::RectI{0, 0, 0, 0}, geo::PointI{2, 2}, geo::PointI{0, 0}, geo::PointI{0, 0}, QString());
    ASSERT_TRUE(cropOn->isCropOnDownload());
    ASSERT_FALSE(cropOn->isCutOnDownload());
    ASSERT_TRUE(cutOn->isCutOnDownload());
    const QString refused = u"Crop and cut of downloaded tiles are not supported for this map"_s;
    const QString notBitmap = u"Server returned type \"application/json\", this is not bitmap image"_s;
    for (const auto& crop : {cropOn, cutOn}) {
        Rig rig(kPng, TileStorageAbilities::readWrite(), {}, nullptr, crop);
        const auto converted = rig.saver->saveDownloadResult(tileRequest(), okResult("image/png", "x"));
        ASSERT_NE(errorOf(converted), nullptr);
        EXPECT_EQ(errorOf(converted)->errorText(), refused);
        const auto other = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpeg", "x"));
        EXPECT_EQ(errorOf(other)->errorText(), refused); // legacy would convert here; the checks pass
        const auto vector = rig.saver->saveDownloadResult(tileRequest(), okResult("application/json", "x"));
        EXPECT_EQ(errorOf(vector)->errorText(), notBitmap); // :481-483
        const auto unknown = rig.saver->saveDownloadResult(tileRequest(), okResult("a/b", "x"));
        EXPECT_EQ(errorOf(unknown)->errorText(),
                  u"Server returned type \"a/b\", this is not bitmap image"_s); // :484-485
        EXPECT_TRUE(rig.files().isEmpty());
        // data-not-exists never reaches the branch
        rig.config->setSaveTileNotExists(true);
        EXPECT_TRUE(isOk(
            rig.saver->saveDownloadResult(tileRequest(), net::result::dataNotExistsByStatusCode(404, "h"))));
        EXPECT_EQ(rig.files(), QStringList{u"z5/7/3.tne"_s});
    }
    Rig vectorMap(kGeoJson, TileStorageAbilities::readWrite(), {}, nullptr, cropOn);
    EXPECT_TRUE(isOk(vectorMap.saver->saveDownloadResult(tileRequest(), okResult("application/json", "{}"))));
    EXPECT_EQ(vectorMap.file(u"z5/7/3.geojson"_s), QByteArray("{}"));
}

// Proves the crop config of the stock corpus only (the saves use a fixed PNG map, not each ZMP's own save
// type). The stock corpus (514 ZMPs, tests/fixtures/zmp/sas.maps): 9 have a crop rect (T-0044 handoff:
// independent grep), none has a cut. Each of the nine is refused with the readable error on a bitmap map
// until R-13.
TEST(TileDownloadResultSaverStuped, StockZmpsWithCropOrCutAreRefused) {
    CG_SKIP_IF_NO_CORPUS();
    const maptype::test::ZmpEnv env;
    QStringList errors;
    const auto set = env.loadAll(maptype::test::fixtureDir() + u"/zmp/sas.maps"_s, errors);
    ASSERT_TRUE(errors.isEmpty());
    ASSERT_EQ(set->guids().size(), 514u);
    int crop = 0;
    int cut = 0;
    for (const QUuid& guid : set->guids()) {
        const auto config = set->zmpByGuid(guid)->tilePostDownloadCropConfig();
        crop += config->isCropOnDownload() ? 1 : 0;
        cut += config->isCutOnDownload() ? 1 : 0;
        if (config->isCropOnDownload() || config->isCutOnDownload()) {
            Rig rig(kPng, TileStorageAbilities::readWrite(), {}, nullptr, config);
            const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("image/png", "x"));
            ASSERT_NE(errorOf(out), nullptr);
            EXPECT_EQ(errorOf(out)->errorText(),
                      u"Crop and cut of downloaded tiles are not supported for this map"_s);
        }
    }
    EXPECT_EQ(crop, 9);
    EXPECT_EQ(cut, 0);
}

TEST(TileDownloadResultSaverStuped, NullCollaboratorsAreRejectedByTheConstructor) {
    QTemporaryDir dir;
    auto holderStorage = std::make_shared<HolderStorage>(
        makeStorage(QDir(dir.path()).filePath(u"cache"_s) + u"/"_s, kJpg, TileStorageAbilities::readWrite()),
        TileStorageAbilities::readWrite());
    const auto make = [&](std::shared_ptr<const net::IGlobalDownloadConfig> config,
                          maptype::TilePostDownloadCropConfigStaticPtr crop,
                          storage::ITileStoragePtr target) {
        return TileDownloadResultSaverStuped(std::move(config), nullptr, registry(), converters(), {}, kJpg,
                                             std::move(crop), std::move(target));
    };
    const auto config = std::make_shared<net::GlobalDownloadConfig>();
    EXPECT_THROW((void)make(nullptr, Rig::noCrop(), holderStorage), std::invalid_argument);
    EXPECT_THROW((void)make(config, nullptr, holderStorage), std::invalid_argument);
    EXPECT_THROW((void)make(config, Rig::noCrop(), nullptr), std::invalid_argument);
    EXPECT_THROW((void)TileDownloadResultSaverStuped(config, nullptr, registry(), nullptr, {}, kJpg,
                                                     Rig::noCrop(), holderStorage),
                 std::invalid_argument);
    EXPECT_EQ(holderStorage->holder.changed().subscriberCount(), 0u);
}

namespace {

// A storage that records what the saver hands to saveTile (content type and bytes); the TNE marker is a
// record with an empty content type and empty data.
class RecordingStorage final : public HolderStorage {
public:
    struct Saved {
        QByteArray contentType;
        QByteArray data;
    };
    using HolderStorage::HolderStorage;
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI, quint8, const storage::MapVersionInfoPtr&,
                                              storage::TileDate, QByteArrayView ct, const QByteArray& data,
                                              bool) override {
        saved.push_back({ct.toByteArray(), data});
        return true;
    }
    std::vector<Saved> saved;
};

struct RecordingRig {
    QTemporaryDir dir;
    std::shared_ptr<RecordingStorage> storage;
    std::shared_ptr<net::GlobalDownloadConfig> config = std::make_shared<net::GlobalDownloadConfig>();
    std::unique_ptr<TileDownloadResultSaverStuped> saver;

    explicit RecordingRig(const ContentTypeInfo& type, maptype::PredicateByBinaryData empty = nullptr) {
        EXPECT_TRUE(dir.isValid());
        storage =
            std::make_shared<RecordingStorage>(makeStorage(QDir(dir.path()).filePath(u"cache"_s) + u"/"_s,
                                                           type, TileStorageAbilities::readWrite()),
                                               TileStorageAbilities::readWrite());
        saver = std::make_unique<TileDownloadResultSaverStuped>(config, std::move(empty), registry(),
                                                                converters(), core::ContentTypeSubst(), type,
                                                                Rig::noCrop(), storage);
    }
};

const QByteArray kPngSig("\x89PNG\r\n\x1a\n", 8);
const ContentTypeInfo kIco{"image/x-icon", ".ico", ContentKind::Bitmap};

QByteArray solidImage(bool png) {
    const auto b = maptype::test::testBitmap(16, 16, 0xFF3366CC);
    const auto f = maptype::test::testBitmapFactory();
    return png ? f->createPngSaver()->save(b) : f->createJpegSaver()->save(b);
}

} // namespace

// Proof 7, :487-495, :506: a JPEG result for a PNG map is re-encoded before it is stored; the request result
// keeps the original; a synonym target stores the received bytes unchanged.
TEST(TileDownloadResultSaverStuped, ConvertedBytesAreStoredAndTheOriginalResultIsReturned) {
    const QByteArray jpeg = solidImage(false);
    {
        RecordingRig rig(kPng);
        const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpeg", jpeg));
        ASSERT_TRUE(isOk(out));
        const auto* withResult =
            dynamic_cast<const maptype::ITileRequestResultWithDownloadResult*>(out.get());
        ASSERT_NE(withResult, nullptr);
        EXPECT_EQ(withResult->downloadResult().contentType, "image/jpeg");
        EXPECT_EQ(withResult->downloadResult().data, jpeg);
        ASSERT_EQ(rig.storage->saved.size(), 1u);
        EXPECT_EQ(rig.storage->saved[0].contentType, "image/png");
        EXPECT_TRUE(rig.storage->saved[0].data.startsWith(kPngSig));
        EXPECT_NE(rig.storage->saved[0].data, jpeg);
    }
    {
        RecordingRig rig(kJpg);
        EXPECT_TRUE(isOk(rig.saver->saveDownloadResult(tileRequest(), okResult("image/jpeg", jpeg))));
        ASSERT_EQ(rig.storage->saved.size(), 1u);
        EXPECT_EQ(rig.storage->saved[0].contentType, "image/jpg");
        EXPECT_EQ(rig.storage->saved[0].data, jpeg);
    }
}

// Proof 8, :507-509: no converter -> the BadMIME text, nothing stored.
TEST(TileDownloadResultSaverStuped, MissingConverterIsTheBadMimeError) {
    const QString bad = u"Server returned type \"%1\", this is not bitmap image"_s;
    const QByteArray png = solidImage(true);
    {
        RecordingRig rig(kIco); // no saver for an icon
        const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("image/png", png));
        ASSERT_NE(errorOf(out), nullptr);
        EXPECT_EQ(errorOf(out)->errorText(), bad.arg(u"image/png"_s));
        EXPECT_TRUE(rig.storage->saved.empty());
    }
    {
        RecordingRig rig(kPng);
        const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("application/json", "{}"));
        ASSERT_NE(errorOf(out), nullptr);
        EXPECT_EQ(errorOf(out)->errorText(), bad.arg(u"application/json"_s));
        EXPECT_TRUE(rig.storage->saved.empty());
    }
}

// Proof 9, :489 + :262-269: the loader raises, the text is the error of the result, nothing stored.
TEST(TileDownloadResultSaverStuped, ConverterExceptionIsTheTextOfTheErrorResult) {
    RecordingRig rig(kJpg);
    const auto out =
        rig.saver->saveDownloadResult(tileRequest(), okResult("image/png", QByteArray("\x01\x02\x03", 3)));
    EXPECT_FALSE(isOk(out));
    ASSERT_NE(errorOf(out), nullptr);
    EXPECT_EQ(errorOf(out)->errorText(), u"FreeBitmap.LoadFromMemory FAIL!"_s);
    EXPECT_TRUE(rig.storage->saved.empty());
}

// Proof 10, :490-495: the empty-tile predicate sees the CONVERTED bytes; a hit stores the TNE marker (option
// on) and the request result stays Ok(original), :261.
TEST(TileDownloadResultSaverStuped, EmptyTilePredicateSeesTheConvertedBytes) {
    std::vector<QByteArray> seen;
    RecordingRig rig(kJpg, [&](const QByteArray& d) {
        seen.push_back(d);
        return d.startsWith(QByteArray("\xFF\xD8", 2));
    });
    rig.config->setSaveTileNotExists(true);
    const QByteArray png = solidImage(true);
    const auto out = rig.saver->saveDownloadResult(tileRequest(), okResult("image/png", png));
    ASSERT_TRUE(isOk(out));
    const auto* withResult = dynamic_cast<const maptype::ITileRequestResultWithDownloadResult*>(out.get());
    ASSERT_NE(withResult, nullptr);
    EXPECT_EQ(withResult->downloadResult().data, png);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_TRUE(seen[0].startsWith(QByteArray("\xFF\xD8", 2)));
    ASSERT_EQ(rig.storage->saved.size(), 1u);
    EXPECT_TRUE(rig.storage->saved[0].contentType.isEmpty());
    EXPECT_TRUE(rig.storage->saved[0].data.isEmpty());
}
