// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_IDList.pas (IIDInterfaceList), i_EnumID.pas, u_IDInterfaceList.pas, u_IDListBase.pas.
//
// Map keyed by NativeInt (qintptr), enumerated in ascending SIGNED key order (CompareID,
// u_IDListBase.pas:174-186; binary search Find :213-236) -- not insertion order. Add keeps an
// existing value, Replace overwrites or inserts, Remove of an absent key is a no-op, GetByID of an
// absent key is null. Not synchronised (legacy neither). IEnumID / IEnumUnknown become snapshot
// vectors (idioms.md section 11).
// Legacy IInterface is the template parameter: std::shared_ptr<T>, T may be const-qualified.
// Not ported: Capacity, the Count setter, the enumerator Skip/Reset/Clone ABI. IIDObjectList (owner of
// TObject) is not ported.

#include "cg/core/sorted_key_list.h"

#include <QtGlobal>

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cg::core {

// FEAT-GEO-033, FEAT-GEO-034
template <class T> class IIdInterfaceList {
public:
    using Ptr = std::shared_ptr<T>;
    virtual ~IIdInterfaceList() = default;

    // Returns the stored object; an existing key is NOT replaced.
    // A null value throws std::invalid_argument unless the list allows nil (legacy: Exception).
    virtual Ptr add(qintptr id, const Ptr& value) = 0;
    [[nodiscard]] virtual bool isExists(qintptr id) const = 0;
    [[nodiscard]] virtual Ptr getById(qintptr id) const = 0;
    virtual void replace(qintptr id, const Ptr& value) = 0;
    virtual void remove(qintptr id) = 0;
    virtual void clear() = 0;
    // Snapshots in ascending id order (legacy GetIDEnum / GetEnumUnknown).
    [[nodiscard]] virtual std::vector<qintptr> ids() const = 0;
    [[nodiscard]] virtual std::vector<Ptr> values() const = 0;
    [[nodiscard]] virtual int count() const = 0;
};

template <class T> class IdInterfaceList final : public IIdInterfaceList<T> {
public:
    using Ptr = std::shared_ptr<T>;
    explicit IdInterfaceList(bool allowNil = false) : allowNil_(allowNil) {}

    Ptr add(qintptr id, const Ptr& value) override {
        if (!allowNil_ && !value)
            throw std::invalid_argument("Interface is nil");
        const auto [index, found] = list_.find(id);
        if (found)
            return list_.at(index).value;
        list_.insert(index, id, value);
        return value;
    }
    [[nodiscard]] bool isExists(qintptr id) const override { return list_.find(id).second; }
    [[nodiscard]] Ptr getById(qintptr id) const override {
        const auto [index, found] = list_.find(id);
        return found ? list_.at(index).value : nullptr;
    }
    void replace(qintptr id, const Ptr& value) override {
        if (!allowNil_ && !value)
            throw std::invalid_argument("Interface is nil");
        const auto [index, found] = list_.find(id);
        if (found)
            list_.at(index).value = value;
        else
            list_.insert(index, id, value);
    }
    void remove(qintptr id) override {
        const auto [index, found] = list_.find(id);
        if (found)
            list_.erase(index);
    }
    void clear() override { list_.clear(); }
    [[nodiscard]] std::vector<qintptr> ids() const override { return list_.keys(); }
    [[nodiscard]] std::vector<Ptr> values() const override { return list_.values(); }
    [[nodiscard]] int count() const override { return list_.count(); }

private:
    bool allowNil_;
    detail::SortedKeyList<qintptr, Ptr, std::less<qintptr>> list_;
};

} // namespace cg::core
