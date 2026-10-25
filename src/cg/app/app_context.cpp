// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/app/app_context.h"

#include "cg/app/log_app.h"
#include "cg/app/merged_zmp_info_set.h"
#include "cg/app/proj_converter_factory_adapter.h"
#include "cg/bitmap/bitmap32_buffer_factory_simple.h"
#include "cg/bitmap/bitmap32_static_factory.h"
#include "cg/bitmap/bitmap_tile_save_load_factory.h"
#include "cg/bitmap/content_type_manager_bitmap.h"
#include "cg/core/config_provider_ini.h"
#include "cg/core/content_type.h"
#include "cg/core/main_config.h"
#include "cg/geo/datum.h"
#include "cg/geo/hash_seed.h"
#include "cg/geo/proj_converter.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/content_converters.h"
#include "cg/maptype/map_types_main_list.h"
#include "cg/maptype/zmp_config.h"
#include "cg/maptype/zmp_file_names_iterator_factory.h"
#include "cg/maptype/zmp_info_set.h"
#include "cg/net/curl_downloader.h"
#include "cg/net/curl_global.h"
#include "cg/net/download_stats.h"
#include "cg/storage/cache_type.h"
#include "cg/storage/global_cache_config.h"
#include "cg/storage/map_version.h"
#include "cg/storage/tile_storage_type_list_simple.h"
#include "cg/vector/appearance_of_mark_factory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(AppContext)
};

// --proxy URL (app-cli.md 1.3): scheme http / https / socks4 / socks4a / socks5 / socks5h (no scheme = http),
// host, optional port, optional user:password. The type, address and login go into the proxy element of the
// internet configuration, in place of what the configuration file said.
core::Result<void> applyProxy(net::InetConfig& inet, const QString& text) {
    const QString trimmed = text.trimmed();
    const QUrl url(trimmed.contains(QStringLiteral("://")) ? trimmed : QStringLiteral("http://") + trimmed);
    net::ProxyType type = net::ProxyType::Http;
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http")) {
        type = net::ProxyType::Http;
    } else if (scheme == QStringLiteral("https")) {
        type = net::ProxyType::Https;
    } else if (scheme == QStringLiteral("socks4")) {
        type = net::ProxyType::Socks4;
    } else if (scheme == QStringLiteral("socks4a")) {
        type = net::ProxyType::Socks4a;
    } else if (scheme == QStringLiteral("socks5")) {
        type = net::ProxyType::Socks5;
    } else if (scheme == QStringLiteral("socks5h")) {
        type = net::ProxyType::Socks5h;
    } else {
        return core::fail(core::ErrorCode::ParseError,
                          QStringLiteral("--proxy: unknown scheme in '%1'").arg(text));
    }
    if (!url.isValid() || url.host().isEmpty()) {
        return core::fail(core::ErrorCode::ParseError, QStringLiteral("--proxy: no host in '%1'").arg(text));
    }
    QString host = url.host();
    if (host.contains(u':')) {
        host = u'[' + host + u']'; // IPv6 literal
    }
    if (url.port() >= 0) {
        host += u':' + QString::number(url.port());
    }
    net::ProxyConfig& proxy = inet.proxy();
    proxy.setUseSystemSettings(false);
    proxy.setUseProxy(true);
    proxy.setType(type);
    proxy.setHost(net::ProxyConfig::normalizeAddress(host.toUtf8()));
    if (!url.userName().isEmpty()) {
        proxy.setUseLogin(true);
        proxy.setLogin(url.userName());
        proxy.setPassword(url.password());
    }
    return {};
}

} // namespace

AppContext::AppContext(PrivateKey /*key*/) {}

