// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_HttpStatusChecker.pas:112-158 (IsOkStatus/IsDownloadErrorStatus/IsContentNotExistStatus
// only -- IsConnectError/IsDownloadError/IsSecurityError classify WinInet last-error codes, which
// have no libcurl equivalent and no port: ADR-0004 rules every curl-level failure is
// ResultKind::UnknownError, full stop). Status codes not in any of the three sets below --
// including 301/302, 304, 403 and 429 -- are LoadErrorByUnknownStatusCode; there is no
// not-modified result type in this taxonomy.

#include <QtGlobal>

namespace cg::net {

// {200,201,202,203,205,206} -- HTTP_STATUS_OK/CREATED/ACCEPTED/PARTIAL/RESET_CONTENT/
// PARTIAL_CONTENT. Notably excludes 204, which is ContentNotExist, not Ok.
[[nodiscard]] bool isOkStatus(quint32 statusCode) noexcept;

// {500,501,502,503,504} -- HTTP_STATUS_SERVER_ERROR/NOT_SUPPORTED/BAD_GATEWAY/SERVICE_UNAVAIL/
// GATEWAY_TIMEOUT.
[[nodiscard]] bool isDownloadErrorStatus(quint32 statusCode) noexcept;

// {204,400,404} -- HTTP_STATUS_NO_CONTENT/BAD_REQUEST/NOT_FOUND.
[[nodiscard]] bool isContentNotExistStatus(quint32 statusCode) noexcept;

} // namespace cg::net
