// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/u_MarkSystemORMCacheBase.pas (TOrmCacheBase, TOrmCacheBaseWithPreparedByCategory,
// TIDDynArrayObject, TDynArrayByRecWithPointer) and the part of mORMot's TDynArray they use
// (mormot.core.data.pas, "sorted by the first field, Int64 compare" mode).
//
// FEAT-GEO-034 (enabler; the feature is held open). The caches have no lock of their own: they are
// reached only under the write lock of their database (docs/specs/marks.md §2.9.7, §3).
//
// Line numbers below are those of u_MarkSystemORMCacheBase.pas unless a file is named.
//
// Rules shared by every class here (legacy-defects L-157, L-158 (instance sizes: legacy literals, handoff
// T-0063 decision 3), docs/handoffs/T-0063.md):
//  - rows are kept sorted by the id = the FIRST field of the row (here: the member `id`), compared as
//    a signed 64-bit integer;
//  - the legacy array has a length (capacity) and a separately remembered count; the port keeps
//    exactly `count` rows in a std::vector, so size() == count and capacity() is never consulted.

#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace cg::marks {

inline constexpr qint64 kUnlimCacheSize = std::numeric_limits<qint64>::max(); // CUnlimCacheSize

// The key of a row of a DynArray: the element itself for the plain id array, the member `id` of the
// legacy first field otherwise.
template <class T> [[nodiscard]] constexpr qint64 dynArrayKey(const T& item) {
    if constexpr (std::is_same_v<T, qint64>) {
        return item;
    } else {
        return item.id;
    }
}

// The subset of mORMot TDynArray (with an external count, Sorted = True) used by the cache units.
// Every method states the TDynArray routine it ports.
template <class T> class DynArray {
public:
    [[nodiscard]] int count() const { return static_cast<int>(items_.size()); }
    [[nodiscard]] std::vector<T>& items() { return items_; }
    [[nodiscard]] const std::vector<T>& items() const { return items_; }

    // Find (mormot.core.data.pas:7287-7316): up to 10 rows a linear scan returning the first match,
    // more rows the binary search SortFind (:7265-7285). They differ only for duplicate ids.
    [[nodiscard]] int find(qint64 id) const {
        const int n = count();
        if (n <= 10) {
            for (int i = 0; i < n; ++i) {
                if (dynArrayKey(items_[static_cast<size_t>(i)]) == id) {
                    return i;
                }
            }
            return -1;
        }
        int l = 0;
        int r = n - 1;
        do {
            const int m = (l + r) >> 1;
            const qint64 k = dynArrayKey(items_[static_cast<size_t>(m)]);
            if (k == id) {
                return m;
            }
            if (k > id) {
                r = m - 1;
            } else {
                l = m + 1;
            }
        } while (l <= r);
        return -1;
    }

    // FastLocateSorted (:7464-7516): true + index of an equal row, or false + the insert position.
    // The array is always sorted at the call boundaries of the legacy code, so the "-1 = not sorted"
    // answer of mORMot does not occur.
    [[nodiscard]] bool fastLocateSorted(qint64 id, int& index) const {
        int n = count();
        if (n == 0) {
            index = 0;
            return false;
        }
        --n;
        const qint64 last = dynArrayKey(items_[static_cast<size_t>(n)]);
        if (id >= last) {
            index = n;
            if (id == last) {
                return true;
            }
            ++index;
            return false;
        }
        index = 0;
        do {
            const int i = (index + n) >> 1;
            const qint64 k = dynArrayKey(items_[static_cast<size_t>(i)]);
            if (id == k) {
                index = i;
                return true;
            }
            if (id > k) {
                index = i + 1;
            } else {
                n = i - 1;
            }
        } while (index <= n);
        return false;
    }

    // Insert (:6707-6730) + FastAddSorted: an index outside [0, count) appends.
    void fastAddSorted(int index, T item) {
        const auto pos = (index >= 0 && index < count()) ? index : count();
        items_.insert(items_.begin() + pos, std::move(item));
    }

    // FastLocateOrAddSorted (:7530-7543): the index of the row, added if it was missing.
    int fastLocateOrAddSorted(const T& item) {
        int index = 0;
        if (!fastLocateSorted(dynArrayKey(item), index)) {
            fastAddSorted(index, item);
        }
        return index;
    }

    // Delete (:6748-6781) + FastDeleteSorted: an index outside [0, count) does nothing.
    void fastDeleteSorted(int index) {
        if (index >= 0 && index < count()) {
            items_.erase(items_.begin() + index);
        }
    }

    // AddArray (:8458-8481): appends `source[start .. start+count)`; count < 0 or past the end means
    // "to the end"; a start at or past the end appends nothing. Returns the number appended.
    // D-66 (negative start): legacy would read before the array; here nothing is appended.
    int addArray(const std::vector<T>& source, int start = 0, int count = -1) {
        const int len = static_cast<int>(source.size());
        if (start < 0 || start >= len) {
            return 0;
        }
        if (count < 0 || count > len - start) {
            count = len - start;
        }
        if (count <= 0) {
            return 0;
        }
        items_.insert(items_.end(), source.begin() + start, source.begin() + start + count);
        return count;
    }

    // New (:6652): appends a zero-initialised row, returns its index.
    int newRow() {
        items_.emplace_back();
        return count() - 1;
    }

    // Sort (:7797-7801): by id, signed; the legacy quick sort is not stable, duplicate ids (which
    // the legacy callers never produce) keep their order here.
    void sort() {
        std::stable_sort(items_.begin(), items_.end(),
                         [](const T& a, const T& b) { return dynArrayKey(a) < dynArrayKey(b); });
    }

    // Clear (:6732): releases the storage.
    void clear() { std::vector<T>().swap(items_); }

private:
    std::vector<T> items_;
};

