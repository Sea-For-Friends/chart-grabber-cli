// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_VectorItemTreeImporterByVectorLoader.pas
//
// Reads a whole file and hands the bytes to a loader. The result is a tree with an empty name and
// no children, also when the loader found nothing (LV-10: its items are then null).

#include "cg/vector/vector_data_factory.h"
#include "cg/vector/vector_data_loader.h"
#include "cg/vector/vector_item_tree_importer.h"

namespace cg::vector {

class VectorItemTreeImporterByVectorLoader final
    : public IVectorItemTreeImporter { // TVectorItemTreeImporterByVectorLoader
public:
    // Precondition, unreachable from legacy, guarded in every build (std::invalid_argument). Trace:
    // u_GlobalState.pas:687 creates the factory (TVectorDataItemMainInfoFactory, the simple one);
    // u_GlobalState.pas:793-797 passes it to TVectorItemTreeImporterListSimple.Create; the list passes
    // it on at all six TVectorItemTreeImporterByVectorLoader.Create sites
    // (u_VectorItemTreeImporterListSimple.pas :203, :228, :253, :278, :324, :349) with a loader that is
    // always built first (VLoader := ... at :190-349).
    VectorItemTreeImporterByVectorLoader(VectorDataItemMainInfoFactoryPtr mainInfoFactory,
                                         VectorDataLoaderPtr loader);

    // The stop token is not polled: no legacy importer polls its cancel notifier here.
    [[nodiscard]] core::Result<VectorItemTreePtr> processImport(std::stop_token stop, const QString& fileName,
                                                                const ImportConfigPtr& config) const override;

private:
    VectorDataItemMainInfoFactoryPtr mainInfoFactory_;
    VectorDataLoaderPtr loader_;
};

} // namespace cg::vector
