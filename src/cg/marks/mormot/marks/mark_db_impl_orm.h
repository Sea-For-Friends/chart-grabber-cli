// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORM.pas (docs/specs/marks.md §2.9.10, §2.9.12). Line numbers in
// the sources are those of that unit.
//
// The mark database of the SQLite backend: TConfigDataElementBaseEmptySaveLoad (the RW lock and the change
// notification, here core::ConfigElement) implementing IMarkDbImpl and IMarkDbInternalORM. Every public
// method, readers included, takes the WRITE lock (they fill caches); the helper and its caches have no lock
// of their own. The lock is non-recursive; legacy never re-enters it in :1-750. One change notification per
// public call, fired when the write lock is released, after the commit.
//
// Ticket T-0114 (O16) defines :1-750: the constructor / destructor, onStateChange, getCategoryId, the single
// mark readers (getMarkSql, getById, markById, firstMarkByName), ormMarkRecFromMark, getMarkInternalId,
// updateMarkImpl, updateMark and updateMarkList. The class is declared WHOLE here; the methods of :752-1214
// are ticket O17's (T-0125): mark_db_impl_orm_query.cpp.
//
// Constructor: the four geometry readers / writers of legacy (:211-214) are the GeometryLonLatFactory of the
// helper (u_MarkDbImplORMHelper's geometry codecs are the free functions of cg/geo/wkb.h); the legacy
// Asserts (:245-250) are not ported (assertions are off in the shipped build, SASPlanet.dproj:92).

#include "cg/core/config_element.h"
#include "cg/core/read_write_state_internal.h"
#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_factory.h"
#include "cg/marks/mark_category_list.h"
#include "cg/marks/mark_db_impl.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_helper.h"
#include "cg/marks/mormot/marks/mark_db_internal_orm.h"
#include "cg/marks/mormot/marks/mark_factory_db_internal_orm.h"
#include "cg/vector/category.h"
#include "cg/vector/vector_data_item_simple.h"
#include "cg/vector/vector_item_subset.h"
#include "cg/vector/vector_item_subset_builder.h"

#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace cg::marks {

// TMarkDbImplORM (:59-218)
class MarkDbImplORM final : public core::ConfigElement, public IMarkDbImpl, public IMarkDbInternalORM {
public:
    // :230-283. `provider` must outlive the object. Preconditions: `state` and `factory` are non-null (the
    // only caller, u_MarkSystemImplORM.pas:183-194, passes the objects created at :144-146 and :165-174); a
    // null one throws std::invalid_argument instead of failing later.
    MarkDbImplORM(quintptr dbId, quint32 cacheSizeMb, std::shared_ptr<core::IReadWriteStateInternal> state,
                  IMarkSystemImplORMClientProvider& provider,
                  std::shared_ptr<const IMarkFactoryDbInternalORM> factory,
                  const geo::GeometryLonLatFactory& geometryFactory,
                  vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory);
    ~MarkDbImplORM() override; // :285-295

    // IMarkDbInternalORM
    [[nodiscard]] vector::VectorDataItemPtr getById(qint64 id) const override; // :339-350

    // IMarkDbImpl, :1-750 (T-0114)
    vector::VectorDataItemPtr updateMark(const vector::VectorDataItemPtr& oldMark,
                                         const vector::VectorDataItemPtr& newMark) override;  // :627-645
    MarkList updateMarkList(const MarkOrIdList& oldList, const MarkList& newList) override;   // :647-746
    [[nodiscard]] vector::VectorDataItemPtr markById(const MarkIdPtr& markId) const override; // :352-372
    [[nodiscard]] vector::VectorDataItemPtr
    firstMarkByName(const QString& name,
                    const vector::CategoryPtr& category) const override; // :374-403

