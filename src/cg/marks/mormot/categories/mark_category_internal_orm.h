// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Categories/i_MarkCategoryInternalORM.pas (41 lines).
//
// Internal view of an SQLite-backend category: its row id and the identity of the database it was
// read from. The legacy GUID is not carried; dynamic_cast replaces Supports().

#include <QtGlobal>

namespace cg::marks {

class IMarkCategoryInternalORM {
public:
    virtual ~IMarkCategoryInternalORM() = default;
    [[nodiscard]] virtual qint64 id() const = 0;
    [[nodiscard]] virtual quintptr dbId() const = 0;
};

} // namespace cg::marks
