// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_DownloadCheckerStuped.pas (docs/specs/maptype.md 2.13.4, LD-20). Expected values are literals read
// from the Pascal (line numbers in the comments) and from the stock ZMP corpus tests/fixtures/zmp/sas.maps
// (counts taken by a script outside the code under test: find/md5sum over the EmptyTiles folders).
#include "cg/core/content_type.h"
#include "cg/core/language_manager.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/download_checker_stuped.h"
#include "cg/maptype/tile_request.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"
#include "cg/net/download_result_factory.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using namespace cg;
using cg::maptype::DownloadCheckerStuped;
using cg::maptype::PredicateByBinaryData;
using cg::maptype::TileRequest;
using cg::maptype::TileRequestWithSizeCheck;
using cg::net::ResultKind;

namespace {

// --- fakes ----------------------------------------------------------------------------------------------

class FakeAntiBan final : public net::IAntiBan {
public:
    void preDownload(const net::DownloadRequest& request) override { preUrls.push_back(request.url); }
    std::optional<net::DownloadResult> postCheckDownload(const net::DownloadRequest& /*request*/,
                                                         const QByteArray& body, quint32& statusCode,
                                                         QByteArray& responseHead) override {
        ++postCalls;
        postBody = body;
        statusCode = 599; // proves the status is the caller's variable, rewritten in place
        responseHead = "rewritten";
        return post;
    }
    std::vector<QByteArray> preUrls;
    int postCalls = 0;
    QByteArray postBody;
    std::optional<net::DownloadResult> post;
};

// A storage that answers tileInfo from a script and records the arguments; everything else is unused.
class FakeStorage final : public storage::ITileStorage {
public:
    [[nodiscard]] core::Result<storage::TileInfo> tileInfo(geo::PointI xy, quint8 zoom,
                                                           const storage::MapVersionInfoPtr& version,
                                                           storage::LoadMode mode) const override {
        ++calls;
        lastXy = xy;
        lastZoom = zoom;
        lastVersion = version;
        lastMode = mode;
        return answer;
    }
    [[nodiscard]] storage::TileStorageTypeAbilities typeAbilities() const override { unused(); }
    [[nodiscard]] storage::StorageStatePtr state() const override { unused(); }
    [[nodiscard]] core::Changeable& stateChangeable() override { unused(); }
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override { unused(); }
    [[nodiscard]] storage::TilePyramidNotifier& tileNotifier() override { unused(); }
    [[nodiscard]] QString tileFileName(geo::PointI, quint8,
                                       const storage::MapVersionInfoPtr&) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::TileInfo>
    tileInfoEx(geo::PointI, quint8, const storage::MapVersionRequest&, storage::LoadMode) const override {
        unused();
    }
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI, quint8,
                                                const storage::MapVersionInfoPtr&) override {
        unused();
    }
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI, quint8, const storage::MapVersionInfoPtr&,
                                              storage::TileDate, QByteArrayView, const QByteArray&,
                                              bool) override {
        unused();
    }
    [[nodiscard]] core::Result<storage::MapVersionList>
    listVersions(geo::PointI, quint8, const storage::MapVersionRequest&) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::TileRectInfoPtr>
    tileRectInfo(geo::RectI, quint8, const storage::MapVersionRequest&, std::stop_token) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::EnumTileInfoPtr> scanTiles(bool, bool) override { unused(); }

    mutable int calls = 0;
    mutable geo::PointI lastXy;
    mutable quint8 lastZoom = 0;
    mutable storage::MapVersionInfoPtr lastVersion;
    mutable storage::LoadMode lastMode = storage::LoadMode::AsIs;
    core::Result<storage::TileInfo> answer{storage::TileInfo{}};

private:
    [[noreturn]] static void unused() { throw std::logic_error("FakeStorage: not used by the checker"); }
};

std::shared_ptr<net::TileDownloaderConfig> makeConfig(bool ignore, QByteArray expected, QByteArray def) {
    net::TileDownloaderSettings s;
    s.ignoreMimeType = ignore;
    s.expectedMimeTypes = std::move(expected);
    s.defaultMimeType = std::move(def);
    return std::make_shared<net::TileDownloaderConfig>(std::make_shared<net::InetConfig>(), std::move(s));
}

