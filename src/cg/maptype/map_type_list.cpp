// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/map_type_list.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cg::maptype {

namespace {

constexpr core::HashValue kInitialHash = 0xc74bb7e2dee15036ULL; // CInitialHash (:52)

// :209-217 and :224-232: the GUID of every item, in order. A null item is an access violation in
// legacy (LD-17); the port throws before the builder gives anything away.
core::HashValue hashOfItems(const std::vector<IMapTypePtr>& items) {
    core::HashValue hash = kInitialHash;
    for (const IMapTypePtr& item : items) {
        if (item == nullptr) {
            throw std::logic_error("MapTypeList: nil item");
        }
        core::hashUpdate(hash, item->guid());
    }
    return hash;
}

// TMapTypeListStatic (:57-130).
class MapTypeListStatic final : public IMapTypeListStatic {
public:
    MapTypeListStatic(core::HashValue hash, std::vector<IMapTypePtr> items)
        : hash_(hash), items_(std::move(items)) {}

    core::HashValue hash() const override { return hash_; }
    int count() const override { return static_cast<int>(items_.size()); }
    IMapTypePtr item(int index) const override { return items_.at(static_cast<std::size_t>(index)); }

    bool isEqual(const std::shared_ptr<const IMapTypeListStatic>& value) const override {
        if (value == nullptr) {
            return false;
        }
        if (value.get() == this) {
            return true;
        }
        if (hash_ != core::kNoHash && value->hash() != core::kNoHash && hash_ != value->hash()) {
            return false;
        }
        if (value->count() != count()) {
            return false;
        }
        for (int i = 0; i < count(); ++i) {
            if (value->item(i) != items_[static_cast<std::size_t>(i)]) {
                return false;
            }
        }
        return true;
    }

private:
    core::HashValue hash_;
    std::vector<IMapTypePtr> items_;
};

// TMapTypeListBuilder (:134-245). Legacy keeps an IInterfaceListSimple; a vector is the same
// container without the reference counts.
class MapTypeListBuilder final : public IMapTypeListBuilder {
public:
    int count() const override { return static_cast<int>(items_.size()); }
    int capacity() const override { return static_cast<int>(items_.capacity()); }

    // TInterfaceListSimple.SetCapacity (:274-284): once the legacy list exists, TList.SetCapacity
    // raises for a value below the count (a negative one included, also after Clear). Before that
    // (FList nil) only a positive value creates the list; anything else is a no-op.
    void setCapacity(int newCapacity) override {
        if (!listExists_ && newCapacity <= 0) {
            return;
        }
        if (newCapacity < count()) {
            throw std::length_error("List capacity out of bounds (" + std::to_string(newCapacity) + ")");
        }
        listExists_ = true;
        if (newCapacity > 0) {
            items_.reserve(static_cast<std::size_t>(newCapacity));
        }
    }

    IMapTypePtr item(int index) const override { return items_[position(index)]; }
    void setItem(int index, const IMapTypePtr& item) override { items_[position(index)] = item; }
    void add(const IMapTypePtr& item) override {
        listExists_ = true; // legacy Add creates FList (:103-113)
        items_.push_back(item);
    }

    // :174-177 (SetCount(0) and SetCapacity(0)): the capacity is released too.
    void clear() override { std::vector<IMapTypePtr>().swap(items_); }

    void remove(int index) override {
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(position(index)));
    }

    void exchange(int index1, int index2) override {
        const std::size_t first = position(index1);
        const std::size_t second = position(index2);
        std::swap(items_[first], items_[second]);
    }

    IMapTypeListStaticPtr makeCopy() const override {
        if (items_.empty()) {
            return nullptr;
        }
        return std::make_shared<MapTypeListStatic>(hashOfItems(items_), items_);
    }

    IMapTypeListStaticPtr makeAndClear() override {
        if (items_.empty()) {
            return nullptr;
        }
        const core::HashValue hash = hashOfItems(items_);
        auto result = std::make_shared<MapTypeListStatic>(hash, std::move(items_));
        items_ = std::vector<IMapTypePtr>(); // the moved-from vector is not relied upon
        listExists_ = false; // legacy CreateWithOwn sets FList to nil (u_InterfaceListStatic.pas:82-89)
        return result;
    }

private:
    // Legacy: EListError from a filled list; an access violation while FList is nil (never filled,
    // or handed over by makeAndClear). The port throws std::out_of_range in both cases.
    std::size_t position(int index) const {
        if (index < 0 || index >= count()) {
            throw std::out_of_range("MapTypeList: index " + std::to_string(index) + " out of range");
        }
        return static_cast<std::size_t>(index);
    }

    std::vector<IMapTypePtr> items_;
    // Legacy FList is nil until something creates it, and clear keeps it; a vector cannot tell.
    bool listExists_ = false;
};

} // namespace

std::unique_ptr<IMapTypeListBuilder> MapTypeListBuilderFactory::build() const {
    return std::make_unique<MapTypeListBuilder>();
}

} // namespace cg::maptype
