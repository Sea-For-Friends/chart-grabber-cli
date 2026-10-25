// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemTreeImporter.pas
//
// Importer interface. Failures travel in the Result: file cannot be read -> IoError; the loader's
// own failure is passed on unchanged.

#include "cg/core/result.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_item_tree.h"

#include <QString>

#include <memory>
#include <stop_token>

namespace cg::vector {

class IVectorItemTreeImporter {
public:
    virtual ~IVectorItemTreeImporter() = default;
    // Legacy "AConfig: IInterface" is typed here; a null config means no import parameters.
    [[nodiscard]] virtual core::Result<VectorItemTreePtr>
    processImport(std::stop_token stop, const QString& fileName, const ImportConfigPtr& config) const = 0;
};
using VectorItemTreeImporterPtr = std::shared_ptr<const IVectorItemTreeImporter>;

} // namespace cg::vector
