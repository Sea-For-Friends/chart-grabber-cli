// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports i_TileRequestResult.pas, u_TileRequestResult.pas (no FEAT id: prerequisite of the download
// subsystem).
#include "cg/maptype/tile_request_result.h"

#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cg::maptype;
using namespace cg;
using namespace Qt::StringLiterals;

namespace {

ITileRequestPtr tileRequest(int x, int y, quint8 zoom) {
    return std::make_shared<const TileRequest>(geo::PointI{x, y}, zoom, nullptr);
}

net::DownloadRequest downloadRequestFor(const ITileRequestPtr& source, const char* url) {
    net::DownloadRequest r;
    r.url = url;
    r.source = source;
    return r;
}

net::DownloadResult resultOf(net::ResultKind kind, const QString& text, quint32 status) {
    net::DownloadResult r;
    r.kind = kind;
    r.text = text;
    r.statusCode = status;
    return r;
}

struct Row {
    const char* name;
    std::function<ITileRequestResultPtr(const ITileRequestPtr&)> make;
    bool canceled, ok, error, storageError, withRequest, withResult;
    QString errorText;
    const char* requestUrl;     // "" when WithDownloadRequest is absent
    net::ResultKind resultKind; // meaningful when withResult
    quint32 resultStatus;
};

template <class T> bool is(const ITileRequestResultPtr& r) {
    return dynamic_cast<const T*>(r.get()) != nullptr;
}

TEST(TileRequestResult, KindTable) {
    const std::vector<Row> rows = {
        {"CanceledBeforBuild",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultCanceledBeforBuildDownloadRequest>(t);
         },
         true,
         false,
         false,
         false,
         false,
         false,
         {},
         "",
         net::ResultKind::Ok,
         0},
        {"CanceledAfterBuild",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultCanceledAfterBuildDownloadRequest>(
                 downloadRequestFor(t, "http://a/1"));
         },
         true,
         false,
         false,
         false,
         true,
         false,
         {},
         "http://a/1",
         net::ResultKind::Ok,
         0},
        {"CanceledAfterDownload",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultCanceledAfterDownloadRequest>(
                 downloadRequestFor(t, "http://a/2"), resultOf(net::ResultKind::Canceled, {}, 11));
         },
         true,
         false,
         false,
         false,
         true,
         true,
         {},
         "http://a/2",
         net::ResultKind::Canceled,
         11},
        {"Ok",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultOk>(
                 downloadRequestFor(t, "http://a/3"), resultOf(net::ResultKind::DataNotExists, {}, 404));
         },
         false,
         true,
         false,
         false,
         true,
         true,
         {},
         "http://a/3",
         net::ResultKind::DataNotExists,
         404},
        {"ErrorBeforBuild",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultErrorBeforBuildDownloadRequest>(
                 t, u"Город не найден"_s);
         },
         false, false, true, false, false, false, u"Город не найден"_s, "", net::ResultKind::Ok, 0},
        {"ErrorAfterBuild",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultErrorAfterBuildDownloadRequest>(
                 downloadRequestFor(t, "http://a/4"), u"город"_s);
         },
         false, false, true, false, true, false, u"город"_s, "http://a/4", net::ResultKind::Ok, 0},
        {"DownloadError",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultDownloadError>(
                 downloadRequestFor(t, "http://a/5"), resultOf(net::ResultKind::Banned, u"Бан"_s, 403));
         },
         false, false, true, false, true, true, u"Download error: Бан"_s, "http://a/5",
         net::ResultKind::Banned, 403},
        {"ErrorAfterDownload",
         [](const ITileRequestPtr& t) {
             return std::make_shared<const TileRequestResultErrorAfterDownloadRequest>(
                 downloadRequestFor(t, "http://a/6"), resultOf(net::ResultKind::NotNecessary, u"x"_s, 304),
                 u"Unknown error"_s);
         },
         false, false, true, false, true, true, u"Unknown error"_s, "http://a/6",
         net::ResultKind::NotNecessary, 304},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.name);
        const ITileRequestPtr src = tileRequest(5, 7, 9);
        const ITileRequestResultPtr r = row.make(src);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->request(),
                  src); // the same object, whether given or reached through the download request
        EXPECT_EQ(is<ITileRequestResultCanceled>(r), row.canceled);
        EXPECT_EQ(is<ITileRequestResultOk>(r), row.ok);
        EXPECT_EQ(is<ITileRequestResultError>(r), row.error);
        EXPECT_FALSE(is<ITileRequestResultStorageError>(r)); // declared, implemented by no class
        EXPECT_EQ(is<ITileRequestResultWithDownloadRequest>(r), row.withRequest);
        EXPECT_EQ(is<ITileRequestResultWithDownloadResult>(r), row.withResult);
        if (const auto* e = dynamic_cast<const ITileRequestResultError*>(r.get())) {
            EXPECT_EQ(e->errorText(), row.errorText);
        }
        if (const auto* q = dynamic_cast<const ITileRequestResultWithDownloadRequest*>(r.get())) {
            EXPECT_EQ(q->downloadRequest().url, QByteArray(row.requestUrl));
            EXPECT_EQ(q->downloadRequest().source, src);
        }
        if (const auto* d = dynamic_cast<const ITileRequestResultWithDownloadResult*>(r.get())) {
            EXPECT_EQ(d->downloadResult().kind, row.resultKind);
            EXPECT_EQ(d->downloadResult().statusCode, row.resultStatus);
        }
    }
}

