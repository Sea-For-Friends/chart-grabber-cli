// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/MarkPicture/i_MarkPicture.pas
//
// Interfaces only; implementations stay in `render` (spec vector.md §2.2.1). The one unit of
// Src/MarkPicture that lives below `render` because the appearance facets use it.
// FEAT-GEO-038 (interfaces only). IMarkPictureListInternal.LoadList stays with the implementation.

#include "cg/core/hash.h"

#include <QByteArray>
#include <QString>
#include <Qt>

#include <memory>

namespace cg::render {
class IBitmapMarker;
}

namespace cg::vector {

class IMarkPicture {
public:
    virtual ~IMarkPicture() = default;
    [[nodiscard]] virtual QByteArray source() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const render::IBitmapMarker> marker() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual Qt::AlignmentFlag textAlignment() const = 0;         // Left | Right | HCenter
    [[nodiscard]] virtual Qt::AlignmentFlag textVerticalAlignment() const = 0; // Top | Bottom | VCenter
    [[nodiscard]] virtual core::HashValue hash() const noexcept = 0;
};
using MarkPicturePtr = std::shared_ptr<const IMarkPicture>;

class IMarkPictureList {
public:
    virtual ~IMarkPictureList() = default;
    [[nodiscard]] virtual int count() const = 0;
    [[nodiscard]] virtual MarkPicturePtr get(int index) const = 0;
    [[nodiscard]] virtual QString name(int index) const = 0;
    [[nodiscard]] virtual int indexByName(const QString& value) const = 0; // -1 if absent
    [[nodiscard]] virtual MarkPicturePtr defaultPicture() const = 0;
    [[nodiscard]] virtual MarkPicturePtr findByName(const QString& value) const = 0; // null if absent
    [[nodiscard]] virtual MarkPicturePtr findByNameOrDefault(const QString& value) const = 0;
};
using MarkPictureListPtr = std::shared_ptr<const IMarkPictureList>;

} // namespace cg::vector
