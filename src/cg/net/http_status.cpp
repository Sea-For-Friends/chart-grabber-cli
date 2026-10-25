// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/net/http_status.h"

namespace cg::net {

bool isOkStatus(quint32 statusCode) noexcept {
    switch (statusCode) {
    case 200:
    case 201:
    case 202:
    case 203:
    case 205:
    case 206:
        return true;
    default:
        return false;
    }
}

bool isDownloadErrorStatus(quint32 statusCode) noexcept {
    switch (statusCode) {
    case 500:
    case 501:
    case 502:
    case 503:
    case 504:
        return true;
    default:
        return false;
    }
}

bool isContentNotExistStatus(quint32 statusCode) noexcept {
    switch (statusCode) {
    case 204:
    case 400:
    case 404:
        return true;
    default:
        return false;
    }
}

} // namespace cg::net
