// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/marks/mark_db_impl_orm.h"

#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mormot/categories/mark_category_internal_orm.h"
#include "cg/marks/mormot/mark_system_orm_tools.h"
#include "cg/vector/appearance_of_vector_item.h"

#include <stdexcept>
#include <utility>
#include <variant>

namespace cg::marks {

namespace {

const core::IReadWriteStateInternal&
requireState(const std::shared_ptr<core::IReadWriteStateInternal>& state) {
    if (!state) {
        // precondition: legacy would fail at :261 (FStateInternal.WriteAccess on nil); the only caller passes
        // the state created at u_MarkSystemImplORM.pas:144-146
        throw std::invalid_argument("MarkDbImplORM: state is null");
    }
    return *state;
}

std::shared_ptr<const IMarkFactoryDbInternalORM>
requireFactory(std::shared_ptr<const IMarkFactoryDbInternalORM> factory) {
    if (!factory) {
        // precondition: legacy would fail on the first use of FFactoryDbInternal; the only caller passes the
        // factory created at u_MarkSystemImplORM.pas:165-174
        throw std::invalid_argument("MarkDbImplORM: factory is null");
    }
    return factory;
}

// Assigned(AMark) of a legacy IInterface
bool isAssigned(const MarkOrId& mark) {
    return std::visit([](const auto& p) { return static_cast<bool>(p); }, mark);
}

// Supports(AMark, IVectorDataItem) / Supports(VMark.MainInfo, IMarkInternalORM) of :537-548, for an object
// that is an IVectorDataItem or an IMarkId (a mark id may itself be both)
const IMarkInternalORM* internalOf(const vector::IVectorDataItem* item) {
    if (item == nullptr) {
        return nullptr;
    }
    const vector::VectorDataItemMainInfoPtr mainInfo = item->mainInfo();
    return dynamic_cast<const IMarkInternalORM*>(mainInfo.get());
}

} // namespace

// :230-283
MarkDbImplORM::MarkDbImplORM(quintptr dbId, quint32 cacheSizeMb,
                             std::shared_ptr<core::IReadWriteStateInternal> state,
                             IMarkSystemImplORMClientProvider& provider,
                             std::shared_ptr<const IMarkFactoryDbInternalORM> factory,
                             const geo::GeometryLonLatFactory& geometryFactory,
                             vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory)
    : dbId_(dbId), state_(std::move(state)), transaction_(provider.transaction()),
      factory_(requireFactory(std::move(factory))), subsetBuilderFactory_(std::move(subsetBuilderFactory)),
      helper_(!requireState(state_).writeAccess(), cacheSizeMb, geometryFactory, provider) {
    stateListener_ = state_->changed().subscribe([this] { onStateChange(); });
}

// :285-295 (the subscription member is destroyed first, then the helper)
MarkDbImplORM::~MarkDbImplORM() = default;

// :297-305
void MarkDbImplORM::onStateChange() {
    const auto guard = writeLock();
    helper_.setReadOnly(!state_->writeAccess());
}

MarkDbImplORM::WriteGuard MarkDbImplORM::writeLockConst() const {
    // The interface methods are const although they fill the caches (legacy has no const); the lock is the
    // write lock exactly as in legacy.
    return const_cast<MarkDbImplORM*>(this)->writeLock(); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

// :307-318. Assert(ACategory <> nil) and Assert(Result > 0) are off in the shipped build.
qint64 MarkDbImplORM::getCategoryId(const vector::CategoryPtr& category) {
    const auto* const internal = dynamic_cast<const IMarkCategoryInternalORM*>(category.get());
    return internal != nullptr ? internal->id() : 0;
}

// :324-337
vector::VectorDataItemPtr MarkDbImplORM::getMarkSql(qint64 id, const QString& name, qint64 categoryId) const {
    OrmMarkRec rec;
    if (helper_.readMarkSql(rec, id, categoryId, name)) {
        return factory_->createMark(rec);
    }
    return nullptr;
}

// :339-350
vector::VectorDataItemPtr MarkDbImplORM::getById(qint64 id) const {
    if (id > 0) {
        const auto guard = writeLockConst();
        return getMarkSql(id);
    }
    return nullptr;
}

// :352-372
vector::VectorDataItemPtr MarkDbImplORM::markById(const MarkIdPtr& markId) const {
    if (markId) {
        qint64 id = 0;
        const auto* const internal = dynamic_cast<const IMarkInternalORM*>(markId.get());
        if (internal != nullptr) {
            id = internal->id();
        }
        if (id > 0) {
            const auto guard = writeLockConst();
            return getMarkSql(id);
        }
    }
    return nullptr;
}

// :374-403
vector::VectorDataItemPtr MarkDbImplORM::firstMarkByName(const QString& name,
                                                         const vector::CategoryPtr& category) const {
    const auto* categoryInternal = dynamic_cast<const IMarkCategoryInternalORM*>(category.get());
    if (categoryInternal != nullptr) {
        if (categoryInternal->dbId() != dbId_ || categoryInternal->id() == 0) {
            categoryInternal = nullptr;
        }
    }

    const auto guard = writeLockConst();
    if (categoryInternal != nullptr) {
        return getMarkSql(0, name, categoryInternal->id());
    }
    return getMarkSql(0, name, 0);
}

// :409-529
OrmMarkRec MarkDbImplORM::ormMarkRecFromMark(const vector::IVectorDataItem& mark) {
    OrmMarkRec rec;

    // :437-447
    const vector::VectorDataItemMainInfoPtr mainInfo = mark.mainInfo();
    if (const auto* const internal = dynamic_cast<const IMarkInternalORM*>(mainInfo.get())) {
        rec.markId = internal->id();
        rec.categoryId = internal->categoryId();
        rec.visible = internal->visible();
    } else if (const auto* const withCategory =
                   dynamic_cast<const vector::IVectorDataItemWithCategory*>(mainInfo.get())) {
        const vector::CategoryPtr category = withCategory->category();
        if (const auto* const categoryInternal =
                dynamic_cast<const IMarkCategoryInternalORM*>(category.get())) {
            rec.categoryId = categoryInternal->id();
        }
    }

    // :449-452 Assert(False); Exit (off in the shipped build): the record is returned as far as it is filled
    if (rec.categoryId == 0) {
        return rec;
    }

    // :454: AMark.Geometry.Bounds on a nil geometry is an access violation. Legacy callers catch it: the
    // facade wraps UpdateMark / UpdateMarkList in `except CatchException` (u_MarkDbByImpl.pas:503-516,
    // :518-534). A reproduction (no D row): the port throws, as the helper does for the same nil
    // (insertMarkSql).
    const geo::GeometryLonLatPtr geometry = mark.geometry();
    if (!geometry) {
        throw MarkSystemORMError("MarkSystemORM: Geometry is empty!");
    }

    rec.name = mark.name();
    rec.desc = mark.desc();
    rec.geometry = geometry;

    const vector::AppearancePtr appearance = mark.appearance();

    if (dynamic_cast<const geo::IGeometryLonLatPoint*>(geometry.get()) != nullptr) { // :463-486
        quint32 textColor = 0;
        quint32 textBgColor = 0;
        qint32 fontSize = 0;
        qint32 markerSize = 0;

        if (const auto* const caption =
                dynamic_cast<const vector::IAppearancePointCaption*>(appearance.get())) {
            textColor = caption->textColor();
            textBgColor = caption->textBgColor();
            fontSize = caption->fontSize();
        }
        if (const auto* const icon = dynamic_cast<const vector::IAppearancePointIcon*>(appearance.get())) {
            markerSize = icon->markerSize();
            rec.picName = icon->picName();
        }

        rec.color1 = textColor;
        rec.color2 = textBgColor;
        rec.scale1 = fontSize;
        rec.scale2 = markerSize;
        rec.geoType = OrmGeoType::Point;
        rec.geoCount = 1;
    } else if (dynamic_cast<const geo::IGeometryLonLatLine*>(geometry.get()) != nullptr) { // :488-503
        quint32 lineColor = 0;
        qint32 lineWidth = 0;

        if (const auto* const line = dynamic_cast<const vector::IAppearanceLine*>(appearance.get())) {
            lineColor = line->lineColor();
            lineWidth = line->lineWidth();
        }

        rec.color1 = lineColor;
        rec.color2 = 0;
        rec.scale1 = lineWidth;
        rec.scale2 = 0;
        rec.geoType = OrmGeoType::Line;
        rec.geoCount = calcMultiGeometryCount(geometry);
    } else if (dynamic_cast<const geo::IGeometryLonLatPolygon*>(geometry.get()) != nullptr) { // :505-525
        quint32 lineColor = 0;
        qint32 lineWidth = 0;
        quint32 fillColor = 0;

        if (const auto* const border =
                dynamic_cast<const vector::IAppearancePolygonBorder*>(appearance.get())) {
            lineColor = border->lineColor();
            lineWidth = border->lineWidth();
        }
        if (const auto* const fill = dynamic_cast<const vector::IAppearancePolygonFill*>(appearance.get())) {
            fillColor = fill->fillColor();
        }

        rec.color1 = lineColor;
        rec.color2 = fillColor;
        rec.scale1 = lineWidth;
        rec.scale2 = 0;
        rec.geoType = OrmGeoType::Poly;
        rec.geoCount = calcMultiGeometryCount(geometry);
    } else { // :526-528
        throw MarkSystemORMError("MarkSystemORM: Unknown geometry type!");
    }
    return rec;
}

// :531-550
bool MarkDbImplORM::getMarkInternalId(const MarkOrId& mark, qint64& id) const {
    id = kEmptyId;
    const IMarkInternalORM* internal = nullptr;
    const vector::IVectorDataItem* item = nullptr;
    if (const auto* const markId = std::get_if<MarkIdPtr>(&mark)) {
        internal = dynamic_cast<const IMarkInternalORM*>(markId->get());
        if (internal == nullptr) {
            item = dynamic_cast<const vector::IVectorDataItem*>(markId->get());
        }
    } else {
        const auto& dataItem = std::get<vector::VectorDataItemPtr>(mark);
        internal = dynamic_cast<const IMarkInternalORM*>(dataItem.get());
        if (internal == nullptr) {
            item = dataItem.get();
        }
    }
    if (internal == nullptr) { // the else branch of :541-548
        internal = internalOf(item);
    }
    if (internal != nullptr && internal->dbId() == dbId_) {
        id = internal->id();
    }
    return id != kEmptyId;
}

// :552-625
vector::VectorDataItemPtr MarkDbImplORM::updateMarkImpl(const MarkOrId& oldMark,
                                                        const vector::VectorDataItemPtr& newMark,
                                                        bool& isChanged, bool useTransactions) {
    isChanged = false;

    // :568-575; the Assert(False) of the failing branch is off: nothing is done
    qint64 idOld = 0;
    if (isAssigned(oldMark)) {
        if (!getMarkInternalId(oldMark, idOld)) {
            return nullptr;
        }
    }

    // :577-592
    vector::VectorDataItemPtr newInternal;
    if (newMark) {
        if (!state_->writeAccess()) {
            return nullptr;
        }
        newInternal = factory_->createInternalMark(newMark);
        if (!newInternal) {
            return nullptr;
        }
    }

    vector::VectorDataItemPtr result;
    if (idOld == 0 && newInternal) { // INSERT
        OrmMarkRec recNew = ormMarkRecFromMark(*newInternal);
        if (helper_.insertMarkSql(recNew, useTransactions)) {
            result = factory_->createMark(recNew);
            isChanged = true;
        }
    } else if (idOld > 0 && !newInternal) { // DELETE
        isChanged = helper_.deleteMarkSql(idOld, useTransactions);
    } else if (idOld > 0 && newInternal) {
        const vector::VectorDataItemPtr oldFromDb = getMarkSql(idOld); // fetch mark from db
        if (!oldFromDb) {
            return nullptr; // Assert(False, 'Old mark not in DB') is off
        }
        if (oldFromDb->isEqual(newInternal.get())) {
            result = oldFromDb; // nothing to do
        } else {                // UPDATE
            const OrmMarkRec recOld = ormMarkRecFromMark(*oldFromDb);
            OrmMarkRec recNew = ormMarkRecFromMark(*newInternal);
            if (helper_.updateMarkSql(recOld, recNew)) {
                result = factory_->createMark(recNew);
                isChanged = true;
            }
        }
    }
    return result;
}

// :627-645
vector::VectorDataItemPtr MarkDbImplORM::updateMark(const vector::VectorDataItemPtr& oldMark,
                                                    const vector::VectorDataItemPtr& newMark) {
    const auto guard = writeLock();
    bool isChanged = false;
    vector::VectorDataItemPtr result = updateMarkImpl(oldMark, newMark, isChanged, true);
    if (isChanged) {
        setChanged();
    }
    return result;
}

// :647-746
MarkList MarkDbImplORM::updateMarkList(const MarkOrIdList& oldList, const MarkList& newList) {
    if (newList) {
        const int newCount = static_cast<int>(newList->size());
        std::vector<vector::VectorDataItemPtr> temp;
        temp.reserve(static_cast<std::size_t>(newCount));
        bool doNotify = false;

        const auto guard = writeLock();
        TransactionRec transaction = transaction_.start(helper_.isReadOnly());
        try {
            int minCount = 0;
            int maxCount = newCount;
            if (oldList) {
                if (oldList.count() < newCount) {
                    minCount = oldList.count();
                    maxCount = newCount;
                } else {
                    minCount = newCount;
                    maxCount = oldList.count();
                }
            }
            bool isChanged = false;
            for (int i = 0; i < minCount; ++i) {
                if (doNotify && i % 1000 == 0) {
                    transaction_.commit(transaction);
                    transaction = transaction_.start(helper_.isReadOnly());
                }
                vector::VectorDataItemPtr result =
                    updateMarkImpl(oldList[i], (*newList)[static_cast<std::size_t>(i)], isChanged, false);
                doNotify = doNotify || isChanged;
                temp.push_back(std::move(result));
            }
            for (int i = minCount; i < maxCount; ++i) {
                MarkOrId oldItem{vector::VectorDataItemPtr{}};
                if (oldList && i < oldList.count()) {
                    oldItem = oldList[i];
                }
                vector::VectorDataItemPtr newItem;
                if (i < newCount) {
                    newItem = (*newList)[static_cast<std::size_t>(i)];
                }
                vector::VectorDataItemPtr result = updateMarkImpl(oldItem, newItem, isChanged, false);
                doNotify = doNotify || isChanged;
                if (i < newCount) { // legacy: I < VTemp.Capacity (= the new count, set at :668)
                    temp.push_back(std::move(result));
                }
                if (doNotify && i % 1000 == 0) {
                    transaction_.commit(transaction);
                    transaction = transaction_.start(helper_.isReadOnly());
                }
            }
            transaction_.commit(transaction);
        } catch (...) {
            transaction_.rollBack(transaction);
            throw;
        }

        if (doNotify) {
            setChanged();
        }
        if (temp.empty()) { // MakeStaticAndClear: nil for an empty list
            return nullptr;
        }
        return std::make_shared<const std::vector<vector::VectorDataItemPtr>>(std::move(temp));
    }

    const auto guard = writeLock();
    // legacy: AOldMarkList.Count on nil (:733), an access violation without a handler of its own; the facade
    // catches it (u_MarkDbByImpl.pas:518-534). A reproduction: the port throws.
    if (!oldList) {
        throw std::runtime_error("MarkDbImplORM: both mark lists are nil");
    }
    std::vector<qint64> ids(static_cast<std::size_t>(oldList.count()));
    for (int i = 0; i < oldList.count(); ++i) {
        if (!getMarkInternalId(oldList[i], ids[static_cast<std::size_t>(i)])) {
            return nullptr; // Assert(False, 'Can''t get internal MarkID') is off: nothing is deleted
        }
    }
    if (helper_.deleteMarkSql(ids)) {
        setChanged();
    }
    return nullptr;
}

} // namespace cg::marks
