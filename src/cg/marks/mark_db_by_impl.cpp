// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_MarkDbByImpl.pas
#include "cg/marks/mark_db_by_impl.h"

#include "cg/marks/mark_system_error_handler.h"

#include <stdexcept>

namespace cg::marks {

namespace {
// Legacy `VImpl.MarkDb.<method>` with a nil MarkDb is an access violation inside the try, i.e. caught.
// Here a null markDb() throws, so it goes down the same caught path.
IMarkDbImpl& dbOf(const IMarkSystemImpl& impl) {
    const auto db = impl.markDb();
    if (!db) {
        throw std::runtime_error("Access violation: the mark system has no mark database");
    }
    return *db; // the backend owns the database for as long as it is the static
}
} // namespace

MarkDbByImpl::MarkDbByImpl(std::shared_ptr<IMarkSystemImplChangeable> markSystemImpl,
                           std::shared_ptr<IMarkFactory> markFactory, core::Notifier& errorNotifier)
    : markSystemImpl_(std::move(markSystemImpl)), markFactory_(std::move(markFactory)),
      errorNotifier_(errorNotifier) {
    implChangeListener_ = markSystemImpl_->changed().subscribe([this] { onImplChange(); }); // :166
    onDbImplChange(); // :167 (legacy calls OnDBImplChange, not OnImplChange: the current backend is not
                      // subscribed)
}

template <class R, class F> R MarkDbByImpl::call(R empty, F&& body) const {
    try {
        const auto impl = markSystemImpl_->getStatic();
        if (impl) {
            return body(*impl);
        }
    } catch (...) {
        catchException(errorNotifier_);
        return empty;
    }
    return empty;
}

template <class F> void MarkDbByImpl::callVoid(F&& body) const {
    try {
        const auto impl = markSystemImpl_->getStatic();
        if (impl) {
            body(*impl);
        }
    } catch (...) {
        catchException(errorNotifier_);
    }
}

MarkIdList MarkDbByImpl::allMarkIdList() const { // GetAllMarkIdList :183-197
    return call(MarkIdList{}, [&](IMarkSystemImpl& i) { return dbOf(i).allMarkIdList(); });
}

core::Notifier& MarkDbByImpl::changeNotifier() { // GetChangeNotifier :199
    return changeNotifier_;
}

std::shared_ptr<IMarkFactory> MarkDbByImpl::factory() const { // GetFactory :204
    return markFactory_;
}

vector::VectorDataItemPtr MarkDbByImpl::markById(const MarkIdPtr& markId) const { // GetMarkByID :209-223
    return call(vector::VectorDataItemPtr{}, [&](IMarkSystemImpl& i) { return dbOf(i).markById(markId); });
}

vector::VectorDataItemPtr
MarkDbByImpl::firstMarkByName(const QString& name,
                              const vector::CategoryPtr& category) const { // :225-242
    return call(vector::VectorDataItemPtr{},
                [&](IMarkSystemImpl& i) { return dbOf(i).firstMarkByName(name, category); });
}

MarkIdList MarkDbByImpl::markIdListByCategory(const vector::CategoryPtr& category) const { // :244-260
    return call(MarkIdList{}, [&](IMarkSystemImpl& i) { return dbOf(i).markIdListByCategory(category); });
}

vector::VectorItemSubsetPtr MarkDbByImpl::markSubsetByCategory(const vector::CategoryPtr& category,
                                                               bool includeHiddenMarks) const { // :262-279
    return call(vector::VectorItemSubsetPtr{}, [&](IMarkSystemImpl& i) {
        return dbOf(i).markSubsetByCategory(category, includeHiddenMarks);
    });
}

vector::VectorItemSubsetPtr
MarkDbByImpl::markSubsetByCategoryInRect(const geo::DoubleRect& rect, const vector::CategoryPtr& category,
                                         bool includeHiddenMarks,
                                         const geo::DoublePoint& lonLatSize) const { // :281-300
    return call(vector::VectorItemSubsetPtr{}, [&](IMarkSystemImpl& i) {
        return dbOf(i).markSubsetByCategoryInRect(rect, category, includeHiddenMarks, lonLatSize);
    });
}

vector::VectorItemSubsetPtr
MarkDbByImpl::markSubsetByCategoryList(const MarkCategoryListPtr& categoryList,
                                       bool includeHiddenMarks) const { // :302-319
    return call(vector::VectorItemSubsetPtr{}, [&](IMarkSystemImpl& i) {
        return dbOf(i).markSubsetByCategoryList(categoryList, includeHiddenMarks);
    });
}

vector::VectorItemSubsetPtr
MarkDbByImpl::markSubsetByCategoryListInRect(const geo::DoubleRect& rect,
                                             const MarkCategoryListPtr& categoryList, bool includeHiddenMarks,
                                             const geo::DoublePoint& lonLatSize) const { // :321-340
    return call(vector::VectorItemSubsetPtr{}, [&](IMarkSystemImpl& i) {
        return dbOf(i).markSubsetByCategoryListInRect(rect, categoryList, includeHiddenMarks, lonLatSize);
    });
}

vector::VectorItemSubsetPtr MarkDbByImpl::findMarks(const QString& search, int maxCount,
                                                    bool includeHiddenMarks,
                                                    bool searchInDescription) const { // :342-361
    return call(vector::VectorItemSubsetPtr{}, [&](IMarkSystemImpl& i) {
        return dbOf(i).findMarks(search, maxCount, includeHiddenMarks, searchInDescription);
    });
}

bool MarkDbByImpl::markVisible(const vector::VectorDataItemPtr& mark) const { // GetMarkVisible :363-377
    return call(true, [&](IMarkSystemImpl& i) { return dbOf(i).markVisible(mark); });
}

bool MarkDbByImpl::markVisibleById(const MarkIdPtr& mark) const { // GetMarkVisibleByID :379-393
    return call(true, [&](IMarkSystemImpl& i) { return dbOf(i).markVisibleById(mark); });
}

void MarkDbByImpl::onImplChange() { // OnImplChange :395-412
    try {
        dbImplChangeListener_.reset();
        const auto impl = markSystemImpl_->getStatic();
        if (impl) {
            dbImplChangeListener_ = dbOf(*impl).changeNotifier().subscribe([this] { onDbImplChange(); });
        }
    } catch (...) {
        catchException(errorNotifier_);
    }
}

void MarkDbByImpl::onDbImplChange() { // OnDbImplChange :414-417
    changeNotifier_.notify();
}

void MarkDbByImpl::setAllMarksInCategoryVisible(const vector::CategoryPtr& category,
                                                bool newVisible) { // :419-434
    callVoid([&](IMarkSystemImpl& i) { dbOf(i).setAllMarksInCategoryVisible(category, newVisible); });
}

void MarkDbByImpl::setMarkVisible(const vector::VectorDataItemPtr& mark, bool visible) { // :436-451
    callVoid([&](IMarkSystemImpl& i) { dbOf(i).setMarkVisible(mark, visible); });
}

void MarkDbByImpl::setMarkVisibleById(const MarkIdPtr& mark, bool visible) { // SetMarkVisibleByID :453-468
    callVoid([&](IMarkSystemImpl& i) { dbOf(i).setMarkVisibleById(mark, visible); });
}

void MarkDbByImpl::setMarkVisibleByIdList(const MarkOrIdList& markList, bool visible) { // :470-485
    callVoid([&](IMarkSystemImpl& i) { dbOf(i).setMarkVisibleByIdList(markList, visible); });
}

void MarkDbByImpl::toggleMarkVisibleByIdList(const MarkOrIdList& markList) { // :487-501
    callVoid([&](IMarkSystemImpl& i) { dbOf(i).toggleMarkVisibleByIdList(markList); });
}

vector::VectorDataItemPtr MarkDbByImpl::updateMark(const vector::VectorDataItemPtr& oldMark,
                                                   const vector::VectorDataItemPtr& newMark) { // :503-517
    return call(vector::VectorDataItemPtr{},
                [&](IMarkSystemImpl& i) { return dbOf(i).updateMark(oldMark, newMark); });
}

MarkList MarkDbByImpl::updateMarkList(const MarkOrIdList& oldList, const MarkList& newList) { // :519-535
    return call(MarkList{}, [&](IMarkSystemImpl& i) { return dbOf(i).updateMarkList(oldList, newList); });
}

} // namespace cg::marks
