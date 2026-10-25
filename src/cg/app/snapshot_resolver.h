// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// New code (no legacy unit). Inherits: the fetch-once pattern of the stock Google/GoogleSat.zmp script
// (Downloader.DoHttpRequest, via net::simpleHttpRequest); applySnapshot mirrors the run-time version change
// of the map settings dialog (Src/MapType/frm_MapTypeEdit.pas:286, VersionRequestConfig.Version :=
// edtVersion.Text; Src/MapVersion/u_MapVersionRequestConfig.pas:95-127). docs/specs/weather-sources.md
// section 3.1, 3.4. Ports: nothing.

#include "cg/app/snapshot.h"
#include "cg/core/result.h"

#include <QString>

#include <stop_token>

namespace cg::maptype {
class IMapType;
}

namespace cg::app {

class AppContext;

// GET definition.indexUrl once with the map's inet settings and request header; status 200 and a non-empty
// body of at most 8 MiB are required, then parseSnapshotIndex. Errors: Cancelled when `stop` fired; IoError
// for the network texts of section 3.4; ParseError (the index text, prefixed `<url>: ` by
// parseSnapshotIndex's caller here) for a document that does not give a snapshot.
[[nodiscard]] core::Result<Snapshot> resolveSnapshot(const SnapshotDefinition& definition,
                                                     const maptype::IMapType& map, const AppContext& app,
                                                     const std::stop_token& stop);

// versionRequestConfig()->setVersion(version) and setShowOtherVersions(false). Never written back (the
// program never saves Maps.ini).
void applySnapshot(maptype::IMapType& map, const QString& version);

} // namespace cg::app
