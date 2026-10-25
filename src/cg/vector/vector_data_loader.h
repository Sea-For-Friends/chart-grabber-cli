// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/i_VectorDataLoader.pas
//
// Loader interface and the load context. The context is a plain value; a default-constructed
// one is legacy TVectorLoadContext.Init.

#include "cg/core/result.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_data_factory.h"
#include "cg/vector/vector_item_subset.h"

#include <QByteArray>

#include <memory>

namespace cg::vector {

struct VectorLoadContext {    // TVectorLoadContext
    IdData* idData = nullptr; // legacy "IdData: Pointer"; per-call state of the map factory
    VectorDataItemMainInfoFactoryPtr mainInfoFactory;
    ImportPointParamsPtr pointParams;
    ImportLineParamsPtr lineParams;
    ImportPolyParamsPtr polygonParams;
};

class IVectorDataLoader {
public:
    virtual ~IVectorDataLoader() = default;
    // Success with a null subset means "no items" (legacy nil). Parse failures: spec §4.
    [[nodiscard]] virtual core::Result<VectorItemSubsetPtr> load(const VectorLoadContext& context,
                                                                 const QByteArray& data) const = 0;
};
using VectorDataLoaderPtr = std::shared_ptr<const IVectorDataLoader>;

} // namespace cg::vector
