// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: u_MarkCategoryDbByImpl.pas
#include "cg/marks/mark_category_db_by_impl.h"

#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_system_error_handler.h"
#include "cg/marks/mark_system_helpers.h" // categoryListToCategoryTree (u_MarkSystemHelpers.pas:408-441)

#include <QString>
#include <QStringList>

#include <stdexcept>
#include <utility>
#include <vector>

namespace cg::marks {

namespace {

// Legacy reads VImpl.CategoryDB without a check. A nil database is an access violation inside the
// method's try, so the error handler runs; the port raises the same case as an exception.
IMarkCategoryDBImpl& categoryDbOf(const IMarkSystemImpl& impl) {
    const auto db = impl.categoryDB();
    if (!db) {
        throw std::runtime_error("Access violation: the mark system has no category database");
    }
    return *db;
}

// Legacy reads VList[i].Name / .Visible without a check: a nil element is an access violation inside
// the method's try. The port raises the same case as an exception.
const IMarkCategory& requireCategory(const MarkCategoryPtr& category) {
    if (!category) {
        throw std::runtime_error("Access violation: nil category in a category list");
    }
    return *category;
}

// The shared filter of GetVisibleCategories / GetVisibleCategoriesIgnoreZoom: a null or empty source
// gives nil, an empty result gives nil (TMarkCategoryList.Build).
template <class Pred> MarkCategoryListPtr filterCategories(const MarkCategoryListPtr& list, Pred keep) {
    if (!list || list->count() == 0) {
        return nullptr;
    }
    std::vector<MarkCategoryPtr> result;
    for (int i = 0; i < list->count(); ++i) {
        const MarkCategoryPtr item = list->item(i);
        if (keep(requireCategory(item))) {
            result.push_back(item);
        }
    }
    return MarkCategoryList::build(std::move(result));
}

// GetCategoryWithSubCategories / GetSubCategoryListForCategory for a non-nil category: the entries
// whose name starts with "<category>\" (case-sensitive StartsStr), preceded by the category itself
// when withCategory is set. A null or empty database list gives nil even when withCategory is set.
MarkCategoryListPtr childrenOf(const MarkCategoryListPtr& list, const MarkCategoryPtr& category,
                               bool withCategory) {
    if (!list || list->count() == 0) {
        return nullptr;
    }
    const QString prefix = category->name() + QChar(u'\\');
    std::vector<MarkCategoryPtr> result;
    if (withCategory) {
        result.push_back(category);
    }
    for (int i = 0; i < list->count(); ++i) {
        const MarkCategoryPtr item = list->item(i);
        if (requireCategory(item).name().startsWith(prefix)) {
            result.push_back(item);
        }
    }
    return MarkCategoryList::build(std::move(result));
}

} // namespace

MarkCategoryDbByImpl::MarkCategoryDbByImpl(std::shared_ptr<IMarkSystemImplChangeable> markSystemImpl,
                                           std::shared_ptr<IMarkCategoryFactory> categoryFactory,
                                           core::Notifier& errorNotifier)
    : markSystemImpl_(std::move(markSystemImpl)), factory_(std::move(categoryFactory)),
      errorNotifier_(errorNotifier) {
    implChangeListener_ = markSystemImpl_->changed().subscribe([this] { onImplChange(); }); // :128
    onDbImplChange(); // :129 legacy calls OnDBImplChange, not OnImplChange: an already open backend is
                      // not subscribed (L-194, both constructors)
}

template <class R, class F> R MarkCategoryDbByImpl::call(R empty, F&& body) const {
    try {
        const auto impl = markSystemImpl_->getStatic();
        if (impl) {
            return body(categoryDbOf(*impl));
        }
    } catch (...) {
        catchException(errorNotifier_);
        return empty;
    }
    return empty;
}

template <class F> void MarkCategoryDbByImpl::callVoid(F&& body) const {
    try {
        const auto impl = markSystemImpl_->getStatic();
        if (impl) {
            body(categoryDbOf(*impl));
        }
    } catch (...) {
        catchException(errorNotifier_);
    }
}

MarkCategoryPtr MarkCategoryDbByImpl::firstCategoryByName(const QString& name) const { // :198-214
    return call(MarkCategoryPtr{}, [&](IMarkCategoryDBImpl& db) { return db.firstCategoryByName(name); });
}

int MarkCategoryDbByImpl::categoryByNameCount(const QString& name) const { // :226-242
    return call(0, [&](IMarkCategoryDBImpl& db) { return db.categoryByNameCount(name); });
}

MarkCategoryListPtr
MarkCategoryDbByImpl::categoryWithSubCategories(const MarkCategoryPtr& category) const { // :244-281
    return call(MarkCategoryListPtr{}, [&](IMarkCategoryDBImpl& db) -> MarkCategoryListPtr {
        auto list = db.categoriesList();
        if (!category) {
            return list; // a null category gives the whole list
        }
        return childrenOf(list, category, true);
    });
}

MarkCategoryListPtr
MarkCategoryDbByImpl::subCategoryListForCategory(const MarkCategoryPtr& category) const { // :283-319
    return call(MarkCategoryListPtr{}, [&](IMarkCategoryDBImpl& db) -> MarkCategoryListPtr {
        auto list = db.categoriesList();
        if (!category) {
            return list; // a null category gives the whole list
        }
        return childrenOf(list, category, false);
    });
}

MarkCategoryListPtr MarkCategoryDbByImpl::categoriesList() const { // :182-196
    return call(MarkCategoryListPtr{}, [](IMarkCategoryDBImpl& db) { return db.categoriesList(); });
}

MarkCategoryListPtr MarkCategoryDbByImpl::visibleCategories(quint8 zoom) const { // :321-353
    const int scale = zoom + 1; // legacy AZoom + 1 (Byte operand, Integer result)
    return call(MarkCategoryListPtr{}, [&](IMarkCategoryDBImpl& db) {
        return filterCategories(db.categoriesList(), [scale](const IMarkCategory& c) {
            return c.visible() && c.afterScale() <= scale && c.beforeScale() >= scale;
        });
    });
}

MarkCategoryListPtr MarkCategoryDbByImpl::visibleCategoriesIgnoreZoom() const { // :355-383
    return call(MarkCategoryListPtr{}, [](IMarkCategoryDBImpl& db) {
        return filterCategories(db.categoriesList(), [](const IMarkCategory& c) { return c.visible(); });
    });
}

MarkCategoryTreePtr
MarkCategoryDbByImpl::categoryListToStaticTree(const MarkCategoryListPtr& list) const { // :167-180
    // No backend: legacy only catches what CategoryListToCategoryTree raises. A nil element raises
    // inside that builder (mark_system_helpers.cpp), and the try here catches it as legacy does.
    try {
        if (list) {
            return categoryListToCategoryTree(list);
        }
    } catch (...) {
        catchException(errorNotifier_);
    }
    return nullptr;
}

MarkCategoryListPtr
MarkCategoryDbByImpl::filterVisibleCategories(const MarkCategoryListPtr& source) const { // :145-165
    // No try and no backend in legacy. Supports(nil, IMarkCategory) is false, so a nil element is
    // skipped rather than raised.
    if (!source) {
        return nullptr;
    }
    std::vector<MarkCategoryPtr> visible;
    for (int i = 0; i < source->count(); ++i) {
        const MarkCategoryPtr item = source->item(i);
        if (item && item->visible()) {
            visible.push_back(item);
        }
    }
    return MarkCategoryList::build(std::move(visible));
}

void MarkCategoryDbByImpl::forceCreateSubCategories(IMarkCategoryDBImpl& db,
                                                    const MarkCategoryPtr& newCategory) const { // :424-454
    if (!newCategory) {
        return;
    }
    // Empty parts are kept, as in the port's other SplitString callers (map_coverage_info.cpp); see
    // the ticket's open questions.
    const QStringList parts = newCategory->name().split(u'\\', Qt::KeepEmptyParts);
    if (parts.size() <= 1) {
        return;
    }
    QString sub = parts.at(0);
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        if (!db.firstCategoryByName(sub)) {
            if (!factory_) {
                throw std::runtime_error("Access violation: the category factory is missing");
            }
            db.updateCategory(nullptr, factory_->createNew(sub));
        }
        sub = sub + QChar(u'\\') + parts.at(i + 1);
    }
}

