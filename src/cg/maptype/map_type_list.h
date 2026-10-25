// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapTypeListStatic.pas, i_MapTypeListBuilder.pas, u_MapTypeListBuilderFactory.pas
//        (TMapTypeListStatic and TMapTypeListBuilder are private to the .cpp, as in legacy).
//
// An ordered list of maps that keeps duplicates, with a content hash, and the builder that makes
// it. The legacy factory's IHashFunction argument is not carried: the hash is core/hash.h.

#include "cg/core/hash.h"
#include "cg/maptype/map_type.h"

#include <memory>

namespace cg::maptype {

class IMapTypeListStatic {
public:
    virtual ~IMapTypeListStatic() = default;
    [[nodiscard]] virtual core::HashValue hash() const = 0;
    [[nodiscard]] virtual int count() const = 0;
    // Out of range: std::out_of_range (legacy: EListError from the list).
    [[nodiscard]] virtual IMapTypePtr item(int index) const = 0;
    // Null: false. Same object: true. Both hashes non-zero and different: false. Counts differ:
    // false. Otherwise pointer identity of the items per position.
    [[nodiscard]] virtual bool isEqual(const std::shared_ptr<const IMapTypeListStatic>& value) const = 0;
};
using IMapTypeListStaticPtr = std::shared_ptr<const IMapTypeListStatic>; // null for an empty list

class IMapTypeListBuilder {
public:
    virtual ~IMapTypeListBuilder() = default;
    [[nodiscard]] virtual int count() const = 0;
    // A lower bound only: legacy's exact value is not reproduced; no legacy caller reads it.
    [[nodiscard]] virtual int capacity() const = 0;
    // Below the count: std::length_error (legacy: EListError). Clear releases the capacity.
    virtual void setCapacity(int newCapacity) = 0;
    [[nodiscard]] virtual IMapTypePtr item(int index) const = 0;
    virtual void setItem(int index, const IMapTypePtr& item) = 0;
    // Duplicates and null are stored; a null item fails only in makeCopy and makeAndClear.
    virtual void add(const IMapTypePtr& item) = 0;
    virtual void clear() = 0;
    virtual void remove(int index) = 0; // legacy Delete
    virtual void exchange(int index1, int index2) = 0;
    // Both return nullptr for an empty builder; makeAndClear hands the content over.
    [[nodiscard]] virtual IMapTypeListStaticPtr makeCopy() const = 0;
    [[nodiscard]] virtual IMapTypeListStaticPtr makeAndClear() = 0;
};

class IMapTypeListBuilderFactory {
public:
    virtual ~IMapTypeListBuilderFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<IMapTypeListBuilder> build() const = 0;
};

// FEAT-NET-030 (contributes; held open)
class MapTypeListBuilderFactory final : public IMapTypeListBuilderFactory {
public:
    [[nodiscard]] std::unique_ptr<IMapTypeListBuilder> build() const override;
};

} // namespace cg::maptype
