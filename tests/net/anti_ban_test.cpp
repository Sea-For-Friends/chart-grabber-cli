// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-012 -- docs/specs/net.md §2.14, §7.13.
#include "cg/net/anti_ban.h"
#include "cg/net/download_request.h"
#include "cg/net/download_result.h"
#include "cg/net/downloader.h"
#include "cg/net/inet_config.h"

#include <QByteArray>

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>

using cg::net::DownloadRequest;
using cg::net::DownloadResult;
using cg::net::IDownloader;
using cg::net::InetSettings;
using cg::net::PreloadPageAntiBan;
using cg::net::ResultKind;

namespace {

// Records every doRequest() it is given; scripted to always return Ok. Stands in for the plain
// GET fetcher PreloadPageAntiBan::processAntiBan uses (spec [D-14]).
class RecordingDownloader final : public IDownloader {
public:
    DownloadResult doRequest(const DownloadRequest& request, std::stop_token /*stop*/) override {
        urls.append(request.url);
        DownloadResult result;
        result.kind = ResultKind::Ok;
        return result;
    }
    QList<QByteArray> urls;
};

} // namespace

TEST(AntiBanTest, DisabledWhenUsePreloadPageIsZero) {
    auto fetcher = std::make_shared<RecordingDownloader>();
    PreloadPageAntiBan antiBan(0, "http://preload/x", fetcher, [](const QByteArray&) {});
    DownloadRequest req;
    for (int i = 0; i < 5; ++i) {
        antiBan.preDownload(req);
    }
    EXPECT_TRUE(fetcher->urls.isEmpty());
}

TEST(AntiBanTest, DisabledWhenPreloadPageIsEmpty) {
    auto fetcher = std::make_shared<RecordingDownloader>();
    PreloadPageAntiBan antiBan(1, "", fetcher, [](const QByteArray&) {});
    DownloadRequest req;
    for (int i = 0; i < 5; ++i) {
        antiBan.preDownload(req);
    }
    EXPECT_TRUE(fetcher->urls.isEmpty());
}

TEST(AntiBanTest, UsePreloadPageOneFiresOnlyOnTheFirstTile) {
    // usePreloadPage == 1 -> act only when count == 1 (once per process), not every tile.
    auto fetcher = std::make_shared<RecordingDownloader>();
    PreloadPageAntiBan antiBan(1, "http://preload/x", fetcher, [](const QByteArray&) {});
    DownloadRequest req;
    for (int i = 0; i < 5; ++i) {
        antiBan.preDownload(req);
    }
    ASSERT_EQ(fetcher->urls.size(), 1);
    EXPECT_EQ(fetcher->urls.at(0), QByteArray("http://preload/x"));
}

TEST(AntiBanTest, UsePreloadPageNFiresEveryNth) {
    auto fetcher = std::make_shared<RecordingDownloader>();
    PreloadPageAntiBan antiBan(3, "http://preload/x", fetcher, [](const QByteArray&) {});
    DownloadRequest req;
    for (int i = 0; i < 9; ++i) {
        antiBan.preDownload(req); // counts 1..9 -> fires on 3, 6, 9
    }
    EXPECT_EQ(fetcher->urls.size(), 3);
}

TEST(AntiBanTest, PostCheckDownloadIsAlwaysNullopt) {
    // u_AntiBanStuped.pas:174 `if False then begin ... end` -- dead code in legacy; not restored.
    auto fetcher = std::make_shared<RecordingDownloader>();
    PreloadPageAntiBan antiBan(1, "http://preload/x", fetcher, [](const QByteArray&) {});
    DownloadRequest req;
    quint32 status = 200;
    QByteArray head = "X: y\r\n\r\n";
    QByteArray body = "tile-bytes";
    EXPECT_EQ(antiBan.postCheckDownload(req, body, status, head), std::nullopt);
    // Enabled or not, it is still a no-op.
    PreloadPageAntiBan disabled(0, "", fetcher, [](const QByteArray&) {});
    EXPECT_EQ(disabled.postCheckDownload(req, body, status, head), std::nullopt);
}
