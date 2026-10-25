// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/storage/sqlite/sqlite_file_tile_storage.h"

#include "cg/core/log.h"
#include "cg/storage/content_type_compat.h"
#include "cg/storage/sqlite/mbtiles_connection.h"
#include "cg/storage/sqlite/oruxmaps_connection.h"
#include "cg/storage/sqlite/rmaps_connection.h"

#include <mutex>
#include <utility>

namespace cg::storage::sqlite {

namespace {

// Ports GetDbNameById (Builder.pas:227-238).
QString dbNameById(SqliteFileFormat format) {
    switch (format) {
    case SqliteFileFormat::MbTiles:
        return QStringLiteral("MBTiles (SQLite3)");
    case SqliteFileFormat::OsmAnd:
        return QStringLiteral("OsmAnd (SQLite3)");
    case SqliteFileFormat::Locus:
        return QStringLiteral("Locus (SQLite3)");
    case SqliteFileFormat::RMaps:
        return QStringLiteral("RMaps (SQLite3)");
    case SqliteFileFormat::OruxMaps:
        return QStringLiteral("OruxMaps (SQLite3)");
    }
    return {};
}

TileInfo notExistsInfo() {
    TileInfo info;
    info.type = TileInfoType::NotExists;
    return info;
}

} // namespace

// Ports TTileStorageSQLiteFileConnectionBuilder (Create, MakeNewConnection).
class SqliteFileConnectionBuilder {
public:
    SqliteFileConnectionBuilder(QString fileName, std::shared_ptr<TileStorageBase> base,
                                core::ContentTypeInfo mainContentType, geo::ProjectionSetPtr projectionSet,
                                SqliteFileFormat format)
        : fileName_(std::move(fileName)), base_(std::move(base)),
          mainContentType_(std::move(mainContentType)), projectionSet_(std::move(projectionSet)),
          format_(format) {}

    // Legacy raises on every failure here and latches FIsFailed; the Result carries the message.
    [[nodiscard]] core::Result<std::unique_ptr<SqliteFileConnection>> makeNewConnection() {
        const std::scoped_lock guard(mutex_);
        core::Result<std::unique_ptr<SqliteFileConnection>> made = make();
        if (!made.has_value()) {
            failed_ = true;
        }
        return made;
    }

private:
    [[nodiscard]] core::Result<std::unique_ptr<SqliteFileConnection>> make() {
        if (failed_) {
            // disable access to the storage
            StorageStateHolder& state = base_->state();
            state.setReadAccess(false);
            state.setScanAccess(false);
            state.setAddAccess(false);
            state.setDeleteAccess(false);
            state.setReplaceAccess(false);
            return core::fail(core::ErrorCode::IoError,
                              QStringLiteral("Can't open file: \"%1\" as %2 database!")
                                  .arg(fileName_, dbNameById(format_)));
        }

        const StorageStatePtr current = base_->state().get();
        const bool isReadOnly = !current->addAccess && !current->replaceAccess && !current->deleteAccess;
        const core::ContentTypeRegistry& registry = core::ContentTypeRegistry::standard();

        std::unique_ptr<SqliteFileConnection> connection;
        switch (format_) {
        case SqliteFileFormat::MbTiles: {
            auto opened = MbtilesConnection::open(isReadOnly, fileName_, mainContentType_, registry,
                                                  projectionSet_, fileInfo_);
            if (!opened.has_value()) {
                return std::unexpected(opened.error());
            }
            connection = std::move(*opened);
            break;
        }
        case SqliteFileFormat::OsmAnd:
        case SqliteFileFormat::Locus:
        case SqliteFileFormat::RMaps: {
            auto opened = RmapsConnection::open(isReadOnly, fileName_, mainContentType_, registry,
                                                projectionSet_, fileInfo_, format_);
            if (!opened.has_value()) {
                return std::unexpected(opened.error());
            }
            connection = std::move(*opened);
            break;
        }
        case SqliteFileFormat::OruxMaps: {
            if (!isReadOnly) {
                return core::fail(core::ErrorCode::Unsupported,
                                  QStringLiteral("OruxMaps (SQLite3) does not support write access!"));
            }
            auto opened =
                OruxmapsConnection::open(fileName_, mainContentType_, registry, projectionSet_, fileInfo_);
            if (!opened.has_value()) {
                return std::unexpected(opened.error());
            }
            connection = std::move(*opened);
            break;
        }
        }

        if (fileInfo_ == nullptr) {
            fileInfo_ = connection->fileInfo();
        }
        return connection;
    }

