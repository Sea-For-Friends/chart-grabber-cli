// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Inherits: fr_ExportMBTiles.pas:550-573 (texts), :375-418 (via
// region::directTilesCopy); fr_TilesDownload.pas:428-431 (via region::allowDownload); region-export.md
// section 9.
#include "cg/app/job_checks.h"

#include "cg/app/app_context.h"
#include "cg/app/probe.h"
#include "cg/app/snapshot.h"
#include "cg/core/string_config_data_element.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/geo/projection.h"
#include "cg/geo/projection_set.h"
#include "cg/geo/projection_type.h"
#include "cg/maptype/map_abilities_config.h"
#include "cg/maptype/map_type_gui_config.h"
#include "cg/maptype/tile_download_subsystem.h"
#include "cg/maptype/tile_post_download_crop_config.h"
#include "cg/maptype/zmp_info.h"
#include "cg/region/export_mbtiles.h"
#include "cg/region/tiles_download.h"
#include "cg/storage/simple_tile_storage_config.h"
#include "cg/storage/tile_storage.h"
#include "cg/storage/tile_storage_abilities.h"
#include "cg/storage/tile_storage_of_map_type.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(JobChecks)
};

constexpr int kExitRefused = 3;
constexpr quint8 kMaxExportZoom = 21; // C-9, 0-based

std::unexpected<CliError> refused(QString message) {
    return std::unexpected(CliError{kExitRefused, std::move(message)});
}

QString quoted(const QString& text) {
    return QStringLiteral("\"%1\"").arg(text);
}

// The file name of the .zmp without directory and without the .zmp (or .source.json, T-0362) extension.
QString zmpStem(const maptype::IMapType& map) {
    QString base = QFileInfo(map.zmp()->fileName()).fileName();
    if (base.endsWith(QLatin1String(".zmp"), Qt::CaseInsensitive)) {
        base.chop(4);
    } else if (base.endsWith(QLatin1String(".source.json"), Qt::CaseInsensitive)) {
        base.chop(12);
    }
    return base;
}

QString candidateLine(const maptype::IMapType& map) {
    return QStringLiteral("  %1  %2  %3")
        .arg(map.zmp()->guid().toString(QUuid::WithBraces).toUpper(), sourceName(map), map.zmp()->fileName());
}

QString noMatch(const QString& id) {
    return Tr::tr("no map source matches %1; run \"chart-grabber-cli sources\"").arg(quoted(id));
}

// Zero or more candidates -> the one, or an error listing them.
std::expected<maptype::IMapTypePtr, CliError> pick(const std::vector<maptype::IMapTypePtr>& found,
                                                   const QString& id) {
    if (found.size() == 1) {
        return found.front();
    }
    QStringList lines{Tr::tr("%1 matches more than one map source:").arg(quoted(id))};
    for (const auto& map : found) {
        lines.append(candidateLine(*map));
    }
    return refused(lines.join(u'\n'));
}

QString lowerBase(const QString& text) {
    return text.toLower();
}

// Output files of app-cli.md 1.5.
bool isDirectoryName(const QString& out) {
    return out.endsWith(u'/') || QFileInfo(out).isDir();
}

QString fileInDirectory(const QString& dir, const QString& stem) {
    return QDir(dir).filePath(stem + QLatin1String(".mbtiles"));
}

QString outputFile(const QString& out, const maptype::IMapType& map) {
    return isDirectoryName(out) ? fileInDirectory(out, zmpStem(map)) : out;
}

bool hasMbtilesExtension(const QString& file) {
    return file.endsWith(QLatin1String(".mbtiles"), Qt::CaseInsensitive);
}

