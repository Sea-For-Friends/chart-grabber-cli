// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_MarkDbByImpl.pas
//
// The IMarkDb facade over the current backend (IMarkSystemImplChangeable.getStatic()). Every method
// but factory() / changeNotifier() asks the backend of the moment; an exception thrown by it is
// logged, the error notifier fires once and the method returns its empty value.

#include "cg/core/notifier.h"
#include "cg/marks/mark_db.h"
#include "cg/marks/mark_system_impl_changeable.h"

#include <memory>
#include <utility>

namespace cg::marks {

class MarkDbByImpl final : public IMarkDb { // TMarkDbByImpl
public:
    // errorNotifier is not owned and must outlive the facade.
    MarkDbByImpl(std::shared_ptr<IMarkSystemImplChangeable> markSystemImpl,
                 std::shared_ptr<IMarkFactory> markFactory, core::Notifier& errorNotifier);

    [[nodiscard]] vector::VectorDataItemPtr
    firstMarkByName(const QString& name, const vector::CategoryPtr& category) const override;
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryList(const MarkCategoryListPtr& categoryList, bool includeHiddenMarks) const override;
    [[nodiscard]] vector::VectorItemSubsetPtr markSubsetByCategory(const vector::CategoryPtr& category,
                                                                   bool includeHiddenMarks) const override;
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryListInRect(const geo::DoubleRect& rect, const MarkCategoryListPtr& categoryList,
                                   bool includeHiddenMarks,
                                   const geo::DoublePoint& lonLatSize) const override;
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryInRect(const geo::DoubleRect& rect, const vector::CategoryPtr& category,
                               bool includeHiddenMarks, const geo::DoublePoint& lonLatSize) const override;
    [[nodiscard]] vector::VectorItemSubsetPtr findMarks(const QString& search, int maxCount,
                                                        bool includeHiddenMarks,
                                                        bool searchInDescription) const override;
    vector::VectorDataItemPtr updateMark(const vector::VectorDataItemPtr& oldMark,
                                         const vector::VectorDataItemPtr& newMark) override;
    MarkList updateMarkList(const MarkOrIdList& oldList, const MarkList& newList) override;
    [[nodiscard]] MarkIdList allMarkIdList() const override;
    [[nodiscard]] MarkIdList markIdListByCategory(const vector::CategoryPtr& category) const override;
    [[nodiscard]] vector::VectorDataItemPtr markById(const MarkIdPtr& markId) const override;
    void setMarkVisibleById(const MarkIdPtr& mark, bool visible) override;
    void setMarkVisible(const vector::VectorDataItemPtr& mark, bool visible) override;
    void setMarkVisibleByIdList(const MarkOrIdList& markList, bool visible) override;
    void toggleMarkVisibleByIdList(const MarkOrIdList& markList) override;
    [[nodiscard]] bool markVisibleById(const MarkIdPtr& mark) const override;
    [[nodiscard]] bool markVisible(const vector::VectorDataItemPtr& mark) const override;
    void setAllMarksInCategoryVisible(const vector::CategoryPtr& category, bool newVisible) override;
    [[nodiscard]] std::shared_ptr<IMarkFactory> factory() const override;
    [[nodiscard]] core::Notifier& changeNotifier() override;

private:
    void onImplChange();
    void onDbImplChange();
    // The shared shape of every legacy method: snapshot of the backend, call, on exception hand it to
    // the error handler and return `empty`.
    template <class R, class F> R call(R empty, F&& body) const;
    template <class F> void callVoid(F&& body) const;

    std::shared_ptr<IMarkSystemImplChangeable> markSystemImpl_;
    std::shared_ptr<IMarkFactory> markFactory_;
    core::Notifier changeNotifier_;
    core::Notifier& errorNotifier_;
    core::Subscription implChangeListener_;
    core::Subscription dbImplChangeListener_;
};

} // namespace cg::marks