TEST(TileRequestResult, BeforeBuildKeepsANullRequest) {
    // legacy TTileRequestResult.Create stores whatever it is given (:135-139)
    const TileRequestResultCanceledBeforBuildDownloadRequest r(nullptr);
    EXPECT_EQ(r.request(), nullptr);
    const TileRequestResultErrorBeforBuildDownloadRequest e(nullptr, u"t"_s);
    EXPECT_EQ(e.request(), nullptr);
    EXPECT_EQ(e.errorText(), u"t"_s);
}

// `Request as ITileDownloadRequest` raises for a request that is not a tile request (:170, :184, :224, :257).
TEST(TileRequestResult, NotATileRequestThrowsInTheConstructorsThatCastLikeLegacy) {
    net::DownloadRequest plain; // source == nullptr
    const net::DownloadResult ok = resultOf(net::ResultKind::Ok, {}, 200);
    const net::DownloadResult bad = resultOf(net::ResultKind::UnknownError, u"e"_s, 0);
    EXPECT_THROW((TileRequestResultCanceledAfterDownloadRequest(plain, ok)), std::logic_error);
    EXPECT_THROW((TileRequestResultOk(plain, ok)), std::logic_error);
    EXPECT_THROW((TileRequestResultErrorAfterDownloadRequest(plain, ok, u"t"_s)), std::logic_error);
    EXPECT_THROW((TileRequestResultDownloadError(plain, bad)), std::logic_error);
    try {
        const TileRequestResultOk r(plain, ok);
        FAIL();
    } catch (const std::logic_error& e) {
        EXPECT_STREQ(e.what(), "Interface not supported");
    }
    // the two constructors that take an ITileDownloadRequest do not cast
    EXPECT_EQ(TileRequestResultCanceledAfterBuildDownloadRequest(plain).request(), nullptr);
    EXPECT_EQ(TileRequestResultErrorAfterBuildDownloadRequest(plain, u"t"_s).request(), nullptr);
}

// IDownloadResultError membership: which result kinds the DownloadError constructor accepts.
TEST(TileRequestResult, DownloadErrorAcceptsExactlyTheErrorKinds) {
    const ITileRequestPtr src = tileRequest(1, 2, 3);
    const std::vector<std::pair<net::ResultKind, bool>> kinds = {
        {net::ResultKind::Ok, false},
        {net::ResultKind::NotNecessary, false},
        {net::ResultKind::Canceled, false},
        {net::ResultKind::ProxyError, true},
        {net::ResultKind::Banned, true},
        {net::ResultKind::BadContentType, true},
        {net::ResultKind::BadContentLength, true},
        {net::ResultKind::BadContentEncoding, true},
        {net::ResultKind::DataNotExists, false},
        {net::ResultKind::LoadError, true},
        {net::ResultKind::NoConnectToServer, true},
        {net::ResultKind::UnknownError, true},
    };
    for (const auto& [kind, isError] : kinds) {
        SCOPED_TRACE(static_cast<int>(kind));
        EXPECT_EQ(isDownloadErrorKind(kind), isError);
        const net::DownloadRequest req = downloadRequestFor(src, "u");
        if (isError) {
            const TileRequestResultDownloadError r(req, resultOf(kind, u"т"_s, 1));
            EXPECT_EQ(r.errorText(), u"Download error: т"_s);
        } else {
            EXPECT_THROW((TileRequestResultDownloadError(req, resultOf(kind, u"т"_s, 1))),
                         std::invalid_argument);
        }
    }
    // both preconditions failing: the port's kind check comes first
    EXPECT_THROW((TileRequestResultDownloadError({}, resultOf(net::ResultKind::Ok, {}, 1))),
                 std::invalid_argument);
}

} // namespace