core::Result<std::unique_ptr<AppContext>> AppContext::create(const AppOptions& options) {
    // app-cli.md 1.6.1 (T-0291): checked first, before any directory is created or any file is read
    if (options.mapsDirs.isEmpty()) {
        return core::fail(core::ErrorCode::NotFound,
                          Tr::tr("no map sources given: pass --maps DIR, a folder of map-source definitions "
                                 "(repeat --maps for more folders)"));
    }
    std::unique_ptr<AppContext> ctx = std::make_unique<AppContext>(PrivateKey{});
    AppContext& c = *ctx;
    c.warn_ = options.warn;

    // u_GlobalState.pas:471-482 -- paths. The program directory, config directory and data directory are the
    // three TPathConfig roots of legacy; the application directory is used as a plain string below (language
    // directory), no path element is built for it.
    core::Result<core::AppPaths> paths = core::AppPaths::detect(options.arguments);
    if (!paths) {
        return std::unexpected(paths.error());
    }
    c.paths_ = paths.value();
    const core::PathConfigPtr baseConfigPath =
        core::PathConfig::create(QString(), c.paths_.configDir, nullptr);
    const core::PathConfigPtr baseDataPath = core::PathConfig::create(QString(), c.paths_.dataDir, nullptr);
    // u_GlobalState.pas:482
    c.cachePath_ =
        core::PathConfig::create(QStringLiteral("PrimaryPath"), QStringLiteral(".\\"), baseDataPath);

    // u_GlobalState.pas:488-493 -- main configuration (read only; a missing file is an empty one, not
    // created)
    core::Result<core::IConfigWriterPtr> main = core::makeMainConfigProvider(c.paths_);
    if (!main) {
        return std::unexpected(main.error());
    }
    c.mainConfig_ = main.value();

    // u_GlobalState.pas:504-514 -- hash seed
    const geo::HashSeed hash;
    // u_GlobalState.pas:517-521 -- version factory
    c.versions_ = std::make_shared<storage::MapVersionFactory>();
    // u_GlobalState.pas:523-527 -- mark appearance factory (argument of ZmpInfoSet only)
    const vector::AppearanceOfMarkFactory appearanceOfMarkFactory;

    // u_GlobalState.pas:531-541 -- TGlobalConfig.Create and ReadConfig; the elements in the order of
    // u_GlobalConfig.pas:225-332, each read from its sub-item of the main configuration
    c.logsPath_ = core::PathConfig::create(QStringLiteral("PrimaryPath"), QStringLiteral(".\\Logs"),
                                           baseDataPath); // u_GlobalConfig.pas:227
    c.mapsPath_ = core::PathConfig::create(QStringLiteral("PrimaryPath"), QStringLiteral(".\\Maps"),
                                           baseConfigPath); // u_GlobalConfig.pas:230
    c.languages_ = std::make_shared<core::LanguageManager>(
        QDir(c.paths_.applicationDir).filePath(QStringLiteral("lang"))); // u_GlobalConfig.pas:275
    c.inet_ = std::make_shared<net::InetConfig>();                       // u_GlobalConfig.pas:277
    c.download_ = std::make_shared<net::GlobalDownloadConfig>();         // u_GlobalConfig.pas:319
    c.zmpConfig_ = std::make_shared<maptype::ZmpConfig>();               // u_GlobalConfig.pas:332
    c.cachePath_->readConfig(c.mainConfig_->subItem(u"PathToCache"));
    c.logsPath_->readConfig(c.mainConfig_->subItem(u"PathToLogs"));
    c.mapsPath_->readConfig(c.mainConfig_->subItem(u"PATHtoMAPS"));
    c.languages_->readConfig(c.mainConfig_->subItem(u"View"));
    c.inet_->readConfig(c.mainConfig_->subItem(u"Internet"));
    c.download_->readConfig(c.mainConfig_->subItem(u"Internet"));
    c.zmpConfig_->readConfig(c.mainConfig_->subItem(u"ZmpDefaultParams"));
    // --cache and --proxy replace what the configuration said (--maps: see the ZMP set below)
    if (!options.cacheDir.isEmpty()) {
        c.cachePath_->setPath(QDir(options.cacheDir).absolutePath());
    }
    if (!options.proxy.isEmpty()) {
        if (core::Result<void> applied = applyProxy(*c.inet_, options.proxy); !applied) {
            return std::unexpected(applied.error());
        }
    }

    // u_GlobalState.pas:549-553 -- background timer notifier: the member gc_ (declared before everything a
    // map holds) u_GlobalState.pas:600-605 -- app-closing signal: the member appClosing_

    // u_GlobalState.pas:609-614 -- geometry factories
    c.projectedFactory_ = std::make_shared<const geo::GeometryProjectedFactory>();
    c.lonLatFactory_ = std::make_shared<const geo::GeometryLonLatFactory>(hash);
    // u_GlobalState.pas:616 -- PROJ factory
    c.projFactory_ = std::make_shared<const ProjConverterFactoryAdapter>(
        std::make_shared<const geo::ProjConverterFactory>());
    // u_GlobalState.pas:620-621 -- datum and projection sets
    const auto datums = std::make_shared<const geo::DatumFactory>(hash);
    c.projectionSets_ = std::make_shared<const geo::ProjectionSetFactory>(hash, datums);
    // u_GlobalState.pas:631 -- cache configuration (readConfig(main) is u_GlobalState.pas:1172, below)
    c.cacheConfig_ = std::make_shared<storage::GlobalCacheConfig>(c.cachePath_);
    // u_GlobalState.pas:632 -- download totals
    c.downloadInfo_ = std::make_shared<net::DownloadInfo>();
    // u_GlobalState.pas:647 -- Berkeley DB helper: removed, T-0333 (owner 2026-10-21)

    // u_GlobalState.pas:567-576, 690-700 -- content types. The bitmap half is built from its codec factory;
    // the buffer and static factories (u_GlobalState.pas:554-563, app-cli.md 2.3) exist only as its
    // arguments.
    c.types_ = std::shared_ptr<const core::ContentTypeRegistry>(std::shared_ptr<void>(),
                                                                &core::ContentTypeRegistry::standard());
    const auto bitmapTypes = std::make_shared<const bitmap::ContentTypeManagerBitmap>(
        std::make_shared<const bitmap::BitmapTileSaveLoadFactory>(
            std::make_shared<const bitmap::Bitmap32StaticFactory>(
                std::make_shared<bitmap::Bitmap32BufferFactorySimple>())));
    c.converters_ = std::make_shared<const maptype::ContentConverters>(*c.types_, bitmapTypes);

    // u_GlobalState.pas:704-708 -- transport (no credential prompt is installed)
    net::curlGlobalInit();
    c.curlInitialized_ = true;
    c.transport_ = std::make_shared<net::CurlDownloaderFactory>(*c.types_, net::makeSystemProxyResolver());

    // u_GlobalState.pas:735-741 -- collector thread (interval: SleepByClass default 1000)
    c.gcThread_ = std::make_unique<core::GarbageCollectorThread>(c.gc_, std::chrono::milliseconds(1000));

    // u_GlobalState.pas:844-858 -- ZMP set; the mark picture list is null
    // app-cli.md 1.6.1 (T-0291): only the --maps folders; legacy reads one configured directory
    // (u_GlobalConfig.pas:230-231); D row of T-0291
    const auto showError = [&c](const QString& message) { c.report(message); };
    // the same folder twice counts once, at the position of its LAST occurrence
    const auto folderKey = [](const QString& dir) {
        const QFileInfo info(dir);
        return info.exists() ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
    };
    for (qsizetype i = 0; i < options.mapsDirs.size(); ++i) {
        bool later = false;
        for (qsizetype j = i + 1; j < options.mapsDirs.size() && !later; ++j) {
            later = folderKey(options.mapsDirs[i]) == folderKey(options.mapsDirs[j]);
        }
        if (!later) {
            c.mapsDirs_.push_back(options.mapsDirs[i]);
        }
    }
    std::vector<std::unique_ptr<maptype::ZmpInfoSet>> sets;
    std::vector<MergedZmpInfoSet::Folder> folders;
    for (const QString& dir : std::as_const(c.mapsDirs_)) {
        const QFileInfo info(dir);
        if (info.exists() && !info.isDir()) {
            return core::fail(core::ErrorCode::NotFound, Tr::tr("--maps: %1 is not a directory").arg(dir));
        }
        sets.push_back(std::make_unique<maptype::ZmpInfoSet>(
            *c.zmpConfig_, *c.languages_, *c.projectionSets_, *c.types_, appearanceOfMarkFactory, nullptr,
            dir, info.isDir() ? maptype::zmpFileNames(dir) : QStringList(), showError));
        folders.push_back({dir, sets.back().get()});
    }
    // through warn_, not report: a replaced definition is not a load error and is not counted
    c.zmpSet_ = std::make_shared<const MergedZmpInfoSet>(
        folders, [&c](const QUuid& guid, const QString& winnerPath, const QStringList& replacedPaths) {
            const QString line = Tr::tr("map source %1: %2 replaces %3")
                                     .arg(guid.toString(QUuid::WithBraces).toUpper(), winnerPath,
                                          replacedPaths.join(QStringLiteral(", ")));
            if (c.warn_) {
                c.warn_(line);
            } else {
                qCWarning(logApp) << line;
            }
        });
    c.mapsPath_->setPath(c.mapsDirs_.back());

    // u_GlobalState.pas:958-966 -- storage types
    c.storageTypes_ =
        std::make_shared<storage::TileStorageTypeListSimple>(c.versions_, *c.cacheConfig_, c.gc_);

    // frm_RegionProcess.pas:413-414 -- tile iterator factory (built in a form in legacy)
    c.tileIteratorFactory_ = std::make_shared<const geo::TileIteratorFactory>(c.projectedFactory_);

    // u_GlobalState.pas:1161-1162 (LoadConfig) creates the configured maps directory; not done: only --maps
    // folders are read (D row of T-0291)
    // u_GlobalState.pas:1172 (LoadConfig)
    c.cacheConfig_->readConfig(c.mainConfig_);

    return ctx;
}

