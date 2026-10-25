// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// clang-format off
// New code (no legacy unit). Inherits: Src/u_GlobalState.pas:442-974, 1155-1210, 1280-1289 (construction order; see the table below)
// Ports: nothing.
//
// The composition root of the command-line program (docs/specs/app-cli.md section 2, option B): it builds only the
// objects the stage-0 chain needs, in the order of TGlobalState.Create / LoadConfig, and shuts them down in the
// order of SendTerminateToThreads. Everything else TGlobalState builds (marks, GPS, geocoders, browser, terrain,
// importers, ...) is not built (app-cli.md 2.3). The program is read-only towards its configuration: nothing here
// calls saveMainConfig or a writeConfig.
//
// Requires an existing QGuiApplication (a ZMP without an icon gets a drawn default icon, app-cli.md 2.6); it does
// not need a running event loop.
//
// Legacy lines ("L" = Src/u_GlobalState.pas unless a file is named), one row per object, in construction order:
//   #  object                          call in create()                                 legacy
//   2  paths                           core::AppPaths::detect                           L:471-482
//   3  main configuration              core::makeMainConfigProvider                     L:488-493
//   4  path elements                   core::PathConfig (base config/data, cache,       L:479-482; u_GlobalConfig.pas:225-231
//                                      Logs, Maps), readConfig PathToCache,
//                                      PathToLogs, PATHtoMAPS
//   5  hash seed                       geo::HashSeed                                    L:504-514
//   6  version factory                 storage::MapVersionFactory                       L:517-521
//   7  mark appearance factory         vector::AppearanceOfMarkFactory                  L:523-527
//   8  language manager                core::LanguageManager, readConfig View           L:531-541; u_GlobalConfig.pas:275
//   9  network configuration           net::InetConfig, net::GlobalDownloadConfig,      L:531-541; u_GlobalConfig.pas:277-281, 319-320
//                                      readConfig Internet
//  10  ZMP defaults                    maptype::ZmpConfig, readConfig ZmpDefaultParams  u_GlobalConfig.pas:332-333
//  11  background timer notifier       core::Notifier                                   L:549-553
//  12  app-closing signal              std::stop_source                                 L:600-605
//  13  geometry factories              geo::GeometryProjectedFactory,                   L:609-614
//                                      geo::GeometryLonLatFactory
//  14  PROJ factory                    geo::ProjConverterFactory + adapter              L:616
//  15  datum and projection sets       geo::DatumFactory, geo::ProjectionSetFactory     L:620-621
//  16  cache configuration             storage::GlobalCacheConfig; readConfig(main)     L:631; L:1172
//  17  download totals                 net::DownloadInfo                                L:632
//  18  Berkeley DB helper              (removed, T-0333)                                L:647
//  19  content types                   core::ContentTypeRegistry::standard(),           L:567-576, 690-700
//                                      bitmap::ContentTypeManagerBitmap,
//                                      maptype::ContentConverters
//  20  transport                       net::curlGlobalInit, makeSystemProxyResolver,    L:704-708
//                                      net::CurlDownloaderFactory
//  21  collector thread                core::GarbageCollectorThread (1000 ms)           L:735-741
//  22  ZMP set                         maptype::zmpFileNames, maptype::ZmpInfoSet       L:844-858
//  23  storage types                   storage::TileStorageTypeListSimple               L:958-966
//  24  map list (loadMaps())           maptype::MapTypesMainList + loadMaps             L:860, 883-892; L:1161-1195
//  25  tile iterator factory           geo::TileIteratorFactory                         RegionProcess/frm_RegionProcess.pas:413-414
// clang-format on

#include "cg/core/app_paths.h"
#include "cg/core/config_writer.h"
#include "cg/core/garbage_collector_thread.h"
#include "cg/core/language_manager.h"
#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/core/result.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/tile_count.h"
#include "cg/maptype/map_type_set.h"
#include "cg/net/global_download_config.h"
#include "cg/net/inet_config.h"
#include "cg/net/proxy_resolver.h"

#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <memory>
#include <stop_token>

namespace cg::net {
class DownloadInfo;
class IDownloaderFactory;
} // namespace cg::net

namespace cg::maptype {
class ContentConverters;
class MapTypesMainList;
class IZmpInfoSet;
class ZmpConfig;
} // namespace cg::maptype

namespace cg::storage {
class GlobalCacheConfig;
class MapVersionFactory;
class TileStorageTypeListSimple;
} // namespace cg::storage

namespace cg::geo {
class ProjectionSetFactory;
}

namespace cg::core {
class ContentTypeRegistry;
}

namespace cg::script {
class IProjConverterFactory;
}

