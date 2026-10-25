// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/MarksDB/SML/u_MarkCategorySmlDbInternal.pas (160). Line references are to that unit.
#include "cg/marks/sml/mark_category_sml_db_internal.h"

#include <utility>

namespace cg::marks {

// TMarkCategorySmlDbInternal.Create (:70-90); assertions are not ported (see the header).
MarkCategorySmlDbInternal::MarkCategorySmlDbInternal(int id, quintptr dbId, QString name, bool visible,
                                                     int afterScale, int beforeScale)
    : id_(id), dbId_(dbId), name_(std::move(name)), visible_(visible), afterScale_(afterScale),
      beforeScale_(beforeScale) {}

int MarkCategorySmlDbInternal::id() const {
    return id_;
}

quintptr MarkCategorySmlDbInternal::dbId() const {
    return dbId_;
}

QString MarkCategorySmlDbInternal::name() const {
    return name_;
}

bool MarkCategorySmlDbInternal::visible() const {
    return visible_;
}

int MarkCategorySmlDbInternal::afterScale() const {
    return afterScale_;
}

int MarkCategorySmlDbInternal::beforeScale() const {
    return beforeScale_;
}

// TMarkCategorySmlDbInternal.IsEqual (:122-146)
bool MarkCategorySmlDbInternal::isEqual(const vector::ICategory* category) const {
    if (category == nullptr) {
        return false;
    }
    if (category == this) {
        return true;
    }
    if (category->name() != name_) {
        return false;
    }
    // Supports(ACategory, IMarkCategory, ...): a category of another class is not equal (:143-145).
    const auto* other = dynamic_cast<const IMarkCategory*>(category);
    if (other == nullptr) {
        return false;
    }
    return other->visible() == visible_ && other->afterScale() == afterScale_ &&
           other->beforeScale() == beforeScale_;
}

// TMarkCategorySmlDbInternal.IsSame (:148-158): the id only, dbId is not compared (L-193).
bool MarkCategorySmlDbInternal::isSame(const vector::ICategory* category) const {
    const auto* other = dynamic_cast<const IMarkCategorySMLInternal*>(category);
    return other != nullptr && other->id() == id_;
}

} // namespace cg::marks
