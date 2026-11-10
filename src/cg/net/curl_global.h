// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the process-wide libcurl init/cleanup implied by Src/Network/Curl/u_CurlHttpClient.pas
// (legacy calls LibCurlInitialize([giAll], GDllName.Curl) once at startup).
//
// Must be called exactly once, from main(), BEFORE any worker thread starts, and cleaned up once
// after every worker has joined. We link libcurl rather than dlopen it, so this is
// curl_global_init/cleanup directly, not a DLL load.

#include <QByteArray>

namespace cg::net {

void curlGlobalInit();
void curlGlobalCleanup();

[[nodiscard]] unsigned curlVersionNum(); // curl_version_info()->version_num
// The User-Agent of the 403 retry (no legacy counterpart): "curl/" + curl_version_info()->version, read once.
[[nodiscard]] QByteArray curlUserAgent();

} // namespace cg::net
