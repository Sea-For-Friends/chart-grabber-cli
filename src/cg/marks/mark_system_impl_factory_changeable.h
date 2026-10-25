// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/u_MarkSystemImplFactoryChangeable.pas (260; docs/specs/marks.md §2.6 Ruling A.1). Line
// numbers in the sources are those of that unit.
//
// The list of the mark database backends: a GUID-keyed set of (GUID, caption, factory) elements.
//
// Not ported: the SML registration block (:138-157; ticket S6 inserts it above the SQLite3 block when
// MarkSystemImplFactorySML lands), the MongoDB and DBMS (ODBC) blocks (:181-223; DV-4, U-3), the ZDBC block
// (:227-247; dead code after the `Exit` of :225), the hash function (the core free functions) and the two
// performance counters (DV-1).

#include "cg/core/guid_set.h"
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_impl_factory.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_item_subset_builder.h"

#include <QUuid>

#include <memory>
#include <vector>

namespace cg::marks {

// TMarkSystemImplFactoryListStatic (class declaration :41-)
class MarkSystemImplFactoryListStatic final : public IMarkSystemImplFactoryListStatic {
public:
    // :119-248, the SQLite3 block only. The arguments are those of MarkSystemImplFactoryORM: all non-null, a
    // precondition (creator trace in mark_system_impl_factory_orm.h); the geometry factory is copied.
    MarkSystemImplFactoryListStatic(vector::MarkPictureListPtr markPictureList,
                                    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                                    const geo::GeometryLonLatFactory& vectorGeometryLonLatFactory,
                                    vector::VectorItemSubsetBuilderFactoryPtr vectorItemSubsetBuilderFactory,
                                    std::shared_ptr<const IMarkFactory> markFactory,
                                    vector::HtmlToHintTextConverterPtr hintConverter);

    [[nodiscard]] std::vector<QUuid> guidEnum() const override;                              // :255-258
    [[nodiscard]] MarkSystemImplFactoryListElementPtr get(const QUuid& guid) const override; // :250-253

private:
    core::GuidInterfaceSet<const IMarkSystemImplFactoryListElement> list_; // :136, Create(False)
};

} // namespace cg::marks
