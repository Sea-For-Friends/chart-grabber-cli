// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// T-0127: the scripted HTTP server of the net tests is a fixture, not a library; maptype's subsystem tests
// use it from this one translation unit (tools/nocmake_build.sh links a module's non-test .cpp files into its
// test binaries).
#include "../net/http_test_server.cpp"