net::DownloadRequest requestOf(const maptype::ITileRequestPtr& source) {
    net::DownloadRequest r;
    r.url = "http://h/t";
    r.source = source; // always stored from an ITileRequestPtr
    return r;
}

struct Out {
    std::optional<net::DownloadResult> result;
    quint32 status = 200;
    QByteArray contentType;
    QByteArray head = "HTTP/1.1 200 OK\r\n";
};

Out run(const DownloadCheckerStuped& checker, const net::DownloadRequest& request, const QByteArray& body,
        QByteArray contentType) {
    Out o;
    o.contentType = std::move(contentType);
    o.result = checker.afterReceiveData(request, body, o.status, o.contentType, o.head);
    return o;
}

DownloadCheckerStuped mimeChecker(bool ignore, QByteArray expected, QByteArray def) {
    return DownloadCheckerStuped(nullptr, {}, {}, makeConfig(ignore, std::move(expected), std::move(def)),
                                 std::make_shared<FakeStorage>());
}

// --- the content-type decision (:164-183), one table
// --------------------------------------------------------

struct MimeRow {
    const char* name;
    bool ignore;
    const char* expected;
    const char* def;
    const char* received;
    bool rejected;
    const char* contentTypeAfter; // the in-place variable after the call
};

TEST(DownloadCheckerStuped, ContentTypeDecisionTable) {
    // The expected lists are the real stock values (grep of tests/fixtures/zmp/sas.maps/*/params.txt) except
    // the first four rows, which pin the rule itself.
    const MimeRow rows[] = {
        {"exact", false, "image/png", "image/jpeg", "image/png", false, "image/png"},
        {"empty gets the default", false, "image/png", "image/jpeg", "", false, "image/jpeg"},
        {"empty gets the default even if not in the list", false, "image/png", "text/zzz", "", false,
         "text/zzz"},
        {"rejected keeps the received type", false, "image/png", "image/jpeg", "image/gif", true,
         "image/gif"},
        {"ignore: any type, default written", true, "image/png", "image/jpeg", "text/html", false,
         "image/jpeg"},
        {"ignore: empty type, default written", true, "image/png", "image/jpeg", "", false, "image/jpeg"},
        {"ignore wins over an empty expected list", true, "", "image/gif", "x/y", false, "image/gif"},
        // LD-20: Pos(received, expected) is a substring search
        {"LD-20 token of a list", false, "image/jpeg,image/png", "image/png", "image/jpeg", false,
         "image/jpeg"},
        {"LD-20 the stock image/png8bit accepts image/png", false, "image/png8bit", "image/png", "image/png",
         false, "image/png"},
        {"LD-20 stock image/png8tr accepts image/png", false, "image/png8tr", "image/png", "image/png", false,
         "image/png"},
        {"LD-20 prefix", false, "image/jpeg,image/png", "x", "image/jp", false, "image/jp"},
        {"LD-20 one letter", false, "image/jpeg,image/png", "x", "e", false, "e"},
        {"LD-20 across the separator", false, "image/jpeg,image/png", "x", "g,i", false, "g,i"},
        {"LD-20 the whole list", false, "text/html , image/gif", "x", "text/html , image/gif", false,
         "text/html , image/gif"},
        {"separator and space belong to the list", false, "text/html , image/gif", "x", "text/html", false,
         "text/html"},
        {"stock charset value matches itself", false, "text/xml; charset=UTF-8", "x",
         "text/xml; charset=UTF-8", false, "text/xml; charset=UTF-8"},
        {"a longer received type is not in the list", false, "image/png", "x", "image/png; mode=24bit", true,
         "image/png; mode=24bit"},
        {"case sensitive", false, "image/png", "x", "IMAGE/PNG", true, "IMAGE/PNG"},
        {"the stock star is no wildcard", false, "*", "x", "image/png", true, "image/png"},
        {"empty expected list rejects any type", false, "", "x", "image/png", true, "image/png"},
        {"order matters: not a substring of the reversed list", false, "image/png,image/jpeg", "x",
         "image/jpeg,image/png", true, "image/jpeg,image/png"},
        {"non-ASCII bytes: same case matches", false, "\xd0\x93\xd0\xbe\xd1\x80\xd0\xbe\xd0\xb4/x", "d",
         "\xd0\x93\xd0\xbe\xd1\x80", false, "\xd0\x93\xd0\xbe\xd1\x80"},
        {"non-ASCII bytes: other case rejects", false, "\xd0\x93\xd0\xbe\xd1\x80\xd0\xbe\xd0\xb4/x", "d",
         "\xd0\xb3\xd0\xbe\xd1\x80", true, "\xd0\xb3\xd0\xbe\xd1\x80"},
    };
    for (const MimeRow& row : rows) {
        SCOPED_TRACE(row.name);
        const auto checker = mimeChecker(row.ignore, row.expected, row.def);
        const Out o = run(checker, requestOf(nullptr), "body", row.received);
        EXPECT_EQ(o.contentType.toStdString(), std::string(row.contentTypeAfter));
        EXPECT_EQ(o.result.has_value(), row.rejected);
        if (row.rejected) {
            EXPECT_EQ(o.result->kind, ResultKind::BadContentType);
            EXPECT_EQ(o.result->contentType.toStdString(), std::string(row.received));
            EXPECT_EQ(o.result->text.toStdString(), std::string("Unexpected content type ") + row.received);
        }
    }
}

