// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarksDB/SML/u_MarkCategorySmlDbInternal.pas (160, class TMarkCategorySmlDbInternal;
// docs/specs/marks.md section 2.8.4). It implements IMarkCategory (the ICategory of the port) and
// IMarkCategorySMLInternal (mark_db_sml_internal.h).
//
// Legacy constructor Asserts (:78-82) are not ported: the shipped build has assertions off
// (SASPlanet.dproj:92). Unlike the SQLite twin, the id is an Integer (int), not a 64-bit row id. isSame()
// compares the id only, not dbId (legacy :148-158; the L-193 row of docs/parity/legacy-defects.md).

#include "cg/marks/mark_category.h"
#include "cg/marks/sml/mark_db_sml_internal.h"
#include "cg/vector/category.h"

#include <QString>
#include <QtGlobal>

namespace cg::marks {

class MarkCategorySmlDbInternal final : public IMarkCategory, public IMarkCategorySMLInternal {
public:
    MarkCategorySmlDbInternal(int id, quintptr dbId, QString name, bool visible, int afterScale,
                              int beforeScale);

    // IMarkCategorySMLInternal
    [[nodiscard]] int id() const override;
    [[nodiscard]] quintptr dbId() const override;

    // ICategory / IMarkCategory
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isSame(const vector::ICategory* category) const override;
    [[nodiscard]] bool isEqual(const vector::ICategory* category) const override;
    [[nodiscard]] bool visible() const override;
    [[nodiscard]] int afterScale() const override;
    [[nodiscard]] int beforeScale() const override;

private:
    int id_;
    quintptr dbId_;
    QString name_;
    bool visible_;
    int afterScale_;
    int beforeScale_;
};

} // namespace cg::marks