    std::mutex mutex_; // ports FLock
    QString fileName_;
    std::shared_ptr<TileStorageBase> base_;
    core::ContentTypeInfo mainContentType_;
    geo::ProjectionSetPtr projectionSet_;
    SqliteFileFormat format_;
    SqliteFileInfoPtr fileInfo_;
    bool failed_ = false; // ports FIsFailed
};

namespace {

// Ports TEnumTileInfoBySQLiteFile. It owns one connection made lazily on the first next() and
// freed when the scan ends. Legacy lets MakeNewConnection / FetchNext raise out of Next(); here
// the error is stored, the scan ends (next() == false) and status() returns it (D-45).
class SqliteFileEnumerator final : public IEnumTileInfo {
public:
    SqliteFileEnumerator(std::shared_ptr<SqliteFileConnectionBuilder> builder,
                         std::shared_ptr<core::Mutex> lock)
        : builder_(std::move(builder)), lock_(std::move(lock)) {}

    [[nodiscard]] bool next(TileInfo& out, geo::PointI& tile, quint8& zoom) override {
        if (isDone_) {
            return false;
        }
        if (connection_ == nullptr) {
            auto made = builder_->makeNewConnection();
            if (!made.has_value()) {
                qCWarning(core::logStorage)
                    << "SqliteFileTileStorage scan: open failed:" << made.error().what();
                status_ = std::unexpected(made.error());
                isDone_ = true;
                return false;
            }
            connection_ = std::move(*made);
        }

        core::Result<bool> result = false;
        {
            core::ReadLock guard(*lock_);
            result = connection_->fetchNext(out, tile, zoom);
        }
        if (!result.has_value()) {
            qCWarning(core::logStorage)
                << "SqliteFileTileStorage scan: fetchNext failed:" << result.error().what();
            status_ = std::unexpected(result.error());
        }
        if (!result.has_value() || !*result) {
            isDone_ = true;
            connection_.reset();
            return false;
        }
        return true;
    }

    [[nodiscard]] core::Result<void> status() const override { return status_; }

private:
    core::Result<void> status_;
    std::shared_ptr<SqliteFileConnectionBuilder> builder_;
    std::shared_ptr<core::Mutex> lock_;
    std::unique_ptr<SqliteFileConnection> connection_;
    bool isDone_ = false;
};

} // namespace

SqliteFileTileStorage::SqliteFileTileStorage(
    TileStorageTypeAbilities typeAbilities, TileStorageAbilities resolvedAbilities,
    MapVersionFactoryPtr mapVersionFactory, geo::ProjectionSetPtr projectionSet,
    TilePyramidNotifierPtr tileNotifier, core::ContentTypeInfo mainContentType, const QString& path,
    SqliteFileFormat format, std::shared_ptr<TileInfoMemCache> memCache)
    : base_(std::make_shared<TileStorageBase>(typeAbilities, resolvedAbilities, std::move(mapVersionFactory),
                                              projectionSet, std::move(tileNotifier), QString())),
      mainContentType_(mainContentType), memCache_(std::move(memCache)),
      fileName_(prepareFileName(path, format)),
      builder_(std::make_shared<SqliteFileConnectionBuilder>(fileName_, base_, std::move(mainContentType),
                                                             std::move(projectionSet), format)),
      pool_([builder = builder_] { return builder->makeNewConnection(); }),
      lock_(std::make_shared<core::Mutex>()) {}

SqliteFileTileStorage::~SqliteFileTileStorage() = default;

TileStorageTypeAbilities SqliteFileTileStorage::typeAbilities() const {
    return base_->typeAbilities();
}

StorageStatePtr SqliteFileTileStorage::state() const {
    return base_->state().get();
}

core::Changeable& SqliteFileTileStorage::stateChangeable() {
    return base_->state();
}

const geo::IProjectionSet& SqliteFileTileStorage::projectionSet() const {
    return base_->projectionSet();
}

TilePyramidNotifier& SqliteFileTileStorage::tileNotifier() {
    return base_->tileNotifier();
}

QString SqliteFileTileStorage::tileFileName(geo::PointI, quint8, const MapVersionInfoPtr&) const {
    return fileName_;
}

// FEAT-STO-019, FEAT-STO-020, FEAT-STO-021 (read path of every flavour).
core::Result<TileInfo> SqliteFileTileStorage::tileInfo(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&,
                                                       LoadMode mode) const {
    // u_TileStorageSQLiteFile.pas:GetTileInfo checks ReadAccess BEFORE the mem-cache (the opposite
    // order to the folder storage); no access leaves Result = nil (:205-209), the port's Unknown.
    if (!base_->state().get()->readAccess) {
        return TileInfo{};
    }
    if (memCache_) {
        std::optional<TileInfo> cached = memCache_->get(xy, zoom, nullptr, mode, true);
        if (cached.has_value()) {
            return *cached;
        }
    }

    auto acquired = pool_.acquire();
    if (!acquired.has_value()) {
        return std::unexpected(acquired.error());
    }
    core::Result<TileInfo> result = notExistsInfo();
    {
        core::ReadLock guard(*lock_);
        result = (*acquired)->fetchOne(xy, zoom, mode);
    }
    pool_.release(std::move(*acquired));
    if (result.has_value() && memCache_) {
        memCache_->add(xy, zoom, nullptr, *result);
    }
    return result;
}

core::Result<TileInfo> SqliteFileTileStorage::tileInfoEx(geo::PointI xy, quint8 zoom,
                                                         const MapVersionRequest&, LoadMode mode) const {
    return tileInfo(xy, zoom, nullptr, mode);
}

core::Result<MapVersionList> SqliteFileTileStorage::listVersions(geo::PointI, quint8,
                                                                 const MapVersionRequest&) const {
    return MapVersionList{}; // FEAT-STO-032 default
}

core::Result<TileRectInfoPtr> SqliteFileTileStorage::tileRectInfo(geo::RectI rect, quint8 zoom,
                                                                  const MapVersionRequest&,
                                                                  std::stop_token stop) const {
    if (!base_->state().get()->readAccess) {
        return TileRectInfoPtr{};
    }

    geo::RectI validated = rect;
    base_->projectionSet().zoom(zoom)->validateTileRect(validated);
    const int width = validated.width();
    const int height = validated.height();
    // Legacy: outside these bounds the result stays nil (no error), and the ORIGINAL rect (not the
    // validated one) is what reaches FetchRectInfo.
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) {
        return TileRectInfoPtr{};
    }