// --- status code and head: the checker decides nothing on them, it carries them (:155-231)
// ------------------------

TEST(DownloadCheckerStuped, StatusCodeAndHeadAreCarriedNotJudged) {
    const auto checker = mimeChecker(false, "image/png", "image/png");
    // a 404 or a 500 with an accepted type passes; the status code is not tested in this unit
    for (const quint32 status : {200u, 404u, 500u, 0u}) {
        Out o;
        o.status = status;
        o.contentType = "image/png";
        EXPECT_FALSE(
            checker.afterReceiveData(requestOf(nullptr), "b", o.status, o.contentType, o.head).has_value());
        EXPECT_EQ(o.status, status);
        EXPECT_EQ(o.head, "HTTP/1.1 200 OK\r\n");
    }
    // a rejection reports the caller's status and head verbatim
    Out bad;
    bad.status = 302;
    bad.head = "HTTP/1.1 302 Found\r\nX: y\r\n";
    bad.contentType = "text/html";
    const auto r = checker.afterReceiveData(requestOf(nullptr), "b", bad.status, bad.contentType, bad.head);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->statusCode, 302u);
    EXPECT_EQ(r->rawResponseHeader, "HTTP/1.1 302 Found\r\nX: y\r\n");
}

// --- result kinds and the order of the gates
// -------------------------------------------------------------------

constexpr const char* kSample = "SAMPLE";

PredicateByBinaryData equalsSample() {
    return [](const QByteArray& d) { return d == kSample; };
}

TEST(DownloadCheckerStuped, BanAndEmptyResults) {
    const auto config = makeConfig(false, "image/png", "image/png");
    const DownloadCheckerStuped ban(nullptr, equalsSample(), {}, config, std::make_shared<FakeStorage>());
    Out o = run(ban, requestOf(nullptr), kSample, "image/png");
    ASSERT_TRUE(o.result.has_value());
    EXPECT_EQ(o.result->kind, ResultKind::Banned);
    EXPECT_EQ(o.result->statusCode, 200u);
    EXPECT_EQ(o.result->rawResponseHeader, "HTTP/1.1 200 OK\r\n");
    EXPECT_FALSE(run(ban, requestOf(nullptr), "OTHER", "image/png").result.has_value());

    const DownloadCheckerStuped empty(nullptr, {}, equalsSample(), config, std::make_shared<FakeStorage>());
    o = run(empty, requestOf(nullptr), kSample, "image/png");
    ASSERT_TRUE(o.result.has_value());
    EXPECT_EQ(o.result->kind, ResultKind::DataNotExists);
    EXPECT_EQ(o.result->text, u"Tile is recognized as empty"_s);
    EXPECT_FALSE(run(empty, requestOf(nullptr), "OTHER", "image/png").result.has_value());
}

