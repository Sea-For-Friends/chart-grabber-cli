// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Categories/u_MarkCategoryDbImplORM.pas (docs/specs/marks.md §2.9.6, §2.9.12).
// Line numbers in the sources are those of that unit.
//
// The category database of the SQLite backend: TConfigDataElementBaseEmptySaveLoad (the RW lock and the
// change notification, here core::ConfigElement) implementing IMarkCategoryDBImpl and
// IMarkCategoryDbInternalORM. Transactions are started HERE (:212, :312, :439); the helper starts none. Every
// method takes the write lock except getCategoryByNameCount (read lock, :425) and isCategoryFromThisDb (no
// lock). The lock is non-recursive: where legacy re-enters LockWrite (_UpdateCategory -> GetCategoryByID,
// :238) the unlocked getCategory() is called instead. One change notification per public call, fired when the
// write lock is released, after the commit. Caches are not undone on a rollback (LC-17).

#include "cg/core/config_element.h"
#include "cg/core/read_write_state_internal.h"
#include "cg/marks/mark_category_db_impl.h"
#include "cg/marks/mormot/categories/mark_category_db_impl_orm_helper.h"
#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"
#include "cg/marks/mormot/categories/mark_category_factory_db_internal_orm.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"

#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

namespace cg::marks {

// TMarkCategoryDbImplORM (:48-97)
class MarkCategoryDbImplORM final : public core::ConfigElement,
                                    public IMarkCategoryDBImpl,
                                    public IMarkCategoryDbInternalORM {
public:
    // :112-143. `provider` must outlive the object. A null `state` throws std::invalid_argument; legacy
    // raises in the constructor (:133), which runs inside the factory Build that the changeable catches
    // (u_MarkSystemImplChangeable.pas:177-188); the port's throw takes the same path. Unreachable: the state
    // is always created (u_MarkSystemImplORM.pas:144).
    MarkCategoryDbImplORM(quintptr dbId, std::shared_ptr<core::IReadWriteStateInternal> state,
                          IMarkSystemImplORMClientProvider& provider);
    ~MarkCategoryDbImplORM() override; // :145-155

    // IMarkCategoryDbInternalORM
    [[nodiscard]] bool isCategoryFromThisDb(const vector::CategoryPtr& category) const override; // :377-388
    [[nodiscard]] MarkCategoryPtr getCategoryById(qint64 id) const override;                     // :401-410
    [[nodiscard]] MarkCategoryPtr getFirstCategoryByName(const QString& name) const override;    // :412-421
    [[nodiscard]] int getCategoryByNameCount(const QString& name) const override;                // :423-431

    // IMarkCategoryDBImpl (the first, second and fourth forward to the methods above: one legacy method
    // serves both interfaces)
    [[nodiscard]] MarkCategoryPtr firstCategoryByName(const QString& name) const override;
    [[nodiscard]] int categoryByNameCount(const QString& name) const override;
    MarkCategoryPtr updateCategory(const MarkCategoryPtr& oldCategory,
                                   const MarkCategoryPtr& newCategory) override; // :272-289
    MarkCategoryListPtr updateCategoryList(const MarkCategoryListPtr& oldList,
                                           const MarkCategoryListPtr& newList) override; // :291-375
    [[nodiscard]] MarkCategoryListPtr categoriesList() const override;                   // :453-478
    void setAllCategoriesVisible(bool visible) override;                                 // :433-451
    [[nodiscard]] core::Notifier& changeNotifier() override { return changed(); }

protected:
    void doReadConfig(const core::IConfigProviderPtr&) override {}      // EmptySaveLoad
    void doWriteConfig(const core::IConfigWriterPtr&) const override {} // EmptySaveLoad

private:
    void onStateChange(); // :157-165
    MarkCategoryPtr updateCategoryImpl(const MarkCategoryPtr& oldCategory, const MarkCategoryPtr& newCategory,
                                       bool& isChanged, bool useTransactions); // :181-270
    MarkCategoryPtr getCategory(qint64 id,
                                const QString& name) const; // :390-399; write lock held by the caller
    // start(helper read-only) -> body -> commit; any exception: rollBack, rethrow (:212-269, :312-367,
    // :439-446). A start that throws is outside the guarded part, as in legacy.
    void inTransaction(const std::function<void()>& body);
    // The const interface methods take the write lock (they fill caches).
    [[nodiscard]] WriteGuard writeLockConst() const;

    quintptr dbId_;
    IMarkSystemImplORMClientProvider& provider_;
    MarkCategoryFactoryDbInternalORM factory_;
    std::shared_ptr<core::IReadWriteStateInternal> state_;
    mutable MarkCategoryDbImplORMHelper helper_; // reached only under the write lock
    core::Subscription stateListener_;           // last: unsubscribed first
};

} // namespace cg::marks
