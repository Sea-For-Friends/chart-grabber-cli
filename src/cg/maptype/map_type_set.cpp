// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_set.h"

#include <QHash>
#include <QString>

#include <stdexcept>
#include <utility>
#include <vector>

namespace cg::maptype {

namespace {

// Basic/u_GUIDInterfaceSetOrdered.pas: a set keyed by GUID that enumerates in insertion order.
// Legacy keeps a GUID-sorted array for lookup and an insertion-ordered array for Items[]; the
// port keeps the ordered vector and a hash index. Only what the map-type set needs is ported
// (no Replace, Delete, Find, enumerators: no caller).
class GuidInterfaceSetOrdered {
public:
    explicit GuidInterfaceSetOrdered(bool allowNil) : allowNil_(allowNil) {}

    // :163-181 (Add): an existing GUID keeps its first item; a null item throws unless allowed.
    void add(const QUuid& guid, const IMapTypePtr& item) {
        if (!allowNil_ && item == nullptr) {
            throw std::invalid_argument("Interface is nil");
        }
        if (index_.contains(guid)) {
            return;
        }
        index_.insert(guid, static_cast<qsizetype>(items_.size()));
        items_.push_back({guid, item});
    }
    [[nodiscard]] IMapTypePtr byGuid(const QUuid& guid) const {
        const auto it = index_.constFind(guid);
        return it == index_.constEnd() ? nullptr : items_[static_cast<std::size_t>(it.value())].item;
    }
    [[nodiscard]] int count() const { return static_cast<int>(items_.size()); }
    [[nodiscard]] const IMapTypePtr& item(int index) const {
        return items_.at(static_cast<std::size_t>(index)).item;
    }
    [[nodiscard]] const QUuid& guid(int index) const {
        return items_.at(static_cast<std::size_t>(index)).guid;
    }
    [[nodiscard]] int capacity() const { return static_cast<int>(items_.capacity()); }
    // :Capacity setter (SetCapacity): below the count it is an error in legacy (SListCapacityError).
    void setCapacity(int newCapacity) {
        if (newCapacity < count()) {
            throw std::length_error(
                QStringLiteral("List capacity out of bounds (%1)").arg(newCapacity).toStdString());
        }
        items_.reserve(static_cast<std::size_t>(newCapacity));
        index_.reserve(newCapacity);
    }
    // TGUIDSetBase.Clear: SetCount(0) and SetCapacity(0).
    void clear() {
        std::vector<Entry>().swap(items_);
        index_.clear();
        index_.squeeze();
    }

private:
    struct Entry {
        QUuid guid;
        IMapTypePtr item;
    };
    bool allowNil_;
    std::vector<Entry> items_;
    QHash<QUuid, qsizetype> index_;
};

constexpr core::HashValue kInitialHash = 0x049626A97A946096ULL; // CInitialHash

class MapTypeSet final : public IMapTypeSet {
public:
    MapTypeSet(core::HashValue hash, GuidInterfaceSetOrdered list) : hash_(hash), list_(std::move(list)) {}

    core::HashValue hash() const override { return hash_; }
    int count() const override { return list_.count(); }
    IMapTypePtr item(int index) const override { return list_.item(index); }
    bool isExists(const QUuid& guid) const override { return list_.byGuid(guid) != nullptr; }
    IMapTypePtr mapTypeByGuid(const QUuid& guid) const override { return list_.byGuid(guid); }

    bool isEqual(const std::shared_ptr<const IMapTypeSet>& value) const override {
        if (value == nullptr) {
            return false;
        }
        if (value.get() == this) {
            return true;
        }
        if (hash_ != core::kNoHash && value->hash() != core::kNoHash && hash_ != value->hash()) {
            return false;
        }
        if (value->count() != list_.count()) {
            return false;
        }
        for (int i = 0; i < list_.count(); ++i) {
            if (list_.item(i) != value->item(i)) {
                return false;
            }
        }
        return true;
    }

private:
    core::HashValue hash_;
    GuidInterfaceSetOrdered list_;
};

class MapTypeSetBuilder final : public IMapTypeSetBuilder {
public:
    explicit MapTypeSetBuilder(bool allowNil) : allowNil_(allowNil), list_(allowNil) {}

    int count() const override { return list_.count(); }
    int capacity() const override { return list_.capacity(); }

    void setCapacity(int newCapacity) override {
        // Legacy creates the list for n > 0 and forwards n <= 0 only to an existing list; the
        // port's list always exists, and a zero reserve on a fresh list is the same no-op.
        if (newCapacity > 0 || list_.count() > 0 || list_.capacity() > 0) {
            list_.setCapacity(newCapacity);
        }
    }

    void add(const IMapTypePtr& item) override { list_.add(item != nullptr ? item->guid() : QUuid(), item); }

    void clear() override { list_.clear(); }

    IMapTypeSetPtr makeCopy() const override {
        if (list_.count() == 0) {
            return nullptr;
        }
        GuidInterfaceSetOrdered copy(allowNil_);
        core::HashValue hash = kInitialHash;
        for (int i = 0; i < list_.count(); ++i) {
            copy.add(list_.guid(i), list_.item(i));
            core::hashUpdate(hash, list_.guid(i));
        }
        return std::make_shared<MapTypeSet>(hash, std::move(copy));
    }

    IMapTypeSetPtr makeAndClear() override {
        if (list_.count() == 0) {
            return nullptr;
        }
        core::HashValue hash = kInitialHash;
        for (int i = 0; i < list_.count(); ++i) {
            core::hashUpdate(hash, list_.guid(i));
        }
        auto result = std::make_shared<MapTypeSet>(hash, std::move(list_));
        list_ = GuidInterfaceSetOrdered(allowNil_); // legacy: FList := nil (capacity 0)
        return result;
    }

private:
    bool allowNil_;
    GuidInterfaceSetOrdered list_;
};

} // namespace

std::unique_ptr<IMapTypeSetBuilder> MapTypeSetBuilderFactory::build(bool allowNil) const {
    return std::make_unique<MapTypeSetBuilder>(allowNil);
}

} // namespace cg::maptype
