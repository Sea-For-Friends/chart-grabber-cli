// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/i_MarkFactorySmlInternal.pas (55): IMarkFactorySmlInternal and the constant
// CNotExistMarkID (docs/specs/marks.md section 2.8.4). Interface and constant only; the class is in
// mark_factory_sml_db_internal.h.
//
// Colours are TColor32 (quint32) and the scales are Integer (int), as in the SML row objects.

#include "cg/geo/geometry_lonlat.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

namespace cg::marks {

// CNotExistMarkID (i_MarkFactorySmlInternal.pas:51): the mark id of a mark that is not in the database.
inline constexpr int kNotExistMarkId = -1;

class IMarkFactorySmlInternal {
public:
    virtual ~IMarkFactorySmlInternal() = default;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    createMark(int id, const QString& name, bool visible, const QString& picName, int categoryId,
               const QString& desc, const geo::GeometryLonLatPtr& geometry, quint32 color1, quint32 color2,
               int scale1, int scale2) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr
    createInternalMark(const vector::VectorDataItemPtr& mark) const = 0;
};

} // namespace cg::marks
