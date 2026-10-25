// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// FEAT-GEO-034 (held open). Ports: Src/MarksDB/mORMot/u_MarkSystemORMTools.pas
// (docs/specs/marks.md §2.9.5).
// Not ported: LonLatDoubleRectToRect (:160-166), cCoordToInt (:59) -- used by the DBMS model only.

#include "cg/geo/geo_types.h"
#include "cg/geo/geometry_lonlat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QString>
#include <QtGlobal>

namespace cg::marks {

// TCSVFieldsBuilder (:37-43, :64-89): the comma-separated column list of an UPDATE that writes
// only the changed fields.
struct CsvFieldsBuilder {
    QByteArrayList fields;

    void clear() { fields.clear(); }
    void add(const QString& fieldName) { fields.append(fieldName.toUtf8()); }
    [[nodiscard]] QByteArray build() const { return fields.join(','); }
};

// :91-124. Each throws MarkSystemORMError with the legacy message.
void checkId(qint64 id);               // id == 0: "MarkSystemORM: ID is empty!"
void checkDeleteResult(bool result);   // "MarkSystemORM: Delete operation is failed!"
void checkUpdateResult(bool result);   // "MarkSystemORM: Update operation is failed!"
void checkRetrieveResult(bool result); // "MarkSystemORM: Nothing to Retrieve!"
void checkExecuteResult(bool result);  // "MarkSystemORM: Execute operation is failed!"

// :126-138. Items of a multi-line / multi-polygon; 1 for everything else, a null geometry included.
[[nodiscard]] int calcMultiGeometryCount(const geo::GeometryLonLatPtr& geometry);

inline constexpr quint32 kCoordToSize = 2147483647U / 360U; // cCoordToSize = MAXLONG div 360 (:60)

// :140-152. Both sizes are 0 for a degenerate (point) rectangle.
void calcGeometrySize(const geo::DoubleRect& rect, quint32& lonSize, quint32& latSize);
// :154-158. Round() to nearest-even, stored into a Cardinal with range checks off (low 32 bits).
void lonLatSizeToInternalSize(const geo::DoublePoint& lonLatSize, quint32& lonSize, quint32& latSize);

} // namespace cg::marks
