// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORM.pas :752-1214 (docs/specs/marks.md §2.9.10, §2.9.12,
// §2.9.9). Ticket T-0125 (O17).
//
// Lock scope is as in legacy, method by method: the two visibility getters take no lock; the id list and
// the subset readers lock only around the helper's record query (the marks are built after unlocking); the
// rectangle readers lock around the query AND the marks they build; the setters lock around the write.
#include "cg/marks/mormot/marks/mark_db_impl_orm.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"

#include <stdexcept>
#include <utility>
#include <variant>

namespace cg::marks {

namespace {

// FVectorItemSubsetBuilderFactory.Build. A null factory is a precondition: the only caller passes the factory
// it was given (u_MarkSystemImplORM.pas:119 -> :193, from u_MarkSystemImplFactoryORM.pas:110,139, from
// u_MarkSystemImplFactoryChangeable.pas:145-235, from u_MarkSystem.pas:154); a null one throws.
std::unique_ptr<vector::IVectorItemSubsetBuilder>
newBuilder(const vector::VectorItemSubsetBuilderFactoryPtr& factory) {
    if (!factory) {
        throw std::runtime_error("MarkDbImplORM: the subset builder factory is null");
    }
    return factory->build();
}

// Supports(AMark.MainInfo, IMarkInternalORM) of :988-998, :1032-1063 (nil mark: null)
const IMarkInternalORM* internalOfMainInfo(const vector::VectorDataItemPtr& mark) {
    if (!mark) {
        return nullptr;
    }
    const vector::VectorDataItemMainInfoPtr mainInfo = mark->mainInfo();
    return dynamic_cast<const IMarkInternalORM*>(mainInfo.get());
}

// Supports(AMark, IMarkInternalORM) of :1000-1010, :1065-1096 (an id object itself)
const IMarkInternalORM* internalOfId(const MarkIdPtr& mark) {
    return dynamic_cast<const IMarkInternalORM*>(mark.get());
}

// Supports(AMarkList.Items[I], IMarkInternalORM) of :1119, :1157: the element itself, no MainInfo fallback
// (spec §2 preamble C3, LC-20): a mark (IVectorDataItem) fails the probe unless the object is also an id.
const IMarkInternalORM* internalOfElement(const MarkOrId& element) {
    return std::visit([](const auto& p) { return dynamic_cast<const IMarkInternalORM*>(p.get()); }, element);
}

} // namespace

// :752-784. categoryId 0 = every category. Marks without geometry and appearance, hidden ones included.
MarkIdList MarkDbImplORM::getMarkIdList(qint64 categoryId) const {
    std::vector<OrmMarkRec> records;
    int count = 0;
    {
        const auto guard = writeLockConst();
        count = helper_.getMarkRecArray(categoryId, true, false, false, records);
    }
    if (count <= 0) {
        return nullptr; // MakeStaticAndClear: nil for an empty list
    }
    std::vector<MarkIdPtr> ids;
    ids.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        ids.push_back(factory_->createMarkId(records[static_cast<std::size_t>(i)]));
    }
    return std::make_shared<const std::vector<MarkIdPtr>>(std::move(ids));
}

// :786-789
MarkIdList MarkDbImplORM::allMarkIdList() const {
    return getMarkIdList();
}

// :791-800. A category that is not of this backend (id 0): nil, not an empty list.
MarkIdList MarkDbImplORM::markIdListByCategory(const vector::CategoryPtr& category) const {
    const qint64 categoryId = getCategoryId(category);
    if (categoryId > 0) {
        return getMarkIdList(categoryId);
    }
    return nullptr;
}

// :806-837. Appends to resultList; categoryId 0 = every category.
void MarkDbImplORM::getMarkSubset(qint64 categoryId, bool includeHiddenMarks,
                                  vector::IVectorItemSubsetBuilder& resultList) const {
    std::vector<OrmMarkRec> records;
    int count = 0;
    {
        const auto guard = writeLockConst();
        count = helper_.getMarkRecArray(categoryId, includeHiddenMarks, true, true, records);
    }
    if (count > 0) {
        // Legacy sets Capacity := VCount on a builder that may hold marks of an earlier category already; a
        // capacity below the count raises EListError in every build (TList), and so does the landed builder.
        resultList.setCapacity(count);
        for (int i = 0; i < count; ++i) {
            resultList.add(factory_->createMark(records[static_cast<std::size_t>(i)]));
        }
    }
}