// S-10 for one file.
std::optional<QString> refusalForOutput(const QString& file, bool overwrite) {
    if (!hasMbtilesExtension(file)) {
        return Tr::tr("the output file name %1 does not end with .mbtiles").arg(quoted(file));
    }
    const QFileInfo info(file);
    const QFileInfo dir(info.absolutePath());
    if (!dir.isDir()) {
        return Tr::tr("the directory of %1 does not exist").arg(quoted(file));
    }
    if (!dir.isWritable()) {
        return Tr::tr("the directory of %1 is not writable").arg(quoted(file));
    }
    if (info.exists() && !overwrite) {
        return Tr::tr("%1 exists; pass --overwrite to replace it").arg(quoted(file));
    }
    return std::nullopt;
}

} // namespace

std::expected<maptype::IMapTypePtr, CliError> findSource(const maptype::IMapTypeSet& maps,
                                                         const QString& id) {
    std::vector<maptype::IMapTypePtr> all;
    all.reserve(static_cast<std::size_t>(maps.count()));
    for (int i = 0; i < maps.count(); ++i) {
        all.push_back(maps.item(i));
    }
    const auto collect = [&all](const auto& match) {
        std::vector<maptype::IMapTypePtr> found;
        std::ranges::copy_if(all, std::back_inserter(found), match);
        return found;
    };

    // Rule 1: GUID, with or without braces. A valid GUID that matches nothing does not fall through.
    const QString trimmed = id.trimmed();
    const QUuid guid =
        QUuid::fromString(trimmed.startsWith(u'{') ? trimmed : QStringLiteral("{%1}").arg(trimmed));
    if (!guid.isNull()) {
        const auto found = collect([&](const maptype::IMapTypePtr& m) { return m->zmp()->guid() == guid; });
        if (found.empty()) {
            return refused(noMatch(id));
        }
        return pick(found, id);
    }

    // Rule 2: the relative file name, with or without .zmp.
    const QString lower = lowerBase(trimmed);
    const auto byFile = collect([&](const maptype::IMapTypePtr& m) {
        const QString file = lowerBase(m->zmp()->fileName());
        const QString noExt = file.endsWith(QLatin1String(".zmp")) ? file.chopped(4) : file;
        return file == lower || noExt == lower;
    });
    if (!byFile.empty()) {
        return pick(byFile, id);
    }

    // Rule 3: the name in the current language.
    const auto byName =
        collect([&](const maptype::IMapTypePtr& m) { return lowerBase(sourceName(*m)) == lower; });
    if (!byName.empty()) {
        return pick(byName, id);
    }
    return refused(noMatch(id));
}

std::optional<QString> refusalForExport(const maptype::IMapType& map, const Job& job, bool asOverlay) {
    const core::ContentTypeInfo* info = map.contentType();
    // The key legacy reads: IMapType.ContentType (fr_ExportMBTiles.pas:390,
    // GetMBTilesContentType(AMapType.ContentType)). Null is unreachable in legacy (the MapType constructor
    // throws first, map_type.cpp:82); no register row.
    const region::MbtilesContentType type =
        info != nullptr ? region::mbtilesContentType(info->type) : region::MbtilesContentType::Unknown;
    const int epsg = map.projectionSet()->zoom(0)->projectionType()->projectionEpsg();

    std::optional<QString> reason;
    bool structural = true; // S-4, S-5 or S-6 (unknown type): what directTilesCopy refuses on its own
    if (!map.isBitmapTiles()) {
        reason = Tr::tr("%1 does not hold bitmap tiles").arg(quoted(sourceName(map)));
    } else if (type == region::MbtilesContentType::Unknown) {
        const QString shown = info != nullptr ? QString::fromLatin1(info->type) : QString();
        reason = Tr::tr("the tiles of %1 are stored as %2; only PNG, JPEG and WebP can be exported")
                     .arg(quoted(sourceName(map)), quoted(shown));
    } else if (epsg != geo::kProjEpsgGoogle) {
        reason = Tr::tr("the projection of %1 is EPSG:%2; only EPSG:3857 (Web Mercator) can be exported")
                     .arg(quoted(sourceName(map)))
                     .arg(epsg);
    } else {
        structural = false;
        if (type == region::MbtilesContentType::Webp && !job.allowWebp) {
            reason = Tr::tr(
                "WebP tiles are not shown by every OpenCPN version; pass --allow-webp to export them anyway");
        }
    }

    // The agreement with the ported predicate: a disagreement is a bug here, reported so a test sees it.
    region::ExportMbtilesParams params;
    // Non-owning; directTilesCopy only reads, and `map` outlives the call (IMapTypePtr is not const).
    (asOverlay ? params.overlay : params.map) =
        maptype::IMapTypePtr(std::shared_ptr<void>(), const_cast<maptype::IMapType*>(&map));
    if (region::directTilesCopy(params) == structural) {
        return Tr::tr("internal: direct copy refused");
    }
    if (reason) {
        return reason;
    }

    // S-7 (C-9)
    for (const quint8 zoom : job.zooms) {
        if (zoom > kMaxExportZoom) {
            return Tr::tr("zoom %1 is above %2 (0-based)").arg(zoom).arg(kMaxExportZoom);
        }
        if (!map.projectionSet()->checkZoom(zoom)) {
            return Tr::tr("zoom %1 is not valid for the projection of %2")
                .arg(zoom)
                .arg(quoted(sourceName(map)));
        }
    }
    return std::nullopt;
}

