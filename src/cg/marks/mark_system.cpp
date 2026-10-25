// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_MarkSystem.pas (class MarkSystem, declared in mark_system.h; ticket T-0137)
#include "cg/marks/mark_system.h"

#include "cg/marks/mark_system_helpers.h"

#include <QString>

#include <stdexcept>
#include <utility>

namespace cg::marks {

namespace {

// A published backend always holds both databases (its constructor builds them:
// u_MarkSystemImplORM.pas:155-197), so a null one is refused here, as the facades refuse it
// (mark_db_by_impl.cpp dbOf). Not reachable with the landed backend.
template <class Db> void requireDatabase(const std::shared_ptr<Db>& db) {
    if (!db) {
        throw std::runtime_error("MarkSystem: the mark system has no mark database");
    }
}

} // namespace

// :118-184, in legacy order. The members are declared in that order; the body does the work.
MarkSystem::MarkSystem(core::PathConfigPtr basePath, std::shared_ptr<IMarkSystemConfigListChangeable> config,
                       vector::MarkPictureListPtr markPictureList, std::shared_ptr<IMarkFactory> markFactory,
                       std::shared_ptr<IMarkCategoryFactory> markCategoryFactory,
                       vector::AppearanceOfMarkFactoryPtr appearanceOfMarkFactory,
                       const geo::GeometryLonLatFactory& vectorGeometryLonLatFactory,
                       vector::VectorItemSubsetBuilderFactoryPtr vectorItemSubsetBuilderFactory,
                       std::stop_token appStarted, std::stop_token appClosing,
                       vector::HtmlToHintTextConverterPtr hintConverter) {
    // :140 FNotifierInternal: errorNotifier_, default-constructed. :142-146 the counters: DV-1, not ported.
    // :148-159 the factory list (the hash function is not a parameter, see mark_system.h).
    implFactoryList_ = std::make_shared<MarkSystemImplFactoryListStatic>(
        std::move(markPictureList), std::move(appearanceOfMarkFactory), vectorGeometryLonLatFactory,
        std::move(vectorItemSubsetBuilderFactory), markFactory, std::move(hintConverter));
    // :161-169 the changeable system; the two notifiers are passed on as stop tokens (Ruling A.2).
    systemImpl_ = std::make_shared<MarkSystemImplChangeable>(std::move(basePath), std::move(config),
                                                             implFactoryList_, errorNotifier_,
                                                             std::move(appStarted), std::move(appClosing));
    // :171-176 the mark database facade over the system.
    markDb_ = std::make_shared<MarkDbByImpl>(systemImpl_, std::move(markFactory), errorNotifier_);
    // :178-183 the category database facade over the system.
    categoryDB_ =
        std::make_shared<MarkCategoryDbByImpl>(systemImpl_, std::move(markCategoryFactory), errorNotifier_);
}

// The join of the build comes before any facade is released (D-57 widened): see the class comment.
MarkSystem::~MarkSystem() {
    systemImpl_->shutdown();
}

core::Notifier& MarkSystem::errorNotifier() { // GetErrorNotifier :314-317
    return errorNotifier_;
}

core::ReadWriteStateChangeablePtr MarkSystem::state() const { // GetState :248-251
    return systemImpl_->state();
}

std::shared_ptr<IMarkDb> MarkSystem::markDb() const { // GetMarkDb :243-246
    return markDb_;
}

std::shared_ptr<IMarkCategoryDB> MarkSystem::categoryDB() const { // GetCategoryDB :211-214
    return categoryDB_;
}

QString
MarkSystem::stringIdByMark(const vector::VectorDataItemPtr& mark) const { // GetStringIdByMark :253-264
    if (!mark) {
        return {};
    }
    const auto impl = systemImpl_->getStatic();
    if (!impl) {
        return {};
    }
    return impl->stringIdByMark(mark);
}

vector::VectorDataItemPtr MarkSystem::markByStringId(const QString& id) const { // GetMarkByStringId :216-227
    if (id.isEmpty()) {
        return nullptr;
    }
    const auto impl = systemImpl_->getStatic();
    if (!impl) {
        return nullptr;
    }
    return impl->markByStringId(id);
}

MarkCategoryPtr
MarkSystem::markCategoryByStringId(const QString& id) const { // GetMarkCategoryByStringId :229-241
    if (id.isEmpty()) {
        return nullptr;
    }
    const auto impl = systemImpl_->getStatic();
    if (!impl) {
        return nullptr;
    }
    return impl->markCategoryByStringId(id);
}

MarkList
MarkSystem::importItemsTree(const vector::VectorItemTreePtr& dataItemTree, // ImportItemsTree :266-288
                            const vector::ImportConfigPtr& importConfig) {
    const auto impl = systemImpl_->getStatic();
    if (!impl) {
        return nullptr;
    }
    const auto markDb = impl->markDb();
    requireDatabase(markDb);
    const auto categoryDb = impl->categoryDB();
    requireDatabase(categoryDb);
    // The factories are the facades' (FMarkDb.Factory, FCategoryDB.Factory: :283, :285), not the backend's.
    const auto markFactory = markDb_->factory();
    const auto categoryFactory = categoryDB_->factory();
    // Qualified: the member of the same name would hide the helper.
    return cg::marks::importItemsTree(dataItemTree, importConfig, *markDb, *markFactory, *categoryDb,
                                      *categoryFactory);
}

vector::VectorItemTreePtr
MarkSystem::categoryTreeToMarkTree(const MarkCategoryTreePtr& categoryTree, // :290-307
                                   bool includeHiddenMarks) const {
    const auto impl = systemImpl_->getStatic();
    if (!impl) {
        return nullptr;
    }
    const auto markDb = impl->markDb();
    requireDatabase(markDb);
    return categoryTreeToMarkTreeHelper(*markDb, categoryTree, includeHiddenMarks);
}

void MarkSystem::deleteCategoryWithMarks(
    const MarkCategoryPtr& category) { // DeleteCategoryWithMarks :186-195
    const MarkIdList markIds = markDb_->markIdListByCategory(category);
    if (markIds && !markIds->empty()) {
        markDb_->updateMarkList(markIds, nullptr);
    }
    categoryDB_->updateCategory(category, nullptr);
}

void MarkSystem::deleteCategoryListWithMarks(const MarkCategoryListPtr& categoryList) { // :197-209
    for (int i = 0; i < categoryList->count(); ++i) {
        const MarkIdList markIds = markDb_->markIdListByCategory(categoryList->item(i));
        if (markIds && !markIds->empty()) {
            markDb_->updateMarkList(markIds, nullptr);
        }
    }
    categoryDB_->updateCategoryList(categoryList, nullptr);
}

MarkSystemImplFactoryListStaticPtr MarkSystem::implFactoryList() const { // GetImplFactoryList :309-312
    return implFactoryList_;
}

} // namespace cg::marks
