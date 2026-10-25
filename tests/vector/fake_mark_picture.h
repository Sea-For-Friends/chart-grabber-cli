// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Test double of IMarkPicture (docs/specs/vector.md §2.2.1): a name and a hash, no bitmap.

#include "cg/vector/mark_picture.h"

#include <memory>
#include <utility>

namespace cg::vector::test {

class FakeMarkPicture final : public IMarkPicture {
public:
    FakeMarkPicture(QString name, core::HashValue hash) : name_(std::move(name)), hash_(hash) {}

    QByteArray source() const override { return {}; }
    std::shared_ptr<const render::IBitmapMarker> marker() const override { return nullptr; }
    QString name() const override { return name_; }
    Qt::AlignmentFlag textAlignment() const override { return Qt::AlignLeft; }
    Qt::AlignmentFlag textVerticalAlignment() const override { return Qt::AlignTop; }
    core::HashValue hash() const noexcept override { return hash_; }

private:
    QString name_;
    core::HashValue hash_;
};

// Knows the pictures "a" and "b" (hash 1); counts findByName lookups. Hoisted from
// config_provider_helpers_test.cpp so that the maptype tests reuse it.
class FakeMarkPictureList final : public IMarkPictureList {
public:
    mutable int lookups = 0;
    int count() const override { return 2; }
    MarkPicturePtr get(int) const override { return nullptr; }
    QString name(int) const override { return {}; }
    int indexByName(const QString&) const override { return -1; }
    MarkPicturePtr defaultPicture() const override {
        return std::make_shared<FakeMarkPicture>(QStringLiteral("a"), 1);
    }
    MarkPicturePtr findByName(const QString& value) const override {
        ++lookups;
        return value == u"a" || value == u"b" ? std::make_shared<FakeMarkPicture>(value, 1) : nullptr;
    }
    MarkPicturePtr findByNameOrDefault(const QString& value) const override { return findByName(value); }
};

} // namespace cg::vector::test
