// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/zmp_info_set.h"

#include "cg/core/config_provider_folder.h"
#include "cg/core/config_provider_zip.h"
#include "cg/core/explorer_sort.h"
#include "cg/maptype/log_maptype.h"
#include "cg/maptype/source_json_container.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <exception>
#include <utility>

namespace cg::maptype {

namespace {

// IsHiddenZmp (u_ZmpInfoSet.pas:82-91): the last path component starts with '.'.
bool isHiddenZmp(const QString& name) {
    const QString fileName = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
    return !fileName.isEmpty() && fileName.front() == QLatin1Char('.');
}

// The legacy names use '\' (PathDelim), the sort compares them as such (spec 2.4.1, DV-2); the
// port's names use '/', so the key restores '\' to keep the order unchanged.
QString sortKey(QString name) {
    return name.replace(QLatin1Char('/'), QLatin1Char('\\'));
}

// The zip provider answers "absent" for a member it could not extract; legacy raised, and the
// handler above dropped the ZMP (D-63, Tech Lead ruling T-0072).
void throwIfDamaged(const core::ConfigProviderZip& zip) {
    if (const auto status = zip.status(); !status) {
        throw std::runtime_error(status.error().what().toUtf8().toStdString());
    }
}

} // namespace

ZmpInfoSet::ZmpInfoSet(const ZmpConfig& zmpConfig, const core::ILanguageManager& languageManager,
                       const geo::ProjectionSetFactory& projectionSetFactory,
                       const core::ContentTypeRegistry& contentTypes,
                       const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
                       const vector::MarkPictureListPtr& markPictureList, const QString& rootFolder,
                       const QStringList& relativeNames, const ZmpErrorSink& showError) {
    // :130-135
    std::vector<QString> names;
    for (const QString& name : relativeNames) {
        if (!isHiddenZmp(name)) {
            names.push_back(name);
        }
    }
    std::ranges::stable_sort(names, [](const QString& a, const QString& b) {
        return core::compareStringOrdinal(sortKey(a), sortKey(b)) < 0;
    });

    int zmpCount = 0; // VMapTypeCount, the pnum of the next ZMP
    const QDir root(rootFolder);
    for (const QString& name : names) {
        const QString fullFileName = root.filePath(name); // :139
        // Every failure of this block is shown and skips the ZMP (:178-183).
        QString error;
        IZmpInfoPtr zmp;
        try {
            core::IConfigProviderPtr container;
            std::shared_ptr<core::ConfigProviderZip> zip;
            if (SourceJsonContainer::isSourceJsonName(name)) { // NEW: map-source-json.md section 2
                auto loaded = SourceJsonContainer::load(fullFileName);
                if (!loaded) {
                    throw std::runtime_error(QStringLiteral("There is an error %1 in map %2")
                                                 .arg(loaded.error().what(), name)
                                                 .toUtf8()
                                                 .toStdString());
                }
                container = *loaded;
            } else if (QFileInfo(fullFileName).isFile()) { // FileExists: false for a directory (:141)
                auto opened = core::ConfigProviderZip::open(fullFileName);
                if (!opened) {
                    throw std::runtime_error(opened.error().what().toUtf8().toStdString());
                }
                zip = *opened;
                container = zip;
            } else {
                container = core::ConfigProviderFolder::create(fullFileName);
            }
            try {
                zmp = std::make_shared<ZmpInfo>(zmpConfig, languageManager, projectionSetFactory,
                                                contentTypes, appearanceOfMarkFactory, markPictureList, name,
                                                container, zmpCount);
            } catch (const ZmpError& e) { // :170-172
                if (zip) {
                    throwIfDamaged(
                        *zip); // a damaged member reads as absent: report the archive error, not "Not found"
                }
                if (e.kind() == ZmpError::Kind::UnknownContentType) {
                    throw; // port-only (D-74): the text already names the map, reported unchanged
                }
                throw std::runtime_error(QStringLiteral("There is an error %1 in map %2")
                                             .arg(e.message(), name)
                                             .toUtf8()
                                             .toStdString());
            }
            if (zip) {
                throwIfDamaged(*zip);
                // Legacy reads the script at map build only when the downloader is on
                // (u_TileDownloadSubsystem.pas:311, :331, :527-532); doing it here makes a damaged
                // GetUrlScript.txt drop the ZMP instead of surfacing as a half-read map.
                if (zmp->tileDownloaderConfig()->enabled) {
                    (void)zip->binary(u"GetUrlScript.txt");
                    throwIfDamaged(*zip);
                }
            }
            if (const IZmpInfoPtr first = zmpByGuid(zmp->guid())) { // :174-177
                throw std::runtime_error(QStringLiteral("Files %1 and %2 have the same GUID")
                                             .arg(first->fileName(), fullFileName)
                                             .toUtf8()
                                             .toStdString());
            }
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
            zmp.reset();
        }
        if (!zmp) {
            qCWarning(logMapType).noquote() << error;
            showError(error);
            continue;
        }
        byGuid_.insert(zmp->guid(), zmp);
        guids_.push_back(zmp->guid());
        ++zmpCount;
    }
    // :190-213 -- the 32 reserved proxy slots belong to the offline-map proxies (spec 2.14).
    // Not ported in this ticket (out of scope, no proxy ticket number yet): they are appended here,
    // their pnum continues at zmpCount.
}

IZmpInfoPtr ZmpInfoSet::zmpByGuid(const QUuid& guid) const {
    return byGuid_.value(guid);
}

} // namespace cg::maptype
