// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/vector_item_tree_importer_by_vector_loader.h"

#include "cg/vector/vector_item_tree.h"

#include <QFile>
#include <QString>

#include <expected>
#include <stdexcept>
#include <utility>

namespace cg::vector {

VectorItemTreeImporterByVectorLoader::VectorItemTreeImporterByVectorLoader(
    VectorDataItemMainInfoFactoryPtr mainInfoFactory, VectorDataLoaderPtr loader)
    : mainInfoFactory_(std::move(mainInfoFactory)), loader_(std::move(loader)) {
    if (!mainInfoFactory_ || !loader_) {
        throw std::invalid_argument("VectorItemTreeImporterByVectorLoader: null main-info factory or loader");
    }
}

core::Result<VectorItemTreePtr>
VectorItemTreeImporterByVectorLoader::processImport(std::stop_token /*stop*/, const QString& fileName,
                                                    const ImportConfigPtr& config) const {
    // Legacy: VMemStream.LoadFromFile (u_VectorItemTreeImporterByVectorLoader.pas:92).
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::IoError,
                          QStringLiteral("cannot read %1: %2").arg(fileName, file.errorString()));
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFile::NoError) {
        return core::fail(core::ErrorCode::IoError,
                          QStringLiteral("cannot read %1: %2").arg(fileName, file.errorString()));
    }

    // Legacy: VContext.Init, MainInfoFactory set, the three params only when AConfig is an
    // IImportConfig (:95-101). The IdData stays nil on this path.
    VectorLoadContext context;
    context.mainInfoFactory = mainInfoFactory_;
    if (config) {
        context.pointParams = config->pointParams();
        context.lineParams = config->lineParams();
        context.polygonParams = config->polyParams();
    }

    core::Result<VectorItemSubsetPtr> loaded = loader_->load(context, data);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    // Legacy: TVectorItemTree.Create('', VVectorData, nil) (:103). The subset may be null (LV-10).
    VectorItemTreePtr tree =
        std::make_shared<VectorItemTree>(QString(), *loaded, std::vector<VectorItemTreePtr>{});
    return tree;
}

} // namespace cg::vector
