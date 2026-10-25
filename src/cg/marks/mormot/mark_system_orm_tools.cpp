// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/mark_system_orm_tools.h"

#include "cg/marks/mormot/mark_system_orm_types.h"

#include <cmath>

namespace cg::marks {

void checkId(qint64 id) {
    if (id == 0) {
        throw MarkSystemORMError("MarkSystemORM: ID is empty!");
    }
}

void checkDeleteResult(bool result) {
    if (!result) {
        throw MarkSystemORMError("MarkSystemORM: Delete operation is failed!");
    }
}

void checkUpdateResult(bool result) {
    if (!result) {
        throw MarkSystemORMError("MarkSystemORM: Update operation is failed!");
    }
}

void checkRetrieveResult(bool result) {
    if (!result) {
        throw MarkSystemORMError("MarkSystemORM: Nothing to Retrieve!");
    }
}

void checkExecuteResult(bool result) {
    if (!result) {
        throw MarkSystemORMError("MarkSystemORM: Execute operation is failed!");
    }
}

int calcMultiGeometryCount(const geo::GeometryLonLatPtr& geometry) {
    if (const auto* line = dynamic_cast<const geo::IGeometryLonLatMultiLine*>(geometry.get())) {
        return static_cast<int>(line->count());
    }
    if (const auto* poly = dynamic_cast<const geo::IGeometryLonLatMultiPolygon*>(geometry.get())) {
        return static_cast<int>(poly->count());
    }
    return 1;
}

void calcGeometrySize(const geo::DoubleRect& rect, quint32& lonSize, quint32& latSize) {
    if (rect.right == rect.left && rect.top == rect.bottom) {
        lonSize = 0;
        latSize = 0;
    } else {
        lonLatSizeToInternalSize(geo::DoublePoint(rect.right - rect.left, rect.top - rect.bottom), lonSize,
                                 latSize);
    }
}

void lonLatSizeToInternalSize(const geo::DoublePoint& lonLatSize, quint32& lonSize, quint32& latSize) {
    // Delphi Round() is banker's rounding to Int64; std::llrint under the default rounding mode is
    // the same. The Cardinal assignment keeps the low 32 bits.
    lonSize = static_cast<quint32>(static_cast<quint64>(std::llrint(lonLatSize.x() * kCoordToSize)));
    latSize = static_cast<quint32>(static_cast<quint64>(std::llrint(lonLatSize.y() * kCoordToSize)));
}

} // namespace cg::marks
