// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_DownloadChecker.pas, u_DownloadCheckerStuped.pas (interface + a null implementation
// only -- the concrete checker depends on ITileStorage and ZMP sample lists and belongs to
// maptype). docs/specs/net.md §2.6. FEAT-NET-011, FEAT-NET-013.

#include "cg/net/download_request.h"
#include "cg/net/download_result.h"

#include <QByteArray>

#include <optional>

namespace cg::net {

// Return std::nullopt to continue the pipeline; a value terminates it. The out-params are
// rewritten IN PLACE (legacy `var` params) -- a checker may change the status code and content
// type the caller then sees. Gate order, observable and implemented by the concrete checker in
// maptype (u_DownloadCheckerStuped.pas:155-231):
//   1. Content-type gate: IgnoreMIMEType overwrites contentType with DefaultMIMEType (no
//      rejection); empty contentType also becomes DefaultMIMEType; otherwise a SUBSTRING search of
//      the received type inside the comma-separated ExpectedMIMETypes list rejects with
//      badContentType ("image/jp" matches "image/jpg,image/png" -- port literally).
//   2. Ban-tile sample gate -> banned.
//   3. Empty-tile sample gate -> dataNotExists("Tile is recognized as empty").
//   4. Same-size gate (only with a size-check source) -> notNecessary("Tile with same size
//      exists").
//   5. AntiBan post-check (a no-op in legacy, u_AntiBanStuped.pas:174).
class IDownloadChecker {
public:
    virtual ~IDownloadChecker() = default;

    // Legacy only calls AntiBan.PreDownload here and always returns nil.
    [[nodiscard]] virtual std::optional<DownloadResult>
    beforeRequest(const DownloadRequest& request) const = 0;

    [[nodiscard]] virtual std::optional<DownloadResult>
    afterReceiveData(const DownloadRequest& request, const QByteArray& body, quint32& statusCode,
                     QByteArray& contentType, QByteArray& responseHead) const = 0;
};

// A checker that never terminates the pipeline. Ships with net for tests and for callers that
// need no gates at all.
class NullDownloadChecker final : public IDownloadChecker {
public:
    [[nodiscard]] std::optional<DownloadResult>
    beforeRequest(const DownloadRequest& /*request*/) const override {
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DownloadResult>
    afterReceiveData(const DownloadRequest& /*request*/, const QByteArray& /*body*/, quint32& /*statusCode*/,
                     QByteArray& /*contentType*/, QByteArray& /*responseHead*/) const override {
        return std::nullopt;
    }
};

} // namespace cg::net