TEST(DownloadCheckerStuped, TheGatesRunInLegacyOrder) {
    const auto config = makeConfig(false, "image/png", "image/png");
    auto store = std::make_shared<FakeStorage>();
    storage::TileInfo same;
    same.type = storage::TileInfoType::Exists;
    same.size = 6; // == strlen("SAMPLE")
    store->answer = same;
    const auto antiBan = std::make_shared<FakeAntiBan>();
    const maptype::ITileRequestPtr sizeCheck =
        std::make_shared<const TileRequestWithSizeCheck>(geo::PointI{1, 2}, quint8{3}, nullptr);
    const auto request = requestOf(sizeCheck);

    // content type first: a rejected type hides ban, empty and size
    DownloadCheckerStuped all(antiBan, equalsSample(), equalsSample(), config, store);
    Out o = run(all, request, kSample, "image/gif");
    EXPECT_EQ(o.result->kind, ResultKind::BadContentType);
    EXPECT_EQ(store->calls, 0);
    EXPECT_EQ(antiBan->postCalls, 0);

    // ban before empty (both predicates match)
    o = run(all, request, kSample, "image/png");
    EXPECT_EQ(o.result->kind, ResultKind::Banned);
    EXPECT_EQ(store->calls, 0);

    // empty before the size check
    const DownloadCheckerStuped emptyAndSize(antiBan, {}, equalsSample(), config, store);
    o = run(emptyAndSize, request, kSample, "image/png");
    EXPECT_EQ(o.result->kind, ResultKind::DataNotExists);
    EXPECT_EQ(store->calls, 0);

    // the size check before the anti-ban post check
    antiBan->post = net::result::loadErrorByUnknownReason(u"from antiban"_s);
    const DownloadCheckerStuped sizeAndBan(antiBan, {}, {}, config, store);
    o = run(sizeAndBan, request, kSample, "image/png");
    EXPECT_EQ(o.result->kind, ResultKind::NotNecessary);
    EXPECT_EQ(antiBan->postCalls, 0);

    // last: the anti-ban post check, with the body and the caller's variables
    o = run(sizeAndBan, request, "SAMPLE-DIFFERENT-SIZE", "image/png");
    ASSERT_TRUE(o.result.has_value());
    EXPECT_EQ(o.result->kind, ResultKind::UnknownError);
    EXPECT_EQ(o.result->text, u"from antiban"_s);
    EXPECT_EQ(antiBan->postCalls, 1);
    EXPECT_EQ(antiBan->postBody, "SAMPLE-DIFFERENT-SIZE");
    EXPECT_EQ(o.status, 599u);
    EXPECT_EQ(o.head, "rewritten");

    // an anti-ban that returns nothing lets the data through
    antiBan->post.reset();
    EXPECT_FALSE(run(sizeAndBan, request, "SAMPLE-DIFFERENT-SIZE", "image/png").result.has_value());
}

// --- the size check (:102-142)
// --------------------------------------------------------------------------------------