// TOrmCacheBase (:41-69, :181-263). `Row` has `qint64 id` as its first member. `legacyItemSize` is the size
// of the packed Delphi record of the row (TDynArray.ItemSize): the size formula must not depend on the C++
// layout (QString is not a pointer). `legacyInstanceSize` is Delphi's InstanceSize of the DESCENDANT class
// in the shipped Win64 build (SASPlanet.exe is PE32+). The two values are DERIVED from Win64 layout rules
// (8-byte VMT pointer, natural alignment, hidden TMonitor pointer last, TDynArray = 4 pointers + 2 booleans
// = 40 bytes, mormot.core.data.pas:965-969), NOT measured with a compiler; a later Windows measurement
// may correct them. They decide WHEN a limited cache empties itself. Only TOrmMarkCache is limited
// (30 %, Marks/u_MarkDbImplORMCache.pas:291-300): 152 = 8 (VMT) + 8 (FCount, padded) + 40 (FRow) +
// 8 (FIsPrepared, padded) + 8 (FDataSize) + 8 (FMaxCacheSize) | + 8 (FPreparedCategoriesCount, padded)
// + 8 (FPreparedCategories) + 40 (FPreparedCategoriesDynArr) | + 8 (FRows) | + 8 (monitor). The unlimited
// caches never compare (Size > High(Int64) is false), so their value is immaterial; they pass their
// derived size too.
template <class Row> class OrmCacheBase {
public:
    OrmCacheBase(qint64 maxCacheSize, int legacyInstanceSize, int legacyItemSize)
        : maxCacheSize_(maxCacheSize), legacyInstanceSize_(legacyInstanceSize),
          legacyItemSize_(legacyItemSize) {}
    virtual ~OrmCacheBase() = default;
    OrmCacheBase(const OrmCacheBase&) = delete;
    OrmCacheBase& operator=(const OrmCacheBase&) = delete;

    // Reset (:208-217)
    virtual void reset() {
        row_.clear();
        dataSize_ = 0;
        isPrepared_ = false;
    }

    // Delete (:253-262)
    virtual void remove(qint64 id) {
        const int i = row_.find(id);
        if (i >= 0) {
            row_.fastDeleteSorted(i);
        }
    }

    [[nodiscard]] int count() const { return row_.count(); }
    [[nodiscard]] bool isPrepared() const { return isPrepared_; }
    [[nodiscard]] std::vector<Row>& rows() { return row_.items(); } // property Rows (valid up to count())

    // GetSize (:203-206): InstanceSize + ItemSize * Count + DataSize (legacy literals, see above).
    [[nodiscard]] qint64 size() const {
        return legacyInstanceSize_ + static_cast<qint64>(legacyItemSize_) * count() + dataSize_;
    }

protected:
    // CheckCacheSize (:219-233): called by the descendants at the start of every add. Over the limit
    // the whole cache is emptied through the (virtual) reset(), prepared flags included.
    void checkCacheSize() {
        if (size() > maxCacheSize_) {
            reset();
        }
    }

    DynArray<Row> row_; // FRow / FRows / FCount
    bool isPrepared_ = false;
    qint64 dataSize_ = 0;
    qint64 maxCacheSize_;

private:
    int legacyInstanceSize_;
    int legacyItemSize_;
};

