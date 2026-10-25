// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test doubles of T-0181 (tiles_download_test.cpp, provider_tiles_download_test.cpp).
// Why new doubles: the scripted subsystem and storage of thread_download_tiles_test.cpp live in that file's
// anonymous namespace and script single-worker timing cases; here every worker of a start shares one
// recording subsystem and one empty storage, and nothing is scripted.

#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_downloader_state.h"
#include "cg/maptype/tile_request_result.h"
#include "cg/storage/tile_storage.h"

#include <QString>

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cg::region::test {

// One request a worker made, in the order the subsystem saw it.
struct RequestRecord {
    geo::PointI tile;
    quint8 zoom = 0;
    QString version; // storeString of the version for download
    bool checkSize = false;
};

// Every getRequestTask is recorded; download() answers Ok with 5 bytes at once, on the calling thread.
// Thread-safe: workers call it concurrently, the test reads it from its own thread.
class RecordingSubsystem final : public maptype::ITileDownloadSubsystem {
public:
    static constexpr quint64 kTileBytes = 5;

    std::shared_ptr<maptype::TileDownloaderStateInternal> stateInternal =
        std::make_shared<maptype::TileDownloaderStateInternal>();

    maptype::ITileRequestTaskPtr getRequestTask(std::stop_token softCancel, std::stop_token cancel,
                                                maptype::ITileRequestTaskFinishNotifierPtr finishNotifier,
                                                geo::PointI tile, quint8 zoom,
                                                const storage::MapVersionInfoPtr& version,
                                                bool checkTileSize) override {
        {
            const std::lock_guard lock(mutex_);
            requests_.push_back(
                RequestRecord{tile, zoom, version ? version->storeString() : QString(), checkTileSize});
        }
        cv_.notify_all();
        auto request = std::make_shared<const maptype::TileRequest>(tile, zoom, version);
        return std::make_shared<maptype::TileRequestTask>(request, softCancel, cancel, finishNotifier);
    }

    std::optional<net::DownloadRequest> getRequest(geo::PointI, quint8,
                                                   const storage::MapVersionInfoPtr&) override {
        return std::nullopt;
    }

    void download(const maptype::ITileRequestTaskPtr& task) override {
        net::DownloadRequest request;
        request.source = task->tileRequest();
        net::DownloadResult result;
        result.kind = net::ResultKind::Ok;
        result.data = QByteArray(static_cast<qsizetype>(kTileBytes), 'x');
        result.contentType = "image/png";
        std::dynamic_pointer_cast<maptype::ITileRequestTaskInternal>(task)->setFinished(
            std::make_shared<maptype::TileRequestResultOk>(std::move(request), std::move(result)));
    }

    std::shared_ptr<maptype::ITileDownloaderStateChangeable> state() const override { return stateInternal; }

    std::vector<RequestRecord> requests() const {
        const std::lock_guard lock(mutex_);
        return requests_;
    }

    // True when at least n requests were seen before the bound ran out. A short bound is used to assert that
    // NOTHING arrives (it can only pass late, never fail on a correct implementation); a long one is only a
    // failure bound.
    bool waitForRequests(size_t n, std::chrono::milliseconds bound) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, bound, [&] { return requests_.size() >= n; });
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<RequestRecord> requests_;
};

// Answers "no tile" (type Unknown) unless a test sets infos before the first worker starts.
class EmptyStorage final : public storage::ITileStorage {
public:
    std::map<std::pair<int, int>, storage::TileInfo> infos; // set before start; read-only afterwards

    [[nodiscard]] core::Result<storage::TileInfo>
    tileInfoEx(geo::PointI xy, quint8, const storage::MapVersionRequest&, storage::LoadMode) const override {
        const auto it = infos.find({xy.x(), xy.y()});
        return it == infos.end() ? storage::TileInfo{} : it->second;
    }

    [[nodiscard]] storage::TileStorageTypeAbilities typeAbilities() const override { unused(); }
    [[nodiscard]] storage::StorageStatePtr state() const override { unused(); }
    [[nodiscard]] core::Changeable& stateChangeable() override { unused(); }
    [[nodiscard]] const geo::IProjectionSet& projectionSet() const override { unused(); }
    [[nodiscard]] storage::TilePyramidNotifier& tileNotifier() override { unused(); }
    [[nodiscard]] QString tileFileName(geo::PointI, quint8,
                                       const storage::MapVersionInfoPtr&) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::TileInfo>
    tileInfo(geo::PointI, quint8, const storage::MapVersionInfoPtr&, storage::LoadMode) const override {
        unused();
    }
    [[nodiscard]] core::Result<bool> deleteTile(geo::PointI, quint8,
                                                const storage::MapVersionInfoPtr&) override {
        unused();
    }
    [[nodiscard]] core::Result<bool> saveTile(geo::PointI, quint8, const storage::MapVersionInfoPtr&,
                                              storage::TileDate, QByteArrayView, const QByteArray&,
                                              bool) override {
        unused();
    }
    [[nodiscard]] core::Result<storage::MapVersionList>
    listVersions(geo::PointI, quint8, const storage::MapVersionRequest&) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::TileRectInfoPtr>
    tileRectInfo(geo::RectI, quint8, const storage::MapVersionRequest&, std::stop_token) const override {
        unused();
    }
    [[nodiscard]] core::Result<storage::EnumTileInfoPtr> scanTiles(bool, bool) override { unused(); }

private:
    [[noreturn]] static void unused() { throw std::logic_error("EmptyStorage: not used by the worker"); }
};

} // namespace cg::region::test
