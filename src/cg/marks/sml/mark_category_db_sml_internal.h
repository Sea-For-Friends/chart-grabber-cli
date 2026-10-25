// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/i_MarkCategoryDBSmlInternal.pas (47): IMarkCategoryDBSmlInternal, the category side
// of the SML database (docs/specs/marks.md section 2.8.4). Interface only; the implementing class is the S4
// ticket (u_MarkCategoryDBSml.pas, 632 lines).
//
// Legacy Initialize(AOperationID, ACancelNotifier) is one std::stop_token (core/background_task.h).

#include "cg/marks/mark_category.h"
#include "cg/vector/category.h"

#include <QString>

#include <stop_token>

namespace cg::marks {

class IMarkCategoryDBSmlInternal {
public:
    virtual ~IMarkCategoryDBSmlInternal() = default;
    virtual void initialize(std::stop_token stop) = 0;
    [[nodiscard]] virtual bool isCategoryFromThisDb(const vector::CategoryPtr& category) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr categoryById(int id) const = 0;
    [[nodiscard]] virtual MarkCategoryPtr firstCategoryByName(const QString& name) const = 0;
    [[nodiscard]] virtual int categoryByNameCount(const QString& name) const = 0;
};

} // namespace cg::marks
