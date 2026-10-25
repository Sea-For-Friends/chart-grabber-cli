// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Categories/u_MarkCategoryDbImplORMCache.pas (docs/specs/marks.md §2.9.6).
// Line numbers below are those of that unit. The caches have no lock of their own: they are reached
// only under the write lock of their database (§2.9.12).
//
// Row layout: the landed cache base (mormot/mark_system_orm_cache_base.h) keys a row by its member `id`,
// the FIRST field of the legacy record; spec §2.9.6 draws it as `categoryId`, the landed base wins.
// For OrmCategoryViewRow the key `id` is the legacy `CategoryId`.

#include "cg/marks/mormot/mark_system_orm_cache_base.h"
#include "cg/marks/mormot/mark_system_orm_types.h"

#include <QString>
#include <QtGlobal>

#include <vector>

namespace cg::marks {

// TOrmCategoryRow (:44-49)
struct OrmCategoryRow {
    qint64 id = 0; // CategoryId
    QString name;
};

// TOrmCategoryViewRow (:69-77)
struct OrmCategoryViewRow {
    qint64 id = 0; // CategoryId
    qint64 viewId = 0;
    bool visible = false;
    quint8 minZoom = 0;
    quint8 maxZoom = 0;
};

// TOrmCategoryCache (:51-63). The row pointers stay valid until the next add / remove / reset.
class OrmCategoryCache : public OrmCacheBase<OrmCategoryRow> {
public:
    explicit OrmCategoryCache(qint64 maxCacheSize);

    [[nodiscard]] OrmCategoryRow* find(qint64 id);             // :185-201
    [[nodiscard]] OrmCategoryRow* find(const QString& name);   // :203-221, linear, AnsiSameText
    void addOrUpdate(const OrmCategoryRec& rec);               // :131-162
    void addPrepared(const std::vector<OrmCategoryRow>& rows); // :164-183, replaces the content
};

// TOrmCategoryViewCache (:79-90)
class OrmCategoryViewCache : public OrmCacheBase<OrmCategoryViewRow> {
public:
    explicit OrmCategoryViewCache(qint64 maxCacheSize);

    [[nodiscard]] OrmCategoryViewRow* find(qint64 categoryId);     // :264-280
    void addOrUpdate(const OrmCategoryRec& rec);                   // :230-262
    void addPrepared(const std::vector<OrmCategoryViewRow>& rows); // :282-301
};

// TOrmCategoryDbCache (:96-101, :112-122): Init creates both caches with CUnlimCacheSize.
struct OrmCategoryDbCache {
    OrmCategoryCache categoryCache{kUnlimCacheSize};
    OrmCategoryViewCache categoryViewCache{kUnlimCacheSize};
};

} // namespace cg::marks