namespace cg::app {

struct AppOptions {
    QStringList arguments;                    // for AppPaths::detect
    QString cacheDir, proxy;                  // empty = from configuration
    QStringList mapsDirs;                     // --maps, absolute, in order; empty = refused (T-0291)
    std::function<void(const QString&)> warn; // replaces the legacy error boxes
};

class AppContext final {
    struct PrivateKey { // only create() can build one: the constructor is public for std::make_unique only
        explicit PrivateKey() = default;
    };

public:
    // Objects 2 ... 23 and 25. Errors: paths cannot be created; configuration unreadable; unusable `proxy`.
    [[nodiscard]] static core::Result<std::unique_ptr<AppContext>> create(const AppOptions& options);
    ~AppContext(); // L:1280-1289, written out in the destructor body (app-cli.md 2.4)

    explicit AppContext(PrivateKey);
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    // Object 24. MapTypesNoMapsError and "no non-layer map" become ErrorCode::NotFound naming the maps
    // directory. FEAT-NET-001 (contributes; held open)
    [[nodiscard]] core::Result<void> loadMaps();
    void requestClose(); // fires appClosing; idempotent
    [[nodiscard]] std::stop_token appClosing() const;
    [[nodiscard]] maptype::IMapTypeSetPtr fullMapsSet() const;
    [[nodiscard]] std::shared_ptr<const geo::GeometryLonLatFactory> lonLatFactory() const;
    [[nodiscard]] std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory() const;
    [[nodiscard]] std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory() const;
    [[nodiscard]] std::shared_ptr<const net::IGlobalDownloadConfig> downloadConfig() const;
    [[nodiscard]] std::shared_ptr<net::DownloadInfo> downloadInfo() const;
    [[nodiscard]] const core::ILanguageManager& languages() const;
    [[nodiscard]] const core::AppPaths& paths() const;
    [[nodiscard]] const QStringList& mapsDirs() const; // the folders read, app-cli.md 1.6.1 (T-0291)
    [[nodiscard]] QString autoSaveDir() const;         // <dataDir>/AutoSave
    [[nodiscard]] QString snapshotDir() const;         // <dataDir>/Snapshots (T-0243)
    [[nodiscard]] std::shared_ptr<net::IDownloaderFactory> downloaderFactory() const; // T-0243
    [[nodiscard]] int zmpErrorCount() const;

private:
    void report(const QString& message); // options.warn and the counter
    void useFileCacheForRamMaps();       // UAT D-1 (app-cli.md 1.6)

    // Members in the order of construction in create(); the destructor body does the shutdown steps
    // explicitly.
    std::function<void(const QString&)> warn_;
    int zmpErrorCount_ = 0;
    QStringList mapsDirs_;         // the --maps folders read, deduplicated (T-0291)
    bool curlInitialized_ = false; // set by create() at object 20; the destructor cleans up only then
    core::AppPaths paths_;
    core::IConfigWriterPtr mainConfig_;
    core::PathConfigPtr mapsPath_;
    core::PathConfigPtr logsPath_;
    core::PathConfigPtr cachePath_;
    std::shared_ptr<storage::MapVersionFactory> versions_;
    std::shared_ptr<core::LanguageManager> languages_;
    std::shared_ptr<net::InetConfig> inet_;
    std::shared_ptr<net::GlobalDownloadConfig> download_;
    std::shared_ptr<maptype::ZmpConfig> zmpConfig_;
    core::Notifier gc_; // declared before everything a map holds
    std::stop_source appClosing_;
    std::shared_ptr<const geo::GeometryProjectedFactory> projectedFactory_;
    std::shared_ptr<const geo::GeometryLonLatFactory> lonLatFactory_;
    std::shared_ptr<const script::IProjConverterFactory> projFactory_;
    std::shared_ptr<const geo::ProjectionSetFactory> projectionSets_;
    std::shared_ptr<storage::GlobalCacheConfig> cacheConfig_;
    std::shared_ptr<net::DownloadInfo> downloadInfo_;
    std::shared_ptr<const core::ContentTypeRegistry> types_;
    std::shared_ptr<const maptype::ContentConverters> converters_;
    std::shared_ptr<net::IDownloaderFactory> transport_;
    std::unique_ptr<core::GarbageCollectorThread> gcThread_;
    std::shared_ptr<const maptype::IZmpInfoSet> zmpSet_;
    std::shared_ptr<storage::TileStorageTypeListSimple> storageTypes_;
    std::unique_ptr<maptype::MapTypesMainList> mapsList_;
    std::shared_ptr<const geo::TileIteratorFactory> tileIteratorFactory_;
};

} // namespace cg::app
