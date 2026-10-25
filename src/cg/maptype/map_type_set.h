// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MapTypeSet.pas, i_MapTypeSetBuilder.pas, u_MapTypeSetBuilderFactory.pas
//        (TMapTypeSet and TMapTypeSetBuilder are private to the .cpp, as in legacy).
//
// An immutable set of maps keyed by GUID that keeps insertion order. The set and the builder
// carry an ordered GUID set that ports Basic/u_GUIDInterfaceSetOrdered.pas (see the .cpp).

#include "cg/core/hash.h"
#include "cg/maptype/map_type.h"

#include <QUuid>

#include <memory>

namespace cg::maptype {

class IMapTypeSet {
public:
    virtual ~IMapTypeSet() = default;
    [[nodiscard]] virtual core::HashValue hash() const = 0;
    [[nodiscard]] virtual int count() const = 0;
    // Insertion order. Out of range: std::out_of_range (legacy: unchecked read).
    [[nodiscard]] virtual IMapTypePtr item(int index) const = 0;
    // Null: false. Same object: true. Both hashes non-zero and different: false. Counts differ:
    // false. Otherwise pointer identity of the items per position.
    [[nodiscard]] virtual bool isEqual(const std::shared_ptr<const IMapTypeSet>& value) const = 0;
    // True when mapTypeByGuid(guid) is not null (a null item stored under the zero GUID does
    // not count).
    [[nodiscard]] virtual bool isExists(const QUuid& guid) const = 0;
    [[nodiscard]] virtual IMapTypePtr mapTypeByGuid(const QUuid& guid) const = 0; // null if absent
};
using IMapTypeSetPtr = std::shared_ptr<const IMapTypeSet>;

class IMapTypeSetBuilder {
public:
    virtual ~IMapTypeSetBuilder() = default;
    [[nodiscard]] virtual int count() const = 0;
    // A lower bound only: legacy's exact value (shrinking, its growth steps) is not reproduced; no
    // legacy caller reads it.
    [[nodiscard]] virtual int capacity() const = 0;
    virtual void setCapacity(int newCapacity) = 0;
    // A GUID already present is ignored (the first item stays). A null item is stored under the
    // zero GUID when the builder allows nil, otherwise std::invalid_argument("Interface is nil").
    virtual void add(const IMapTypePtr& item) = 0;
    virtual void clear() = 0;
    // Both return nullptr for an empty builder; makeAndClear hands the content over.
    [[nodiscard]] virtual IMapTypeSetPtr makeCopy() const = 0;
    [[nodiscard]] virtual IMapTypeSetPtr makeAndClear() = 0;
};

class IMapTypeSetBuilderFactory {
public:
    virtual ~IMapTypeSetBuilderFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<IMapTypeSetBuilder> build(bool allowNil) const = 0;
};

// FEAT-NET-001 (contributes; held open)
class MapTypeSetBuilderFactory final : public IMapTypeSetBuilderFactory {
public:
    [[nodiscard]] std::unique_ptr<IMapTypeSetBuilder> build(bool allowNil) const override;
};

} // namespace cg::maptype
