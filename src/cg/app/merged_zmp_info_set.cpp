// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// NEW (T-0239, T-0291, app-cli.md 1.6.1); no legacy counterpart: legacy reads one maps directory
// (u_ZmpInfoSet.pas:126-185). See merged_zmp_info_set.h.
#include "cg/app/merged_zmp_info_set.h"

#include <QDir>

namespace cg::app {

MergedZmpInfoSet::MergedZmpInfoSet(const std::vector<Folder>& folders, const ReplaceSink& onReplaced) {
    // the paths of the definitions of one GUID, in folder order; the last one is the winner
    QHash<QUuid, QStringList> paths;
    for (const Folder& folder : folders) {
        for (const QUuid& guid : folder.set->guids()) {
            const maptype::IZmpInfoPtr zmp = folder.set->zmpByGuid(guid);
            if (!byGuid_.contains(guid)) {
                guids_.push_back(guid);
            }
            byGuid_.insert(guid, zmp);
            paths[guid].push_back(QDir(folder.dir).filePath(zmp->fileName()));
        }
    }
    if (!onReplaced) {
        return;
    }
    for (const QUuid& guid : guids_) {
        const QStringList& all = paths.value(guid);
        if (all.size() > 1) {
            onReplaced(guid, all.back(), all.mid(0, all.size() - 1));
        }
    }
}

maptype::IZmpInfoPtr MergedZmpInfoSet::zmpByGuid(const QUuid& guid) const {
    return byGuid_.value(guid);
}

} // namespace cg::app