AppContext::~AppContext() {
    // u_GlobalState.pas:1280-1289 (SendTerminateToThreads), in this order and not left to member order
    requestClose();     // :1287 FAppClosingNotifierInternal.ExecuteOperation
    mapsList_.reset();  // every MapType and its download subsystem
    gcThread_.reset();  // :1288 FGCThread.Terminate (the destructor joins)
    transport_.reset(); // the downloader factory owns the curl share: it goes before the global cleanup
    if (curlInitialized_) {
        net::curlGlobalCleanup();
    }
}

void AppContext::report(const QString& message) {
    ++zmpErrorCount_;
    if (warn_) {
        warn_(message);
    } else {
        qCWarning(logApp) << message;
    }
}

core::Result<void> AppContext::loadMaps() {
    // u_GlobalState.pas:1163-1169 -- Maps.ini in the maps directory (a missing file is an empty
    // configuration); app-cli.md 1.6.1: DIR/Maps.ini of the last --maps folder, never written.
    // T-0357: DIR/Maps.conf is read when it exists, else DIR/Maps.ini (a SAS.Planet folder); no rename.
    const QDir lastDir(mapsDirs_.back());
    const QString mapsName = QFileInfo::exists(lastDir.filePath(QStringLiteral("Maps.conf")))
                                 ? QStringLiteral("Maps.conf")
                                 : QStringLiteral("Maps.ini");
    core::Result<std::shared_ptr<core::ConfigProviderIni>> mapsIni =
        core::ConfigProviderIni::open(lastDir.filePath(mapsName));
    if (!mapsIni) {
        return std::unexpected(mapsIni.error());
    }
    // u_GlobalState.pas:860, 883-892 -- map list over the ZMP set
    auto list = std::make_unique<maptype::MapTypesMainList>(
        std::make_shared<maptype::MapTypeSetBuilderFactory>(), zmpSet_);
    try {
        // u_GlobalState.pas:1174-1195; appId 1 = the first instance (u_GlobalState.pas:464-467)
        list->loadMaps(languages_, versions_, cacheConfig_, storageTypes_, gc_, appClosing_.get_token(),
                       inet_, download_, transport_, types_, converters_, projFactory_, 1,
                       logsPath_->fullPath(), mapsIni.value(), mainConfig_->orCreateSubItem(u"MapsList"),
                       [this](const QString& message) { report(message); });
    } catch (const std::runtime_error& e) { // MapTypesNoMapsError (a runtime_error) and "no non-layer map"
        QString message = QStringLiteral("no map source loaded from %1: %2")
                              .arg(mapsDirs_.join(QStringLiteral(", ")), QString::fromUtf8(e.what()));
        return core::fail(core::ErrorCode::NotFound, message);
    }
    mapsList_ = std::move(list);
    useFileCacheForRamMaps();
    return {};
}