// TOrmCacheBaseWithPreparedByCategory (:75-90, :265-296)
template <class Row> class OrmCacheBaseWithPreparedByCategory : public OrmCacheBase<Row> {
public:
    using OrmCacheBase<Row>::OrmCacheBase;

    void reset() override {
        OrmCacheBase<Row>::reset();
        preparedCategories_.clear();
    }

    [[nodiscard]] bool isCategoryPrepared(qint64 categoryId) const {
        return preparedCategories_.find(categoryId) >= 0;
    }

protected:
    DynArray<qint64> preparedCategories_; // FPreparedCategoriesDynArr, sorted
};

// TIDDynArrayObject (:93-115, :299-392): sorted array of ids, no duplicates through add().
class IdDynArrayObject {
public:
    [[nodiscard]] bool find(qint64 id) const;
    void add(qint64 id);
    void addArray(const std::vector<qint64>& array, int startIndex = 0, int count = -1);
    void remove(qint64 id); // legacy Delete
    void reset();

    [[nodiscard]] const std::vector<qint64>& idArray() const { return array_.items(); } // valid up to count()
    [[nodiscard]] int count() const { return array_.count(); }

private:
    DynArray<qint64> array_;
};

// TRecWithPointer (:117-124). `Data` is the legacy Pointer: an object owned by the cache (dpObj) or a
// counted interface (dpIntf); both are a shared_ptr here, `nullptr` is the nil pointer.
template <class Data> struct RecWithPointer {
    qint64 id = 0;
    int size = 0;
    std::shared_ptr<Data> data;
};

