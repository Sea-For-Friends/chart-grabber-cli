// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: the process-wide libcurl init/cleanup implied by Src/Network/Curl/u_CurlHttpClient.pas
// (legacy calls LibCurlInitialize([giAll], GDllName.Curl) once at startup). docs/specs/net.md
// §2.8.1. FEAT-NET-009.
//
// Must be called exactly once, from main(), BEFORE any worker thread starts, and cleaned up once
// after every worker has joined (docs/specs/net.md §2.8.1 rule 1). We link libcurl rather than
// dlopen it [D-7], so this is curl_global_init/cleanup directly, not a DLL load.

namespace cg::net {

void curlGlobalInit();
void curlGlobalCleanup();

[[nodiscard]] unsigned curlVersionNum(); // curl_version_info()->version_num

} // namespace cg::net
