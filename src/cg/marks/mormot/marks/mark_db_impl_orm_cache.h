// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORMCache.pas, first part: TOrmMarkImageCache,
// TOrmMarkAppearanceCache, TOrmMarkViewCache, TOrmMarkIdIndex (docs/specs/marks.md §2.9.7). Line numbers
// below are those of that unit. The caches have no lock of their own: they are reached only under the write
// lock of their database.
//
// Not in this file yet (a later ticket appends them below): TOrmMarkIdByCategoryIndex, TOrmMarkCache,
// TOrmMarkGeometryCache and the TOrmMarkDbCache record that creates all seven.
//
// Row layout: the landed cache base keys a row by its member `id`, the FIRST field of the legacy record
// (here ImageId, AppearanceId, MarkId, MarkId). The row pointers returned by find stay valid until the next
// add / remove / reset.

#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mormot/mark_system_orm_cache_base.h"
#include "cg/marks/mormot/mark_system_orm_model.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QString>
#include <QtGlobal>

#include <memory>
#include <vector>

namespace cg::marks {

// TOrmMarkImageRow (:46-49)
struct OrmMarkImageRow {
    qint64 id = 0; // ImageId
    QString name;
};

// TOrmMarkImageCache (:53-66)
class OrmMarkImageCache : public OrmCacheBase<OrmMarkImageRow> {
public:
    explicit OrmMarkImageCache(qint64 maxCacheSize);

    [[nodiscard]] OrmMarkImageRow* find(qint64 id);           // :327-343
    [[nodiscard]] OrmMarkImageRow* find(const QString& name); // :345-363, linear, AnsiSameText
    void addOrIgnore(const OrmMarkRec& rec);                  // :365-368
    void addOrIgnore(qint64 imageId, const QString& name);    // :370-394
    void remove(qint64 id) override;                          // :396-413 (Delete)
};

// TOrmMarkAppearanceRow (:72-78)
struct OrmMarkAppearanceRow {
    qint64 id = 0; // AppearanceId
    quint32 color1 = 0;
    quint32 color2 = 0;
    qint32 scale1 = 0;
    qint32 scale2 = 0;
};

// TOrmMarkAppearanceCache (:82-99)
class OrmMarkAppearanceCache : public OrmCacheBase<OrmMarkAppearanceRow> {
public:
    explicit OrmMarkAppearanceCache(qint64 maxCacheSize);

    [[nodiscard]] OrmMarkAppearanceRow* find(qint64 id); // :424-443
    [[nodiscard]] OrmMarkAppearanceRow* find(quint32 color1, quint32 color2, qint32 scale1,
                                             qint32 scale2);                                   // :445-469
    void addOrIgnore(const OrmMarkRec& rec);                                                   // :471-474
    void addOrIgnore(qint64 id, quint32 color1, quint32 color2, qint32 scale1, qint32 scale2); // :476-506
};

// TOrmMarkViewRow (:105-110)
struct OrmMarkViewRow {
    qint64 id = 0; // MarkId
    qint64 viewId = 0;
    qint64 categoryId = 0;
    bool visible = false;
};

// TOrmMarkViewCache (:114-126)
class OrmMarkViewCache : public OrmCacheBaseWithPreparedByCategory<OrmMarkViewRow> {
public:
    explicit OrmMarkViewCache(qint64 maxCacheSize);

    [[nodiscard]] OrmMarkViewRow* find(qint64 markId);                               // :925-941
    void addOrUpdate(const OrmMarkRec& rec);                                         // :943-946
    void addOrUpdate(qint64 markId, qint64 viewId, qint64 categoryId, bool visible); // :948-981
    void addPrepared(qint64 categoryId, const std::vector<OrmMarkViewRow>& rows);    // :983-1047
};

// TOrmMarkIdIndexRec (:132-137)
struct OrmMarkIdIndexRec {
    qint64 id = 0; // MarkId
    qint64 categoryId = 0;
    qint64 imageId = 0;
    qint64 appearanceId = 0;
};

// TOrmMarkIdIndex (:141-161)
class OrmMarkIdIndex : public OrmCacheBase<OrmMarkIdIndexRec> {
public:
    explicit OrmMarkIdIndex(qint64 maxCacheSize);

    [[nodiscard]] OrmMarkIdIndexRec* find(qint64 markId);         // :517-533
    void addOrUpdate(const OrmMarkRec& rec);                      // :535-562
    void addPrepared(const std::vector<OrmMarkIdIndexRec>& rows); // :564-580
    void addArray(const std::vector<OrmMarkIdIndexRec>& array, int startIndex = 0,
                  int count = -1);                                                   // :582-635
    [[nodiscard]] static OrmMarkIdIndexRec markRecToIndexRec(const OrmMarkRec& rec); // :637-646
};

// TOrmMarkIdByCategoryIndex (:167-188): category id -> sorted array of mark ids, in a
// TDynArrayByRecWithPointer that owns the arrays (dpObj). Unlimited.
class OrmMarkIdByCategoryIndex {
public:
    explicit OrmMarkIdByCategoryIndex(qint64 maxCacheSize);

