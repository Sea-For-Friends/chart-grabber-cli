// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_ZmpInfoSet.pas, u_ZmpInfoSet.pas except the 32 reserved proxy slots (:190-213, spec 2.14).
//
// The set of every loadable ZMP, in load order. Built once at start-up by loading each ZMP found by
// zmpFileNames(); a ZMP that cannot be loaded is reported through the error sink and skipped.

#include "cg/core/content_type.h"
#include "cg/core/language_manager.h"
#include "cg/geo/projection_set.h"
#include "cg/maptype/zmp_config.h"
#include "cg/maptype/zmp_info.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/mark_picture.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <vector>

namespace cg::maptype {

class IZmpInfoSet { // i_ZmpInfoSet.pas:31-35
public:
    virtual ~IZmpInfoSet() = default;
    [[nodiscard]] virtual IZmpInfoPtr
    zmpByGuid(const QUuid& guid) const = 0;                            // GetZmpByGUID; nullptr if absent
    [[nodiscard]] virtual const std::vector<QUuid>& guids() const = 0; // GetIterator; insertion order
};

// Replaces legacy ShowErrorMessage (a modal box). Called on the constructing thread, once per rejected
// ZMP, in load order, before the next ZMP is read.
using ZmpErrorSink = std::function<void(const QString& message)>;

class ZmpInfoSet final : public IZmpInfoSet { // TZmpInfoSet, u_ZmpInfoSet.pas:43-62
public:
    // The collaborators are only used while loading (as the legacy constructor), so they are plain
    // references; they must be the same objects ZmpInfo is meant to be built with (spec 2.4).
    // `rootFolder` is legacy AFilesIterator.GetRootFolderName, `relativeNames` is zmpFileNames(rootFolder).
    ZmpInfoSet(const ZmpConfig& zmpConfig, const core::ILanguageManager& languageManager,
               const geo::ProjectionSetFactory& projectionSetFactory,
               const core::ContentTypeRegistry& contentTypes,
               const vector::IAppearanceOfMarkFactory& appearanceOfMarkFactory,
               const vector::MarkPictureListPtr& markPictureList, const QString& rootFolder,
               const QStringList& relativeNames, const ZmpErrorSink& showError);

    [[nodiscard]] IZmpInfoPtr zmpByGuid(const QUuid& guid) const override;
    [[nodiscard]] const std::vector<QUuid>& guids() const override { return guids_; }

private:
    QHash<QUuid, IZmpInfoPtr> byGuid_;
    std::vector<QUuid> guids_;
};

} // namespace cg::maptype