std::optional<QString> refusalForDownload(const maptype::IMapType& map) {
    // S-8: T-0159 refuses crop / cut at save time, this refuses at start.
    const auto crop = map.zmp()->tilePostDownloadCropConfig();
    if (crop && (crop->isCropOnDownload() || crop->isCutOnDownload())) {
        return Tr::tr("%1 has crop or cut after download switched on; this is not supported")
            .arg(quoted(sourceName(map)));
    }
    // S-9
    if (!map.abilities()->useDownload()) {
        return Tr::tr("downloads are disabled for this map");
    }
    // T-0366 (port extension): a JavaScript source that does not load is turned off here, with its reason.
    map.tileDownloadSubsystem()->checkRequestBuilder();
    if (!region::allowDownload(map)) {
        if (!map.storageConfig()->allowAdd()) {
            return Tr::tr("the cache of this map is read-only");
        }
        const QString why = map.tileDownloadSubsystem()->state()->getStatic()->disableReason;
        return why.isEmpty() ? Tr::tr("the download subsystem of this map is disabled")
                             : Tr::tr("the download subsystem of this map is disabled: %1").arg(why);
    }
    return std::nullopt;
}

// S-16 (T-0277, app-cli.md 1.7). DEVIATION (T-0277, L-18): a refusal guard; the count itself stays
// legacy (tile_count.cpp wrappingRectArea). The check uses the projected bounds of the WHOLE area, a
// sufficient (conservative) bound, not the per-part rectangle legacy multiplies (u_GeometryFunc.pas:757,
// 790). The rectangle is outside rounding: floor of the left and top, ceil of the right and bottom.
std::optional<QString> refusalForAreaSize(const maptype::IMapType& map,
                                          const geo::IGeometryLonLatPolygon& area,
                                          std::span<const quint8> zooms) {
    constexpr qint64 kMaxTiles = std::numeric_limits<qint32>::max();
    const geo::LonLatRect bounds = area.bounds();
    for (const quint8 zoom : zooms) {
        const geo::DoubleRect rect = map.projectionSet()->zoom(zoom)->lonLatRect2TileRectFloat(bounds);
        const qint64 width =
            static_cast<qint64>(std::ceil(rect.right)) - static_cast<qint64>(std::floor(rect.left));
        const qint64 height =
            static_cast<qint64>(std::ceil(rect.bottom)) - static_cast<qint64>(std::floor(rect.top));
        if (width * height > kMaxTiles) {
            const QString text = Tr::tr("the area is too large at zoom %1 (0-based): more than %2 tiles; "
                                        "choose a smaller area or lower zooms");
            return text.arg(static_cast<int>(zoom)).arg(kMaxTiles);
        }
    }
    return std::nullopt;
}

