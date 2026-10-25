// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Export/MBTiles/u_StorageExportToMBTiles.pas
//
// The MBTiles writer: the base class (:40-110, :173-399) and the Classic flavour (:114-131,
// :403-546). The TileMill flavour (:133-151, :550-757) follows (T-0171).
// docs/specs/region-export.md §2.5.

#include "cg/core/result.h"
#include "cg/geo/geo_types.h"
#include "cg/storage/sqlite/sqlite_db.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cg::region {

// Legacy TSQLiteStorageMBTilesBase; spec name kept, so the abstract-class rule is waived.
// NOLINTNEXTLINE(readability-identifier-naming)
class SqliteStorageMbtilesBase {
public:
    SqliteStorageMbtilesBase(QString exportPath, QString exportFileName, QString name, QString description,
                             QString attribution, bool isLayer, QString imgFormat, bool useXYZScheme,
                             bool forceDropTarget, bool replaceTiles);
    virtual ~SqliteStorageMbtilesBase();
    SqliteStorageMbtilesBase(const SqliteStorageMbtilesBase&) = delete;
    SqliteStorageMbtilesBase& operator=(const SqliteStorageMbtilesBase&) = delete;

    [[nodiscard]] virtual core::Result<void> add(geo::PointI tile, quint8 zoom, const QByteArray& data) = 0;
    [[nodiscard]] virtual core::Result<void> open(const geo::LonLatRect& lonLatRect,
                                                  std::span<const quint8> zooms) = 0;
    [[nodiscard]] virtual core::Result<void> close();
    [[nodiscard]] core::Result<void> commitAndBeginTran();

protected:
    [[nodiscard]] core::Result<void> readMetadata();
    [[nodiscard]] core::Result<void> writeMetadata(const std::vector<std::pair<QString, QString>>& rows);
    [[nodiscard]] QString boundsStr(const geo::LonLatRect& r) const;
    [[nodiscard]] QString centerStr(const geo::LonLatRect& r, quint8 minZoom) const;
    [[nodiscard]] geo::PointI tileXY(geo::PointI tile, quint8 zoom) const;
    [[nodiscard]] core::Result<void> openInternal(std::span<const QByteArrayView> tablesDdl,
                                                  bool deleteIfExists);

    QString name_;
    QString description_;
    QString attribution_;
    QString imgType_;
    QString imgFormat_;
    QString scheme_;
    bool useXYZScheme_;

    QString exportPath_;
    QString exportFileName_;

    bool forceDropTarget_;
    bool replaceTiles_;

    std::optional<storage::sqlite::SqliteDb> db_;

    int existingMinZoom_ = -1;
    int existingMaxZoom_ = -1;
    geo::LonLatRect existingBounds_;
};

class SqliteStorageMbtilesClassic final : public SqliteStorageMbtilesBase {
public:
    using SqliteStorageMbtilesBase::SqliteStorageMbtilesBase;

    [[nodiscard]] core::Result<void> add(geo::PointI tile, quint8 zoom, const QByteArray& data) override;
    [[nodiscard]] core::Result<void> open(const geo::LonLatRect& lonLatRect,
                                          std::span<const quint8> zooms) override;
    [[nodiscard]] core::Result<void> close() override;

private:
    std::optional<storage::sqlite::SqliteStmt> insertStmt_;
};

// Legacy TSQLiteStorageMBTilesTileMill (:133-151).
class SqliteStorageMbtilesTileMill final : public SqliteStorageMbtilesBase {
public:
    using SqliteStorageMbtilesBase::SqliteStorageMbtilesBase;

    [[nodiscard]] core::Result<void> add(geo::PointI tile, quint8 zoom, const QByteArray& data) override;
    [[nodiscard]] core::Result<void> open(const geo::LonLatRect& lonLatRect,
                                          std::span<const quint8> zooms) override;
    [[nodiscard]] core::Result<void> close() override;

private:
    std::optional<storage::sqlite::SqliteStmt> insertMapStmt_;
    std::optional<storage::sqlite::SqliteStmt> insertImagesStmt_;
};

} // namespace cg::region
