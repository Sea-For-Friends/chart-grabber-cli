// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/i_MarkDbSmlInternal.pas (71): the three SML-internal interfaces
// IMarkSMLInternal, IMarkCategorySMLInternal and IMarkDbSmlInternal (docs/specs/marks.md section 2.8.4).
// Interfaces only; the classes are in mark_id.h, mark_category_sml_db_internal.h and the S5 mark db.
//
// IMarkSMLInternal is the view of an SML mark object that its database uses to find its row (id, database
// identity, category id, name, visibility). IMarkCategorySMLInternal is the same view of an SML category.
// Legacy ids are Integer (int here); dbId is NativeInt, carried as quintptr (the object address of the
// opened system, as in the SQLite backend).

#include "cg/vector/vector_data_item_simple.h"

#include <QString>
#include <QtGlobal>

#include <stop_token>

namespace cg::marks {

class IMarkSMLInternal {
public:
    virtual ~IMarkSMLInternal() = default;
    [[nodiscard]] virtual int id() const = 0;
    [[nodiscard]] virtual quintptr dbId() const = 0;
    [[nodiscard]] virtual int categoryId() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual bool visible() const = 0;
    // Mutates a shared mark object (legacy SetVisible); the implementation keeps the flag atomic.
    virtual void setVisible(bool value) const = 0;
};

class IMarkCategorySMLInternal {
public:
    virtual ~IMarkCategorySMLInternal() = default;
    [[nodiscard]] virtual int id() const = 0;
    [[nodiscard]] virtual quintptr dbId() const = 0;
};

class IMarkDbSmlInternal {
public:
    virtual ~IMarkDbSmlInternal() = default;
    // Legacy Initialize(AOperationID, ACancelNotifier): the pair is one stop_token (core/background_task.h).
    virtual void initialize(std::stop_token stop) = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr byId(int id) const = 0;
};

} // namespace cg::marks
