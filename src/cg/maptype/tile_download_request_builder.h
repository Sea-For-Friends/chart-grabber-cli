// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_TileDownloadRequestBuilder.pas, u_TileDownloadRequestBuilder.pas
//
// The interface a download slot asks for the next request, and the abstract base that carries the
// builder's config and its lock. The cancel pair (INotifierOperation, OperationID) is a
// std::stop_token; a default-constructed token means "never cancelled".

#include "cg/maptype/tile_download_request_builder_config.h"
#include "cg/maptype/tile_request.h"
#include "cg/net/download_request.h"
#include "cg/net/download_stats.h"

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>

namespace cg::maptype {

class ITileDownloadRequestBuilder {
public:
    virtual ~ITileDownloadRequestBuilder() = default;
    // nullopt = "no request" (cancelled, builder disabled, or the tile does not exist).
    // May throw any std::exception (std::runtime_error for a script error, std::out_of_range 'List index out
    // of bounds (N)' for a bad zoom). The download job must catch it and turn it into "error before build",
    // as u_TileDownloaderSimple.pas:183-197 does.
    [[nodiscard]] virtual std::optional<net::DownloadRequest>
    buildRequest(const ITileRequestPtr& source,
                 const std::shared_ptr<net::LastResponseInfo>& lastResponseInfo,
                 const std::stop_token& cancel) = 0;
};
using ITileDownloadRequestBuilderPtr = std::shared_ptr<ITileDownloadRequestBuilder>;

// u_TileDownloadRequestBuilder.pas:37-54. FCS is a recursive critical section (:67, SyncBigRecursive).
// NOLINTNEXTLINE(readability-identifier-naming): abstract class with logic (spec 2.12.4)
class TileDownloadRequestBuilder : public ITileDownloadRequestBuilder {
protected:
    explicit TileDownloadRequestBuilder(std::shared_ptr<TileDownloadRequestBuilderConfig> config);

    [[nodiscard]] TileDownloadRequestBuilderConfig& config() const { return *config_; }
    void lock() { mutex_.lock(); }
    void unlock() { mutex_.unlock(); }

    // Lock()/try..finally Unlock() of the legacy BuildRequest.
    class Locker {
    public:
        explicit Locker(TileDownloadRequestBuilder& owner) : owner_(owner) { owner_.lock(); }
        ~Locker() { owner_.unlock(); }
        Locker(const Locker&) = delete;
        Locker& operator=(const Locker&) = delete;

    private:
        TileDownloadRequestBuilder& owner_;
    };

private:
    std::shared_ptr<TileDownloadRequestBuilderConfig> config_;
    std::recursive_mutex mutex_;
};

} // namespace cg::maptype
