// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_IDListBase.pas (TIDListBase) AND u_GUIDSet.pas (TGUIDSetBase) -- the two legacy bases are
// copies of each other with a different key (NativeInt / TGUID); one template stands for both, and
// for the array halves of TIDInterfaceList / TGUIDInterfaceSet / TGUIDObjectSet.
//
// A vector of (key, value) kept ordered by key ascending with the legacy binary search `Find`
// (leftmost equal element, otherwise the insertion point == std::lower_bound). Not synchronised
// (legacy neither). Capacity / SetCount of the legacy class are not ported (see T-0055 handoff).

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace cg::core::detail {

template <class Key, class Value, class Compare> class SortedKeyList {
public:
    struct Entry {
        Key key;
        Value value;
    };

    [[nodiscard]] int count() const noexcept { return static_cast<int>(items_.size()); }

    // Legacy Find: index of the key when present, else the index where it would be inserted.
    [[nodiscard]] std::pair<std::size_t, bool> find(const Key& key) const {
        const auto it = std::lower_bound(items_.begin(), items_.end(), key,
                                         [](const Entry& e, const Key& k) { return Compare{}(e.key, k); });
        const std::size_t index = static_cast<std::size_t>(it - items_.begin());
        return {index, it != items_.end() && !Compare{}(key, it->key)};
    }

    [[nodiscard]] const Entry& at(std::size_t index) const { return items_[index]; }
    [[nodiscard]] Entry& at(std::size_t index) { return items_[index]; }

    void insert(std::size_t index, Key key, Value value) {
        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index),
                      Entry{std::move(key), std::move(value)});
    }
    void erase(std::size_t index) { items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index)); }
    void clear() { items_.clear(); }

    [[nodiscard]] std::vector<Key> keys() const {
        std::vector<Key> out;
        out.reserve(items_.size());
        for (const Entry& e : items_)
            out.push_back(e.key);
        return out;
    }
    [[nodiscard]] std::vector<Value> values() const {
        std::vector<Value> out;
        out.reserve(items_.size());
        for (const Entry& e : items_)
            out.push_back(e.value);
        return out;
    }

private:
    std::vector<Entry> items_;
};

} // namespace cg::core::detail