TEST(DownloadCheckerStuped, SizeCheckPath) {
    const auto config = makeConfig(false, "image/png", "image/png");
    const auto store = std::make_shared<FakeStorage>();
    const DownloadCheckerStuped checker(nullptr, {}, {}, config, store);
    const auto version = std::make_shared<const storage::MapVersionInfo>(u"v7"_s, core::HashValue{});
    const maptype::ITileRequestPtr withCheck =
        std::make_shared<const TileRequestWithSizeCheck>(geo::PointI{5, 7}, quint8{9}, version);
    const maptype::ITileRequestPtr plain =
        std::make_shared<const TileRequest>(geo::PointI{5, 7}, quint8{9}, version);

    storage::TileInfo exists;
    exists.type = storage::TileInfoType::Exists;
    exists.size = 4;
    store->answer = exists;

    // equal size: "not necessary"; the storage is asked for tile, zoom, version, WITH data (:123)
    Out o = run(checker, requestOf(withCheck), "abcd", "image/png");
    ASSERT_TRUE(o.result.has_value());
    EXPECT_EQ(o.result->kind, ResultKind::NotNecessary);
    EXPECT_EQ(o.result->text, u"Tile with same size exists"_s);
    EXPECT_EQ(o.result->statusCode, 200u);
    EXPECT_EQ(o.result->rawResponseHeader, "HTTP/1.1 200 OK\r\n");
    EXPECT_EQ(store->calls, 1);
    EXPECT_EQ(store->lastXy, (geo::PointI{5, 7}));
    EXPECT_EQ(store->lastZoom, 9);
    EXPECT_EQ(store->lastVersion, version);
    EXPECT_EQ(store->lastMode, storage::LoadMode::WithData);

    // other size: continues
    EXPECT_FALSE(run(checker, requestOf(withCheck), "abcde", "image/png").result.has_value());
    EXPECT_EQ(store->calls, 2);

    // a plain tile request, a request without a tile source: the storage is not asked
    EXPECT_FALSE(run(checker, requestOf(plain), "abcd", "image/png").result.has_value());
    EXPECT_FALSE(run(checker, requestOf(nullptr), "abcd", "image/png").result.has_value());
    EXPECT_EQ(store->calls, 2);

    // a missing tile and a TNE have size 0 (u_TileInfoBasic.pas:142-145, 237-240): an empty body "equals"
    // them
    storage::TileInfo missing;
    missing.type = storage::TileInfoType::NotExists;
    store->answer = missing;
    EXPECT_EQ(run(checker, requestOf(withCheck), QByteArray(), "image/png").result->kind,
              ResultKind::NotNecessary);
    EXPECT_FALSE(run(checker, requestOf(withCheck), "abcd", "image/png").result.has_value());
    storage::TileInfo tne;
    tne.type = storage::TileInfoType::TneExists;
    store->answer = tne;
    EXPECT_EQ(run(checker, requestOf(withCheck), QByteArray(), "image/png").result->kind,
              ResultKind::NotNecessary);

    // a storage error is an exception in legacy, caught by the downloader
    // (u_DownloaderHttpByCurl.pas:324-326) as BuildLoadErrorByUnknownReason(E.Message): returned, with the
    // error text
    store->answer = core::fail(core::ErrorCode::IoError, u"disk gone"_s);
    o = run(checker, requestOf(withCheck), QByteArray(), "image/png");
    ASSERT_TRUE(o.result.has_value());
    EXPECT_EQ(o.result->kind, ResultKind::UnknownError);
    EXPECT_EQ(o.result->text, u"disk gone"_s);

    // A nil answer (TileInfoType::Unknown; folder / SQLite single-file without read access, archive) leaves
    // CheckOldTileSize False (u_DownloadCheckerStuped.pas:135-141): no result, the checker continues
    // (the transport ends the empty 200 body, net/curl_downloader.cpp:148). The storage WAS asked, once.
    // Closes D-106. The mirrored row above: a NotExists answer of size 0 (BerkeleyDB, SQLite region, RAM)
    // is NotNecessary for an empty body.
    store->answer = storage::TileInfo{};
    const int callsBefore = store->calls;
    o = run(checker, requestOf(withCheck), QByteArray(), "image/png");
    EXPECT_FALSE(o.result.has_value());
    EXPECT_EQ(store->calls, callsBefore + 1);
    store->answer = missing;
    o = run(checker, requestOf(withCheck), QByteArray(), "image/png");
    ASSERT_TRUE(o.result.has_value());
    EXPECT_EQ(o.result->kind, ResultKind::NotNecessary);
    EXPECT_EQ(o.result->text, u"Tile with same size exists"_s);
}

// Port preconditions (legacy: access violations at :167 / :122 inside the downloader's `except`, unreachable
// from the only creator): a null config or a null storage is rejected by the constructor.
TEST(DownloadCheckerStuped, NullConfigOrStorageIsRejectedByTheConstructor) {
    const auto config = makeConfig(false, "image/png", "image/png");
    EXPECT_THROW(DownloadCheckerStuped(nullptr, {}, {}, nullptr, std::make_shared<FakeStorage>()),
                 std::invalid_argument);
    EXPECT_THROW(DownloadCheckerStuped(nullptr, {}, {}, config, nullptr), std::invalid_argument);
}

// --- BeforeRequest (:144-153)
// ----------------------------------------------------------------------------------------

TEST(DownloadCheckerStuped, BeforeRequestCallsAntiBanAndNeverCancels) {
    const auto config = makeConfig(false, "image/png", "image/png");
    const auto antiBan = std::make_shared<FakeAntiBan>();
    const DownloadCheckerStuped withAntiBan(antiBan, equalsSample(), equalsSample(), config,
                                            std::make_shared<FakeStorage>());
    net::DownloadRequest r;
    r.url = "http://example/1";
    EXPECT_FALSE(withAntiBan.beforeRequest(r).has_value());
    r.url = "http://example/2";
    EXPECT_FALSE(withAntiBan.beforeRequest(r).has_value());
    ASSERT_EQ(antiBan->preUrls.size(), 2u);
    EXPECT_EQ(antiBan->preUrls[0], "http://example/1");
    EXPECT_EQ(antiBan->preUrls[1], "http://example/2");

    const DownloadCheckerStuped withoutAntiBan(nullptr, {}, {}, config, std::make_shared<FakeStorage>());
    EXPECT_FALSE(withoutAntiBan.beforeRequest(r).has_value());
}

