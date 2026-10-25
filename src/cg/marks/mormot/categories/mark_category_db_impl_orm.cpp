// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/categories/mark_category_db_impl_orm.h"

#include "cg/marks/mark_category_list.h"
#include "cg/marks/mormot/categories/mark_category_internal_orm.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QDebug>

#include <stdexcept>
#include <utility>
#include <vector>

namespace cg::marks {

namespace {

const core::IReadWriteStateInternal&
requireState(const std::shared_ptr<core::IReadWriteStateInternal>& state) {
    if (!state) {
        throw std::invalid_argument(
            "MarkCategoryDbImplORM: state is null"); // legacy raises in the constructor (:133), inside the
                                                     // factory Build that the changeable catches
    }
    return *state;
}

// _OrmCategoryRecFromCategory (:167-179); afterScale / beforeScale are truncated to a byte as Delphi does
// with range checks off.
OrmCategoryRec ormCategoryRecFromCategory(qint64 categoryId, const IMarkCategory& category) {
    OrmCategoryRec rec;
    rec.categoryId = categoryId;
    rec.name = category.name();
    rec.viewId = 0;
    rec.visible = category.visible();
    rec.minZoom = static_cast<quint8>(category.afterScale());
    rec.maxZoom = static_cast<quint8>(category.beforeScale());
    return rec;
}

} // namespace

// :112-143
MarkCategoryDbImplORM::MarkCategoryDbImplORM(quintptr dbId,
                                             std::shared_ptr<core::IReadWriteStateInternal> state,
                                             IMarkSystemImplORMClientProvider& provider)
    : dbId_(dbId), provider_(provider), factory_(dbId), state_(std::move(state)),
      helper_(!requireState(state_).writeAccess(), provider) {
    stateListener_ = state_->changed().subscribe([this] { onStateChange(); });
}

// :145-155 (the subscription member is destroyed first, then the helper)
MarkCategoryDbImplORM::~MarkCategoryDbImplORM() = default;

// :157-165
void MarkCategoryDbImplORM::onStateChange() {
    const auto guard = writeLock();
    helper_.setReadOnly(!state_->writeAccess());
}

MarkCategoryDbImplORM::WriteGuard MarkCategoryDbImplORM::writeLockConst() const {
    // The interface methods are const although they fill the caches (legacy has no const); the lock is the
    // write lock exactly as in legacy.
    return const_cast<MarkCategoryDbImplORM*>(this)
        ->writeLock(); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

void MarkCategoryDbImplORM::inTransaction(const std::function<void()>& body) {
    IMarkSystemORMTransaction& transaction = provider_.transaction();
    TransactionRec rec = transaction.start(helper_.isReadOnly());
    try {
        body();
        transaction.commit(rec);
    } catch (...) {
        transaction.rollBack(rec);
        throw;
    }
}

// :181-270
MarkCategoryPtr MarkCategoryDbImplORM::updateCategoryImpl(const MarkCategoryPtr& oldCategory,
                                                          const MarkCategoryPtr& newCategory, bool& isChanged,
                                                          bool useTransactions) {
    MarkCategoryPtr result;
    isChanged = false;

    if (!oldCategory && !newCategory) {
        throw MarkSystemORMError("MarkSystemORM: Old and New is nil!");
    }

    // :206-210
    qint64 categoryId = 0;
    const auto* const categoryInternal = dynamic_cast<const IMarkCategoryInternalORM*>(oldCategory.get());
    if (categoryInternal != nullptr && categoryInternal->dbId() == dbId_) {
        categoryId = categoryInternal->id();
    }

    const auto body = [&] {
        if (oldCategory && !newCategory) { // DELETE
            if (categoryInternal != nullptr && categoryId > 0) {
                isChanged = helper_.deleteCategorySql(categoryId);
            }
        } else if (!oldCategory && newCategory) { // INSERT
            // :225-233: the "Unknown category interface" branch cannot be reached, the argument is typed
            OrmCategoryRec recNew = ormCategoryRecFromCategory(0, *newCategory);
            if (helper_.insertCategorySql(recNew)) {
                result = factory_.createCategory(recNew);
                isChanged = true;
            }
        } else if (oldCategory && newCategory) { // UPDATE
            MarkCategoryPtr categoryOld;
            if (categoryId > 0) {
                categoryOld = getCategory(categoryId, QString()); // :238 (GetCategoryByID, lock held)
                if (categoryOld) {
                    if (categoryOld->isEqual(newCategory.get())) {
                        result = categoryOld;
                        categoryOld.reset(); // force abort update
                    }
                } else {
                    categoryId = 0;
                }
            }
            if (categoryOld) {
                OrmCategoryRec recNew = ormCategoryRecFromCategory(0, *newCategory);
                const OrmCategoryRec recOld = ormCategoryRecFromCategory(categoryId, *categoryOld);
                if (helper_.updateCategorySql(recOld, recNew)) {
                    result = factory_.createCategory(recNew);
                    isChanged = true;
                }
            }
        }
    };

    if (useTransactions) {
        inTransaction(body);
    } else {
        body();
    }
    return result;
}

// :272-289
MarkCategoryPtr MarkCategoryDbImplORM::updateCategory(const MarkCategoryPtr& oldCategory,
                                                      const MarkCategoryPtr& newCategory) {
    const auto guard = writeLock();
    bool isChanged = false;
    MarkCategoryPtr result = updateCategoryImpl(oldCategory, newCategory, isChanged, true);
    if (isChanged) {
        setChanged();
    }
    return result;
}

// :291-375
MarkCategoryListPtr MarkCategoryDbImplORM::updateCategoryList(const MarkCategoryListPtr& oldList,
                                                              const MarkCategoryListPtr& newList) {
    const auto guard = writeLock();
    bool doNotify = false;
    MarkCategoryListPtr result;

    inTransaction([&] {
        bool isChanged = false;
        if (newList) {
            const int newCount = newList->count();
            std::vector<MarkCategoryPtr> temp;
            temp.reserve(static_cast<size_t>(newCount));

            int minCount = 0;
            int maxCount = newCount;
            if (oldList) {
                if (oldList->count() < newCount) {
                    minCount = oldList->count();
                    maxCount = newCount;
                } else {
                    minCount = newCount;
                    maxCount = oldList->count();
                }
            }

            for (int i = 0; i < minCount; ++i) {
                MarkCategoryPtr res = updateCategoryImpl(oldList ? oldList->item(i) : nullptr,
                                                         newList->item(i), isChanged, false);
                doNotify = doNotify || isChanged;
                temp.push_back(std::move(res));
            }

            for (int i = minCount; i < maxCount; ++i) {
                MarkCategoryPtr oldItem;
                if (oldList && i < oldList->count()) {
                    oldItem = oldList->item(i);
                }
                MarkCategoryPtr newItem;
                if (i < newCount) {
                    newItem = newList->item(i);
                }
                MarkCategoryPtr res = updateCategoryImpl(oldItem, newItem, isChanged, false);
                doNotify = doNotify || isChanged;
                if (i < newCount) { // legacy: I < VTemp.Capacity (= the new count, set at :316)
                    temp.push_back(std::move(res));
                }
            }
            result = MarkCategoryList::build(std::move(temp));
        } else {
            if (!oldList) {
                // legacy: AOldCategoryList.Count on nil, an access violation inside the guarded part (:357);
                // the transaction is rolled back and the exception leaves
                throw std::runtime_error("MarkCategoryDbImplORM: both category lists are nil");
            }
            for (int i = 0; i < oldList->count(); ++i) {
                std::ignore = updateCategoryImpl(oldList->item(i), nullptr, isChanged, false);
                doNotify = doNotify || isChanged;
            }
        }
    });

    if (doNotify) {
        setChanged();
    }
    return result;
}

// :377-388. The legacy Assert(Assigned(ACategory)) is off in the shipped build; a null category is "no".
bool MarkCategoryDbImplORM::isCategoryFromThisDb(const vector::CategoryPtr& category) const {
    const auto* const internal = dynamic_cast<const IMarkCategoryInternalORM*>(category.get());
    return internal != nullptr && internal->dbId() == dbId_;
}

// :390-399
MarkCategoryPtr MarkCategoryDbImplORM::getCategory(qint64 id, const QString& name) const {
    OrmCategoryRec rec;
    if (helper_.readCategorySql(rec, id, name)) {
        return factory_.createCategory(rec);
    }
    return nullptr;
}

// :401-410
MarkCategoryPtr MarkCategoryDbImplORM::getCategoryById(qint64 id) const {
    const auto guard = writeLockConst();
    return getCategory(id, QString());
}

// :412-421
MarkCategoryPtr MarkCategoryDbImplORM::getFirstCategoryByName(const QString& name) const {
    const auto guard = writeLockConst();
    return getCategory(0, name);
}

// :423-431
int MarkCategoryDbImplORM::getCategoryByNameCount(const QString& name) const {
    const auto guard = readLock();
    return helper_.countCategorySql(name);
}

MarkCategoryPtr MarkCategoryDbImplORM::firstCategoryByName(const QString& name) const {
    return getFirstCategoryByName(name);
}

int MarkCategoryDbImplORM::categoryByNameCount(const QString& name) const {
    return getCategoryByNameCount(name);
}

// :433-451
void MarkCategoryDbImplORM::setAllCategoriesVisible(bool visible) {
    const auto guard = writeLock();
    inTransaction([&] { helper_.setAllCategoriesVisibleSql(visible); });
    setChanged();
}

// :453-478
MarkCategoryListPtr MarkCategoryDbImplORM::categoriesList() const {
    std::vector<OrmCategoryRec> recs;
    int count = 0;
    {
        const auto guard = writeLockConst();
        count = helper_.getCategoriesRecArray(recs);
    }
    if (count <= 0) {
        return nullptr;
    }
    std::vector<MarkCategoryPtr> list;
    list.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        list.push_back(factory_.createCategory(recs[static_cast<size_t>(i)]));
    }
    return MarkCategoryList::build(std::move(list));
}

} // namespace cg::marks
