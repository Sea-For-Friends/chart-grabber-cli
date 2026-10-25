// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/u_MarkCategoryFactorySmlDbInternal.pas (78, class TMarkCategoryFactorySmlDbInternal;
// docs/specs/marks.md section 2.8.4). It implements IMarkCategoryFactoryDbInternal
// (mark_category_factory_db_internal.h) and stamps the dbId of its database on every category it creates.
//
// Unlike the SQLite twin there is one CreateCategory overload: the legacy SML factory has no record overload.

#include "cg/marks/sml/mark_category_factory_db_internal.h"

#include <QtGlobal>

namespace cg::marks {

class MarkCategoryFactorySmlDbInternal final : public IMarkCategoryFactoryDbInternal {
public:
    explicit MarkCategoryFactorySmlDbInternal(quintptr dbId);

    [[nodiscard]] MarkCategoryPtr createCategory(int id, const QString& name, bool visible, int afterScale,
                                                 int beforeScale) const override;

private:
    quintptr dbId_;
};

} // namespace cg::marks