std::optional<QString> refusalForCacheBuild(const storage::ITileStorage& storage, const QString& sourceName) {
    // Other ITileStorage implementations (test doubles) have no build.
    if (const auto* facade = dynamic_cast<const storage::TileStorageOfMapType*>(&storage)) {
        if (const std::optional<QString> cause = facade->buildError()) {
            return Tr::tr("%1: cannot open the cache: %2").arg(quoted(sourceName), *cause);
        }
    }
    return std::nullopt;
}

std::optional<QString> refusalForSnapshot(const maptype::IMapType& map) {
    const auto definition = snapshotDefinition(map);
    if (!definition) {
        return Tr::tr("%1: the snapshot keys are invalid: %2")
            .arg(quoted(sourceName(map)), definition.error().what());
    }
    if (const auto limit = maxZoom(map); !limit) { // T-0245, every source
        return Tr::tr("%1: %2").arg(quoted(sourceName(map)), limit.error().what());
    }
    if (!*definition) {
        return std::nullopt; // a static source
    }
    if (map.tileStorage()->typeAbilities().version != storage::VersionSupport::MultiVersions) {
        return Tr::tr("%1: a snapshot source needs a cache that keeps tile versions (SQLite 71); its cache "
                      "type is %2")
            .arg(quoted(sourceName(map)))
            .arg(static_cast<int>(map.storageConfig()->cacheType()));
    }
    return std::nullopt;
}

namespace {
// S-15 (T-0275): can the cache be written at `path`? Nothing is created. One trailing '/' is dropped as
// prepareFileName does (u_TileStorageSQLiteFileConnectionBuilder.pas:210-212): the path of a single-file
// cache (CacheType 72-76 with an extension) is the file itself. The nearest existing entry must be writable,
// and an ancestor must not be a regular file.
bool cacheLocationWritable(QString path) {
    if (path.size() > 1 && path.endsWith(u'/')) {
        path.chop(1);
    }
    if (path.isEmpty()) {
        return true;
    }
    const QFileInfo target(path);
    QFileInfo nearest = target;
    while (!nearest.exists()) {
        const QFileInfo parent(nearest.absolutePath());
        if (parent.absoluteFilePath() == nearest.absoluteFilePath()) {
            return false;
        }
        nearest = parent;
    }
    if (nearest.isFile() && nearest.absoluteFilePath() != target.absoluteFilePath()) {
        return false;
    }
    return nearest.isWritable();
}
} // namespace

