// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0193: the scripted HTTP server of the net tests is a fixture, not a library; the end-to-end test of the
// CLI uses it from this one translation unit (tools/nocmake_build.sh links a module's non-test .cpp files
// into its test binaries), exactly as tests/maptype/http_test_server_shared.cpp does (T-0127).
#include "../net/http_test_server.cpp"