// :839-856. A category that is not of this backend gives id 0 (assert off) and so EVERY mark.
vector::VectorItemSubsetPtr MarkDbImplORM::markSubsetByCategory(const vector::CategoryPtr& category,
                                                                bool includeHiddenMarks) const {
    const auto resultList = newBuilder(subsetBuilderFactory_);
    qint64 categoryId = 0;
    if (category) {
        categoryId = getCategoryId(category);
    }
    getMarkSubset(categoryId, includeHiddenMarks, *resultList);
    return resultList->makeStaticAndClear();
}

// :858-881. A null list: every mark. An entry that is not of this backend is skipped (assert off).
vector::VectorItemSubsetPtr MarkDbImplORM::markSubsetByCategoryList(const MarkCategoryListPtr& categoryList,
                                                                    bool includeHiddenMarks) const {
    const auto resultList = newBuilder(subsetBuilderFactory_);
    if (!categoryList) {
        getMarkSubset(0, includeHiddenMarks, *resultList);
    } else {
        for (int i = 0; i < categoryList->count(); ++i) {
            const qint64 categoryId = getCategoryId(categoryList->item(i));
            if (categoryId > 0) {
                getMarkSubset(categoryId, includeHiddenMarks, *resultList);
            }
        }
    }
    return resultList->makeStaticAndClear();
}

// :887-909. No lock of its own: the callers hold the write lock.
void MarkDbImplORM::getMarkSubsetByRect(std::vector<qint64>& categoryIds, const geo::DoubleRect& rect,
                                        bool includeHiddenMarks, const geo::DoublePoint& lonLatSize,
                                        vector::IVectorItemSubsetBuilder& resultList) const {
    std::vector<OrmMarkRec> records;
    const int count =
        helper_.getMarkRecArrayByRect(categoryIds, rect, includeHiddenMarks, lonLatSize, records);
    for (int i = 0; i < count; ++i) {
        resultList.add(factory_->createMark(records[static_cast<std::size_t>(i)]));
    }
}

// :911-943. The category id is read outside the lock; the query and the marks are built inside it. A
// category that is not of this backend leaves the id list EMPTY, which the helper reads as every category.
vector::VectorItemSubsetPtr
MarkDbImplORM::markSubsetByCategoryInRect(const geo::DoubleRect& rect, const vector::CategoryPtr& category,
                                          bool includeHiddenMarks, const geo::DoublePoint& lonLatSize) const {
    const auto resultList = newBuilder(subsetBuilderFactory_);

    std::vector<qint64> categoryIds;
    if (category) {
        const qint64 id = getCategoryId(category);
        if (id > 0) {
            categoryIds.push_back(id);
        }
    }

    {
        const auto guard = writeLockConst();
        getMarkSubsetByRect(categoryIds, rect, includeHiddenMarks, lonLatSize, *resultList);
    }
    return resultList->makeStaticAndClear();
}

// :945-982. The whole of it, the category ids included, is under the lock. A null list, and a list with no
// entry of this backend, both give an empty id list: every mark.
vector::VectorItemSubsetPtr MarkDbImplORM::markSubsetByCategoryListInRect(
    const geo::DoubleRect& rect, const MarkCategoryListPtr& categoryList, bool includeHiddenMarks,
    const geo::DoublePoint& lonLatSize) const {
    const auto resultList = newBuilder(subsetBuilderFactory_);

    std::vector<qint64> categoryIds;
    {
        const auto guard = writeLockConst();
        if (categoryList) {
            categoryIds.reserve(static_cast<std::size_t>(categoryList->count()));
            for (int i = 0; i < categoryList->count(); ++i) {
                const qint64 id = getCategoryId(categoryList->item(i));
                if (id > 0) {
                    categoryIds.push_back(id);
                }
            }
        }
        getMarkSubsetByRect(categoryIds, rect, includeHiddenMarks, lonLatSize, *resultList);
    }
    return resultList->makeStaticAndClear();
}

// :988-998, no lock. True for a null mark and for a mark without the internal interface.
bool MarkDbImplORM::markVisible(const vector::VectorDataItemPtr& mark) const {
    if (const auto* const internal = internalOfMainInfo(mark)) {
        return internal->visible();
    }
    return true;
}

// :1000-1010, no lock.
bool MarkDbImplORM::markVisibleById(const MarkIdPtr& mark) const {
    if (const auto* const internal = internalOfId(mark)) {
        return internal->visible();
    }
    return true;
}

