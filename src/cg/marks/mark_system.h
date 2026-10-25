// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkSystem.pas (interface, M6b) and u_MarkSystem.pas (class MarkSystem, ticket T-0137; spec
// docs/specs/marks.md §2.6 MarkSystem paragraph, Rulings A.1-A.2, §2.10).

#include "cg/core/notifier.h"
#include "cg/core/path_config.h"
#include "cg/core/read_write_state.h"
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_db.h"
#include "cg/marks/mark_category_db_by_impl.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_category_tree.h"
#include "cg/marks/mark_db.h"
#include "cg/marks/mark_db_by_impl.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/mark_system_config.h"
#include "cg/marks/mark_system_impl_changeable.h"
#include "cg/marks/mark_system_impl_factory.h"
#include "cg/marks/mark_system_impl_factory_changeable.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/html_to_hint_text_converter.h"
#include "cg/vector/import_config.h"
#include "cg/vector/mark_picture.h"
#include "cg/vector/vector_data_item_simple.h"
#include "cg/vector/vector_item_subset_builder.h"
#include "cg/vector/vector_item_tree.h"

#include <QString>

#include <memory>
#include <stop_token>

namespace cg::marks {

class IMarkSystem {
public:
    virtual ~IMarkSystem() = default;
    [[nodiscard]] virtual core::Notifier& errorNotifier() = 0;
    [[nodiscard]] virtual core::ReadWriteStateChangeablePtr state() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkDb> markDb() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkCategoryDB> categoryDB() const = 0;
    [[nodiscard]] virtual QString stringIdByMark(const vector::VectorDataItemPtr& mark) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr markByStringId(const QString& id) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr markCategoryByStringId(const QString& id) const = 0;
    virtual MarkList importItemsTree(const vector::VectorItemTreePtr& dataItemTree,
                                     const vector::ImportConfigPtr& importConfig) = 0;
    virtual void deleteCategoryWithMarks(const MarkCategoryPtr& category) = 0;
    virtual void deleteCategoryListWithMarks(const MarkCategoryListPtr& categoryList) = 0;
    [[nodiscard]] virtual vector::VectorItemTreePtr
    categoryTreeToMarkTree(const MarkCategoryTreePtr& categoryTree, bool includeHiddenMarks) const = 0;
    [[nodiscard]] virtual MarkSystemImplFactoryListStaticPtr implFactoryList() const = 0;
};

// FEAT-GEO-034
// TMarkSystem (u_MarkSystem.pas:56-102). Owns the error notifier, the factory list, the changeable system and
// the two facades, built in legacy order (:118-184). The application-started and -closing notifiers are
// std::stop_token (spec ruling A.2). Collaborators are non-null, a precondition (creator:
// u_GlobalState.pas:777-791). Not parameters: the hash function (core free functions, §2.6) and the
// performance counters (DV-1).
// Preconditions: (1) the error notifier is a value member of this object and the changeable system and both
// facades hold it by reference, so nothing obtained from the system (facades, state, notifier) outlives it;
// to be checked when GlobalState is ported. (2) deleteCategoryListWithMarks takes a non-null list: legacy
// dereferences it at once (:197-209) and its only caller, u_MarkDbGUIHelper.pas:456-457, sits inside
// `if Assigned(VList)`; no guard.
// Destruction ends and joins the build first (D-57 widened, L-row of T-0137), then releases the facades:
// legacy releases them while the build can still notify them.
class MarkSystem final : public IMarkSystem {
public:
    MarkSystem(core::PathConfigPtr basePath,                                  // ABasePath
               std::shared_ptr<IMarkSystemConfigListChangeable> config,       // AConfig
               vector::MarkPictureListPtr markPictureList,                    // AMarkPictureList
               std::shared_ptr<IMarkFactory> markFactory,                     // AMarkFactory
               std::shared_ptr<IMarkCategoryFactory> markCategoryFactory,     // AMarkCategoryFactory
               vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,    // AAppearanceOfMarkFactory
               const geo::GeometryLonLatFactory& vectorGeometryLonLatFactory, // AVectorGeometryLonLatFactory
               vector::VectorItemSubsetBuilderFactoryPtr
                   vectorItemSubsetBuilderFactory,                // AVectorItemSubsetBuilderFactory
               std::stop_token appStarted,                        // AAppStartedNotifier
               std::stop_token appClosing,                        // AAppClosingNotifier
               vector::HtmlToHintTextConverterPtr hintConverter); // AHintConverter
    ~MarkSystem() override; // first ends and joins the build, then the members go (reverse order)

    [[nodiscard]] core::Notifier& errorNotifier() override;
    [[nodiscard]] core::ReadWriteStateChangeablePtr state() const override;
    [[nodiscard]] std::shared_ptr<IMarkDb> markDb() const override;
    [[nodiscard]] std::shared_ptr<IMarkCategoryDB> categoryDB() const override;
    [[nodiscard]] QString stringIdByMark(const vector::VectorDataItemPtr& mark) const override;
    [[nodiscard]] vector::VectorDataItemPtr markByStringId(const QString& id) const override;
    [[nodiscard]] MarkCategoryPtr markCategoryByStringId(const QString& id) const override;
    // Throws std::invalid_argument for a nil tree or config while the backend is published (the helper's
    // refusal: the config is first read at u_MarkSystemHelpers.pas:86, the tree at :106); legacy's access
    // violation passes through here (no try, u_MarkSystem.pas:266-288).
    MarkList importItemsTree(const vector::VectorItemTreePtr& dataItemTree,
                             const vector::ImportConfigPtr& importConfig) override;
    // A null mark or category is passed to the facade, as legacy does.
    void deleteCategoryWithMarks(const MarkCategoryPtr& category) override;
    void deleteCategoryListWithMarks(const MarkCategoryListPtr& categoryList) override;
    [[nodiscard]] vector::VectorItemTreePtr categoryTreeToMarkTree(const MarkCategoryTreePtr& categoryTree,
                                                                   bool includeHiddenMarks) const override;
    [[nodiscard]] MarkSystemImplFactoryListStaticPtr implFactoryList() const override;

private:
    core::Notifier errorNotifier_;                         // FNotifierInternal (:140)
    MarkSystemImplFactoryListStaticPtr implFactoryList_;   // FImplFactoryList (:148-159)
    std::shared_ptr<MarkSystemImplChangeable> systemImpl_; // FSystemImpl (:161-169)
    std::shared_ptr<IMarkDb> markDb_;                      // FMarkDb (:171-176)
    std::shared_ptr<IMarkCategoryDB> categoryDB_;          // FCategoryDB (:178-183); destroyed first
};

} // namespace cg::marks
