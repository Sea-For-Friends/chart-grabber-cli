// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_DownloadResult.pas (14 interfaces), u_DownloadResult.pas (19 classes). docs/specs/net.md
// §2.4. FEAT-NET-013. Nineteen legacy classes, none carrying more than four extra fields and four
// of them discarding the code they are constructed with, collapse into one closed enum plus one
// struct per idioms.md §13.

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace cg::net {

// Ordered by the legacy Supports() precedence at the seven call sites that probe this hierarchy
// (u_ThreadDownloadTiles.pas:512-551 and others) -- the order is part of the contract.
enum class ResultKind : std::uint8_t {
    Ok,                 // TDownloadResultOk
    NotNecessary,       // TDownloadResultNotNecessary
    Canceled,           // TDownloadResultCanceled
    ProxyError,         // TDownloadResultProxyError
    Banned,             // TDownloadResultBanned
    BadContentType,     // TDownloadResultBadContentType
    BadContentLength,   // TDownloadResultBadContentLength
    BadContentEncoding, // TDownloadResultBadContentEncoding
    DataNotExists,      // TDownloadResultDataNotExists (+ByStatusCode, +ZeroSize)
    LoadError,          // TDownloadResultLoadError (+ByStatusCode/ByUnknownStatusCode/ByErrorCode)
    NoConnectToServer,  // TDownloadResultNoConnetctToServer (legacy spells it "Connetct")
    UnknownError,       // TDownloadResultUnknownError
};

// The retry driver -- NOT derivable from `kind` by intuition, transcribed verbatim from
// u_DownloadResult.pas. This single boolean decides whether the caller's 30-second backoff fires.
// Do NOT "improve" this: `Banned` is server-exists-true and must NOT retry or back off.
//
// | Kind               | isServerExists | Legacy line |
// |--------------------|-----------------|-------------|
// | Ok                 | true            | :347        |
// | NotNecessary        | true            | :621        |
// | Canceled            | false           | :681        |
// | ProxyError          | false           | :396        |
// | NoConnectToServer   | false           | :403        |
// | UnknownError        | false           | :410        |
// | Banned              | true            | :430        |
// | BadContentType      | true            | :497        |
// | BadContentLength    | true            | :460        |
// | BadContentEncoding  | true            | :715        |
// | DataNotExists       | true            | :578        |
// | LoadError           | true            | :550        |
[[nodiscard]] constexpr bool kindImpliesServerExists(ResultKind kind) noexcept {
    switch (kind) {
    case ResultKind::Canceled:
    case ResultKind::ProxyError:
    case ResultKind::NoConnectToServer:
    case ResultKind::UnknownError:
        return false;
    case ResultKind::Ok:
    case ResultKind::NotNecessary:
    case ResultKind::Banned:
    case ResultKind::BadContentType:
    case ResultKind::BadContentLength:
    case ResultKind::BadContentEncoding:
    case ResultKind::DataNotExists:
    case ResultKind::LoadError:
        return true;
    }
    return true; // unreachable: every enumerator is handled above.
}

struct DownloadResult {
    ResultKind kind = ResultKind::UnknownError;
    quint32 statusCode = 0;       // 0 when there was no server response
    QByteArray rawResponseHeader; // LAST response only, verbatim CRLF blob
    QByteArray contentType;       // Ok / BadContentType only
    QByteArray contentEncoding;   // BadContentEncoding only
    QByteArray data;              // Ok / BadContentEncoding only
    QString text;                 // ErrorText or ReasonText, already formatted

    [[nodiscard]] bool isServerExists() const noexcept { return kindImpliesServerExists(kind); }

    // True for the classes that declare IDownloadResultWithServerRespond in legacy: Ok, Banned,
    // BadContentLength, BadContentType, BadContentEncoding, DataNotExists, NotNecessary. This is
    // why TSimpleHttpDownloader reports status 0 for the others even when the HTTP status was in
    // fact known (u_SimpleHttpDownloader.pas:115-118). Do NOT "improve" this either.
    [[nodiscard]] bool hasServerRespond() const noexcept;
};

} // namespace cg::net
