// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/marks/mark_db_impl_orm_cache.h"

#include "cg/marks/mormot/mark_system_orm_log.h"

#include <algorithm>
#include <cmath>

namespace cg::marks {

namespace {

// Win64 Delphi InstanceSize of the descendants, DERIVED by the layout rules of the cache base header (not
// measured). TOrmCacheBase descendant: 8 VMT + 8 FCount + 40 FRow + 8 FIsPrepared + 8 FDataSize + 8
// FMaxCacheSize + 8 FRows + 8 monitor = 96; with the prepared-by-category fields (+8
// FPreparedCategoriesCount, +8 FPreparedCategories, +40 FPreparedCategoriesDynArr) = 152. All four caches are
// unlimited (CUnlimCacheSize, :297-304), so the values are never compared.
constexpr int kInstanceSize = 96;
constexpr int kViewInstanceSize = 152;
// Packed record sizes (TDynArray.ItemSize): ImageId 8 + Name pointer 8; AppearanceId 8 + 2 Cardinal + 2
// Integer; MarkId 8 + ViewId 8 + CategoryID 8 + Visible 1; four TID.
constexpr int kImageItemSize = 16;
constexpr int kAppearanceItemSize = 24;
constexpr int kViewItemSize = 25;
constexpr int kIdIndexItemSize = 32;
// TOrmMarkCache (limited, 30 %): instance 152 (the field list of the view cache above, derived in
// mark_system_orm_cache_base.h) and the packed TOrmMarkRow: MarkId 8 + Name 8 + Desc 8 + GeoType 1 +
// GeoCount 4 = 29 (docs/specs/marks.md §2.9.7).
constexpr int kMarkInstanceSize = 152;
constexpr int kMarkItemSize = 29;

} // namespace

// ------------------------------------------------------------------ OrmMarkImageCache

// TOrmMarkImageCache.Create (:320-325)
OrmMarkImageCache::OrmMarkImageCache(qint64 maxCacheSize)
    : OrmCacheBase<OrmMarkImageRow>(maxCacheSize, kInstanceSize, kImageItemSize) {}

// TOrmMarkImageCache.Find by id (:327-343)
OrmMarkImageRow* OrmMarkImageCache::find(qint64 id) {
    ormLogEnter("OrmMarkImageCache::find");
    const int i = row_.find(id);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

// TOrmMarkImageCache.Find by name (:345-363): first row in id order whose name is AnsiSameText.
// D-80, the name-lookup deviation (spec §2.9.11, DV-5): AnsiSameText (CompareStringW, user locale, ignore
// case) becomes QString::compare(Qt::CaseInsensitive), see OrmCategoryCache::find for the cases that come out
// differently.
OrmMarkImageRow* OrmMarkImageCache::find(const QString& name) {
    ormLogEnter("OrmMarkImageCache::find");
    for (OrmMarkImageRow& row : rows()) {
        if (name.compare(row.name, Qt::CaseInsensitive) == 0) {
            return &row;
        }
    }
    return nullptr;
}

// TOrmMarkImageCache.AddOrIgnore (:365-368)
void OrmMarkImageCache::addOrIgnore(const OrmMarkRec& rec) {
    addOrIgnore(rec.picId, rec.picName);
}

// TOrmMarkImageCache.AddOrIgnore (:370-394): an existing id keeps its first name. The Length(Name) *
// SizeOf(Char) of Delphi is UTF-16 units * 2. The Assert(False) branch (-1 from FastLocateSorted) cannot
// happen on a sorted array.
void OrmMarkImageCache::addOrIgnore(qint64 imageId, const QString& name) {
    ormLogEnter("OrmMarkImageCache::addOrIgnore");
    checkCacheSize();
    int i = 0;
    if (row_.fastLocateSorted(imageId, i)) {
        return;
    }
    row_.fastAddSorted(i, OrmMarkImageRow{imageId, name});
    dataSize_ += name.size() * 2;
}

// TOrmMarkImageCache.Delete (:396-413)
void OrmMarkImageCache::remove(qint64 id) {
    ormLogEnter("OrmMarkImageCache::remove");
    const int i = row_.find(id);
    if (i >= 0) {
        const qint64 itemDataSize = rows()[static_cast<size_t>(i)].name.size() * 2;
        row_.fastDeleteSorted(i);
        dataSize_ -= itemDataSize;
    }
}

// ------------------------------------------------------------------ OrmMarkAppearanceCache

// TOrmMarkAppearanceCache.Create (:417-422)
OrmMarkAppearanceCache::OrmMarkAppearanceCache(qint64 maxCacheSize)
    : OrmCacheBase<OrmMarkAppearanceRow>(maxCacheSize, kInstanceSize, kAppearanceItemSize) {}

// TOrmMarkAppearanceCache.Find by id (:424-443)
OrmMarkAppearanceRow* OrmMarkAppearanceCache::find(qint64 id) {
    ormLogEnter("OrmMarkAppearanceCache::find");
    const int i = row_.find(id);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

// TOrmMarkAppearanceCache.Find by the four values (:445-469): linear, first row in id order.
OrmMarkAppearanceRow* OrmMarkAppearanceCache::find(quint32 color1, quint32 color2, qint32 scale1,
                                                   qint32 scale2) {
    ormLogEnter("OrmMarkAppearanceCache::find");
    for (OrmMarkAppearanceRow& row : rows()) {
        if (row.color1 == color1 && row.color2 == color2 && row.scale1 == scale1 && row.scale2 == scale2) {
            return &row;
        }
    }
    return nullptr;
}

// TOrmMarkAppearanceCache.AddOrIgnore (:471-474)
void OrmMarkAppearanceCache::addOrIgnore(const OrmMarkRec& rec) {
    addOrIgnore(rec.appearanceId, rec.color1, rec.color2, rec.scale1, rec.scale2);
}

// TOrmMarkAppearanceCache.AddOrIgnore (:476-506): an existing id keeps its first values.
void OrmMarkAppearanceCache::addOrIgnore(qint64 id, quint32 color1, quint32 color2, qint32 scale1,
                                         qint32 scale2) {
    ormLogEnter("OrmMarkAppearanceCache::addOrIgnore");
    checkCacheSize();
    int i = 0;
    if (row_.fastLocateSorted(id, i)) {
        return;
    }
    row_.fastAddSorted(i, OrmMarkAppearanceRow{id, color1, color2, scale1, scale2});
}

// ------------------------------------------------------------------ OrmMarkViewCache

// TOrmMarkViewCache.Create (:918-923)
OrmMarkViewCache::OrmMarkViewCache(qint64 maxCacheSize)
    : OrmCacheBaseWithPreparedByCategory<OrmMarkViewRow>(maxCacheSize, kViewInstanceSize, kViewItemSize) {}

// TOrmMarkViewCache.Find (:925-941)
OrmMarkViewRow* OrmMarkViewCache::find(qint64 markId) {
    ormLogEnter("OrmMarkViewCache::find");
    const int i = row_.find(markId);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

// TOrmMarkViewCache.AddOrUpdate (:943-946)
void OrmMarkViewCache::addOrUpdate(const OrmMarkRec& rec) {
    addOrUpdate(rec.markId, rec.viewId, rec.categoryId, rec.visible);
}

// TOrmMarkViewCache.AddOrUpdate (:948-981)
void OrmMarkViewCache::addOrUpdate(qint64 markId, qint64 viewId, qint64 categoryId, bool visible) {
    ormLogEnter("OrmMarkViewCache::addOrUpdate");
    checkCacheSize();
    int i = 0;
    if (row_.fastLocateSorted(markId, i)) {
        OrmMarkViewRow& row = rows()[static_cast<size_t>(i)];
        row.viewId = viewId;
        row.categoryId = categoryId;
        row.visible = visible;
    } else {
        row_.fastAddSorted(i, OrmMarkViewRow{markId, viewId, categoryId, visible});
    }
}

// TOrmMarkViewCache.AddPrepared (:983-1047). categoryId > 0: the category joins the prepared list and the
// rows whose mark id is not cached yet are added (the cached ones keep their values); any other value
// (0 = the whole table): the cache is replaced by `rows` and flagged prepared. The ids inside `rows` are not
// de-duplicated among themselves (L-157).
void OrmMarkViewCache::addPrepared(qint64 categoryId, const std::vector<OrmMarkViewRow>& rowsToAdd) {
    ormLogEnter("OrmMarkViewCache::addPrepared");
    checkCacheSize();
    const int count = static_cast<int>(rowsToAdd.size());
    if (categoryId > 0) {
        int i = 0;
        if (!preparedCategories_.fastLocateSorted(categoryId, i)) {
            preparedCategories_.fastAddSorted(i, categoryId);
        }
        if (count <= 0) {
            return;
        }
        std::vector<bool> bits(rowsToAdd.size(), false);
        int missing = 0;
        for (int k = 0; k < count; ++k) {
            if (row_.find(rowsToAdd[static_cast<size_t>(k)].id) < 0) {
                bits[static_cast<size_t>(k)] = true;
                ++missing;
            }
        }
        if (missing == count) {
            row_.addArray(rowsToAdd);
            row_.sort();
        } else if (missing > 0) {
            for (int k = 0; k < count; ++k) {
                if (bits[static_cast<size_t>(k)]) {
                    const int j = row_.newRow();
                    rows()[static_cast<size_t>(j)] = rowsToAdd[static_cast<size_t>(k)];
                }
            }
            row_.sort();
        }
    } else {
        reset();
        if (count == 1) {
            row_.fastAddSorted(0, rowsToAdd[0]);
        } else if (count > 1) {
            row_.addArray(rowsToAdd);
            row_.sort();
        }
        isPrepared_ = true;
    }
}

// ------------------------------------------------------------------ OrmMarkIdIndex

// TOrmMarkIdIndex.Create (:510-515)
OrmMarkIdIndex::OrmMarkIdIndex(qint64 maxCacheSize)
    : OrmCacheBase<OrmMarkIdIndexRec>(maxCacheSize, kInstanceSize, kIdIndexItemSize) {}

// TOrmMarkIdIndex.Find (:517-533)
OrmMarkIdIndexRec* OrmMarkIdIndex::find(qint64 markId) {
    ormLogEnter("OrmMarkIdIndex::find");
    const int i = row_.find(markId);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

// TOrmMarkIdIndex.AddOrUpdate (:535-562)
void OrmMarkIdIndex::addOrUpdate(const OrmMarkRec& rec) {
    ormLogEnter("OrmMarkIdIndex::addOrUpdate");
    checkCacheSize();
    int i = 0;
    if (row_.fastLocateSorted(rec.markId, i)) {
        OrmMarkIdIndexRec& row = rows()[static_cast<size_t>(i)];
        row.categoryId = rec.categoryId;
        row.imageId = rec.picId;
        row.appearanceId = rec.appearanceId;
    } else {
        row_.fastAddSorted(i, markRecToIndexRec(rec));
    }
}

// TOrmMarkIdIndex.AddPrepared (:564-580): the whole table replaces the content (no CheckCacheSize).
void OrmMarkIdIndex::addPrepared(const std::vector<OrmMarkIdIndexRec>& rowsToAdd) {
    ormLogEnter("OrmMarkIdIndex::addPrepared");
    reset();
    if (rowsToAdd.size() == 1) {
        row_.fastAddSorted(0, rowsToAdd[0]);
    } else if (rowsToAdd.size() > 1) {
        row_.addArray(rowsToAdd);
        row_.sort();
    }
    isPrepared_ = true;
}

// TOrmMarkIdIndex.AddArray (:582-635): adds the rows of the window whose mark id is not cached yet; no
// CheckCacheSize, the prepared flag is not touched.
// D-66: a negative startIndex, or a window running over the end of the array, is limited to the array;
// legacy reads AArray[AStartIndex+I] out of range (:613, :626; mORMot AddArray called at :605, :619). No
// legacy caller passes a window (u_MarkDbImplORMHelper.pas:1443 passes the whole array).
void OrmMarkIdIndex::addArray(const std::vector<OrmMarkIdIndexRec>& array, int startIndex, int count) {
    ormLogEnter("OrmMarkIdIndex::addArray");
    if (startIndex < 0) {
        return;
    }
    int vsize = count < 0 ? static_cast<int>(array.size()) - startIndex : count;
    if (vsize <= 0) {
        return;
    }
    vsize = std::min(vsize, static_cast<int>(array.size()) - startIndex);
    if (vsize <= 0) {
        return;
    }
    if (row_.count() == 0) {
        row_.addArray(array, startIndex, vsize);
        row_.sort();
        return;
    }
    std::vector<bool> bits(static_cast<size_t>(vsize), false);
    int missing = 0;
    for (int k = 0; k < vsize; ++k) {
        if (row_.find(array[static_cast<size_t>(startIndex) + static_cast<size_t>(k)].id) < 0) {
            bits[static_cast<size_t>(k)] = true;
            ++missing;
        }
    }
    if (missing == vsize) {
        row_.addArray(array, startIndex, vsize);
        row_.sort();
    } else if (missing > 0) {
        // legacy: Capacity := FCount + VCount (only a reservation; capacity() is never read)
        for (int k = 0; k < vsize; ++k) {
            if (bits[static_cast<size_t>(k)]) {
                const int j = row_.newRow();
                rows()[static_cast<size_t>(j)] =
                    array[static_cast<size_t>(startIndex) + static_cast<size_t>(k)];
            }
        }
        row_.sort();
    }
}

// TOrmMarkIdIndex.MarkRecToIndexRec (:637-646)
OrmMarkIdIndexRec OrmMarkIdIndex::markRecToIndexRec(const OrmMarkRec& rec) {
    return OrmMarkIdIndexRec{rec.markId, rec.categoryId, rec.picId, rec.appearanceId};
}

// ------------------------------------------------------------------ OrmMarkIdByCategoryIndex

// TOrmMarkIdByCategoryIndex.Create (:650-654): dpObj, the arrays are owned by the index.
OrmMarkIdByCategoryIndex::OrmMarkIdByCategoryIndex(qint64 maxCacheSize) : array_(maxCacheSize) {}

// TOrmMarkIdByCategoryIndex.Find (:662-685). Legacy asserts Data <> nil and dereferences it; every row is
// created in addPrepared with an array, so no guard here.
bool OrmMarkIdByCategoryIndex::find(qint64 categoryId, const std::vector<qint64>*& ids, int& count) {
    ormLogEnter("OrmMarkIdByCategoryIndex::find");
    DynArrayByRecWithPointer<IdDynArrayObject>::Rec* item = nullptr;
    if (array_.find(categoryId, item)) {
        ids = &item->data->idArray();
        count = item->data->count();
        return true;
    }
    return false;
}

// TOrmMarkIdByCategoryIndex.AddPrepared (:687-727): a known category gets its array emptied and refilled
// with the window; a new one gets a new array (size 0, updateIfExists).
void OrmMarkIdByCategoryIndex::addPrepared(qint64 categoryId, const std::vector<qint64>& markIds,
                                           int startIndex, int count) {
    ormLogEnter("OrmMarkIdByCategoryIndex::addPrepared");
    DynArrayByRecWithPointer<IdDynArrayObject>::Rec* item = nullptr;
    if (array_.find(categoryId, item)) {
        item->data->reset();
        item->data->addArray(markIds, startIndex, count);
    } else {
        auto array = std::make_shared<IdDynArrayObject>();
        array->addArray(markIds, startIndex, count);
        array_.add(categoryId, std::move(array), 0, true);
    }
}

// TOrmMarkIdByCategoryIndex.Add (:729-745): only into an already prepared category.
void OrmMarkIdByCategoryIndex::add(qint64 categoryId, qint64 markId) {
    ormLogEnter("OrmMarkIdByCategoryIndex::add");
    DynArrayByRecWithPointer<IdDynArrayObject>::Rec* item = nullptr;
    if (array_.find(categoryId, item)) {
        item->data->add(markId);
    }
}

// TOrmMarkIdByCategoryIndex.Delete (:747-763)
void OrmMarkIdByCategoryIndex::remove(qint64 categoryId, qint64 markId) {
    ormLogEnter("OrmMarkIdByCategoryIndex::remove");
    DynArrayByRecWithPointer<IdDynArrayObject>::Rec* item = nullptr;
    if (array_.find(categoryId, item)) {
        item->data->remove(markId);
    }
}

// TOrmMarkIdByCategoryIndex.Reset (:765-767)
void OrmMarkIdByCategoryIndex::reset() {
    array_.reset();
}

// ------------------------------------------------------------------ OrmMarkCache

// TOrmMarkCache.Create (:772-775)
OrmMarkCache::OrmMarkCache(qint64 maxCacheSize)
    : OrmCacheBaseWithPreparedByCategory<OrmMarkRow>(maxCacheSize, kMarkInstanceSize, kMarkItemSize) {}

// TOrmMarkCache.Find (:777-793)
OrmMarkRow* OrmMarkCache::find(qint64 id) {
    ormLogEnter("OrmMarkCache::find");
    const int i = row_.find(id);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

namespace {
// (Length(Name) + Length(Desc)) * SizeOf(Char): UTF-16 units * 2.
qint64 textBytes(const QString& name, const QString& desc) {
    return (static_cast<qint64>(name.size()) + desc.size()) * 2;
}
} // namespace

// TOrmMarkCache.AddOrUpdate (:795-825). CheckCacheSize first (even for an update); an update adds the
// difference of the text sizes. The Assert(False) branch (-1 from FastLocateSorted) cannot happen.
void OrmMarkCache::addOrUpdate(const OrmMarkRec& rec) {
    ormLogEnter("OrmMarkCache::addOrUpdate");
    checkCacheSize();
    qint64 size = textBytes(rec.name, rec.desc);
    int i = 0;
    if (row_.fastLocateSorted(rec.markId, i)) {
        OrmMarkRow& row = rows()[static_cast<size_t>(i)];
        size -= textBytes(row.name, row.desc);
        row = markRecToMarkRow(rec);
        dataSize_ += size;
    } else {
        row_.fastAddSorted(i, markRecToMarkRow(rec));
        dataSize_ += size;
    }
}

// TOrmMarkCache.AddPrepared (:827-902). CheckCacheSize first. categoryId > 0: the category joins the
// prepared list and only the rows whose mark id is not cached yet are added (their text counts); any other
// value: Reset, the whole table replaces the content and is flagged prepared. The ids inside `rows` are
// not de-duplicated among themselves (L-157).
void OrmMarkCache::addPrepared(qint64 categoryId, const std::vector<OrmMarkRow>& rowsToAdd) {
    ormLogEnter("OrmMarkCache::addPrepared");
    checkCacheSize();
    const int count = static_cast<int>(rowsToAdd.size());
    if (categoryId > 0) {
        int i = 0;
        if (!preparedCategories_.fastLocateSorted(categoryId, i)) {
            preparedCategories_.fastAddSorted(i, categoryId);
        }
        if (count == 0) {
            return;
        }
        std::vector<bool> bits(rowsToAdd.size(), false);
        int missing = 0;
        for (int k = 0; k < count; ++k) {
            if (row_.find(rowsToAdd[static_cast<size_t>(k)].id) < 0) {
                bits[static_cast<size_t>(k)] = true;
                ++missing;
            }
        }
        if (missing == count) {
            row_.addArray(rowsToAdd);
            row_.sort();
            for (const OrmMarkRow& r : rowsToAdd) {
                dataSize_ += textBytes(r.name, r.desc);
            }
        } else if (missing > 0) {
            for (int k = 0; k < count; ++k) {
                if (bits[static_cast<size_t>(k)]) {
                    const int j = row_.newRow();
                    const OrmMarkRow& r = rowsToAdd[static_cast<size_t>(k)];
                    rows()[static_cast<size_t>(j)] = r;
                    dataSize_ += textBytes(r.name, r.desc);
                }
            }
            row_.sort();
        }
    } else {
        reset();
        if (count == 1) {
            row_.fastAddSorted(0, rowsToAdd[0]);
        } else if (count > 1) {
            row_.addArray(rowsToAdd);
            row_.sort();
        }
        isPrepared_ = true;
        for (const OrmMarkRow& r : rowsToAdd) {
            dataSize_ += textBytes(r.name, r.desc);
        }
    }
}

// TOrmMarkCache.MarkRecToMarkRow (:904-914)
OrmMarkRow OrmMarkCache::markRecToMarkRow(const OrmMarkRec& rec) {
    return OrmMarkRow{rec.markId, rec.name, rec.desc, rec.geoType, rec.geoCount};
}

// ------------------------------------------------------------------ OrmMarkGeometryCache

// TOrmMarkGeometryCache.Create (:1051-1056) / Destroy (:1058-1064; the members free themselves)
OrmMarkGeometryCache::OrmMarkGeometryCache(qint64 maxCacheSize)
    : maxCacheSize_(maxCacheSize), geometryArray_(maxCacheSize) {}

// L-158 FIX, the one place of it. Legacy keeps FIsPrepared / FPreparedCategories after the inner array
// emptied itself (CheckCacheSize first in Add / AddArray, u_MarkSystemORMCacheBase.pas:430-442, :488-492,
// :629-642), so IsCategoryPrepared / IsPrepared stay true for geometries that are gone and the helper
// (u_MarkDbImplORMHelper.pas:1521, :1536) never refills them. The inner array resets exactly when its
// size() is over its limit at the start of the call; the same test is made here, one statement before the
// call, and then the prepared state goes with the rows.
void OrmMarkGeometryCache::forgetPreparedState() {
    isPrepared_ = false;
    preparedCategories_.reset();
}

// TOrmMarkGeometryCache.IsCategoryPrepared (:1066-1069)
bool OrmMarkGeometryCache::isCategoryPrepared(qint64 categoryId) const {
    return isPrepared_ || preparedCategories_.find(categoryId);
}

// TOrmMarkGeometryCache.Find (:1071-1088)
bool OrmMarkGeometryCache::find(qint64 markId, geo::GeometryLonLatPtr& geometry) {
    ormLogEnter("OrmMarkGeometryCache::find");
    DynArrayByRecWithPointer<const geo::IGeometryLonLat>::Rec* item = nullptr;
    if (geometryArray_.find(markId, item)) {
        geometry = item->data;
        return true;
    }
    return false;
}

// TOrmMarkGeometryCache.AddOrUpdate (:1090-1103): an existing row is replaced (updateIfExists).
void OrmMarkGeometryCache::addOrUpdate(qint64 markId, qint32 size, const geo::GeometryLonLatPtr& geometry) {
    ormLogEnter("OrmMarkGeometryCache::addOrUpdate");
    if (geometryArray_.size() > maxCacheSize_) {
        forgetPreparedState();
    }
    geometryArray_.add(markId, geometry, size, true);
}

// TOrmMarkGeometryCache.AddPrepared (:1105-1133): the rows go in with AddArray (ids already cached keep
// their data), then the category (> 0) or the whole table counts as prepared. The ids inside `rows` are
// not de-duplicated among themselves (L-157).
void OrmMarkGeometryCache::addPrepared(qint64 categoryId, const std::vector<OrmMarkGeometryRec>& rows) {
    ormLogEnter("OrmMarkGeometryCache::addPrepared");
    std::vector<RecWithPointer<const geo::IGeometryLonLat>> array;
    array.reserve(rows.size());
    for (const OrmMarkGeometryRec& r : rows) {
        array.push_back(RecWithPointer<const geo::IGeometryLonLat>{r.id, r.size, r.geometry});
    }
    if (geometryArray_.size() > maxCacheSize_) {
        forgetPreparedState();
    }
    geometryArray_.addArray(array);
    if (categoryId > 0) {
        preparedCategories_.add(categoryId);
    } else {
        isPrepared_ = true;
    }
}

// TOrmMarkGeometryCache.Delete (:1135-1144)
void OrmMarkGeometryCache::remove(qint64 markId) {
    ormLogEnter("OrmMarkGeometryCache::remove");
    geometryArray_.remove(markId);
}

// TOrmMarkGeometryCache.Reset (:1146-1155). Legacy clears the array and the category list but never
// FIsPrepared; the helper calls it after deleting marks (u_MarkDbImplORMHelper.pas:547), so the whole-table
// flag would outlive the rows. Cleared here as part of the L-158 fix.
void OrmMarkGeometryCache::reset() {
    ormLogEnter("OrmMarkGeometryCache::reset");
    geometryArray_.reset();
    forgetPreparedState();
}

// ------------------------------------------------------------------ OrmMarkDbCache

// TOrmMarkDbCache.Init (:286-305). Delphi Round is banker's rounding (nearbyint, default mode).
void OrmMarkDbCache::init(qint64 maxCacheSize) {
    const auto markCacheSize = static_cast<qint64>(std::nearbyint(0.3 * static_cast<double>(maxCacheSize)));
    const auto geometryCacheSize =
        static_cast<qint64>(std::nearbyint(0.7 * static_cast<double>(maxCacheSize)));

    markCache = std::make_unique<OrmMarkCache>(markCacheSize);
    markGeometryCache = std::make_unique<OrmMarkGeometryCache>(geometryCacheSize);

    // no limits for View / Image / Appearance caches
    markViewCache = std::make_unique<OrmMarkViewCache>(kUnlimCacheSize);
    markImageCache = std::make_unique<OrmMarkImageCache>(kUnlimCacheSize);
    markAppearanceCache = std::make_unique<OrmMarkAppearanceCache>(kUnlimCacheSize);

    // and no limits for the indexes
    markIdIndex = std::make_unique<OrmMarkIdIndex>(kUnlimCacheSize);
    markIdByCategoryIndex = std::make_unique<OrmMarkIdByCategoryIndex>(kUnlimCacheSize);
}

// TOrmMarkDbCache.Done (:307-316)
void OrmMarkDbCache::done() {
    markIdByCategoryIndex.reset();
    markIdIndex.reset();
    markAppearanceCache.reset();
    markImageCache.reset();
    markViewCache.reset();
    markGeometryCache.reset();
    markCache.reset();
}

} // namespace cg::marks
