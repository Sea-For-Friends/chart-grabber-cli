// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/net/download_result.h"

namespace cg::net {

bool DownloadResult::hasServerRespond() const noexcept {
    switch (kind) {
    case ResultKind::Ok:
    case ResultKind::Banned:
    case ResultKind::BadContentLength:
    case ResultKind::BadContentType:
    case ResultKind::BadContentEncoding:
    case ResultKind::DataNotExists:
    case ResultKind::NotNecessary:
        return true;
    case ResultKind::Canceled:
    case ResultKind::ProxyError:
    case ResultKind::NoConnectToServer:
    case ResultKind::UnknownError:
    case ResultKind::LoadError:
        return false;
    }
    return false; // unreachable: every enumerator is handled above.
}

} // namespace cg::net
