// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-013. docs/specs/net.md §7.2, §2.4 isServerExists table.
#include "cg/net/download_result.h"

#include <gtest/gtest.h>

using cg::net::DownloadResult;
using cg::net::ResultKind;

namespace {
DownloadResult withKind(ResultKind kind) {
    DownloadResult r;
    r.kind = kind;
    return r;
}
} // namespace

TEST(DownloadResult, OkIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::Ok).isServerExists());
}

TEST(DownloadResult, NotNecessaryIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::NotNecessary).isServerExists());
}

TEST(DownloadResult, CanceledIsNotServerExists) {
    EXPECT_FALSE(withKind(ResultKind::Canceled).isServerExists());
}

TEST(DownloadResult, ProxyErrorIsNotServerExists) {
    EXPECT_FALSE(withKind(ResultKind::ProxyError).isServerExists());
}

TEST(DownloadResult, BannedIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::Banned).isServerExists());
}

TEST(DownloadResult, BadContentTypeIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::BadContentType).isServerExists());
}

TEST(DownloadResult, BadContentLengthIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::BadContentLength).isServerExists());
}

TEST(DownloadResult, BadContentEncodingIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::BadContentEncoding).isServerExists());
}

TEST(DownloadResult, DataNotExistsIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::DataNotExists).isServerExists());
}

TEST(DownloadResult, LoadErrorIsServerExists) {
    EXPECT_TRUE(withKind(ResultKind::LoadError).isServerExists());
}

TEST(DownloadResult, NoConnectToServerIsNotServerExists) {
    EXPECT_FALSE(withKind(ResultKind::NoConnectToServer).isServerExists());
}

TEST(DownloadResult, UnknownErrorIsNotServerExists) {
    EXPECT_FALSE(withKind(ResultKind::UnknownError).isServerExists());
}

TEST(DownloadResult, HasServerRespondMatchesLegacyInterfaceSet) {
    // IDownloadResultWithServerRespond: Ok, Banned, BadContentLength, BadContentType,
    // BadContentEncoding, DataNotExists, NotNecessary. Canceled/ProxyError/NoConnectToServer/
    // UnknownError/LoadError do not implement it.
    for (ResultKind kind :
         {ResultKind::Ok, ResultKind::Banned, ResultKind::BadContentLength, ResultKind::BadContentType,
          ResultKind::BadContentEncoding, ResultKind::DataNotExists, ResultKind::NotNecessary}) {
        EXPECT_TRUE(withKind(kind).hasServerRespond()) << static_cast<int>(kind);
    }
    for (ResultKind kind : {ResultKind::Canceled, ResultKind::ProxyError, ResultKind::NoConnectToServer,
                            ResultKind::UnknownError, ResultKind::LoadError}) {
        EXPECT_FALSE(withKind(kind).hasServerRespond()) << static_cast<int>(kind);
    }
}

TEST(DownloadResult, BannedIsServerExistsTrueSoNoRetry) {
    // The single most dangerous value to get wrong: a ban must NOT trigger the retry loop's
    // 30-second backoff, which fires only while isServerExists() == false.
    const DownloadResult banned = withKind(ResultKind::Banned);
    ASSERT_TRUE(banned.isServerExists());
    EXPECT_TRUE(banned.hasServerRespond());
}
