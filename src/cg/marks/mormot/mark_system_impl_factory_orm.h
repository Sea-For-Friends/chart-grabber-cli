// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/u_MarkSystemImplFactoryORM.pas (148; docs/specs/marks.md §2.9.10 last bullet).
// Line numbers in the sources are those of that unit.
//
// The factory of the SQLite mark database: keeps the collaborators and builds a MarkSystemImplORM from them.
//
// Not ported: the client type (only SQLite3 is ported, DV-4), the hash function (the core free functions are
// used, as in MarkFactory), the two performance counters (DV-1).

#include "cg/geo/geometry_factory.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_impl_factory.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_item_subset_builder.h"

#include <QString>

#include <memory>
#include <stop_token>

namespace cg::marks {

// TMarkSystemImplFactoryORM (class declaration :43-)
class MarkSystemImplFactoryORM final : public IMarkSystemImplFactory {
public:
    // :85-115. Precondition (legacy Asserts :97-103, compiled out: SASPlanet.dproj:92): the picture list, the
    // appearance factory, the subset-builder factory, the mark factory and the hint converter are non-null.
    // Unreachable: the only creator is u_GlobalState.pas:777-791 -> u_MarkSystem.pas:148-159 ->
    // u_MarkSystemImplFactoryChangeable.pas:160-172, and all five are built unconditionally before it
    // (u_GlobalState.pas:523, :545, :587, :767, :790). A null one throws std::invalid_argument as the port's
    // rendering of the compiled-out Asserts (untested, no register row). The geometry factory is copied.
    MarkSystemImplFactoryORM(vector::MarkPictureListPtr markPictureList,
                             vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                             const geo::GeometryLonLatFactory& geometryFactory,
                             vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory,
                             std::shared_ptr<const IMarkFactory> markFactory,
                             vector::HtmlToHintTextConverterPtr hintConverter);

    [[nodiscard]] bool isInitializationRequired() const override; // :117-120
    // :122-146. `stop` is accepted and never polled. Throws what the MarkSystemImplORM constructor throws.
    std::shared_ptr<IMarkSystemImpl> build(std::stop_token stop, const QString& basePath,
                                           const MarkSystemImplConfigStaticPtr& implConfig) override;

private:
    vector::MarkPictureListPtr markPictureList_;
    vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory_;
    geo::GeometryLonLatFactory geometryFactory_;
    vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory_;
    std::shared_ptr<const IMarkFactory> markFactory_;
    vector::HtmlToHintTextConverterPtr hintConverter_;
};

} // namespace cg::marks
