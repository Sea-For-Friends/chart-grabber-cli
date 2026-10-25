// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkDbImpl.pas  (the same list as IMarkDb minus factory(); both kept, as legacy)

#include "cg/core/notifier.h"
#include "cg/geo/geo_types.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_id.h"
#include "cg/vector/category.h"
#include "cg/vector/vector_data_item_simple.h"
#include "cg/vector/vector_item_subset.h"

#include <QString>

namespace cg::marks {

class IMarkDbImpl {
public:
    virtual ~IMarkDbImpl() = default;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    firstMarkByName(const QString& name, const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual vector::VectorItemSubsetPtr
    markSubsetByCategoryList(const MarkCategoryListPtr& categoryList, bool includeHiddenMarks) const = 0;
    [[nodiscard]] virtual vector::VectorItemSubsetPtr
    markSubsetByCategory(const vector::CategoryPtr& category, bool includeHiddenMarks) const = 0;
    [[nodiscard]] virtual vector::VectorItemSubsetPtr
    markSubsetByCategoryListInRect(const geo::DoubleRect& rect, const MarkCategoryListPtr& categoryList,
                                   bool includeHiddenMarks, const geo::DoublePoint& lonLatSize) const = 0;
    [[nodiscard]] virtual vector::VectorItemSubsetPtr
    markSubsetByCategoryInRect(const geo::DoubleRect& rect, const vector::CategoryPtr& category,
                               bool includeHiddenMarks, const geo::DoublePoint& lonLatSize) const = 0;
    [[nodiscard]] virtual vector::VectorItemSubsetPtr findMarks(const QString& search, int maxCount,
                                                                bool includeHiddenMarks,
                                                                bool searchInDescription) const = 0;
    // Add: oldMark is null; delete: newMark is null.
    virtual vector::VectorDataItemPtr updateMark(const vector::VectorDataItemPtr& oldMark,
                                                 const vector::VectorDataItemPtr& newMark) = 0;
    // oldList: marks, mark ids or null; newList null deletes every old one (decision C3).
    virtual MarkList updateMarkList(const MarkOrIdList& oldList, const MarkList& newList) = 0;
    [[nodiscard]] virtual MarkIdList allMarkIdList() const = 0;
    [[nodiscard]] virtual MarkIdList markIdListByCategory(const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr markById(const MarkIdPtr& markId) const = 0;
    virtual void setMarkVisibleById(const MarkIdPtr& mark, bool visible) = 0;
    virtual void setMarkVisible(const vector::VectorDataItemPtr& mark, bool visible) = 0;
    virtual void setMarkVisibleByIdList(const MarkOrIdList& markList, bool visible) = 0;
    virtual void toggleMarkVisibleByIdList(const MarkOrIdList& markList) = 0;
    [[nodiscard]] virtual bool markVisibleById(const MarkIdPtr& mark) const = 0;
    [[nodiscard]] virtual bool markVisible(const vector::VectorDataItemPtr& mark) const = 0;
    virtual void setAllMarksInCategoryVisible(const vector::CategoryPtr& category, bool newVisible) = 0;
    [[nodiscard]] virtual core::Notifier& changeNotifier() = 0;
};

} // namespace cg::marks