// UAT D-1 ruling 2026-10-15 (app-cli.md 1.6): legacy keeps a CacheType=9 map in a 100-tile memory cache
// (u_TileInfoBasicMemCache.pas:271-282), so its export is incomplete; the program switches it to a file
// cache as frm_MapTypeEdit.pas:266 would. D row of T-0198.
void AppContext::useFileCacheForRamMaps() {
    const maptype::IMapTypeSetPtr maps = mapsList_->fullMapsSet();
    for (int i = 0; i < maps->count(); ++i) {
        const auto cfg = maps->item(i)->storageConfig();
        storage::CacheTypeId code = cfg->cacheType();
        if (code == storage::CacheTypeId::Default) {
            code = cacheConfig_->defCache();
        }
        // an empty NameInCache keeps RAM: not in the corpus, 14 of 14 CacheType=9 maps name it (maps commit
        // 9b988fd8f39a)
        if (code == storage::CacheTypeId::Ram && !cfg->nameInCache().isEmpty()) {
            cfg->setCacheType(cacheConfig_->defCache() != storage::CacheTypeId::Ram
                                  ? storage::CacheTypeId::Default
                                  : storage::CacheTypeId::Sqlite);
        }
    }
}

void AppContext::requestClose() {
    appClosing_.request_stop();
}