    // IMarkDbImpl, :752-1214 (O17, mark_db_impl_orm_query.cpp)
    [[nodiscard]] MarkIdList allMarkIdList() const override; // :786-789
    [[nodiscard]] MarkIdList
    markIdListByCategory(const vector::CategoryPtr& category) const override; // :791-800
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryList(const MarkCategoryListPtr& categoryList,
                             bool includeHiddenMarks) const override; // :858-881
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategory(const vector::CategoryPtr& category,
                         bool includeHiddenMarks) const override; // :839-856
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryListInRect(const geo::DoubleRect& rect, const MarkCategoryListPtr& categoryList,
                                   bool includeHiddenMarks,
                                   const geo::DoublePoint& lonLatSize) const override; // :945-982
    [[nodiscard]] vector::VectorItemSubsetPtr
    markSubsetByCategoryInRect(const geo::DoubleRect& rect, const vector::CategoryPtr& category,
                               bool includeHiddenMarks,
                               const geo::DoublePoint& lonLatSize) const override; // :911-943
    [[nodiscard]] vector::VectorItemSubsetPtr
    findMarks(const QString& search, int maxCount, bool includeHiddenMarks,
              bool searchInDescription) const override;                                   // :1179-1212
    void setMarkVisibleById(const MarkIdPtr& mark, bool visible) override;                // :1065-1096
    void setMarkVisible(const vector::VectorDataItemPtr& mark, bool visible) override;    // :1032-1063
    void setMarkVisibleByIdList(const MarkOrIdList& markList, bool visible) override;     // :1098-1146
    void toggleMarkVisibleByIdList(const MarkOrIdList& markList) override;                // :1148-1173
    [[nodiscard]] bool markVisibleById(const MarkIdPtr& mark) const override;             // :1000-1010
    [[nodiscard]] bool markVisible(const vector::VectorDataItemPtr& mark) const override; // :988-998
    void setAllMarksInCategoryVisible(const vector::CategoryPtr& category,
                                      bool newVisible) override; // :1012-1030
    [[nodiscard]] core::Notifier& changeNotifier() override { return changed(); }

protected:
    void doReadConfig(const core::IConfigProviderPtr&) override {}      // EmptySaveLoad
    void doWriteConfig(const core::IConfigWriterPtr&) const override {} // EmptySaveLoad

private:
    friend struct MarkDbImplORMTestAccess; // tests/marks: the private methods

    void onStateChange(); // :297-305
    // :324-337; write lock held by the caller. id > 0 by id, else by name (and category when categoryId > 0).
    [[nodiscard]] vector::VectorDataItemPtr getMarkSql(qint64 id = 0, const QString& name = QString(),
                                                       qint64 categoryId = 0) const;
    // :307-318 (O17 callers only). The row id of an SQLite-backend category, else 0.
    [[nodiscard]] static qint64 getCategoryId(const vector::CategoryPtr& category);
    // :409-529, the appearance mapping of spec 2.9.2. Throws MarkSystemORMError "MarkSystemORM: Unknown
    // geometry type!" for a geometry that is none of point / line / polygon.
    [[nodiscard]] static OrmMarkRec ormMarkRecFromMark(const vector::IVectorDataItem& mark);
    // :531-550: the id of a mark (or mark id) of THIS database; false when it has none.
    [[nodiscard]] bool getMarkInternalId(const MarkOrId& mark, qint64& id) const;
    // :552-625
    vector::VectorDataItemPtr updateMarkImpl(const MarkOrId& oldMark,
                                             const vector::VectorDataItemPtr& newMark, bool& isChanged,
                                             bool useTransactions);
    // The const interface methods take the write lock (they fill caches).
    [[nodiscard]] WriteGuard writeLockConst() const;

    // O17 (:752-784, :806-837, :887-909)
    [[nodiscard]] MarkIdList getMarkIdList(qint64 categoryId = 0) const;
    void getMarkSubset(qint64 categoryId, bool includeHiddenMarks,
                       vector::IVectorItemSubsetBuilder& resultList) const;
    void getMarkSubsetByRect(std::vector<qint64>& categoryIds, const geo::DoubleRect& rect,
                             bool includeHiddenMarks, const geo::DoublePoint& lonLatSize,
                             vector::IVectorItemSubsetBuilder& resultList) const;

    quintptr dbId_;
    std::shared_ptr<core::IReadWriteStateInternal> state_;
    IMarkSystemORMTransaction& transaction_;
    std::shared_ptr<const IMarkFactoryDbInternalORM> factory_;
    vector::VectorItemSubsetBuilderFactoryPtr subsetBuilderFactory_;
    mutable MarkDbImplORMHelper helper_; // reached only under the write lock
    core::Subscription stateListener_;   // last: unsubscribed first
};

} // namespace cg::marks
