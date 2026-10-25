// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// FEAT-GEO-034 (held open). Ports: Src/MarksDB/mORMot/t_MarkSystemORM.pas
// (docs/specs/marks.md §2.9.14). TID is qint64; TIDDynArray / the *DynArray aliases are
// std::vector at their users.

#include "cg/geo/geometry_lonlat.h"
#include "cg/marks/mormot/mark_system_orm_model.h"

#include <QString>
#include <QtGlobal>

#include <stdexcept>

namespace cg::marks {

// EMarkSystemORMError (:36).
class MarkSystemORMError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr qint64 kEmptyId = 0; // cEmptyID (:73)

// TOrmCategoryRec (:41-48); the member initialisers are cEmptyOrmCategoryRec (:75-82).
struct OrmCategoryRec {
    qint64 categoryId = 0;
    QString name;
    qint64 viewId = 0;
    bool visible = true;
    quint8 minZoom = 3;
    quint8 maxZoom = 23;
};

// TOrmMarkRec (:49-68); the member initialisers are cEmptyOrmMarkRec (:84-103).
struct OrmMarkRec {
    qint64 markId = 0;
    qint64 categoryId = 0;
    qint64 picId = 0;
    QString picName;
    qint64 appearanceId = 0;
    quint32 color1 = 0;
    quint32 color2 = 0;
    qint32 scale1 = 0;
    qint32 scale2 = 0;
    QString name;
    QString desc;
    qint64 viewId = 0;
    bool visible = true;
    quint32 geoLonSize = 0;
    quint32 geoLatSize = 0;
    OrmGeoType geoType = OrmGeoType::Undef;
    qint32 geoCount = 0;
    geo::GeometryLonLatPtr geometry;
};

} // namespace cg::marks
