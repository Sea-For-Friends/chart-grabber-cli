// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Shared by the request-builder tests (T-0096): a scripted net::IDownloader, a counting proj-converter
// factory and a factory double for the lazy wrapper.

#include "cg/core/language_manager.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/tile_download_request_builder_factory_pascal_script.h"
#include "cg/maptype/tile_request.h"
#include "cg/net/download_checker.h"
#include "cg/net/downloader.h"
#include "cg/net/inet_config.h"
#include "cg/net/tile_downloader_config.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cg::maptype::test {

inline geo::ProjectionSetPtr googleProjectionSet() {
    static const auto datums = std::make_shared<const geo::DatumFactory>(geo::HashSeed{});
    static const geo::ProjectionSetFactory factory{geo::HashSeed{}, datums};
    return factory.byCode(geo::kProjEpsgGoogle, geo::kTileSplitQuadrate256x256);
}

inline net::DownloadResult okReply(quint32 status, const QByteArray& header = {},
                                   const QByteArray& data = {}) {
    net::DownloadResult r;
    r.kind = net::ResultKind::Ok;
    r.statusCode = status;
    r.rawResponseHeader = header;
    r.data = data;
    return r;
}

inline net::DownloadResult notFoundReply() {
    net::DownloadResult r;
    r.kind = net::ResultKind::DataNotExists;
    r.statusCode = 404;
    return r;
}

// Answers from a FIFO queue (an empty queue gives a Canceled result) and records every request. `onRequest`
// runs inside doRequest, before the answer: the hook the overlap tests use.
class ScriptedDownloader final : public net::IDownloader {
public:
    net::DownloadResult doRequest(const net::DownloadRequest& request, std::stop_token /*stop*/) override {
        std::function<void()> hook;
        {
            std::lock_guard guard(mutex_);
            requests.push_back(request);
            hook = onRequest;
        }
        if (hook) {
            hook();
        }
        std::lock_guard guard(mutex_);
        if (replies.empty()) {
            net::DownloadResult none;
            none.kind = net::ResultKind::Canceled;
            return none;
        }
        net::DownloadResult r = std::move(replies.front());
        replies.pop_front();
        return r;
    }

    std::mutex mutex_;
    std::deque<net::DownloadResult> replies;
    std::vector<net::DownloadRequest> requests;
    std::function<void()> onRequest;
};

// byInitString counts its callers and the largest number inside it at once.
class CountingProjFactory final : public script::IProjConverterFactory {
public:
    [[nodiscard]] std::shared_ptr<const script::IProjConverter> byEpsg(int /*epsg*/) const override {
        return nullptr;
    }
    [[nodiscard]] std::shared_ptr<const script::IProjConverter>
    byInitString(const QByteArray& args) const override {
        const int now = ++inside;
        int seen = maxInside.load();
        while (now > seen && !maxInside.compare_exchange_weak(seen, now)) {
        }
        ++calls;
        lastArgs = args;
        std::this_thread::yield();
        --inside;
        return nullptr;
    }

    mutable std::atomic<int> inside{0};
    mutable std::atomic<int> maxInside{0};
    mutable std::atomic<int> calls{0};
    mutable QByteArray lastArgs;
};

inline std::shared_ptr<TileDownloadRequestBuilderConfig>
makeBuilderConfig(const QByteArray& urlBase, const QByteArray& header = {}, bool useDownloader = false,
                  const QByteArray& projArgs = {}, const QByteArray& serverNames = {}) {
    TileDownloadRequestBuilderConfigStatic cfg;
    cfg.urlBase = urlBase;
    cfg.requestHeader = header;
    cfg.isUseDownloader = useDownloader;
    cfg.defaultProjConverterArgs = projArgs;
    cfg.serverNames = serverNames;
    return std::make_shared<TileDownloadRequestBuilderConfig>(
        std::make_shared<const TileDownloadRequestBuilderConfigStatic>(cfg));
}

inline std::shared_ptr<const TileRequest> tileRequest(int x, int y, int zoom) {
    return std::make_shared<const TileRequest>(QPoint(x, y), static_cast<quint8>(zoom), nullptr);
}

// A language manager whose current code can be set to anything; setting it notifies like the real one.
class FakeLanguageManager final : public core::ILanguageManager {
public:
    [[nodiscard]] QString currentLanguageCode() const override {
        std::lock_guard guard(mutex_);
        return code_;
    }
    void setCurrentLanguageCode(const QString& code) override {
        {
            std::lock_guard guard(mutex_);
            code_ = code;
        }
        changed().notify();
    }
    [[nodiscard]] int currentLanguageIndex() const override { return 0; }
    void setCurrentLanguageIndex(int /*index*/) override {}
    [[nodiscard]] core::ILanguageListStaticPtr languageList() const override { return nullptr; }

protected:
    void doReadConfig(const core::IConfigProviderPtr& /*src*/) override {}
    void doWriteConfig(const core::IConfigWriterPtr& /*dst*/) const override {}

private:
    mutable std::mutex mutex_;
    QString code_ = QStringLiteral("en");
};

// Counts the messages the message handler sees (the sandbox warnings go through qCWarning). Not nested.
class SandboxLogCapture {
public:
    SandboxLogCapture() { previous_ = qInstallMessageHandler(&SandboxLogCapture::handle); }
    ~SandboxLogCapture() {
        qInstallMessageHandler(previous_);
        const std::scoped_lock lock(mutex());
        messages().clear();
    }
    SandboxLogCapture(const SandboxLogCapture&) = delete;
    SandboxLogCapture& operator=(const SandboxLogCapture&) = delete;

    [[nodiscard]] static int countContaining(const QString& text) {
        const std::scoped_lock lock(mutex());
        int n = 0;
        for (const QString& m : messages()) {
            n += m.contains(text) ? 1 : 0;
        }
        return n;
    }

private:
    static std::mutex& mutex() {
        static std::mutex m;
        return m;
    }
    static QStringList& messages() {
        static QStringList l;
        return l;
    }
    static void handle(QtMsgType, const QMessageLogContext&, const QString& msg) {
        const std::scoped_lock lock(mutex());
        messages().append(msg);
    }
    QtMessageHandler previous_ = nullptr;
};

// Everything a factory needs besides the script and the config.
struct FactoryEnv {
    QTemporaryDir tmp;
    std::shared_ptr<FakeLanguageManager> lang{std::make_shared<FakeLanguageManager>()};
    std::shared_ptr<net::TileDownloaderConfig> downloaderConfig{std::make_shared<net::TileDownloaderConfig>(
        std::make_shared<net::InetConfig>(), net::TileDownloaderSettings{})};
    std::shared_ptr<CountingProjFactory> projFactory{std::make_shared<CountingProjFactory>()};
    geo::ProjectionSetPtr projectionSet{googleProjectionSet()};
    net::IDownloadCheckerPtr checker{std::make_shared<net::NullDownloadChecker>()};

    [[nodiscard]] std::shared_ptr<TileDownloadRequestBuilderFactoryPascalScript>
    factory(const QByteArray& script, std::shared_ptr<TileDownloadRequestBuilderConfig> config,
            const QString& zmpName = QStringLiteral("test.zmp")) {
        return std::make_shared<TileDownloadRequestBuilderFactoryPascalScript>(
            zmpName, script, std::move(config), downloaderConfig, projectionSet, checker, projFactory, lang,
            nullptr, std::make_shared<storage::MapVersionFactory>(),
            std::make_shared<const core::ContentTypeRegistry>(), 7, tmp.path());
    }
};

} // namespace cg::maptype::test