// --- the stock corpus
// -----------------------------------------------------------------------------------------------------

struct CorpusMap {
    QString name; // relative to sas.maps
    std::shared_ptr<const net::TileDownloaderSettings> downloader;
    std::vector<QByteArray> emptySamples;
    std::vector<QByteArray> banSamples;
};

struct Corpus {
    std::vector<CorpusMap> all;

    Corpus() {
        const maptype::test::ZmpEnv env;
        const QString root = maptype::test::fixtureDir() + u"/zmp/sas.maps"_s;
        QStringList errors;
        const auto set = env.loadAll(root, errors);
        EXPECT_TRUE(errors.isEmpty());
        for (const QUuid& guid : set->guids()) {
            const maptype::IZmpInfoPtr zmp = set->zmpByGuid(guid);
            CorpusMap m;
            m.name = QDir(root).relativeFilePath(zmp->fileName());
            m.downloader = zmp->tileDownloaderConfig();
            m.emptySamples = zmp->emptyTileSamples();
            m.banSamples = zmp->banTileSamples();
            all.push_back(std::move(m));
        }
    }

    DownloadCheckerStuped checkerOf(const CorpusMap& m) const {
        return DownloadCheckerStuped(
            nullptr,
            m.banSamples.empty() ? PredicateByBinaryData()
                                 : maptype::makePredicateByStaticSampleList(m.banSamples),
            m.emptySamples.empty() ? PredicateByBinaryData()
                                   : maptype::makePredicateByStaticSampleList(m.emptySamples),
            std::make_shared<net::TileDownloaderConfig>(std::make_shared<net::InetConfig>(), *m.downloader),
            std::make_shared<FakeStorage>());
    }
};

// The 514-map corpus, loaded once (the set and its environment are gone after the load: the maps keep
// copies).
const Corpus& stockCorpus() {
    static const Corpus corpus;
    return corpus;
}

// 514 stock maps; the EmptyTiles folders hold 49 files in 35 maps (find over the fixture), BanTiles folders
// none.
TEST(DownloadCheckerStuped, StockSamplesAreEmptyForTheirOwnMapAndForNoOtherUnlessSharedBytes) {
    CG_SKIP_IF_NO_CORPUS();
    const Corpus& corpus = stockCorpus();
    ASSERT_EQ(corpus.all.size(), 514u);
    int mapsWithSamples = 0;
    int samples = 0;
    int banSamples = 0;
    int pairs = 0;
    int shared = 0;
    for (const CorpusMap& a : corpus.all) {
        banSamples += static_cast<int>(a.banSamples.size());
        if (a.emptySamples.empty()) {
            continue;
        }
        ++mapsWithSamples;
        samples += static_cast<int>(a.emptySamples.size());
        // 1. own map: every sample is recognised as empty, with the ZMP's own content type settings
        const DownloadCheckerStuped own = corpus.checkerOf(a);
        for (const QByteArray& s : a.emptySamples) {
            SCOPED_TRACE(a.name.toStdString());
            const Out o = run(own, requestOf(nullptr), s, QByteArray());
            ASSERT_TRUE(o.result.has_value());
            EXPECT_EQ(o.result->kind, ResultKind::DataNotExists);
            EXPECT_EQ(o.result->text, u"Tile is recognized as empty"_s);
        }
        // 2. every other map: empty exactly when that map has the same bytes among its samples
        for (const CorpusMap& b : corpus.all) {
            if (&a == &b || b.emptySamples.empty()) {
                continue;
            }
            const DownloadCheckerStuped other = corpus.checkerOf(b);
            for (const QByteArray& s : a.emptySamples) {
                ++pairs;
                const bool sameBytes = std::ranges::find(b.emptySamples, s) != b.emptySamples.end();
                shared += sameBytes ? 1 : 0;
                const Out o = run(other, requestOf(nullptr), s, QByteArray());
                EXPECT_EQ(o.result.has_value(), sameBytes)
                    << a.name.toStdString() << " -> " << b.name.toStdString();
            }
        }
    }
    EXPECT_EQ(mapsWithSamples, 35);
    EXPECT_EQ(samples, 49);
    EXPECT_EQ(banSamples, 0); // the stock corpus has no BanTiles at all
    EXPECT_EQ(pairs, 1666);   // 49 samples x the 34 other maps with samples
    EXPECT_EQ(shared, 96);    // pairs whose bytes the other map also lists (script: md5 over the folders)
}

