// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Marks/u_MarkDbImplORMHelper.pas, the write half (docs/specs/marks.md §2.9.8
// W-1, W-3, W-4; statements S-20 ... S-2D, S-40, S-42 of §2.9.5). Line numbers in the sources are those of
// that unit.
//
// Scope of this file: ticket T-0095 (O12): the constructor (:230-259), the destructor (:261-265, the cache
// frees itself), the picture / appearance helpers (:333-454), DeleteMarkSQL (:456-616) and InsertMarkSQL
// (:618-774); ticket T-0100 (O13): UpdateMarkSQL (:776-1010), UpdateMarkView (:1176-1264),
// SetMarksInCategoryVisibleSQL (:1266-1347), the two geometry decoders (:267-299, the constructor now
// carries the geometry factory); ticket T-0105 (O14): ReadMarkSQL (:1012-1174), the four _FillPrepare*
// functions (:1349-1644), GetMarkRecArray (:1646-1751) and, with them, the read-only branch of
// SetMarksInCategoryVisibleSQL (:1321-1344). The geometry encoders (:301-331) are the free functions of
// cg/geo/wkb.h.
// ticket T-0111 (O15): _GetMarkIDArrayByRectSQL (:1753-1819), GetMarkRecArrayByRect (:1968-2070),
// _GetMarkRecArrayByTextSQLite3 (:2072-2106), GetMarkRecArrayByText (:2159-2269) and SetReadOnly
// (:2271-2279). Not ported (spec 2.9.8): :1821-1966 (DBMS and MongoDB rectangle), :2108-2157 (the non-SQLite
// text search), :584-606 and every non-SQLite branch.
//
// Every method is called with the write lock of the owning mark database held (§2.9.12); the helper and its
// caches have no lock of their own. The helper starts a transaction only where legacy does; each statement
// runs under the connection's dbMutex.

#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_factory.h"
#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mormot/mark_system_impl_orm_client_provider.h"
#include "cg/marks/mormot/mark_system_orm_types.h"
#include "cg/marks/mormot/marks/mark_db_impl_orm_cache.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <vector>

namespace cg::marks {

// TMarkWithCategoryID
struct MarkWithCategoryId {
    qint64 markId = 0;
    qint64 categoryId = 0;
};

// TMarkDbImplORMHelper (:65-213)
class MarkDbImplORMHelper {
public:
    // The factory and the provider must outlive the helper.
    MarkDbImplORMHelper(bool isReadOnly, quint32 cacheSizeMb,
                        const geo::GeometryLonLatFactory& geometryFactory,
                        IMarkSystemImplORMClientProvider& provider); // :230-259

    bool deleteMarkSql(qint64 markId, bool useTransactions);          // :456-519
    bool deleteMarkSql(const std::vector<qint64>& markIds);           // :521-616
    bool insertMarkSql(OrmMarkRec& rec, bool useTransactions);        // :618-774
    bool updateMarkSql(const OrmMarkRec& oldRec, OrmMarkRec& newRec); // :776-1010
    // :1012-1174: the mark by id (markId > 0), else by name (and category when categoryId > 0); false when
    // there is none. `rec` is reset first (cEmptyOrmMarkRec).
    bool readMarkSql(OrmMarkRec& rec, qint64 markId, qint64 categoryId, const QString& name);
    bool updateMarkView(qint64 markId, qint64 categoryId, bool visible, bool useTransaction); // :1176-1264
    bool setMarksInCategoryVisibleSql(qint64 categoryId, bool visible);                       // :1266-1347
    // :1646-1751: categoryId > 0 the marks of that category in ascending mark id, 0 all marks in ascending
    // mark id; hidden ones only with includeHidden. `out` is cleared first; returns out.size().
    int getMarkRecArray(qint64 categoryId, bool includeHidden, bool includeGeometry, bool includeAppearance,
                        std::vector<OrmMarkRec>& out);

    // :1968-2070. S-41 over the R-tree (bounding boxes only), then readMarkSql for every id; hidden marks
    // only with includeHidden. `categoryIds`: empty = every category; 128 or more are filtered in memory and
    // the list is sorted in place (as legacy sorts the caller's array). `out` is cleared first; returns
    // out.size().
    int getMarkRecArrayByRect(std::vector<qint64>& categoryIds, const geo::DoubleRect& rect,
                              bool includeHidden, const geo::DoublePoint& lonLatSize,
                              std::vector<OrmMarkRec>& out);
    // :2159-2269. S-43 on mName, then on mDesc; the text is lower-cased and sent as an FTS query (bound).
    int getMarkRecArrayByText(const QString& text, int maxCount, bool includeHidden, bool searchInDescription,
                              std::vector<OrmMarkRec>& out);

    [[nodiscard]] bool isReadOnly() const { return isReadOnly_; }
    void setReadOnly(bool value); // :2271-2279: read-only -> writable drops the view cache (fake ids)

private:
    // :267-282 (Assert(ABlob <> '') is off in the shipped build); a blob that does not parse throws
    // MarkSystemORMError (legacy: EReadError / EAbort from the stream reader).
    [[nodiscard]] geo::GeometryLonLatPtr geometryFromBlob(const QByteArray& blob,
                                                          const geo::PointsMeta* meta) const;
    // :284-299: an empty blob, or one that does not parse, is "no meta" (an empty PointsMeta).
    [[nodiscard]] geo::PointsMeta geometryMetaFromBlob(const QByteArray& blob) const;

    friend struct MarkDbImplORMHelperTestAccess; // tests/marks: the caches and the private helpers

    qint64 addMarkImage(const QString& picName); // :333-361  cache -> S-20 -> S-21
    void readMarkImage(OrmMarkRec& rec);         // :363-389  points only; cache -> S-22
    qint64 addMarkAppearance(quint32 color1, quint32 color2, qint32 scale1, qint32 scale2); // :391-426
    void readMarkAppearance(OrmMarkRec& rec); // :428-454  cache -> S-2A

    int fillPrepareMarkIdIndex(qint64 categoryId);        // :1349-1452  S-3A / S-3B
    void fillPrepareMarkIdCache(qint64 categoryId);       // :1454-1497  S-3C
    void fillPrepareMarkGeometryCache(qint64 categoryId); // :1499-1591  S-3D + S-3E
    void fillPrepareMarkViewCache(qint64 categoryId);     // :1593-1644  S-3F

    // :1753-1819  S-41. `out` is cleared first; returns out.size().
    int getMarkIdArrayByRectSql(const std::vector<qint64>& categoryIds, const geo::DoubleRect& rect,
                                quint32 lonSize, quint32 latSize, bool receiveCategoryId,
                                std::vector<MarkWithCategoryId>& out);
    // :2072-2106  S-43. Returns the number of ids found in both columns together.
    int getMarkRecArrayByTextSqlite3(const QString& search, int maxCount, bool searchInDescription,
                                     std::vector<qint64>& nameIds, std::vector<qint64>& descIds);

    qint64 userId_;
    IMarkSystemImplORMClientProvider& provider_;
    const geo::GeometryLonLatFactory& geometryFactory_;
    OrmMarkDbCache cache_;
    bool isReadOnly_;
};

} // namespace cg::marks
