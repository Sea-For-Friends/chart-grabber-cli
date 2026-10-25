// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/mORMot/Marks/i_MarkDbInternalORM.pas (57). Interfaces only; the implementing
// classes are later tickets (docs/specs/marks.md section 2.9.10).
//
// IMarkInternalORM is the view of a SQLite mark object that the database uses to find its row
// (id, database identity, category id, name, visibility). IMarkDbInternalORM is the database side
// that returns a mark by its id. Legacy ids are TID (qint64); dbId is NativeInt, which the port
// carries as quintptr (the object address of the opened system, as in the SML backend).

#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

namespace cg::marks {

class IMarkInternalORM {
public:
    virtual ~IMarkInternalORM() = default;
    [[nodiscard]] virtual qint64 id() const = 0;
    [[nodiscard]] virtual quintptr dbId() const = 0;
    [[nodiscard]] virtual qint64 categoryId() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual bool visible() const = 0;
    // Mutates a shared mark object (legacy SetVisible); the implementation keeps the flag atomic.
    virtual void setVisible(bool value) const = 0;
};

class IMarkDbInternalORM {
public:
    virtual ~IMarkDbInternalORM() = default;
    [[nodiscard]] virtual vector::VectorDataItemPtr getById(qint64 id) const = 0;
};

} // namespace cg::marks