// :1012-1030. A category that is not of this backend: nothing.
void MarkDbImplORM::setAllMarksInCategoryVisible(const vector::CategoryPtr& category, bool newVisible) {
    const qint64 categoryId = getCategoryId(category);
    if (categoryId > 0) {
        const auto guard = writeLock();
        if (helper_.setMarksInCategoryVisibleSql(categoryId, newVisible)) {
            setChanged();
        }
    }
}

// :1032-1063. The flag of the object flips before (and whether or not) the row is written; the lock covers
// the row write only.
void MarkDbImplORM::setMarkVisible(const vector::VectorDataItemPtr& mark, bool visible) {
    if (!mark) {
        return;
    }
    qint64 id = 0;
    qint64 categoryId = 0;
    if (const auto* const internal = internalOfMainInfo(mark)) {
        id = internal->id();
        categoryId = internal->categoryId();
        if (internal->visible() == visible) {
            return;
        }
        internal->setVisible(visible);
    }
    if (id > 0 && categoryId > 0) {
        const auto guard = writeLock();
        if (helper_.updateMarkView(id, categoryId, visible, true)) {
            setChanged();
        }
    }
}

// :1065-1096
void MarkDbImplORM::setMarkVisibleById(const MarkIdPtr& mark, bool visible) {
    if (!mark) {
        return;
    }
    qint64 id = 0;
    qint64 categoryId = 0;
    if (const auto* const internal = internalOfId(mark)) {
        id = internal->id();
        categoryId = internal->categoryId();
        if (internal->visible() == visible) {
            return;
        }
        internal->setVisible(visible);
    }
    if (id > 0 && categoryId > 0) {
        const auto guard = writeLock();
        if (helper_.updateMarkView(id, categoryId, visible, true)) {
            setChanged();
        }
    }
}

// :1098-1146. One transaction for the whole list (one BEGIN), the lock around all of it; the notification
// fires once, when the lock is released, only if some row changed. An element that is not of the internal
// kind, or already has the wanted flag, is skipped.
void MarkDbImplORM::setMarkVisibleByIdList(const MarkOrIdList& markList, bool visible) {
    if (!markList || markList.count() <= 0) {
        return;
    }
    const auto guard = writeLock();
    TransactionRec transaction = transaction_.start(helper_.isReadOnly());
    bool isChanged = false;
    try {
        for (int i = 0; i < markList.count(); ++i) {
            qint64 id = 0;
            qint64 categoryId = 0;
            if (const auto* const internal = internalOfElement(markList[i])) {
                id = internal->id();
                categoryId = internal->categoryId();
                if (internal->visible() == visible) {
                    continue;
                }
                internal->setVisible(visible);
            }
            if (id > 0 && categoryId > 0) {
                if (helper_.updateMarkView(id, categoryId, visible, false)) {
                    isChanged = true;
                }
            }
        }
        transaction_.commit(transaction);
    } catch (...) {
        transaction_.rollBack(transaction);
        throw;
    }
    if (isChanged) {
        setChanged();
    }
}

// :1148-1173, no lock of its own. Shows the marks when strictly fewer are visible than hidden (a tie hides);
// an element without the internal interface counts for neither.
void MarkDbImplORM::toggleMarkVisibleByIdList(const MarkOrIdList& markList) {
    if (!markList || markList.count() <= 0) {
        return;
    }
    int visibleCount = 0;
    int invisibleCount = 0;
    for (int i = 0; i < markList.count(); ++i) {
        if (const auto* const internal = internalOfElement(markList[i])) {
            if (internal->visible()) {
                ++visibleCount;
            } else {
                ++invisibleCount;
            }
        }
    }
    setMarkVisibleByIdList(markList, visibleCount < invisibleCount);
}

// :1179-1212. The lock covers the query only; the marks are built after it.
vector::VectorItemSubsetPtr MarkDbImplORM::findMarks(const QString& search, int maxCount,
                                                     bool includeHiddenMarks,
                                                     bool searchInDescription) const {
    const auto resultList = newBuilder(subsetBuilderFactory_);
    std::vector<OrmMarkRec> records;
    int count = 0;
    {
        const auto guard = writeLockConst();
        count =
            helper_.getMarkRecArrayByText(search, maxCount, includeHiddenMarks, searchInDescription, records);
    }
    for (int i = 0; i < count; ++i) {
        resultList->add(factory_->createMark(records[static_cast<std::size_t>(i)]));
    }
    return resultList->makeStaticAndClear();
}

} // namespace cg::marks
