// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkSystemImpl.pas  (one opened database)

#include "cg/core/read_write_state.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_db_impl.h"
#include "cg/marks/mark_db_impl.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>

#include <memory>

namespace cg::marks {

class IMarkSystemImpl {
public:
    virtual ~IMarkSystemImpl() = default;
    [[nodiscard]] virtual core::ReadWriteStateChangeablePtr state() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkDbImpl> markDb() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkCategoryDBImpl> categoryDB() const = 0;
    [[nodiscard]] virtual QString stringIdByMark(const vector::VectorDataItemPtr& mark) const = 0;
    [[nodiscard]] virtual vector::VectorDataItemPtr markByStringId(const QString& id) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr markCategoryByStringId(const QString& id) const = 0;
};

} // namespace cg::marks
