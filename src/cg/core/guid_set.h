// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_GUIDSet.pas (IGUIDInterfaceSet, IGUIDObjectSet), u_GUIDInterfaceSet.pas, u_GUIDObjectSet.pas,
//        u_GUIDSet.pas (TGUIDSetBase), u_GUIDTool.pas (CompareGUIDs).
//
// Maps keyed by GUID, enumerated in ascending key order. The order is CompareGUIDs
// (u_GUIDTool.pas:30-110): Data1 as an UNSIGNED 32-bit number, then Data2, Data3 (unsigned 16-bit),
// then Data4[0..7] byte by byte -- field by field, not the memory bytes and not the textual form.
// Add keeps an existing value, Replace overwrites or inserts, Remove of an absent key is a no-op,
// GetByGUID of an absent key is null. Not synchronised (legacy neither): callers lock.
// The legacy enumerators (IEnumGUID / IEnumUnknown) become snapshot vectors (idioms.md section 11).
// Not ported: Capacity (a reserve hint), the Count setter, the enumerator Skip/Reset/Clone ABI.
//
// Legacy IInterface becomes the template parameter: the interface set stores std::shared_ptr<T>
// (T may be const-qualified, e.g. GuidInterfaceSet<const IMark>); the object set stores T*.

#include "cg/core/sorted_key_list.h"

#include <QUuid>

#include <memory>
#include <stdexcept>
#include <vector>

namespace cg::core {

// u_GUIDTool.CompareGUIDs: -1 / 0 / 1.
[[nodiscard]] int compareGuids(const QUuid& a, const QUuid& b) noexcept;

struct GuidLess {
    bool operator()(const QUuid& a, const QUuid& b) const noexcept { return compareGuids(a, b) < 0; }
};

// FEAT-GEO-033, FEAT-GEO-034
template <class T> class IGuidInterfaceSet {
public:
    using Ptr = std::shared_ptr<T>;
    virtual ~IGuidInterfaceSet() = default;

    // Returns the stored object; an existing key is NOT replaced.
    // A null value throws std::invalid_argument unless the set allows nil (legacy: Exception).
    virtual Ptr add(const QUuid& guid, const Ptr& value) = 0;
    [[nodiscard]] virtual bool isExists(const QUuid& guid) const = 0;
    [[nodiscard]] virtual Ptr getByGuid(const QUuid& guid) const = 0;
    virtual void replace(const QUuid& guid, const Ptr& value) = 0;
    virtual void remove(const QUuid& guid) = 0;
    virtual void clear() = 0;
    // Snapshots in key order (legacy GetGUIDEnum / GetEnumUnknown).
    [[nodiscard]] virtual std::vector<QUuid> guids() const = 0;
    [[nodiscard]] virtual std::vector<Ptr> values() const = 0;
    // Index in key order; index must be in [0, count()) (legacy: unchecked).
    [[nodiscard]] virtual Ptr item(int index) const = 0;
    [[nodiscard]] virtual int count() const = 0;
};

// Non-owning object variant (legacy TObject; GetIsObjectOwner is False for the only implementation).
template <class T> class IGuidObjectSet {
public:
    virtual ~IGuidObjectSet() = default;

    virtual T* add(const QUuid& guid, T* obj) = 0;
    [[nodiscard]] virtual bool isExists(const QUuid& guid) const = 0;
    [[nodiscard]] virtual T* getByGuid(const QUuid& guid) const = 0;
    virtual void replace(const QUuid& guid, T* obj) = 0;
    virtual void remove(const QUuid& guid) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual std::vector<QUuid> guids() const = 0;
    [[nodiscard]] virtual bool isObjectOwner() const = 0;
    [[nodiscard]] virtual int count() const = 0;
};

template <class T> class GuidInterfaceSet final : public IGuidInterfaceSet<T> {
public:
    using Ptr = std::shared_ptr<T>;
    explicit GuidInterfaceSet(bool allowNil = false) : allowNil_(allowNil) {}

    Ptr add(const QUuid& guid, const Ptr& value) override {
        if (!allowNil_ && !value)
            throw std::invalid_argument("Interface is nil");
        const auto [index, found] = list_.find(guid);
        if (found)
            return list_.at(index).value;
        list_.insert(index, guid, value);
        return value;
    }
    [[nodiscard]] bool isExists(const QUuid& guid) const override { return list_.find(guid).second; }
    [[nodiscard]] Ptr getByGuid(const QUuid& guid) const override {
        const auto [index, found] = list_.find(guid);
        return found ? list_.at(index).value : nullptr;
    }
    void replace(const QUuid& guid, const Ptr& value) override {
        if (!allowNil_ && !value)
            throw std::invalid_argument("Interface is nil");
        const auto [index, found] = list_.find(guid);
        if (found)
            list_.at(index).value = value;
        else
            list_.insert(index, guid, value);
    }
    void remove(const QUuid& guid) override {
        const auto [index, found] = list_.find(guid);
        if (found)
            list_.erase(index);
    }
    void clear() override { list_.clear(); }
    [[nodiscard]] std::vector<QUuid> guids() const override { return list_.keys(); }
    [[nodiscard]] std::vector<Ptr> values() const override { return list_.values(); }
    [[nodiscard]] Ptr item(int index) const override {
        return list_.at(static_cast<std::size_t>(index)).value;
    }
    [[nodiscard]] int count() const override { return list_.count(); }

private:
    bool allowNil_;
    detail::SortedKeyList<QUuid, Ptr, GuidLess> list_;
};

template <class T> class GuidObjectSet final : public IGuidObjectSet<T> {
public:
    explicit GuidObjectSet(bool allowNil = false) : allowNil_(allowNil) {}

    T* add(const QUuid& guid, T* obj) override {
        if (!allowNil_ && !obj)
            throw std::invalid_argument("Object is nil");
        const auto [index, found] = list_.find(guid);
        if (found)
            return list_.at(index).value;
        list_.insert(index, guid, obj);
        return obj;
    }
    [[nodiscard]] bool isExists(const QUuid& guid) const override { return list_.find(guid).second; }
    [[nodiscard]] T* getByGuid(const QUuid& guid) const override {
        const auto [index, found] = list_.find(guid);
        return found ? list_.at(index).value : nullptr;
    }
    void replace(const QUuid& guid, T* obj) override {
        if (!allowNil_ && !obj)
            throw std::invalid_argument("Object is nil");
        const auto [index, found] = list_.find(guid);
        if (found)
            list_.at(index).value = obj;
        else
            list_.insert(index, guid, obj);
    }
    void remove(const QUuid& guid) override {
        const auto [index, found] = list_.find(guid);
        if (found)
            list_.erase(index);
    }
    void clear() override { list_.clear(); }
    [[nodiscard]] std::vector<QUuid> guids() const override { return list_.keys(); }
    [[nodiscard]] bool isObjectOwner() const override { return false; }
    [[nodiscard]] int count() const override { return list_.count(); }

private:
    bool allowNil_;
    detail::SortedKeyList<QUuid, T*, GuidLess> list_;
};

} // namespace cg::core
