// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorItemTreeExporter.pas
//
// Exporter interface. Legacy ProcessExport is a procedure; the port returns core::Result<void>
// as the landed importer does. AOperationID is dropped (as in T-0129, deviation 5): cancel is the
// std::stop_token that replaces INotifierOperation.

#include "cg/core/result.h"
#include "cg/vector/vector_item_tree.h"

#include <QString>

#include <memory>
#include <stop_token>

namespace cg::vector {

class IVectorItemTreeExporter {
public:
    virtual ~IVectorItemTreeExporter() = default;
    [[nodiscard]] virtual core::Result<void> processExport(std::stop_token stop, const QString& fileName,
                                                           const VectorItemTreePtr& tree) const = 0;
};
using VectorItemTreeExporterPtr = std::shared_ptr<const IVectorItemTreeExporter>;

} // namespace cg::vector