// TDynArrayByRecWithPointer (:131-170, :394-642). The dpCustom kind and the OnItemDelete callback
// have no user in the legacy tree (both users pass nil) and are not ported.
template <class Data> class DynArrayByRecWithPointer {
public:
    using Rec = RecWithPointer<Data>;

    explicit DynArrayByRecWithPointer(qint64 maxCacheSize) : maxCacheSize_(maxCacheSize) {}
    DynArrayByRecWithPointer(const DynArrayByRecWithPointer&) = delete;
    DynArrayByRecWithPointer& operator=(const DynArrayByRecWithPointer&) = delete;

    // Find (:418-428): the pointer stays valid until the next add / delete / reset.
    [[nodiscard]] bool find(qint64 id, Rec*& rec) {
        const int i = array_.find(id);
        if (i >= 0) {
            rec = &array_.items()[static_cast<size_t>(i)];
            return true;
        }
        return false;
    }

    // Add (:430-486). CheckCacheSize runs first, even for an update of an existing row.
    void add(qint64 id, std::shared_ptr<Data> data, int size, bool updateIfExists) {
        checkCacheSize();
        int i = 0;
        if (!array_.fastLocateSorted(id, i)) {
            Rec rec;
            rec.id = id;
            rec.size = size;
            rec.data = std::move(data);
            const bool hasData = rec.data != nullptr;
            array_.fastAddSorted(i, std::move(rec));
            if (hasData) {
                dataSize_ += size;
            }
        } else if (updateIfExists) {
            Rec& rec = array_.items()[static_cast<size_t>(i)];
            if (rec.data != nullptr) {
                rec.data.reset();
                dataSize_ -= rec.size;
            }
            rec.data = std::move(data);
            rec.size = size;
            if (rec.data != nullptr) {
                dataSize_ += size;
            }
        }
    }

    // AddArray (:488-552). Rows whose id is already present are skipped (their data is not taken).
    void addArray(const std::vector<Rec>& array) {
        checkCacheSize();
        const int vsize = static_cast<int>(array.size());
        if (vsize <= 0) {
            return;
        }
        if (array_.count() == 0) {
            addFull(array);
            return;
        }
        std::vector<bool> bits(array.size(), false);
        int vcount = 0;
        for (int i = 0; i < vsize; ++i) {
            if (array_.find(array[static_cast<size_t>(i)].id) < 0) {
                bits[static_cast<size_t>(i)] = true;
                ++vcount;
            }
        }
        if (vcount == vsize) {
            addFull(array);
        } else if (vcount > 0) {
            // legacy: Capacity := Count + VCount (only a reservation; capacity() is never read)
            const int newCount = array_.count() + vcount;
            array_.items().reserve(static_cast<size_t>(newCount));
            for (int i = 0; i < vsize; ++i) {
                if (bits[static_cast<size_t>(i)]) {
                    const int j = array_.newRow();
                    Rec& rec = array_.items()[static_cast<size_t>(j)];
                    rec = array[static_cast<size_t>(i)];
                    if (rec.data != nullptr) {
                        dataSize_ += rec.size;
                    }
                }
            }
            array_.sort();
        }
    }

    // Delete (:554-582)
    void remove(qint64 id) {
        const int i = array_.find(id);
        if (i >= 0) {
            Rec& rec = array_.items()[static_cast<size_t>(i)];
            if (rec.data != nullptr) {
                rec.data.reset();
                dataSize_ -= rec.size;
            }
            array_.fastDeleteSorted(i);
        }
    }

    // Reset (:584-622): releases every data pointer, empties the array, DataSize = 0.
    void reset() {
        array_.clear();
        dataSize_ = 0;
    }

    [[nodiscard]] std::vector<Rec>& rows() { return array_.items(); } // valid up to count()
    [[nodiscard]] int count() const { return array_.count(); }

    // GetSize (:624-627): InstanceSize + packed TRecWithPointer (8 + 4 + 8 = 20 bytes, Win64) * Count +
    // DataSize. InstanceSize 112 is DERIVED by Win64 layout rules, not measured with a compiler: 8 (VMT) +
    // 8 (FCount, padded) + 8 (FArray) + 40 (FDynArray) + 16 (FOnDelete, a method pointer) + 8 (FDataType,
    // padded) + 8 (FDataSize) + 8 (FMaxCacheSize) + 8 (monitor). This is the geometry cache (70 %,
    // Marks/u_MarkDbImplORMCache.pas:291-300), the only limited instance of this class.
    [[nodiscard]] qint64 size() const { return kLegacyInstanceSize + kLegacyItemSize * count() + dataSize_; }

private:
    static constexpr qint64 kLegacyInstanceSize = 112;
    static constexpr qint64 kLegacyItemSize = 20;

    // CheckCacheSize (:629-642)
    void checkCacheSize() {
        if (size() > maxCacheSize_) {
            reset();
        }
    }

    // AddFull (local procedure of AddArray)
    void addFull(const std::vector<Rec>& array) {
        array_.addArray(array);
        array_.sort();
        for (const Rec& rec : array) {
            if (rec.data != nullptr) {
                dataSize_ += rec.size;
            }
        }
    }

    DynArray<Rec> array_;
    qint64 dataSize_ = 0;
    qint64 maxCacheSize_;
};

} // namespace cg::marks
