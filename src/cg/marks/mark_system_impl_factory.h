// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_MarkSystemImplFactory.pas

#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_config.h"

#include <QString>
#include <QUuid>

#include <memory>
#include <stop_token>
#include <vector>

namespace cg::marks {

class IMarkSystemImplFactory {
public:
    virtual ~IMarkSystemImplFactory() = default;
    [[nodiscard]] virtual bool isInitializationRequired() const = 0;
    // Legacy (AOperationID, ACancelNotifier) is the stop token. Throws when the database cannot be opened.
    virtual std::shared_ptr<IMarkSystemImpl> build(std::stop_token stop, const QString& basePath,
                                                   const MarkSystemImplConfigStaticPtr& implConfig) = 0;
};

class IMarkSystemImplFactoryListElement {
public:
    virtual ~IMarkSystemImplFactoryListElement() = default;
    [[nodiscard]] virtual QUuid guid() const = 0;
    [[nodiscard]] virtual QString caption() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IMarkSystemImplFactory> factory() const = 0;
};
using MarkSystemImplFactoryListElementPtr = std::shared_ptr<const IMarkSystemImplFactoryListElement>;

class IMarkSystemImplFactoryListStatic {
public:
    virtual ~IMarkSystemImplFactoryListStatic() = default;
    // IEnumGUID: the guids in ascending GUID order (CompareGUIDs), not registration order (spec marks.md §2.6
    // Ruling A.1: the legacy list is a sorted GUID set).
    [[nodiscard]] virtual std::vector<QUuid> guidEnum() const = 0;
    [[nodiscard]] virtual MarkSystemImplFactoryListElementPtr
    get(const QUuid& guid) const = 0; // null when unknown
};
using MarkSystemImplFactoryListStaticPtr = std::shared_ptr<const IMarkSystemImplFactoryListStatic>;

} // namespace cg::marks
