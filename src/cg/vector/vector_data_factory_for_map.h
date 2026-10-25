// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Vector/u_VectorDataFactoryForMap.pas
//
// The main-info factory for map layers. Each item gets the next index of its IdData and an info
// URL built from the map's URL prefix. Index and URL take no part in hash or isEqual (LV-3).

#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/string_provider.h"
#include "cg/vector/vector_data_factory.h"

#include <QtGlobal>

#include <atomic>
#include <memory>

namespace cg::vector {

// TIdData. One per map-tile load; the loader passes its address in VectorLoadContext::idData.
// nextIndex is the only shared mutable word of the factory.
struct IdData {
    StringProviderPtr urlPrefix;
    std::atomic<qint32> nextIndex{0};
};

class VectorDataItemMainInfoFactoryForMap final
    : public IVectorDataItemMainInfoFactory { // TVectorDataItemMainInfoFactoryForMap
public:
    explicit VectorDataItemMainInfoFactoryForMap(HtmlToHintTextConverterPtr hintConverter); // non-null

    // Null idData: returns null (legacy nil; the Assert is a no-op in the shipped build).
    [[nodiscard]] VectorDataItemMainInfoPtr buildMainInfo(IdData* idData, const QString& name,
                                                          const QString& desc) const override;

private:
    HtmlToHintTextConverterPtr hintConverter_; // FHintConverter
};

} // namespace cg::vector