    auto acquired = pool_.acquire();
    if (!acquired.has_value()) {
        return std::unexpected(acquired.error());
    }
    core::Result<TileRectInfoPtr> result = TileRectInfoPtr{};
    {
        core::ReadLock guard(*lock_);
        result = (*acquired)->fetchRectInfo(rect, zoom, stop);
    }
    pool_.release(std::move(*acquired));
    return result;
}

core::Result<bool> SqliteFileTileStorage::saveTile(geo::PointI xy, quint8 zoom,
                                                   const MapVersionInfoPtr& version, TileDate loadDate,
                                                   QByteArrayView contentType, const QByteArray& data,
                                                   bool overwrite) {
    if (overwrite) {
        if (!base_->state().get()->replaceAccess) {
            return false;
        }
    } else if (!base_->state().get()->addAccess) {
        return false;
    }

    if (!contentType.isEmpty() && !contentTypeCompatibleForSave(mainContentType_, contentType)) {
        return core::fail(core::ErrorCode::Unsupported,
                          QStringLiteral("Bad content type for this tile storage"));
    }

    if (contentType.isEmpty() || data.isNull()) {
        // TNE is not supported, all we can do here is delete existing tile (if any)
        return deleteTile(xy, zoom, version);
    }

    auto acquired = pool_.acquire();
    if (!acquired.has_value()) {
        return std::unexpected(acquired.error());
    }
    core::Result<bool> saved = false;
    {
        core::WriteLock guard(*lock_);
        saved = (*acquired)->insertOrUpdate(xy, zoom, data, overwrite);
    }
    pool_.release(std::move(*acquired));

    if (saved.has_value() && *saved) {
        if (memCache_) {
            TileInfo info;
            info.type = TileInfoType::Exists;
            info.loadDate = std::move(loadDate);
            info.size = static_cast<quint32>(data.size());
            info.data = data;
            info.contentType = mainContentType_.type;
            memCache_->add(xy, zoom, nullptr, std::move(info));
        }
        base_->notifyTileUpdate(xy, zoom, nullptr); // outside the lock -- spec §3.1 rule 3
    }
    return saved;
}

core::Result<bool> SqliteFileTileStorage::deleteTile(geo::PointI xy, quint8 zoom, const MapVersionInfoPtr&) {
    if (!base_->state().get()->deleteAccess) {
        return false;
    }

    auto acquired = pool_.acquire();
    if (!acquired.has_value()) {
        return std::unexpected(acquired.error());
    }
    core::Result<bool> deleted = false;
    {
        core::WriteLock guard(*lock_);
        deleted = (*acquired)->deleteTile(xy, zoom);
    }
    pool_.release(std::move(*acquired));

    if (deleted.has_value() && *deleted) {
        if (memCache_) {
            memCache_->add(xy, zoom, nullptr, notExistsInfo());
        }
        base_->notifyTileUpdate(xy, zoom, nullptr);
    }
    return deleted;
}

core::Result<EnumTileInfoPtr> SqliteFileTileStorage::scanTiles(bool, bool) {
    if (!base_->state().get()->scanAccess) {
        return EnumTileInfoPtr{};
    }
    return EnumTileInfoPtr{std::make_unique<SqliteFileEnumerator>(builder_, lock_)};
}

} // namespace cg::storage::sqlite
