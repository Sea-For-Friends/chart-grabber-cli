// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mormot/categories/mark_category_db_internal_orm.h"

#include <utility>

namespace cg::marks {

// TMarkCategoryDbInternalORM.Create (:79-100); assertions are not ported (see the header).
MarkCategoryDbInternalORM::MarkCategoryDbInternalORM(qint64 id, quintptr dbId, QString name, bool visible,
                                                     int afterScale, int beforeScale)
    : id_(id), dbId_(dbId), name_(std::move(name)), visible_(visible), afterScale_(afterScale),
      beforeScale_(beforeScale) {}

qint64 MarkCategoryDbInternalORM::id() const {
    return id_;
}

quintptr MarkCategoryDbInternalORM::dbId() const {
    return dbId_;
}

QString MarkCategoryDbInternalORM::name() const {
    return name_;
}

bool MarkCategoryDbInternalORM::visible() const {
    return visible_;
}

int MarkCategoryDbInternalORM::afterScale() const {
    return afterScale_;
}

int MarkCategoryDbInternalORM::beforeScale() const {
    return beforeScale_;
}

// TMarkCategoryDbInternalORM.IsEqual (:132-156)
bool MarkCategoryDbInternalORM::isEqual(const vector::ICategory* category) const {
    if (category == nullptr) {
        return false;
    }
    if (category == this) {
        return true;
    }
    if (category->name() != name_) {
        return false;
    }
    // Supports(ACategory, IMarkCategory, ...): a category of another class is not equal.
    const auto* other = dynamic_cast<const IMarkCategory*>(category);
    if (other == nullptr) {
        return false;
    }
    return other->visible() == visible_ && other->afterScale() == afterScale_ &&
           other->beforeScale() == beforeScale_;
}

// TMarkCategoryDbInternalORM.IsSame (:158-168): row id only, dbId is not compared.
bool MarkCategoryDbInternalORM::isSame(const vector::ICategory* category) const {
    const auto* other = dynamic_cast<const IMarkCategoryInternalORM*>(category);
    return other != nullptr && other->id() == id_;
}

} // namespace cg::marks