std::stop_token AppContext::appClosing() const {
    return appClosing_.get_token();
}

maptype::IMapTypeSetPtr AppContext::fullMapsSet() const {
    return mapsList_ ? mapsList_->fullMapsSet() : nullptr;
}

std::shared_ptr<const geo::GeometryLonLatFactory> AppContext::lonLatFactory() const {
    return lonLatFactory_;
}

std::shared_ptr<const geo::GeometryProjectedFactory> AppContext::projectedFactory() const {
    return projectedFactory_;
}

std::shared_ptr<const geo::TileIteratorFactory> AppContext::tileIteratorFactory() const {
    return tileIteratorFactory_;
}

std::shared_ptr<const net::IGlobalDownloadConfig> AppContext::downloadConfig() const {
    return download_;
}

std::shared_ptr<net::DownloadInfo> AppContext::downloadInfo() const {
    return downloadInfo_;
}

const core::ILanguageManager& AppContext::languages() const {
    return *languages_;
}

const core::AppPaths& AppContext::paths() const {
    return paths_;
}

const QStringList& AppContext::mapsDirs() const {
    return mapsDirs_;
}

QString AppContext::autoSaveDir() const {
    return paths_.dataDir + QStringLiteral("/AutoSave");
}

QString AppContext::snapshotDir() const {
    return paths_.dataDir + QStringLiteral("/Snapshots");
}

std::shared_ptr<net::IDownloaderFactory> AppContext::downloaderFactory() const {
    return transport_;
}

int AppContext::zmpErrorCount() const {
    return zmpErrorCount_;
}

} // namespace cg::app
