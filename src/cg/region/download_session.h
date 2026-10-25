// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Download/i_DownloadSession.pas (IDownloadSession, :39-133),
//        Src/RegionProcess/Download/u_DownloadSession.pas (TDownloadSession, :41-139)
//
// The session record of one region download: map type and version objects, the zoom and its zoom
// list, the polygon, the replace / check flags and the progress counters. One pure interface and
// its one implementation in this header (brief 6a). Not thread-safe: the progress info of the
// download (RD-6) is its lock. save / load are declared here and ported by T-0165.

#include "cg/core/config_provider.h"
#include "cg/geo/geo_types.h"
#include "cg/maptype/map_type.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_date.h"

#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace cg::core {
class IConfigWriter;
} // namespace cg::core

namespace cg::maptype {
class IMapTypeSet;
} // namespace cg::maptype

namespace cg::net {
class IGlobalDownloadConfig;
} // namespace cg::net

namespace cg::geo {
class GeometryLonLatFactory;
class IGeometryLonLatPolygon;
} // namespace cg::geo

namespace cg::region {

using PolygonPtr = std::shared_ptr<const geo::IGeometryLonLatPolygon>;

class IDownloadSession {
public:
    virtual ~IDownloadSession() = default;

    [[nodiscard]] virtual maptype::IMapTypePtr mapType() const = 0;
    [[nodiscard]] virtual maptype::MapVersionRequestPtr versionForCheck() const = 0;
    [[nodiscard]] virtual storage::MapVersionInfoPtr versionForDownload() const = 0;
    [[nodiscard]] virtual quint8 zoom() const = 0;
    virtual void setZoom(quint8 value) = 0;
    [[nodiscard]] virtual std::vector<quint8> zoomArr() const = 0; // a copy, as legacy GetZoomArr
    [[nodiscard]] virtual PolygonPtr polygon() const = 0;
    [[nodiscard]] virtual bool secondLoadTne() const = 0;
    [[nodiscard]] virtual storage::TileDate replaceTneOlderDate() const = 0; // invalid = NaN = off
    [[nodiscard]] virtual bool replaceExistTiles() const = 0;
    [[nodiscard]] virtual bool checkExistTileSize() const = 0;
    [[nodiscard]] virtual bool checkExistTileDate() const = 0;
    [[nodiscard]] virtual storage::TileDate checkTileDate() const = 0;
    [[nodiscard]] virtual qint64 processed() const = 0;
    virtual void setProcessed(qint64 value) = 0;
    [[nodiscard]] virtual quint64 downloadedSize() const = 0;
    virtual void setDownloadedSize(quint64 value) = 0;
    [[nodiscard]] virtual qint64 downloadedCount() const = 0;
    virtual void setDownloadedCount(qint64 value) = 0;
    [[nodiscard]] virtual geo::PointI lastProcessedPoint() const = 0;
    virtual void setLastProcessedPoint(geo::PointI value) = 0;
    [[nodiscard]] virtual qint64 lastProcessedCount() const = 0;
    [[nodiscard]] virtual geo::PointI lastSuccessfulPoint() const = 0;
    virtual void setLastSuccessfulPoint(geo::PointI value) = 0;
    [[nodiscard]] virtual double elapsedTime() const = 0; // days (TDateTime)
    virtual void setElapsedTime(double value) = 0;
    [[nodiscard]] virtual bool autoCloseAtFinish() const = 0;
    virtual void setAutoCloseAtFinish(bool value) = 0;
    [[nodiscard]] virtual int workersCount() const = 0;
    [[nodiscard]] virtual int workerIndex() const = 0;
    [[nodiscard]] virtual QString autosavePrefix() const = 0;
    virtual void setAutosavePrefix(const QString& value) = 0;
    [[nodiscard]] virtual int autosaveInterval() const = 0; // minutes, 0 = off
    virtual void setAutosaveInterval(int value) = 0;
    virtual void save(core::IConfigWriter& sessionSection) = 0; // not const, as legacy Save
    virtual void load(const core::IConfigProviderPtr& sessionSection, const maptype::IMapTypeSet& fullMapsSet,
                      const net::IGlobalDownloadConfig& downloadConfig,
                      const geo::GeometryLonLatFactory& lonLatFactory) = 0;
};

class DownloadSession final : public IDownloadSession {
public:
    DownloadSession(); // Create (:155-159)
    DownloadSession(maptype::IMapTypePtr mapType, maptype::MapVersionRequestPtr versionForCheck,
                    storage::MapVersionInfoPtr versionForDownload, quint8 zoom, std::vector<quint8> zoomArr,
                    PolygonPtr polygon, bool secondLoadTne, storage::TileDate replaceTneOlderDate,
                    bool replaceExistTiles, bool checkExistTileSize, bool checkExistTileDate,
                    storage::TileDate checkTileDate, bool autoCloseAtFinish, int autoSaveInterval,
                    QString autosavePrefix, int workersCount, int workerIndex); // Create (:161-199)