// The sample files counted straight from disk by the test (the loader test above expects the same 49).
TEST(DownloadCheckerStuped, StockSampleCountMatchesTheFilesOnDisk) {
    CG_SKIP_IF_NO_CORPUS();
    const QString root = maptype::test::fixtureDir() + u"/zmp/sas.maps"_s;
    int files = 0;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.contains(u"/EmptyTiles/"_s, Qt::CaseInsensitive) ||
            path.contains(u"/BanTiles/"_s, Qt::CaseInsensitive)) {
            ++files;
        }
    }
    EXPECT_EQ(files, 49);
}

// Banned: the stock corpus has no BanTiles, so the real sample files are used as ban list of a real map's
// settings (the code path is the same).
TEST(DownloadCheckerStuped, BanSamplesFromStockBytesGiveBanned) {
    CG_SKIP_IF_NO_CORPUS();
    const Corpus& corpus = stockCorpus();
    const CorpusMap* withSamples = nullptr;
    for (const CorpusMap& m : corpus.all) {
        if (!m.emptySamples.empty()) {
            withSamples = &m;
            break;
        }
    }
    ASSERT_NE(withSamples, nullptr);
    CorpusMap asBan = *withSamples;
    asBan.banSamples = withSamples->emptySamples;
    asBan.emptySamples.clear();
    const DownloadCheckerStuped checker = corpus.checkerOf(asBan);
    for (const QByteArray& s : asBan.banSamples) {
        const Out o = run(checker, requestOf(nullptr), s, QByteArray());
        ASSERT_TRUE(o.result.has_value());
        EXPECT_EQ(o.result->kind, ResultKind::Banned);
    }
    QByteArray changed = asBan.banSamples.front();
    changed[0] = static_cast<char>(changed[0] ^ 1);
    EXPECT_FALSE(run(checker, requestOf(nullptr), changed, QByteArray()).result.has_value());
}

// The MIME table from the stock ContentType / DefaultContentType values: each map's own ContentType text is a
// substring of itself (accepted), the ignore flag maps (5 in the corpus: IgnoreContentType=1) take any type
// and get their DefaultContentType written back.
TEST(DownloadCheckerStuped, StockContentTypeValues) {
    CG_SKIP_IF_NO_CORPUS();
    const Corpus& corpus = stockCorpus();
    int ignoring = 0;
    int stockPng8bit = 0;
    for (const CorpusMap& m : corpus.all) {
        SCOPED_TRACE(m.name.toStdString());
        const DownloadCheckerStuped checker = corpus.checkerOf(m);
        const QByteArray own = m.downloader->expectedMimeTypes;
        if (m.downloader->ignoreMimeType) {
            ++ignoring;
            const Out o = run(checker, requestOf(nullptr), "x", "text/html");
            EXPECT_FALSE(o.result.has_value());
            EXPECT_EQ(o.contentType, m.downloader->defaultMimeType);
            continue;
        }
        EXPECT_FALSE(run(checker, requestOf(nullptr), "x", own).result.has_value());
        // a type that no stock list contains is rejected
        EXPECT_EQ(run(checker, requestOf(nullptr), "x", "application/x-sas-none").result->kind,
                  ResultKind::BadContentType);
        if (own == "image/png8bit") {
            ++stockPng8bit;
            // LD-20 in the corpus: the server's "image/png" passes only because of the substring rule
            EXPECT_FALSE(run(checker, requestOf(nullptr), "x", "image/png").result.has_value());
        }
    }
    EXPECT_EQ(ignoring, 5);
    EXPECT_EQ(stockPng8bit, 7);
}

} // namespace