void MarkCategoryDbByImpl::onImplChange() { // OnImplChange :385-403
    try {
        dbImplChangeListener_.reset();
        const auto impl = markSystemImpl_->getStatic();
        if (impl) {
            dbImplChangeListener_ =
                categoryDbOf(*impl).changeNotifier().subscribe([this] { onDbImplChange(); });
        }
        onDbImplChange(); // :399 inside the try: a throw from the subscription skips the notification
    } catch (...) {
        catchException(errorNotifier_);
    }
}

void MarkCategoryDbByImpl::onDbImplChange() { // OnDbImplChange :405-408
    changeNotifier_.notify();
}

void MarkCategoryDbByImpl::setAllCategoriesVisible(bool visible) { // :410-422
    callVoid([&](IMarkCategoryDBImpl& db) { db.setAllCategoriesVisible(visible); });
}

MarkCategoryPtr MarkCategoryDbByImpl::updateCategory(const MarkCategoryPtr& oldCategory,
                                                     const MarkCategoryPtr& newCategory) { // :456-474
    return call(MarkCategoryPtr{}, [&](IMarkCategoryDBImpl& db) {
        forceCreateSubCategories(db, newCategory);
        return db.updateCategory(oldCategory, newCategory);
    });
}

MarkCategoryListPtr MarkCategoryDbByImpl::updateCategoryList(const MarkCategoryListPtr& oldList,
                                                             const MarkCategoryListPtr& newList) { // :476-499
    return call(MarkCategoryListPtr{}, [&](IMarkCategoryDBImpl& db) {
        if (newList) {
            for (int i = 0; i < newList->count(); ++i) {
                forceCreateSubCategories(db, newList->item(i));
            }
        }
        return db.updateCategoryList(oldList, newList);
    });
}

std::shared_ptr<IMarkCategoryFactory> MarkCategoryDbByImpl::factory() const { // GetFactory :221-224
    return factory_;
}

core::Notifier& MarkCategoryDbByImpl::changeNotifier() { // GetChangeNotifier :216-219
    return changeNotifier_;
}

} // namespace cg::marks