std::expected<std::vector<ResolvedSource>, CliError>
resolveJob(const Job& job, const AppContext& app, const std::function<void(const QString&)>& warn) {
    const bool downloads = job.command == Command::Download || job.command == Command::Run;
    const bool exports = job.command == Command::Export || job.command == Command::Run;
    if (!downloads && !exports && job.command != Command::Estimate) {
        return std::vector<ResolvedSource>{};
    }

    // T-0283 (app-cli.md 1.7): export uses the frame stored by the last download, a time is no input of it
    if (job.command == Command::Export && job.time) {
        return refused(
            Tr::tr("--time is not accepted by export: export uses the frame stored by the last download"));
    }

    // S-1
    const maptype::IMapTypeSetPtr maps = app.fullMapsSet();
    if (!maps || maps->count() == 0) {
        QStringList folders;
        for (const QString& dir : app.mapsDirs()) {
            folders.push_back(quoted(dir));
        }
        return refused(Tr::tr("no map source is loaded from %1").arg(folders.join(QStringLiteral(", "))));
    }

    // S-3, in the order of fr_ExportMBTiles.pas:554-570. parseJob (job.cpp checkJob) already refuses an empty
    // --out and a job without a source with exit 2; they are kept for a Job built by hand.
    if (exports && job.out.trimmed().isEmpty()) {
        return refused(Tr::tr("Output file name is not set or incorrect!"));
    }
    if (job.zooms.empty()) {
        return refused(Tr::tr("Please select at least one zoom"));
    }
    if (job.source.isEmpty() && job.overlay.isEmpty()) {
        return refused(Tr::tr("Please select at least one map or overlay layer"));
    }
    // S-11 (C-14): given texts
    if (exports) {
        const QPair<const char*, QString> given[] = {
            {"--name", job.name}, {"--description", job.description}, {"--attribution", job.attribution}};
        for (const auto& [flag, text] : given) {
            if (text.contains(u'\'')) {
                return refused(Tr::tr("%1 must not contain an apostrophe").arg(QString::fromLatin1(flag)));
            }
        }
    }

    std::vector<ResolvedSource> result;
    for (const bool asOverlay : {false, true}) {
        const QString& id = asOverlay ? job.overlay : job.source;
        if (id.isEmpty()) {
            continue;
        }
        auto found = findSource(*maps, id);
        if (!found) {
            return std::unexpected(found.error());
        }
        const maptype::IMapTypePtr& map = *found;
        // S-15, every command (T-0275, L-44): the facade builds its backend in its constructor, so the cause
        // is there already.
        if (const auto cache = map->tileStorage()) { // never null for a MapType
            if (const auto reason = refusalForCacheBuild(*cache, sourceName(*map))) {
                return refused(*reason);
            }
        }
        if (warn && map->zmp()->isLayer() != asOverlay) {
            warn(asOverlay ? Tr::tr("--overlay %1 is not a layer in its ZMP; it is exported as an overlay")
                                 .arg(quoted(id))
                           : Tr::tr("--source %1 is a layer in its ZMP; it is exported as a base map")
                                 .arg(quoted(id)));
        }
        if (const auto reason = refusalForExport(*map, job, asOverlay)) {
            return refused(*reason);
        }
        if (downloads) {
            if (const auto reason = refusalForDownload(*map)) {
                return refused(*reason);
            }
            // S-15 (T-0275, L-44): the sqlite / file backends open their files at the first save, so a
            // location that cannot be written shows only after --save-retries. Nothing is created here.
            if (const auto* facade =
                    dynamic_cast<const storage::TileStorageOfMapType*>(map->tileStorage().get())) {
                const QString folder = facade->path();
                if (!cacheLocationWritable(folder)) {
                    return refused(
                        Tr::tr("%1: cannot write the cache folder %2").arg(quoted(sourceName(*map)), folder));
                }
            }
        }
        if (const auto reason = refusalForSnapshot(*map)) { // S-14, every command
            return refused(*reason);
        }
        // T-0283 (app-cli.md 1.7): --time needs a snapshot source of the wmts-default type. The definition is
        // valid: refusalForSnapshot has accepted it.
        if (job.time) {
            const auto definition = snapshotDefinition(*map).value_or(std::nullopt);
            if (!definition || definition->type != SnapshotIndexType::WmtsDefault) {
                return refused(Tr::tr("--time: %1 has no selectable time").arg(quoted(sourceName(*map))));
            }
        }
        ResolvedSource source{map, asOverlay, {}, {}, {}, {}, {}};
        // T-0245 (weather-sources.md 6): the zooms above the source's MaxZoom are skipped. refusalForSnapshot
        // has accepted the key, so the value is there.
        source.zooms = job.zooms;
        if (const std::optional<quint8> limit = maxZoom(*map).value_or(std::nullopt)) {
            QStringList skipped;
            std::erase_if(source.zooms, [&](quint8 zoom) {
                if (zoom <= *limit) {
                    return false;
                }
                skipped.append(QString::number(zoom));
                return true;
            });
            if (source.zooms.empty()) {
                return refused(Tr::tr("%1: every chosen zoom is above its maximum zoom %2")
                                   .arg(quoted(sourceName(*map)))
                                   .arg(*limit));
            }
            if (!skipped.isEmpty() && warn) {
                warn(Tr::tr("%1: zooms %2 skipped, the source stops at zoom %3")
                         .arg(quoted(sourceName(*map)), skipped.join(u','))
                         .arg(*limit));
            }
        }
        if (exports) {
            // Name: --name for the base (trimmed as the frame does, :331-334), else the source's name.
            source.name = asOverlay ? QString() : job.name.trimmed();
            if (source.name.isEmpty()) {
                source.name = sourceName(*map);
                if (source.name.contains(u'\'')) {
                    source.name.remove(u'\'');
                    if (warn) {
                        warn(Tr::tr("the apostrophe is removed from the name of %1")
                                 .arg(quoted(sourceName(*map))));
                    }
                }
            }
            // Attribution: --attribution (trimmed as the frame does, :341-344), else the License text of the
            // source's ZMP in the current language (T-0222; legacy leaves the field empty, D row).
            source.attribution = job.attribution.trimmed();
            if (source.attribution.isEmpty()) {
                const core::IStringByLanguagePtr license = map->zmp()->license();
                // unreachable null: every ZmpInfo constructor sets it (zmp_info.cpp:190)
                if (license != nullptr) {
                    source.attribution = license->getString(app.languages().currentLanguageIndex());
                }
                if (source.attribution.contains(u'\'')) {
                    source.attribution.remove(u'\'');
                    if (warn) {
                        warn(Tr::tr("the apostrophe is removed from the attribution of %1")
                                 .arg(quoted(sourceName(*map))));
                    }
                }
            }
        }
        result.push_back(std::move(source));
    }

    // S-13 (no tile at any zoom) is checked by the caller with the estimate (tile_estimate.h anyTile).
    if (!exports) {
        return result;
    }

    // Output files, app-cli.md 1.5. The base first, then the overlay.
    const ResolvedSource* base = nullptr;
    for (ResolvedSource& source : result) {
        if (!source.asOverlay) {
            source.outFile = outputFile(job.out, *source.map);
            base = &source;
        }
    }
    for (ResolvedSource& source : result) {
        if (!source.asOverlay) {
            continue;
        }
        if (!job.overlayOut.isEmpty()) {
            source.outFile = job.overlayOut;
        } else if (base != nullptr) {
            const QFileInfo baseFile(base->outFile);
            source.outFile = fileInDirectory(baseFile.absolutePath(),
                                             baseFile.completeBaseName() + QLatin1String("-overlay"));
        } else {
            source.outFile = outputFile(job.out, *source.map);
        }
    }
    // S-10
    for (const ResolvedSource& source : result) {
        if (const auto reason = refusalForOutput(source.outFile, job.overwrite)) {
            return refused(*reason);
        }
    }
    return result;
}

std::optional<QString> termsOfUseUrl(const maptype::IMapType& map) {
    const auto zmp = map.zmp();
    const auto data = zmp ? zmp->dataProvider() : nullptr;
    const auto params = data ? data->subItem(u"params.txt") : nullptr;
    const auto section = params ? params->subItem(u"PARAMS") : nullptr;
    if (!section) {
        return std::nullopt;
    }
    QString url = section->readString(u"TermsOfUseWarning", {}).trimmed();
    if (url.isEmpty()) {
        return std::nullopt;
    }
    return url;
}

bool navigationWarning(const maptype::IMapType& map) {
    const auto zmp = map.zmp();
    const auto data = zmp ? zmp->dataProvider() : nullptr;
    const auto params = data ? data->subItem(u"params.txt") : nullptr;
    const auto section = params ? params->subItem(u"PARAMS") : nullptr;
    if (!section) {
        return false;
    }
    const QString value = section->readString(u"NavigationWarning", {}).trimmed();
    return value == u"1" || value.compare(u"true", Qt::CaseInsensitive) == 0;
}

} // namespace cg::app
