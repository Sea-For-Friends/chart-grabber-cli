// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileRequest.pas, u_TileRequest.pas
//
// Immutable (tile, zoom, version) triple. TileRequestWithSizeCheck is a marker subtype: the
// download checker asks `dynamic_cast<const ITileRequestWithSizeCheck*>(request)`.
// net::DownloadRequest::source must always be stored from an ITileRequestPtr: the checker casts it back to
// const ITileRequest* before the dynamic_cast to ITileRequestWithSizeCheck.

#include "cg/geo/geo_types.h"
#include "cg/storage/map_version.h"

#include <QtGlobal>

#include <memory>

namespace cg::maptype {

class ITileRequest {
public:
    virtual ~ITileRequest() = default;
    [[nodiscard]] virtual geo::PointI tile() const = 0;
    [[nodiscard]] virtual quint8 zoom() const = 0;
    [[nodiscard]] virtual storage::MapVersionInfoPtr versionInfo() const = 0;
};
using ITileRequestPtr = std::shared_ptr<const ITileRequest>;

// i_TileRequest.pas:43-45: in legacy it descends from ITileRequest; here it is a pure marker
// (TileRequestWithSizeCheck derives from TileRequest), which is all Supports() ever tests.
class ITileRequestWithSizeCheck {
public:
    virtual ~ITileRequestWithSizeCheck() = default;
};

// net::DownloadRequest::source back to the request it was stored from (see the top of this file): the one
// place that casts. Null source gives a null pointer.
[[nodiscard]] inline ITileRequestPtr tileRequestFromSource(const std::shared_ptr<const void>& source) {
    return std::static_pointer_cast<const ITileRequest>(source);
}

class TileRequest : public ITileRequest {
public:
    TileRequest(geo::PointI tile, quint8 zoom, storage::MapVersionInfoPtr versionInfo);

    [[nodiscard]] geo::PointI tile() const override;
    [[nodiscard]] quint8 zoom() const override;
    [[nodiscard]] storage::MapVersionInfoPtr versionInfo() const override;

private:
    geo::PointI tile_;
    quint8 zoom_;
    storage::MapVersionInfoPtr versionInfo_;
};

class TileRequestWithSizeCheck final : public TileRequest, public ITileRequestWithSizeCheck {
public:
    using TileRequest::TileRequest;
};

} // namespace cg::maptype
