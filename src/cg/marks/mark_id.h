// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkId.pas  (u_MarkId.pas, the SML implementation, is a later ticket)
//
// MarkList / MarkIdList stand for legacy IInterfaceListStatic holding IVectorDataItem / IMarkId
// (spec marks.md section 2 preamble); a null pointer is "no list". They live here because every
// header that needs them already includes this one.

#include "cg/vector/category.h"
#include "cg/vector/vector_data_item_simple.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace cg::marks {

enum class MarkIdType : std::uint8_t { Point = 0, Line, Poly }; // midPoint, midLine, midPoly

class IMarkId {
public:
    virtual ~IMarkId() = default;
    [[nodiscard]] virtual vector::CategoryPtr category() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual int multiGeometryCount() const = 0;
    [[nodiscard]] virtual MarkIdType markType() const = 0;
    // Raw pointers, as the landed vector::IVectorDataItem::isEqual / ICategory::isSame.
    [[nodiscard]] virtual bool isSameId(const IMarkId* markId) const = 0;
    [[nodiscard]] virtual bool isSameMark(const vector::IVectorDataItem* mark) const = 0;
};
using MarkIdPtr = std::shared_ptr<const IMarkId>;

using ArrayOfMarkId = std::vector<MarkIdPtr>; // TArrayOfMarkId (used by the GUI units)
using MarkList = std::shared_ptr<const std::vector<vector::VectorDataItemPtr>>;
using MarkIdList = std::shared_ptr<const std::vector<MarkIdPtr>>;

// One element read out of a legacy IInterfaceListStatic whose elements are IVectorDataItem or
// IMarkId (spec marks.md section 2 preamble, decision C3). A null pointer in either alternative is nil.
using MarkOrId = std::variant<vector::VectorDataItemPtr, MarkIdPtr>;

// One legacy IInterfaceListStatic ARGUMENT that holds marks or mark ids (never both). The converting
// constructors are implicit on purpose: call sites stay literal translations of the Pascal line.
class MarkOrIdList {
public:
    MarkOrIdList() = default;       // nil
    MarkOrIdList(std::nullptr_t) {} // NOLINT(google-explicit-constructor): spec marks.md C3 rule 1
    MarkOrIdList(MarkList marks)
        : list_(std::move(marks)) {}                        // NOLINT(google-explicit-constructor): C3 rule 1
    MarkOrIdList(MarkIdList ids) : list_(std::move(ids)) {} // NOLINT(google-explicit-constructor): C3 rule 1

    // Assigned(AList): false for nil of either kind.
    [[nodiscard]] explicit operator bool() const {
        return std::visit([](const auto& l) { return static_cast<bool>(l); }, list_);
    }
    // AList.Count; 0 for nil.
    [[nodiscard]] int count() const {
        return std::visit([](const auto& l) { return l ? static_cast<int>(l->size()) : 0; }, list_);
    }
    // AList[index]; precondition 0 <= index < count().
    [[nodiscard]] MarkOrId operator[](int index) const {
        return std::visit([index](const auto& l) { return MarkOrId{(*l)[static_cast<std::size_t>(index)]}; },
                          list_);
    }

private:
    std::variant<MarkList, MarkIdList> list_; // default: a null MarkList
};

} // namespace cg::marks