    [[nodiscard]] maptype::IMapTypePtr mapType() const override;
    [[nodiscard]] maptype::MapVersionRequestPtr versionForCheck() const override;
    [[nodiscard]] storage::MapVersionInfoPtr versionForDownload() const override;
    [[nodiscard]] quint8 zoom() const override;
    void setZoom(quint8 value) override;
    [[nodiscard]] std::vector<quint8> zoomArr() const override;
    [[nodiscard]] PolygonPtr polygon() const override;
    [[nodiscard]] bool secondLoadTne() const override;
    [[nodiscard]] storage::TileDate replaceTneOlderDate() const override;
    [[nodiscard]] bool replaceExistTiles() const override;
    [[nodiscard]] bool checkExistTileSize() const override;
    [[nodiscard]] bool checkExistTileDate() const override;
    [[nodiscard]] storage::TileDate checkTileDate() const override;
    [[nodiscard]] qint64 processed() const override;
    void setProcessed(qint64 value) override;
    [[nodiscard]] quint64 downloadedSize() const override;
    void setDownloadedSize(quint64 value) override;
    [[nodiscard]] qint64 downloadedCount() const override;
    void setDownloadedCount(qint64 value) override;
    [[nodiscard]] geo::PointI lastProcessedPoint() const override;
    void setLastProcessedPoint(geo::PointI value) override;
    [[nodiscard]] qint64 lastProcessedCount() const override;
    [[nodiscard]] geo::PointI lastSuccessfulPoint() const override;
    void setLastSuccessfulPoint(geo::PointI value) override;
    [[nodiscard]] double elapsedTime() const override;
    void setElapsedTime(double value) override;
    [[nodiscard]] bool autoCloseAtFinish() const override;
    void setAutoCloseAtFinish(bool value) override;
    [[nodiscard]] int workersCount() const override;
    [[nodiscard]] int workerIndex() const override;
    [[nodiscard]] QString autosavePrefix() const override;
    void setAutosavePrefix(const QString& value) override;
    [[nodiscard]] int autosaveInterval() const override;
    void setAutosaveInterval(int value) override;
    void save(core::IConfigWriter& sessionSection) override; // placeholder until T-0165
    void load(const core::IConfigProviderPtr& sessionSection, const maptype::IMapTypeSet& fullMapsSet,
              const net::IGlobalDownloadConfig& downloadConfig,
              const geo::GeometryLonLatFactory& lonLatFactory) override; // placeholder until T-0165

private:
    // _InitSession (:201-228)
    void initSession();

    quint8 zoom_{};
    std::vector<quint8> zoomArr_;
    maptype::IMapTypePtr mapType_;
    PolygonPtr polygon_;
    maptype::MapVersionRequestPtr versionForCheck_;
    storage::MapVersionInfoPtr versionForDownload_;
    bool secondLoadTne_{};
    storage::TileDate replaceTneOlderDate_;
    bool replaceExistTiles_{};
    bool checkExistTileSize_{};
    bool checkExistTileDate_{};
    storage::TileDate checkTileDate_;
    double elapsedTime_{};
    qint64 processed_{};
    // Legacy FProcessedFromLastSuccessfulPoint: no getter in IDownloadSession, written by the setters.
    qint64 processedFromLastSuccessfulPoint_{};
    quint64 downloadedSize_{};
    qint64 downloadedCount_{};
    geo::PointI lastProcessedPoint_;
    qint64 lastProcessedCount_{};
    geo::PointI lastSuccessfulPoint_;
    bool autoCloseAtFinish_{};
    int autoSaveInterval_{};
    QString autosavePrefix_;
    int workersCount_{};
    int workerIndex_{};
};

} // namespace cg::region
