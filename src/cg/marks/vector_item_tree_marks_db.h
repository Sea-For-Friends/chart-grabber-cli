// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_VectorItemTreeMarksDb.pas
//
// FEAT-GEO-041 (contributes; the id closes with the later of V-M2 / V-M3, the tables that register this
// class). Importer-exporter of one marks database file (.sml / .db3). The file is opened through the
// factory list entry of the database GUID; export writes the tree into the marks database, import reads
// its category tree back as a vector item tree. The import config is ignored, as in legacy.

#include "cg/core/result.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_factory.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_item_tree.h"
#include "cg/vector/vector_item_tree_exporter.h"
#include "cg/vector/vector_item_tree_importer.h"

#include <QString>
#include <QUuid>

#include <memory>
#include <stop_token>

namespace cg::marks {

class VectorItemTreeMarksDb final : public vector::IVectorItemTreeExporter,
                                    public vector::IVectorItemTreeImporter { // TVectorItemTreeMarksDb
public:
    // Collaborators are non-null: a null one is refused with std::invalid_argument at construction (legacy
    // stores nil and fails at the first call; no D row, as T-0146 did for constructor nulls).
    VectorItemTreeMarksDb(const QUuid& database, std::shared_ptr<IMarkFactory> markFactory,
                          std::shared_ptr<IMarkCategoryFactory> categoryFactory,
                          vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                          MarkSystemImplFactoryListStaticPtr implFactoryList);

    [[nodiscard]] core::Result<void> processExport(std::stop_token stop, const QString& fileName,
                                                   const vector::VectorItemTreePtr& tree) const override;
    [[nodiscard]] core::Result<vector::VectorItemTreePtr>
    processImport(std::stop_token stop, const QString& fileName,
                  const vector::ImportConfigPtr& config) const override;

private:
    // Null when the GUID is unknown or the list has no element for it; an exception of the factory's
    // build propagates to the caller (the public methods convert it).
    [[nodiscard]] std::shared_ptr<IMarkSystemImpl> getImpl(std::stop_token stop, const QString& fileName,
                                                           bool isReadOnly) const;
    [[nodiscard]] vector::ImportConfigPtr getConfigForExporter() const;

    QUuid database_;
    std::shared_ptr<IMarkFactory> markFactory_;
    std::shared_ptr<IMarkCategoryFactory> categoryFactory_;
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;
    MarkSystemImplFactoryListStaticPtr implFactoryList_;
};

} // namespace cg::marks
