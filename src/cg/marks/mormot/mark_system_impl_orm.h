// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/u_MarkSystemImplORM.pas (269; docs/specs/marks.md §2.9.10 last bullet). Line
// numbers in the sources are those of that unit.
//
// One opened SQLite mark database: the read / write state, the client provider (the file), the category
// database, the internal mark factory and the mark database. The constructor builds them in the legacy order
// (:138-197); an exception of any step leaves the constructor and so the factory's build. The destructor
// releases them in the order of :202-207, the provider last: both databases keep a reference to it.
//
// Lifetime: the objects returned by markDb() / categoryDB() must not be used after this object is gone
// (legacy keeps the provider alive through its own reference count; here the databases hold a plain reference
// to it).
//
// Not ported: the four geometry readers / writers of :176-180 (the mark database takes the one geometry
// factory, mark_db_impl_orm.h), the hash function, the two performance counters (DV-1), the client type (only
// SQLite3 is ported, DV-4), the log set-up of :266-267.

#include "cg/core/read_write_state_internal.h"
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_config.h"
#include "cg/marks/mormot/categories/mark_category_db_impl_orm.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm.h"
#include "cg/marks/mormot/marks/mark_factory_db_internal_orm.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_item_subset_builder.h"

#include <QString>

#include <memory>
#include <stop_token>

namespace cg::marks {

// TMarkSystemImplORM (class declaration :53-)
class MarkSystemImplORM final : public IMarkSystemImpl {
public:
    // :110-197. `stop` (legacy AOperationID / ACancelNotifier) is accepted and never polled. Throws
    // MarkSystemORMError "MarkSystemORM: Unknown Impl config interface!" when `implConfig` is not an
    // IMarkSystemImplConfigORM (:140-142); the client provider throws MarkSystemORMError when the file cannot
    // be opened. The file is opened writable unless the impl config is read-only (:144). The collaborators
    // are non-null preconditions (creator trace in mark_system_impl_factory_orm.h); the geometry factory is
    // copied.
    MarkSystemImplORM(const std::stop_token& stop, const QString& basePath,
                      vector::MarkPictureListPtr markPictureList,
                      vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                      const geo::GeometryLonLatFactory& geometryFactory,
                      vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory,
                      std::shared_ptr<const IMarkFactory> markFactory,
                      vector::HtmlToHintTextConverterPtr hintConverter,
                      const MarkSystemImplConfigStaticPtr& implConfig);
    ~MarkSystemImplORM() override; // :200-209

    [[nodiscard]] core::ReadWriteStateChangeablePtr state() const override;                     // :251-254
    [[nodiscard]] std::shared_ptr<IMarkDbImpl> markDb() const override;                         // :246-249
    [[nodiscard]] std::shared_ptr<IMarkCategoryDBImpl> categoryDB() const override;             // :211-214
    [[nodiscard]] QString stringIdByMark(const vector::VectorDataItemPtr& mark) const override; // :256-264
    [[nodiscard]] vector::VectorDataItemPtr markByStringId(const QString& id) const override;   // :216-229
    [[nodiscard]] MarkCategoryPtr markCategoryByStringId(const QString& id) const override;     // :231-244

private:
    // Declaration order is construction order; the destructor resets the shared members explicitly.
    quintptr dbId_; // :138, the address of this object
    std::shared_ptr<core::ReadWriteStateInternal> state_;
    geo::GeometryLonLatFactory geometryFactory_; // referenced by the helper of the mark database
    std::unique_ptr<MarkSystemImplORMClientProvider> clientProvider_;
    std::shared_ptr<MarkCategoryDbImplORM> categoryDb_;
    std::shared_ptr<MarkFactoryDbInternalORM> factoryDbInternal_;
    std::shared_ptr<MarkDbImplORM> markDb_;
};

} // namespace cg::marks
