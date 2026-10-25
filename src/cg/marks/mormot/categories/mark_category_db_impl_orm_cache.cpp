// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/categories/mark_category_db_impl_orm_cache.h"

#include "cg/marks/mormot/mark_system_orm_log.h"

namespace cg::marks {

namespace {

// Win64 Delphi InstanceSize of the descendants, DERIVED by the layout rules of the cache base header
// (not measured): 8 VMT + 8 FCount + 40 FRow + 8 FIsPrepared + 8 FDataSize + 8 FMaxCacheSize + 8 FRows
// + 8 monitor = 96. Both caches are unlimited, so the value is never compared.
constexpr int kInstanceSize = 96;
// Packed record sizes (TDynArray.ItemSize): CategoryId 8 + Name pointer 8; 8 + 8 + 1 + 1 + 1.
constexpr int kCategoryItemSize = 16;
constexpr int kViewItemSize = 19;

} // namespace

// TOrmCategoryCache.Create (:126-129)
OrmCategoryCache::OrmCategoryCache(qint64 maxCacheSize)
    : OrmCacheBase<OrmCategoryRow>(maxCacheSize, kInstanceSize, kCategoryItemSize) {}

// TOrmCategoryCache.AddOrUpdate (:131-162); the Length(Name) * SizeOf(Char) of Delphi is UTF-16 units * 2.
void OrmCategoryCache::addOrUpdate(const OrmCategoryRec& rec) {
    ormLogEnter("OrmCategoryCache::addOrUpdate");
    checkCacheSize();
    qint64 size = rec.name.size() * 2;
    int i = 0;
    if (row_.fastLocateSorted(rec.categoryId, i)) {
        OrmCategoryRow& row = rows()[static_cast<size_t>(i)];
        size -= row.name.size() * 2;
        row.name = rec.name;
        dataSize_ += size;
    } else {
        row_.fastAddSorted(i, OrmCategoryRow{rec.categoryId, rec.name});
        dataSize_ += size;
    }
}

// TOrmCategoryCache.AddPrepared (:164-183). DataSize is not counted for the added rows (legacy
// does not either; immaterial, the cache is unlimited).
void OrmCategoryCache::addPrepared(const std::vector<OrmCategoryRow>& rowsToAdd) {
    ormLogEnter("OrmCategoryCache::addPrepared");
    reset();
    if (rowsToAdd.size() == 1) {
        row_.fastAddSorted(0, rowsToAdd[0]);
    } else if (rowsToAdd.size() > 1) {
        row_.addArray(rowsToAdd);
        row_.sort();
    }
    isPrepared_ = true;
}

// TOrmCategoryCache.Find by id (:185-201)
OrmCategoryRow* OrmCategoryCache::find(qint64 id) {
    ormLogEnter("OrmCategoryCache::find");
    ormLogEnter("OrmCategoryCache::find");
    const int i = row_.find(id);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

// TOrmCategoryCache.Find by name (:203-221): first row in id order whose name is AnsiSameText.
// Deviation DV-5 (spec §2.9.11). Legacy AnsiSameText = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE),
// a linguistic comparison; the port is QString::compare(Qt::CaseInsensitive): simple per-code-point case
// folding, locale-independent. Legacy equal / port different: expansions (Straße = Strasse, æ = ae,
// ligatures, digraphs), precomposed vs combining sequences, ignorable characters (soft hyphen, ZWJ); under
// a tr/az user locale the I/i/ı/İ pairs come out the opposite way. (Windows facts: documented NLS
// behaviour, not verified on this machine.)
OrmCategoryRow* OrmCategoryCache::find(const QString& name) {
    ormLogEnter("OrmCategoryCache::find");
    for (OrmCategoryRow& row : rows()) {
        if (name.compare(row.name, Qt::CaseInsensitive) == 0) {
            return &row;
        }
    }
    return nullptr;
}

// TOrmCategoryViewCache.Create (:225-228)
OrmCategoryViewCache::OrmCategoryViewCache(qint64 maxCacheSize)
    : OrmCacheBase<OrmCategoryViewRow>(maxCacheSize, kInstanceSize, kViewItemSize) {}

// TOrmCategoryViewCache.AddOrUpdate (:230-262)
void OrmCategoryViewCache::addOrUpdate(const OrmCategoryRec& rec) {
    ormLogEnter("OrmCategoryViewCache::addOrUpdate");
    checkCacheSize();
    int i = 0;
    if (row_.fastLocateSorted(rec.categoryId, i)) {
        OrmCategoryViewRow& row = rows()[static_cast<size_t>(i)];
        row.viewId = rec.viewId;
        row.visible = rec.visible;
        row.minZoom = rec.minZoom;
        row.maxZoom = rec.maxZoom;
    } else {
        row_.fastAddSorted(
            i, OrmCategoryViewRow{rec.categoryId, rec.viewId, rec.visible, rec.minZoom, rec.maxZoom});
    }
}

// TOrmCategoryViewCache.Find (:264-280)
OrmCategoryViewRow* OrmCategoryViewCache::find(qint64 categoryId) {
    ormLogEnter("OrmCategoryViewCache::find");
    ormLogEnter("OrmCategoryViewCache::find");
    const int i = row_.find(categoryId);
    return i >= 0 ? &rows()[static_cast<size_t>(i)] : nullptr;
}

// TOrmCategoryViewCache.AddPrepared (:282-301)
void OrmCategoryViewCache::addPrepared(const std::vector<OrmCategoryViewRow>& rowsToAdd) {
    ormLogEnter("OrmCategoryViewCache::addPrepared");
    reset();
    if (rowsToAdd.size() == 1) {
        row_.fastAddSorted(0, rowsToAdd[0]);
    } else if (rowsToAdd.size() > 1) {
        row_.addArray(rowsToAdd);
        row_.sort();
    }
    isPrepared_ = true;
}

} // namespace cg::marks
