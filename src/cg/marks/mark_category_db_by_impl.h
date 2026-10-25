// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkCategoryDbByImpl.pas
//
// The IMarkCategoryDB facade over the current backend (IMarkSystemImplChangeable.getStatic()). Most
// methods ask the backend of the moment; an exception thrown by it is logged, the error notifier fires
// once and the method returns its empty value. categoryListToStaticTree() and filterVisibleCategories()
// never reach the backend. Saving "A\B\C" first creates the missing parents "A" and "A\B".

#include "cg/core/notifier.h"
#include "cg/marks/mark_category_db.h"
#include "cg/marks/mark_system_impl_changeable.h"

#include <memory>
#include <utility>

namespace cg::marks {

class MarkCategoryDbByImpl final : public IMarkCategoryDB { // TMarkCategoryDbByImpl
public:
    // errorNotifier is not owned and must outlive the facade. categoryFactory may be null: it matters only
    // when a missing parent category has to be created.
    MarkCategoryDbByImpl(std::shared_ptr<IMarkSystemImplChangeable> markSystemImpl,
                         std::shared_ptr<IMarkCategoryFactory> categoryFactory,
                         core::Notifier& errorNotifier);

    [[nodiscard]] MarkCategoryPtr firstCategoryByName(const QString& name) const override;
    [[nodiscard]] int categoryByNameCount(const QString& name) const override;
    [[nodiscard]] MarkCategoryListPtr
    categoryWithSubCategories(const MarkCategoryPtr& category) const override;
    [[nodiscard]] MarkCategoryListPtr
    subCategoryListForCategory(const MarkCategoryPtr& category) const override;
    [[nodiscard]] MarkCategoryListPtr categoriesList() const override;
    [[nodiscard]] MarkCategoryListPtr visibleCategories(quint8 zoom) const override;
    [[nodiscard]] MarkCategoryListPtr visibleCategoriesIgnoreZoom() const override;
    [[nodiscard]] MarkCategoryTreePtr
    categoryListToStaticTree(const MarkCategoryListPtr& list) const override;
    [[nodiscard]] MarkCategoryListPtr
    filterVisibleCategories(const MarkCategoryListPtr& source) const override;
    MarkCategoryPtr updateCategory(const MarkCategoryPtr& oldCategory,
                                   const MarkCategoryPtr& newCategory) override;
    MarkCategoryListPtr updateCategoryList(const MarkCategoryListPtr& oldList,
                                           const MarkCategoryListPtr& newList) override;
    void setAllCategoriesVisible(bool visible) override;
    [[nodiscard]] std::shared_ptr<IMarkCategoryFactory> factory() const override;
    [[nodiscard]] core::Notifier& changeNotifier() override;

private:
    void onImplChange();
    void onDbImplChange();
    // Legacy ForceCreateSubCategories: creates each missing proper prefix of the new name, in order.
    void forceCreateSubCategories(IMarkCategoryDBImpl& db, const MarkCategoryPtr& newCategory) const;
    // The shared shape of every legacy method: snapshot of the backend, call, on exception hand it to
    // the error handler and return `empty`.
    template <class R, class F> R call(R empty, F&& body) const;
    template <class F> void callVoid(F&& body) const;

    std::shared_ptr<IMarkSystemImplChangeable> markSystemImpl_;
    std::shared_ptr<IMarkCategoryFactory> factory_;
    core::Notifier changeNotifier_;
    core::Notifier& errorNotifier_;
    core::Subscription implChangeListener_;
    core::Subscription dbImplChangeListener_;
};

} // namespace cg::marks