    // Find: on success `ids` points at the cached array (valid up to `count` entries) and `count` is its
    // length. Legacy hands out a reference-counted array that survives later changes; here the pointer
    // is into the live vector and is invalid after the next reset / addPrepared of that category.
    [[nodiscard]] bool find(qint64 categoryId, const std::vector<qint64>*& ids, int& count); // :662-685
    void addPrepared(qint64 categoryId, const std::vector<qint64>& markIds, int startIndex,
                     int count);                   // :687-727
    void add(qint64 categoryId, qint64 markId);    // :729-745
    void remove(qint64 categoryId, qint64 markId); // :747-763 (Delete)
    void reset();                                  // :765-768

private:
    DynArrayByRecWithPointer<IdDynArrayObject> array_;
};

// TOrmMarkRow (:194-200), packed Win64: 8 + 8 + 8 + 1 + 4 = 29 bytes.
struct OrmMarkRow {
    qint64 id = 0; // MarkId
    QString name;
    QString desc;
    OrmGeoType geoType = OrmGeoType::Undef;
    qint32 geoCount = 0;
};

// TOrmMarkCache (:194-219). Limited (30 % of the cache size, OrmMarkDbCache::init).
class OrmMarkCache : public OrmCacheBaseWithPreparedByCategory<OrmMarkRow> {
public:
    explicit OrmMarkCache(qint64 maxCacheSize);

    [[nodiscard]] OrmMarkRow* find(qint64 id);                                // :777-793
    void addOrUpdate(const OrmMarkRec& rec);                                  // :795-825
    void addPrepared(qint64 categoryId, const std::vector<OrmMarkRow>& rows); // :827-902
    [[nodiscard]] static OrmMarkRow markRecToMarkRow(const OrmMarkRec& rec);  // :904-914
};

// TOrmMarkGeometryRec (:225-230 area), the input row of OrmMarkGeometryCache::addPrepared.
struct OrmMarkGeometryRec {
    qint64 id = 0; // MarkID
    qint32 size = 0;
    geo::GeometryLonLatPtr geometry;
};

// TOrmMarkGeometryCache (:225-257). Limited (70 %). Its rows live in an inner DynArrayByRecWithPointer
// (112 + 20 * rows + bytes, see the cache base); its prepared state lives outside that array.
// L-158 FIXED (deviation D-88): whenever the inner array is going to empty itself (over its
// limit at the start of an add), or reset() is called, the prepared state is cleared with it; legacy keeps it
// (see the .cpp).
class OrmMarkGeometryCache {
public:
    explicit OrmMarkGeometryCache(qint64 maxCacheSize);
    OrmMarkGeometryCache(const OrmMarkGeometryCache&) = delete;
    OrmMarkGeometryCache& operator=(const OrmMarkGeometryCache&) = delete;

    [[nodiscard]] bool isCategoryPrepared(qint64 categoryId) const;                       // :1066-1069
    [[nodiscard]] bool find(qint64 markId, geo::GeometryLonLatPtr& geometry);             // :1071-1088
    void addOrUpdate(qint64 markId, qint32 size, const geo::GeometryLonLatPtr& geometry); // :1090-1103
    void addPrepared(qint64 categoryId, const std::vector<OrmMarkGeometryRec>& rows);     // :1105-1133
    void remove(qint64 markId);                                   // :1135-1144 (Delete)
    void reset();                                                 // :1146-1155
    [[nodiscard]] bool isPrepared() const { return isPrepared_; } // property IsPrepared

private:
    // The one place of the L-158 fix.
    void forgetPreparedState();

    bool isPrepared_ = false;
    qint64 maxCacheSize_;
    IdDynArrayObject preparedCategories_;
    DynArrayByRecWithPointer<const geo::IGeometryLonLat> geometryArray_;
};

// TOrmMarkDbCache (:263-275): the seven caches of one database. Init / Done are init / done (legacy record
// without a constructor; an unset member is null).
struct OrmMarkDbCache {
    std::unique_ptr<OrmMarkCache> markCache;
    std::unique_ptr<OrmMarkGeometryCache> markGeometryCache;
    std::unique_ptr<OrmMarkViewCache> markViewCache;
    std::unique_ptr<OrmMarkImageCache> markImageCache;
    std::unique_ptr<OrmMarkAppearanceCache> markAppearanceCache;
    std::unique_ptr<OrmMarkIdIndex> markIdIndex;
    std::unique_ptr<OrmMarkIdByCategoryIndex> markIdByCategoryIndex;

    void init(qint64 maxCacheSize); // :286-305: mark cache 30 %, geometry cache 70 %, the rest unlimited
    void done();                    // :307-316: freed in reverse order
};

} // namespace cg::marks
